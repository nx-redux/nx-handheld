// Device harness for pico8native_libretro.so: a minimal libretro frontend that
// loads the core, runs a cart for <frames> retro_runs and unloads it, without
// minarch or the display.
//
//   core_host <pico8native_libretro.so> <system dir> <save dir> <cart> <frames> <out.ppm>
//
// HOST_BUTTONS (RETRO_DEVICE_ID_JOYPAD_* mask) is held from frame 300 on. The core
// finds pico8shim.so and lib/ next to itself, and PICO-8's home under
// $SHARED_USERDATA_PATH/P8-native/home. Built for the device by `make corehost`.
#include <dlfcn.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libretro.h"

static const char *system_dir, *save_dir;
static const uint32_t* last_fb;
static uint32_t frame_copy[128 * 128];
static unsigned frames_seen, frames_null, frames_changed;
static unsigned long long audio_total, audio_nonzero;
static uint32_t held;
static int frame_no, shutdown_asked;

static void log_printf(enum retro_log_level level, const char* fmt, ...) {
	(void)level;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool env_cb(unsigned cmd, void* data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback*)data)->log = log_printf;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char**)data = system_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char**)data = save_dir;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return *(enum retro_pixel_format*)data == RETRO_PIXEL_FORMAT_XRGB8888;
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		return true;
	case RETRO_ENVIRONMENT_SHUTDOWN:
		shutdown_asked = 1;
		return true;
	default:
		return false; // as minarch for a software core's SET_SYSTEM_AV_INFO
	}
}

static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	frames_seen++;
	if (!data || w != 128 || h != 128 || pitch != 512) {
		frames_null++;
		return;
	}
	if (memcmp(frame_copy, data, sizeof(frame_copy)) != 0)
		frames_changed++;
	memcpy(frame_copy, data, sizeof(frame_copy));
	last_fb = frame_copy;
}

static size_t audio_batch(const int16_t* data, size_t frames) {
	audio_total += frames;
	for (size_t i = 0; i < frames * 2; i++)
		audio_nonzero += data[i] != 0;
	return frames;
}
static void audio_one(int16_t l, int16_t r) {
	(void)l, (void)r;
}
static void poll(void) {}
static int16_t state(unsigned port, unsigned dev, unsigned idx, unsigned id) {
	(void)idx;
	return port == 0 && dev == RETRO_DEVICE_JOYPAD && frame_no >= 300 && (held >> id & 1);
}

static double now_s(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

#define SYM(name) ((typeof(&name))dlsym(core, #name))

int main(int argc, char** argv) {
	setvbuf(stdout, NULL, _IOLBF, 0); // a killed run still leaves its lines in a log file
	if (argc < 7) {
		fprintf(stderr, "usage: %s <core.so> <system dir> <save dir> <cart> <frames> <out.ppm>\n", argv[0]);
		return 2;
	}
	system_dir = argv[2];
	save_dir = argv[3];
	int frames = atoi(argv[5]);
	held = getenv("HOST_BUTTONS") ? (uint32_t)strtoul(getenv("HOST_BUTTONS"), NULL, 0) : 0;
	void* core = dlopen(argv[1], RTLD_NOW);
	if (!core) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		return 1;
	}
	struct retro_system_info si;
	SYM(retro_get_system_info)(&si);
	printf("host: %s %s, extensions %s, need_fullpath %d\n", si.library_name, si.library_version,
		   si.valid_extensions, si.need_fullpath);
	SYM(retro_set_environment)(env_cb);
	SYM(retro_set_video_refresh)(video);
	SYM(retro_set_audio_sample)(audio_one);
	SYM(retro_set_audio_sample_batch)(audio_batch);
	SYM(retro_set_input_poll)(poll);
	SYM(retro_set_input_state)(state);
	SYM(retro_init)();
	struct retro_game_info game = {.path = argv[4]};
	double t0 = now_s();
	if (!SYM(retro_load_game)(&game)) {
		fprintf(stderr, "host: retro_load_game failed\n");
		return 1;
	}
	struct retro_system_av_info av;
	SYM(retro_get_system_av_info)(&av);
	printf("host: av %ux%u aspect %.2f fps %.1f rate %.0f\n", av.geometry.base_width, av.geometry.base_height,
		   av.geometry.aspect_ratio, av.timing.fps, av.timing.sample_rate);
	double first = 0;
	for (frame_no = 0; frame_no < frames && !shutdown_asked; frame_no++) {
		SYM(retro_run)();
		if (frame_no == 0)
			first = now_s();
	}
	double t1 = now_s();
	printf("host: %d runs, %u video calls (%u NULL, %u changed), shutdown asked %d\n", frame_no, frames_seen,
		   frames_null, frames_changed, shutdown_asked);
	printf("host: first run %.2f s after load, then %.1f runs/s\n", first - t0,
		   frame_no > 1 ? (frame_no - 1) / (t1 - first) : 0.0);
	printf("host: audio %llu frames (%.1f per run), %llu non-zero samples\n", audio_total,
		   frame_no ? (double)audio_total / frame_no : 0.0, audio_nonzero);
	if (last_fb) {
		FILE* f = fopen(argv[6], "wb");
		if (f) {
			fprintf(f, "P6\n128 128\n255\n");
			for (int i = 0; i < 128 * 128; i++) {
				uint8_t rgb[3] = {(uint8_t)(last_fb[i] >> 16), (uint8_t)(last_fb[i] >> 8), (uint8_t)last_fb[i]};
				fwrite(rgb, 1, 3, f);
			}
			fclose(f);
		}
	}
	double u0 = now_s();
	SYM(retro_unload_game)();
	SYM(retro_deinit)();
	printf("host: unload took %.2f s\n", now_s() - u0);
	dlclose(core);
	return shutdown_asked || frame_no != frames;
}
