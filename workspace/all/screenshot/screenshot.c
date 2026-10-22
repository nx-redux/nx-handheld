#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <linux/fb.h>

#include "../common/drm_scanout.h"
#include "../common/capture_defs.h"
#include "../common/file_util.h"
#include "../common/proc_spawn.h"

#define PID_FILE "/tmp/screenshot.pid"
#define SCREENSHOT_DIR "/mnt/SDCARD/Images/Screenshots"
#define OSD_TOAST_PATH "/tmp/trimui_osd/osd_toast_msg"
#define INPUT_COUNT 5

// evdev codes for L2/R2 analog triggers
#define ABS_Z_CODE 2  // L2 trigger axis
#define ABS_RZ_CODE 5 // R2 trigger axis

#define COOLDOWN_MS 1000 // minimum ms between screenshots

static int inputs[INPUT_COUNT] = {};
static volatile int quit = 0;

static void on_term(int sig) {
	quit = 1;
}

static void cleanup(void) {
	remove(PID_FILE);
	for (int i = 0; i < INPUT_COUNT; i++) {
		if (inputs[i] >= 0)
			close(inputs[i]);
	}
}

#define FB_MIRROR_VIDEO_SIZE_FALLBACK "1280x720"

// Toast size:1 draws the 400px-wide bg_msg_w2.png background
#define TOAST_BG_WIDTH 400

static int screen_width(void) {
	int w = 1280;
	FILE* f = fopen("/sys/class/graphics/fb0/virtual_size", "r");
	if (f) {
		int fw = 0;
		if (fscanf(f, "%d", &fw) == 1 && fw > 0)
			w = fw;
		fclose(f);
	}
	return w;
}

// Toast rendered by trimui_osdd (ignored silently if the daemon isn't running)
static void osd_toast(const char* msg, int duration_ms) {
	FILE* f = fopen(OSD_TOAST_PATH, "w");
	if (!f)
		return;
	fprintf(f,
			"{\n"
			"    \"type\":\"default\",\n"
			"    \"id\":\"com.trimui.osd.msg.default\",\n"
			"    \"duration\":%d,\n"
			"    \"size\":1,\n"
			"    \"x\":%d,\n"
			"    \"y\":500,\n"
			"    \"w\":300,\n"
			"    \"h\":80,\n"
			"    \"message\":\"%s\",\n"
			"    \"font\":\"\",\n"
			"    \"bg\":\"\",\n"
			"    \"icon\":\"\",\n"
			"    \"fontsize\":24,\n"
			"    \"fontcolor\":\"FFFFFFFF\"\n"
			"}\n",
			duration_ms, (screen_width() - TOAST_BG_WIDTH) / 2, msg);
	fclose(f);
}

// A live mirror is one whose writer still holds the advisory flock taken at
// creation (generic_video.c capture_check). A leftover file from a dead or
// exec'd-away process carries no lock: encoding it would produce a stale
// frame from the previous app, so treat it as garbage.
static int mirror_live(void) {
	int fd = open(FB_MIRROR_PATH, O_RDONLY);
	if (fd < 0)
		return 0;
	int live = 0;
	if (flock(fd, LOCK_EX | LOCK_NB) == 0)
		flock(fd, LOCK_UN);
	else if (errno == EWOULDBLOCK)
		live = 1;
	close(fd);
	return live;
}

