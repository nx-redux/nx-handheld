// Host test for pico8native_libretro.so as minarch drives it: the core, built for the host,
// starts fake_pico8 (as Bios/P8/pico8_64) under pico8shim.so and a fake libSDL2.
//
//   test_core <dir with pico8native_libretro.so, pico8shim.so, fake_pico8>
//
// - a frame that takes 4 s (past the old 3 s limit) does not end the game: retro_run keeps
//   returning at once with the last picture and no sound, a save waits that frame out,
//   and the frame is shown when it comes
// - the ipc file is gone from /tmp once the first frame is done
// - PICO-8 exiting with 0 (its own shutdown) ends the game without an error; exiting
//   with 3 ends it with one
// - a shim that turns states off (its arena full) makes serialize_size 0
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "libretro.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

static char root[64], system_dir[128], save_dir[128], cart[160];
static int shutdown_asked, errors, quit_logged, states_off_logged;
static unsigned video_calls, video_null;
static uint32_t last_pixel0;
static size_t audio_this_run;
static long longest_run; // ms, of any retro_run since the last load

static void log_printf(enum retro_log_level level, const char* fmt, ...) {
	char msg[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	fprintf(stderr, "[%d] %s", (int)level, msg);
	if (level >= RETRO_LOG_ERROR)
		errors++;
	if (strstr(msg, "PICO-8 quit"))
		quit_logged++;
	if (strstr(msg, "without save states"))
		states_off_logged++;
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
		return false;
	}
}

static void video(const void* data, unsigned w, unsigned h, size_t pitch) {
	(void)w, (void)h, (void)pitch;
	video_calls++;
	if (!data) {
		video_null++;
		return;
	}
	last_pixel0 = ((const uint32_t*)data)[0] & 0xffffff; // fake_pico8's frame number
}

static size_t audio_batch(const int16_t* data, size_t frames) {
	(void)data;
	audio_this_run += frames;
	return frames;
}
static void audio_one(int16_t l, int16_t r) {
	(void)l, (void)r;
}
static void poll_cb(void) {}
static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id) {
	(void)port, (void)dev, (void)idx, (void)id;
	return 0;
}

static void* core;
#define SYM(name) ((typeof(&name))dlsym(core, #name))

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int ipc_file_exists(void) {
	char path[64];
	snprintf(path, sizeof(path), "/tmp/p8native.%d.ipc", (int)getpid());
	return access(path, F_OK) == 0;
}

// One retro_run: how long it took; *shown is 1 if it gave a picture
static long run(int* shown) {
	unsigned null_before = video_null, calls_before = video_calls;
	audio_this_run = 0;
	long t0 = now_ms();
	SYM(retro_run)();
	long took = now_ms() - t0;
	if (took > longest_run)
		longest_run = took;
	CHECK(video_calls == calls_before + 1); // one picture or dupe per run, always
	*shown = video_null == null_before;
	if (!*shown)
		CHECK(audio_this_run == 0); // no sound while the frame is still running
	return took;
}

static void load(void) {
	shutdown_asked = errors = quit_logged = states_off_logged = 0;
	video_calls = video_null = 0;
	longest_run = 0;
	struct retro_game_info game = {.path = cart};
	CHECK(SYM(retro_load_game)(&game));
}

static void unload(void) {
	long t0 = now_ms();
	SYM(retro_unload_game)();
	CHECK(now_ms() - t0 < 1500);
	CHECK(!ipc_file_exists());
	unsetenv("FAKE_SLOW_AT");
	unsetenv("FAKE_SLOW_MS");
	unsetenv("FAKE_EXIT_AT");
	unsetenv("FAKE_DIE_AT");
	unsetenv("FAKE_FILL_ARENA");
}

// Runs until n pictures were shown (or the game ends, or 15 s pass)
static void run_until_shown(int n) {
	long t0 = now_ms();
	for (int shown_count = 0; shown_count < n && !shutdown_asked && now_ms() - t0 < 15000;) {
		int shown;
		run(&shown);
		shown_count += shown;
	}
}

