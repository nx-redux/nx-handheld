// Host test for pico8shim.so: plays the core's side of p8ipc against fake_pico8 running
// under LD_PRELOAD=pico8shim.so with a fake libSDL2. Usage: test_shim <shim.so> <fake_pico8> <fps>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../p8ipc.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

#define FRAMES 120
#define RETRO_B 0
#define RETRO_START 3
#define RETRO_UP 4
#define RETRO_A 8

static char root[64], ipc_path[96];
static pid_t child;

static void cleanup(void) {
	if (child > 0)
		kill(child, SIGKILL);
	unlink(ipc_path);
	char cmd[96];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", root);
}

static uint32_t px(struct p8ipc* ipc, int i) {
	return ipc->fb[i];
}

// Posts one frame request and waits for it; frame_ms 17/17/16 as the core sends them
static void step(struct p8ipc* ipc, uint32_t buttons, uint32_t frame_ms) {
	ipc->buttons = buttons;
	ipc->frame_ms = frame_ms;
	sem_post(&ipc->frame_req);
	CHECK(p8ipc_wait(&ipc->frame_done, 3000) == 0);
}

int main(int argc, char** argv) {
	CHECK(argc == 4);
	int fps = atoi(argv[3]);
	snprintf(root, sizeof(root), "/tmp/nx_test_shim.%d", (int)getpid());
	snprintf(ipc_path, sizeof(ipc_path), "/tmp/p8native_test_shim.%d.ipc", (int)getpid());
	atexit(cleanup);
	CHECK(mkdir(root, 0755) == 0);

	char home[128], cdata[128];
	snprintf(home, sizeof(home), "%s/home", root);
	snprintf(cdata, sizeof(cdata), "%s/Saves/P8/cdata", root);
	CHECK(mkdir(home, 0755) == 0);

	struct p8ipc* ipc = p8ipc_create(ipc_path);
	CHECK(ipc);

	child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		setenv("P8NATIVE_IPC", ipc_path, 1);
		setenv("P8NATIVE_HOME", home, 1);
		setenv("P8NATIVE_CDATA", cdata, 1);
		setenv("FAKE_FPS", argv[3], 1);
		setenv("LD_PRELOAD", argv[1], 1);
		execl(argv[2], argv[2], (char*)NULL);
		_exit(127);
	}

	uint32_t granted = 0, audio_total = 0, last_ticks = 0;
	uint32_t serial0 = 0, presented = 0;
	int16_t expect = 0;
	for (int i = 0; i < FRAMES; i++) {
		uint32_t ms = i % 3 == 2 ? 16 : 17;
		uint32_t buttons = 0;
		if (i >= 30 && i < 40)
			buttons = 1u << RETRO_B;
		else if (i >= 40 && i < 50)
			buttons = (1u << RETRO_A) | (1u << RETRO_START) | (1u << RETRO_UP);
		else if (i >= 50 && i < 60)
			buttons = 0xffffffffu & ~((1u << RETRO_B) | (1u << RETRO_A) | (1u << RETRO_START) | (1u << RETRO_UP)) & 0xffffu;
		uint32_t before = ipc->fb_serial;
		step(ipc, buttons, ms);
		granted += ms;
		if (i == 0)
			serial0 = before;
		int fresh = ipc->fb_serial != before;
		// one present per request at most; the first may also carry the frame PICO-8
		// presented before any request, if that came after `before` was read
		CHECK(ipc->fb_serial - before <= (i == 0 ? 2u : 1u));
		presented += fresh;

		// PICO-8's clock never runs ahead of the time granted, nor backwards
		uint32_t ticks = px(ipc, 1);
		CHECK(ticks <= granted && ticks >= last_ticks);
		last_ticks = ticks;

		if (fresh) {
			uint32_t bits = px(ipc, 2);
			if (buttons == 1u << RETRO_B)
				CHECK(bits == 1); // B → controller A (O)
			else if (buttons & (1u << RETRO_A))
				CHECK(bits == (2 | 4 | 8)); // A → controller B (X), Start, Up
			else
				CHECK(bits == 0);			  // anything else held reaches nothing
			CHECK(px(ipc, 3) == 0);			  // no real input leaked
			CHECK(px(ipc, 100) == 0x123456u); // the rest of the screen, converted
		}

		// Audio: exactly the slice's share at the rate PICO-8 opened, mono → both sides, no gaps
		CHECK(ipc->audio_rate == 22050);
		for (uint32_t k = 0; k < ipc->audio_frames; k++) {
			CHECK(ipc->audio[k * 2] == expect && ipc->audio[k * 2 + 1] == expect);
			expect++;
		}
		audio_total += ipc->audio_frames;
	}
	CHECK(audio_total == 22050u * granted / 1000);
	printf("test_shim %d fps: %u requests → %u frames, %u audio frames, clock %u/%u ms (serial from %u)\n", fps, FRAMES,
		   presented, audio_total, last_ticks, granted, serial0);
	if (fps == 60)
		CHECK(presented >= FRAMES - 2);
	else
		CHECK(presented >= FRAMES / 2 - 2 && presented <= FRAMES / 2 + 2);

	// The cart's save went to the shared save dir, not PICO-8's home
	char path[256];
	struct stat st;
	snprintf(path, sizeof(path), "%s/fake.p8d.txt", cdata);
	CHECK(stat(path, &st) == 0 && st.st_size == 6);
	snprintf(path, sizeof(path), "%s/cdata/fake.p8d.txt", home);
	CHECK(stat(path, &st) != 0);

	// A command runs while parked, instead of a frame
	uint32_t serial = ipc->fb_serial;
	ipc->cmd = P8CMD_SAVE;
	step(ipc, 0, 17);
	CHECK(ipc->cmd_result == 0 && ipc->reason[0] && ipc->cmd == P8CMD_NONE);
	CHECK(ipc->fb_serial == serial);
	step(ipc, 0, 17);
	CHECK(ipc->fb_serial == serial + 1 || fps == 30);

	// Quit: accepted at once, and PICO-8 exits on the SDL_QUIT it is handed
	ipc->cmd = P8CMD_QUIT;
	step(ipc, 0, 16);
	CHECK(ipc->cmd_result == 1);
	int status = 0;
	pid_t w = 0;
	for (int i = 0; i < 100 && w == 0; i++) {
		w = waitpid(child, &status, WNOHANG);
		if (w == 0)
			usleep(10000);
	}
	CHECK(w == child);
	child = 0;
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	p8ipc_close(ipc);
	printf("test_shim %d fps: ok\n", fps);
	return 0;
}
