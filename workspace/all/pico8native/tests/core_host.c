// Device harness for pico8native_libretro.so: a minimal libretro frontend that
// loads the core, runs a cart for <frames> retro_runs and unloads it, without
// minarch or the display.
//
//   core_host <pico8native_libretro.so> <system dir> <save dir> <cart> <frames> <out.ppm>
//
// HOST_BUTTONS (RETRO_DEVICE_ID_JOYPAD_* mask) is held from frame 300 on. The core
// finds pico8shim.so and lib/ next to itself, and PICO-8's home under
// $SHARED_USERDATA_PATH/P8-native/home. Built for the device by `make corehost`.
//
// Save states, as minarch does them (HOST_STATE is the state file):
//   HOST_SAVE_AT=<n>  before run n: serialize_size, serialize, write the whole buffer
//   HOST_LOAD_AT=<n>  from run n on: read the file, unserialize, retried each run until
//                     it takes (as minarch's resume) or 600 runs pass
// A hash of the picture is printed for the 4 runs after a save and after a load: an
// exact continuation prints the same hashes.
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
static uint32_t last_hash;

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
	uint32_t hash = 2166136261u;
	for (int i = 0; i < 128 * 128; i++)
		hash = (hash ^ ((const uint32_t*)data)[i]) * 16777619u;
	last_hash = hash;
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

static void* core;

static int save_state(const char* path) {
	double t0 = now_s();
	size_t size = SYM(retro_serialize_size)();
	double t1 = now_s();
	size_t again = SYM(retro_serialize_size)(); // cached: no frame ran
	double t2 = now_s();
	if (!size) {
		printf("host: serialize_size 0\n");
		return -1;
	}
	void* buf = calloc(1, size);
	int ok = buf && SYM(retro_serialize)(buf, size);
	double t3 = now_s();
	FILE* f = ok ? fopen(path, "wb") : NULL;
	ok = f && fwrite(buf, 1, size, f) == size;
	if (f)
		ok = fclose(f) == 0 && ok;
	sync();
	double t4 = now_s();
	printf("host: save at run %d: size %zu (again %zu), serialize_size %.0f ms, again %.1f ms, serialize %.1f ms, "
		   "write+sync %.0f ms → %s\n",
		   frame_no, size, again, (t1 - t0) * 1e3, (t2 - t1) * 1e3, (t3 - t2) * 1e3, (t4 - t3) * 1e3, ok ? "ok" : "FAILED");
	free(buf);
	return ok ? 0 : -1;
}

// 1 loaded, 0 refused, -1 no file
static int load_state(const char* path, int quiet) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	double t0 = now_s();
	size_t min = SYM(retro_serialize_size)();
	double t1 = now_s();
	size_t size = (size_t)len > min ? (size_t)len : min; // as minarch's State_readData
	void* buf = calloc(1, size);
	int ok = buf && fread(buf, 1, (size_t)len, f) == (size_t)len;
	fclose(f);
	double t2 = now_s();
	ok = ok && SYM(retro_unserialize)(buf, size);
	double t3 = now_s();
	if (ok || !quiet)
		printf("host: load at run %d: file %ld, serialize_size %zu (%.0f ms), read %.0f ms, unserialize %.0f ms → %s\n",
			   frame_no, len, min, (t1 - t0) * 1e3, (t2 - t1) * 1e3, (t3 - t2) * 1e3, ok ? "ok" : "refused");
	free(buf);
	return ok;
}

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
	const char* state_path = getenv("HOST_STATE");
	int save_at = getenv("HOST_SAVE_AT") ? atoi(getenv("HOST_SAVE_AT")) : -1;
	int load_at = getenv("HOST_LOAD_AT") ? atoi(getenv("HOST_LOAD_AT")) : -1;
	int report = 0, load_tries = 0;
	core = dlopen(argv[1], RTLD_NOW);
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
		if (state_path && frame_no == save_at && save_state(state_path) == 0)
			report = 4;
		if (state_path && load_at >= 0 && frame_no >= load_at) {
			int r = load_state(state_path, load_tries > 0);
			if (r == 1) {
				printf("host: loaded after %d tries\n", load_tries + 1);
				report = 4;
				load_at = -1;
			} else if (r < 0 || ++load_tries >= 600) {
				printf("host: load gave up (%s)\n", r < 0 ? "no file" : "600 tries");
				load_at = -1;
			}
		}
		SYM(retro_run)();
		if (frame_no == 0)
			first = now_s();
		if (report) {
			printf("host: run %d picture %08x\n", frame_no, last_hash);
			report--;
		}
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