static void stall(void) {
	setenv("FAKE_SLOW_AT", "30", 1);
	setenv("FAKE_SLOW_MS", "4000", 1);
	load();
	CHECK(ipc_file_exists()); // until the first frame is done
	run_until_shown(1);
	CHECK(!ipc_file_exists());
	// up to the slow frame (fake frame 30) and through it: every run returns quickly
	run_until_shown(25);
	CHECK(!shutdown_asked);
	unsigned dupes_before = video_null;
	long t0 = now_ms();
	int shown = 1;
	while (shown && now_ms() - t0 < 2000) // to the slow frame
		run(&shown);
	CHECK(!shown);
	// a save in the middle of it waits for the frame, then takes the state
	for (int i = 0; i < 5; i++)
		run(&shown);
	CHECK(!shown);
	size_t size = SYM(retro_serialize_size)(); // the menu's question: answered at once
	CHECK(size > 0);
	void* buf = malloc(size);
	CHECK(buf);
	long s0 = now_ms();
	CHECK(SYM(retro_serialize)(buf, size));
	long save_ms = now_ms() - s0;
	free(buf);
	CHECK(!shutdown_asked);
	run(&shown);
	CHECK(shown); // the frame that ended during the save
	uint32_t slow_frame = last_pixel0;
	run_until_shown(30);
	CHECK(!shutdown_asked && last_pixel0 > slow_frame);
	unsigned dupes = video_null - dupes_before;
	printf("test_core: a 4 s frame: %u dupes, the save waited %ld ms, longest run %ld ms, then frame %u\n", dupes,
		   save_ms, longest_run, last_pixel0);
	CHECK(longest_run < 400 && dupes >= 6); // the save waited out the rest
	CHECK(errors == 0);
	unload();
}

static void own_quit(void) {
	setenv("FAKE_EXIT_AT", "20", 1);
	load();
	run_until_shown(100);
	CHECK(shutdown_asked && quit_logged == 1 && errors == 0);
	int shown;
	run(&shown); // the frontend runs on until it stops: dupes
	CHECK(!shown);
	unload();
	printf("test_core: PICO-8's own quit ends the game without an error\n");
}

static void dies(void) {
	setenv("FAKE_DIE_AT", "20", 1);
	load();
	run_until_shown(100);
	CHECK(shutdown_asked && quit_logged == 0 && errors > 0);
	unload();
	printf("test_core: PICO-8 exiting with 3 ends the game with an error\n");
}

static void states_off(void) {
	setenv("FAKE_FILL_ARENA", "1", 1);
	load();
	run_until_shown(10);
	CHECK(!shutdown_asked);
	CHECK(SYM(retro_serialize_size)() == 0);
	CHECK(states_off_logged == 1);
	CHECK(SYM(retro_serialize_size)() == 0);
	CHECK(states_off_logged == 1);
	unload();
	printf("test_core: states off in the shim: serialize_size 0\n");
}

static void states_on(void) {
	load();
	run_until_shown(20);
	CHECK(SYM(retro_serialize_size)() > 0);
	unload();
}

int main(int argc, char** argv) {
	CHECK(argc == 2);
	setvbuf(stdout, NULL, _IOLBF, 0);
	snprintf(root, sizeof(root), "/tmp/nx_test_core.%d", (int)getpid());
	snprintf(system_dir, sizeof(system_dir), "%s/Bios/P8", root);
	snprintf(save_dir, sizeof(save_dir), "%s/Saves/P8", root);
	snprintf(cart, sizeof(cart), "%s/Roms/fake.p8", root);
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
			 "mkdir -p %s %s %s/Roms %s/shared && cp %s/fake_pico8 %s/pico8_64 && : >%s/pico8.dat && : >%s",
			 system_dir, save_dir, root, root, argv[1], system_dir, system_dir, cart);
	CHECK(system(cmd) == 0);
	char shared[96];
	snprintf(shared, sizeof(shared), "%s/shared", root);
	setenv("SHARED_USERDATA_PATH", shared, 1);
	setenv("FAKE_FPS", "60", 1);
	setenv("FAKE_STATES", "1", 1);

	char so[256];
	snprintf(so, sizeof(so), "%s/pico8native_libretro.so", argv[1]);
	core = dlopen(so, RTLD_NOW);
	if (!core)
		fprintf(stderr, "dlopen: %s\n", dlerror());
	CHECK(core);
	SYM(retro_set_environment)(env_cb);
	SYM(retro_set_video_refresh)(video);
	SYM(retro_set_audio_sample)(audio_one);
	SYM(retro_set_audio_sample_batch)(audio_batch);
	SYM(retro_set_input_poll)(poll_cb);
	SYM(retro_set_input_state)(input);
	SYM(retro_init)();

	states_on();
	stall();
	own_quit();
	dies();
	states_off();

	SYM(retro_deinit)();
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", root);
	printf("test_core: ok\n");
	return 0;
}