// Geometry published by the mirror's writer alongside the raw frames
static void mirror_video_size(char* out, size_t n) {
	snprintf(out, n, "%s", FB_MIRROR_VIDEO_SIZE_FALLBACK);
	FILE* f = fopen(FB_MIRROR_INFO_PATH, "r");
	if (f) {
		int w = 0, h = 0;
		if (fscanf(f, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
			snprintf(out, n, "%dx%d", w, h);
		fclose(f);
	}
}

// Returns 1 if the visible fb0 pane holds any non-black RGB pixel. On tg5040
// fb0 is the real scanout but goes black briefly around app transitions
// (apps zero it on quit). Geometry or read failures assume content so a
// sampler gap can never block a capture that might have worked.
static int fb0_has_content(void) {
	int fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0)
		return 0;
	int result = 1;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 &&
		ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
		var.bits_per_pixel == 32 && var.xres > 0 && var.yres > 0 &&
		fix.line_length >= var.xres * 4) {
		// Only the framebuffer's own channel bits decide blackness: an
		// opaque-black fill (alpha 0xFF) or garbage in unused bits must not
		// read as content.
		uint32_t rgb_mask = (((var.red.length ? (1u << var.red.length) - 1 : 0)) << var.red.offset) |
							(((var.green.length ? (1u << var.green.length) - 1 : 0)) << var.green.offset) |
							(((var.blue.length ? (1u << var.blue.length) - 1 : 0)) << var.blue.offset);
		if (!rgb_mask)
			rgb_mask = 0x00FFFFFF;
		static uint32_t line[4096];
		size_t line_bytes = (size_t)var.xres * 4;
		if (line_bytes > sizeof(line))
			line_bytes = sizeof(line);
		result = 0;
		for (unsigned y = 0; y < var.yres && !result; y += 8) {
			off_t off = ((off_t)var.yoffset + y) * fix.line_length +
						(off_t)var.xoffset * 4;
			ssize_t n = pread(fd, line, line_bytes, off);
			if (n < (ssize_t)line_bytes) {
				result = 1; // can't sample: assume content
				break;
			}
			for (size_t x = 0; x < line_bytes / 4; x += 4) {
				if (line[x] & rgb_mask) {
					result = 1;
					break;
				}
			}
		}
	}
	close(fd);
	return result;
}

// Per-process temp names (set in main): a one-shot CLI capture may run while
// the armed daemon is capturing too.
static char drm_raw_path[64];

// tg5040 composite source: the sunxi display engine's write-back channel,
// exposed as the disp2 debug attr below, returns the final composited panel
// output — every layer (app, video, OSD panel, toasts), exactly what the
// panel shows. fb0 only ever holds the app's own layer, so this is the only
// tg5040 source that can see the rest. Kernel semantics (disp_capture_dump_store,
// lichee linux-4.9 dev_disp.c): the filename written to the attr is the output
// path, `.bmp` selects ARGB8888 + 54-byte header, the target is opened
// O_CREAT|O_EXCL so a pre-existing file is a silent no-op (always unlink
// first), and the writer is blocked ~1s (disp_delay_ms) until the dump lands.
// Kernel-context vfs_write: the target must live on tmpfs, never the vfat card.
#define DISP_CAPTURE_ATTR "/sys/class/disp/disp/attr/capture_dump"
static char disp_bmp_path[64];
static char disp_raw_path[64];
#define DISP_BMP_HEADER 54

// Compile-time tg5040-only (a sunxi-disp2 hack; tg5050's 5.15 kernel has no
// /sys/class/disp at all) + runtime attr probe for older firmware.
static int disp_capture_usable(void) {
	return strcmp(PLATFORM, "tg5040") == 0 &&
		   access(DISP_CAPTURE_ATTR, W_OK) == 0;
}

// Trigger a write-back dump and strip the BMP header into disp_raw_path for
// the rawvideo encode (dump data is top-down BGRA: no flip). Fills video_size
// from the dump's own header rather than assuming panel geometry.
static int disp_capture_raw(char* video_size, size_t vn, char* pixfmt, size_t pn) {
	unlink(disp_bmp_path);
	int fd = open(DISP_CAPTURE_ATTR, O_WRONLY);
	if (fd < 0)
		return 0;
	// The kernel store drops the final byte of the written string (it assumes
	// echo's trailing newline; verified live: a bare path came out truncated
	// to ".bm"), so send one for it to eat. The write blocks ~1s.
	char attr_arg[sizeof(disp_bmp_path) + 1];
	snprintf(attr_arg, sizeof(attr_arg), "%s\n", disp_bmp_path);
	ssize_t wr = write(fd, attr_arg, strlen(attr_arg));
	close(fd);
	if (wr < 0)
		return 0;

	int ok = 0;
	FILE* in = fopen(disp_bmp_path, "r");
	if (!in)
		return 0;
	uint8_t hdr[DISP_BMP_HEADER];
	if (fread(hdr, 1, sizeof(hdr), in) == sizeof(hdr) &&
		hdr[0] == 'B' && hdr[1] == 'M') {
		int32_t w, h;
		memcpy(&w, hdr + 18, sizeof(w));
		memcpy(&h, hdr + 22, sizeof(h));
		if (h < 0) // negative height is the BMP top-down convention
			h = -h;
		struct stat st;
		size_t frame = (size_t)w * h * 4;
		if (w > 0 && w <= 4096 && h > 0 && h <= 4096 &&
			fstat(fileno(in), &st) == 0 &&
			(size_t)st.st_size >= DISP_BMP_HEADER + frame) {
			uint8_t* buf = malloc(frame);
			if (buf && fread(buf, 1, frame, in) == frame) {
				FILE* out = fopen(disp_raw_path, "w");
				if (out) {
					ok = fwrite(buf, 1, frame, out) == frame;
					fclose(out);
				}
			}
			free(buf);
			if (ok) {
				snprintf(video_size, vn, "%dx%d", w, h);
				snprintf(pixfmt, pn, "%s", "bgra");
			}
		}
	}
	fclose(in);
	unlink(disp_bmp_path);
	return ok;
}

// Capture the current DRM scanout (see common/drm_scanout.c) to drm_raw_path
// and fill video_size ("WxH") + pixfmt for the ffmpeg rawvideo encode. This
// is the primary tg5050 source — it captures ANY app, including third-party
// paks that don't publish the GPU mirror; on tg5040 drm_scanout_read fails
// fast and the fbdev path takes over.
static int drm_capture_raw(char* video_size, size_t vn, char* pixfmt, size_t pn) {
	drm_scanout_info info;
	if (!drm_scanout_read(&info, NULL, 0))
		return 0;
	size_t frame = (size_t)info.width * info.height * 4;
	uint8_t* buf = malloc(frame);
	if (!buf)
		return 0;
	int ok = 0;
	if (drm_scanout_read(&info, buf, frame)) {
		FILE* out = fopen(drm_raw_path, "w");
		if (out) {
			ok = fwrite(buf, 1, frame, out) == frame;
			fclose(out);
		}
	}
	free(buf);
	if (ok) {
		snprintf(video_size, vn, "%ux%u", info.width, info.height);
		snprintf(pixfmt, pn, "%s", info.pixfmt);
	}
	return ok;
}

static int has_suffix(const char* s, const char* suffix) {
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

// Captures the screen to output (NULL: timestamped JPEG in SCREENSHOT_DIR;
// a .bmp output is lossless — the stock ffmpeg has no PNG encoder). notify toasts the result through
// trimui_osdd; the CLI keeps quiet so the toast can't land in the next shot.
// Returns 1 on success.
static int capture_screenshot(const char* output_arg, int notify) {
	// Pick a source, in order of preference:
	//   1. live GPU mirror (app-published, vsync'd frame)
	//   2. DRM plane readback (composited scanout — works for ANY app on
	//      tg5050, including third-party paks; fails fast where the DRM node
	//      has no KMS planes)
	//   3. disp write-back dump (tg5040 composite: sees video layers, the
	//      OSD panel and toasts — everything fb0 misses — at the cost of a
	//      ~1s in-kernel block per shot)
	//   4. non-black fb0 via fbdev (tg5040, where GL renders through fb0)
	// Retry briefly: enabling capture wakes idle apps via PLAT_pokeCapture
	// (fires once the app has been idle ~1s, ≤500ms poll cadence) and app
	// transitions repaint fb0 within a frame or two.
	enum { SRC_NONE,
		   SRC_MIRROR,
		   SRC_DRM,
		   SRC_DISP,
		   SRC_FBDEV } src = SRC_NONE;
	char video_size[32];
	char pixfmt[8];
	int disp_failed = 0;
	for (int i = 0; i < 20 && src == SRC_NONE; i++) {
		if (mirror_live()) {
			src = SRC_MIRROR;
			break;
		}
		if (drm_capture_raw(video_size, sizeof(video_size), pixfmt, sizeof(pixfmt))) {
			src = SRC_DRM;
			break;
		}
		if (!disp_failed && disp_capture_usable()) {
			if (disp_capture_raw(video_size, sizeof(video_size), pixfmt, sizeof(pixfmt))) {
				src = SRC_DISP;
				break;
			}
			// one attempt only: a broken dump path must fall through to
			// fbdev now, not burn 20 blocking retries first
			disp_failed = 1;
		}
		if (fbdev_usable() && fb0_has_content()) {
			src = SRC_FBDEV;
			break;
		}
		usleep(100000);
	}
	if (src == SRC_NONE) {
		// Nothing can supply pixels here: fail honestly instead of writing
		// an all-black JPEG and claiming success.
		if (notify)
			osd_toast("Capture not available here", 2000);
		return 0;
	}
	if (src == SRC_MIRROR)
		mirror_video_size(video_size, sizeof(video_size));

	char output[512];
	if (output_arg) {
		snprintf(output, sizeof(output), "%s", output_arg);
	} else {
		time_t now = time(NULL);
		struct tm* t = localtime(&now);
		char stamp[32];
		snprintf(stamp, sizeof(stamp), "SCR_%04d%02d%02d_%02d%02d%02d",
				 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
				 t->tm_hour, t->tm_min, t->tm_sec);
		// A capture can take over a second (source retries, the disp dump),
		// so the next shot may land in the same second: number it instead of
		// letting ffmpeg -y replace the first (SCR_..._2.jpg, _3, ...).
		snprintf(output, sizeof(output), SCREENSHOT_DIR "/%s.jpg", stamp);
		for (int n = 2; access(output, F_OK) == 0 && n < 1000; n++)
			snprintf(output, sizeof(output), SCREENSHOT_DIR "/%s_%d.jpg", stamp, n);
	}
	mkdir_parent_mode(output, 0755); // output may be any CLI path
	const char* codec = has_suffix(output, ".bmp") ? "bmp" : "mjpeg";

	char* q = (char*)codec;
	// glReadPixels frames are bottom-to-top RGBA: vflip at encode
	char* mirror_argv[] = {FFMPEG_PATH, "-nostdin",
						   "-f", "rawvideo", "-pixel_format", "rgba",
						   "-video_size", video_size,
						   "-i", FB_MIRROR_PATH,
						   "-vf", "vflip",
						   "-frames:v", "1", "-c:v", q, "-q:v", "2",
						   "-y", output,
						   NULL};
	// scanout / write-back buffers are top-down: no flip
	char* raw_argv[] = {FFMPEG_PATH, "-nostdin",
						"-f", "rawvideo", "-pixel_format", pixfmt,
						"-video_size", video_size,
						"-i", src == SRC_DRM ? drm_raw_path : disp_raw_path,
						"-frames:v", "1", "-c:v", q, "-q:v", "2",
						"-y", output,
						NULL};
	char* fbdev_argv[] = {FFMPEG_PATH, "-nostdin",
						  "-f", "fbdev", "-i", "/dev/fb0",
						  "-frames:v", "1", "-c:v", q, "-q:v", "2",
						  "-y", output,
						  NULL};
	char** argv = src == SRC_MIRROR						? mirror_argv
				  : (src == SRC_DRM || src == SRC_DISP) ? raw_argv
														: fbdev_argv;

	// Run ffmpeg detached and silent, and wait (single frame capture is fast)
	int ok = spawn_quiet_wait(argv, true);
	if (src == SRC_DRM)
		remove(drm_raw_path);
	if (src == SRC_DISP)
		remove(disp_raw_path);
	if (notify)
		osd_toast(ok ? "Screenshot saved" : "Screenshot failed", 1500);
	if (ok && !notify)
		printf("%s\n", output);
	return ok;
}

static void usage(const char* argv0) {
	fprintf(stderr,
			"usage: %s                    run the L2+R2 capture daemon\n"
			"       %s --capture [FILE]   take one screenshot now and exit\n"
			"\n"
			"FILE defaults to " SCREENSHOT_DIR "/SCR_<timestamp>.jpg;\n"
			"FILE ending in .bmp is saved lossless, anything else as JPEG\n"
			"(the device's ffmpeg has no PNG encoder). Prints the saved path.\n",
			argv0, argv0);
}

int main(int argc, char* argv[]) {
	snprintf(drm_raw_path, sizeof(drm_raw_path), "/tmp/screenshot_drm.%d.raw", (int)getpid());
	snprintf(disp_bmp_path, sizeof(disp_bmp_path), "/tmp/screenshot_disp.%d.bmp", (int)getpid());
	snprintf(disp_raw_path, sizeof(disp_raw_path), "/tmp/screenshot_disp.%d.raw", (int)getpid());

	if (argc > 1) {
		if (strcmp(argv[1], "--capture") == 0 || strcmp(argv[1], "-c") == 0) {
			if (argc > 3) {
				usage(argv[0]);
				return 2;
			}
			if (argc == 3 && has_suffix(argv[2], ".png")) {
				fprintf(stderr, "screenshot: no PNG encoder on this device; use .jpg or .bmp\n");
				return 2;
			}
			// One-shot: no PID file, so apps don't start mirroring for us;
			// the DRM (tg5050) / disp write-back (tg5040) readbacks see the
			// composited panel without it.
			if (capture_screenshot(argc == 3 ? argv[2] : NULL, 0))
				return 0;
			fprintf(stderr, "screenshot: capture failed\n");
			return 1;
		}
		usage(argv[0]);
		return strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ? 0 : 2;
	}

	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	// The OSD toggle launches us with a plain `&`; a controlling-terminal HUP
	// (e.g. an adb shell exiting) must run cleanup, not kill us mid-flight
	// leaving a stale PID file that keeps every app mirroring forever.
	// Respect an inherited SIG_IGN (nohup) instead of overriding it.
	struct sigaction old_hup;
	sigaction(SIGHUP, NULL, &old_hup);
	if (old_hup.sa_handler != SIG_IGN)
		sigaction(SIGHUP, &sa, NULL);

	// Write PID file
	FILE* f = fopen(PID_FILE, "w");
	if (f) {
		fprintf(f, "%d", getpid());
		fclose(f);
	}

	// Open input devices
	char path[32];
	for (int i = 0; i < INPUT_COUNT; i++) {
		sprintf(path, "/dev/input/event%i", i);
		inputs[i] = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	}

	int l2_pressed = 0;
	int r2_pressed = 0;
	int combo_latched = 0; // require both triggers released before the next shot
	uint32_t last_capture_ms = 0;
	struct input_event ev;
	struct timeval tod;

	while (!quit) {
		gettimeofday(&tod, NULL);
		uint32_t now_ms = tod.tv_sec * 1000 + tod.tv_usec / 1000;

		for (int i = 0; i < INPUT_COUNT; i++) {
			if (inputs[i] < 0)
				continue;
			while (read(inputs[i], &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_ABS) {
					if (ev.code == ABS_Z_CODE)
						l2_pressed = ev.value > 0;
					else if (ev.code == ABS_RZ_CODE)
						r2_pressed = ev.value > 0;
				}
			}
		}

		if (l2_pressed && r2_pressed) {
			if (!combo_latched && (now_ms - last_capture_ms) > COOLDOWN_MS) {
				combo_latched = 1;
				capture_screenshot(NULL, 1);
				last_capture_ms = now_ms;
			}
		} else {
			combo_latched = 0;
		}

		usleep(16666); // ~60fps polling
	}

	cleanup();
	return 0;
}
