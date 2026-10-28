// Device spike for the shim: plays the core's side of p8ipc against the real pico8_64.
//
//   spike_host <pico8shim.so> <frames> <out.ppm> <pico8_64> [pico8 args...]
//
// Runs pico8_64 with SDL's dummy drivers and the shim preloaded (P8NATIVE_HOME and
// P8NATIVE_CDATA pass through from the environment), posts <frames> frame requests with
// frame_ms=16 (holding $SPIKE_BUTTONS from request 300), writes the last frame to <out.ppm>,
// prints frames/s and audio totals, then asks it to quit. The shim's own log (formats, audio spec, window) goes to stderr.
// Built for the device by `make spike` (see Makefile).
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../p8ipc.h"

static double now_s(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int write_ppm(const char* path, const uint32_t* fb) {
	FILE* f = fopen(path, "wb");
	if (!f)
		return -1;
	fprintf(f, "P6\n%d %d\n255\n", P8_W, P8_H);
	for (int i = 0; i < P8_W * P8_H; i++) {
		uint8_t rgb[3] = {(uint8_t)(fb[i] >> 16), (uint8_t)(fb[i] >> 8), (uint8_t)fb[i]};
		fwrite(rgb, 1, 3, f);
	}
	return fclose(f);
}

int main(int argc, char** argv) {
	if (argc < 5) {
		fprintf(stderr, "usage: %s <pico8shim.so> <frames> <out.ppm> <pico8_64> [args...]\n", argv[0]);
		return 2;
	}
	int frames = atoi(argv[2]);
	// SPIKE_BUTTONS: joypad bitmask (RETRO_DEVICE_ID_JOYPAD_*) held from request 300 on
	uint32_t held = getenv("SPIKE_BUTTONS") ? (uint32_t)strtoul(getenv("SPIKE_BUTTONS"), NULL, 0) : 0;
	char ipc_path[64];
	snprintf(ipc_path, sizeof(ipc_path), "/tmp/p8native.%d.ipc", (int)getpid());
	struct p8ipc* ipc = p8ipc_create(ipc_path);
	if (!ipc) {
		perror("p8ipc_create");
		return 1;
	}

	pid_t child = fork();
	if (child == 0) {
		setenv("P8NATIVE_IPC", ipc_path, 1);
		setenv("SDL_VIDEODRIVER", "dummy", 0); // an experiment may name another
		setenv("SDL_AUDIODRIVER", "dummy", 0);
		setenv("LD_PRELOAD", argv[1], 1);
		execv(argv[4], argv + 4);
		_exit(127);
	}

	uint32_t first_serial = ipc->fb_serial, presented = 0, window = 0;
	uint64_t audio_total = 0, audio_nonzero = 0;
	double t0 = now_s(), first_done = 0;
	int done = 0;
	for (; done < frames; done++) {
		uint32_t before = ipc->fb_serial;
		ipc->buttons = done >= 300 ? held : 0; // after PICO-8's boot
		ipc->frame_ms = 16;
		sem_post(&ipc->frame_req);
		if (p8ipc_wait(&ipc->frame_done, 10000) != 0) {
			fprintf(stderr, "spike: no frame_done for request %d (reason: %s)\n", done, ipc->reason);
			break;
		}
		if (done == 0)
			first_done = now_s();
		presented += ipc->fb_serial != before;
		window += ipc->fb_serial != before;
		if ((done + 1) % 100 == 0) { // presents per 100 requests: boot, then the cart's own rate
			printf("spike: requests %d-%d: %u presents\n", done - 98, done + 1, window);
			window = 0;
		}
		audio_total += ipc->audio_frames;
		for (uint32_t k = 0; k < ipc->audio_frames * 2; k++)
			audio_nonzero += ipc->audio[k] != 0;
	}
	double t1 = now_s();

	printf("spike: %d requests answered, %u presents (serial %u → %u)\n", done, presented, first_serial,
		   ipc->fb_serial);
	printf("spike: first frame after %.2f s; then %.1f requests/s\n", first_done - t0,
		   done > 1 ? (done - 1) / (t1 - first_done) : 0.0);
	printf("spike: audio rate %u Hz, %llu frames total, %llu non-zero samples\n", ipc->audio_rate,
		   (unsigned long long)audio_total, (unsigned long long)audio_nonzero);
	if (ipc->reason[0])
		printf("spike: shim reason: %s\n", ipc->reason);
	if (write_ppm(argv[3], ipc->fb) == 0)
		printf("spike: last frame → %s\n", argv[3]);

	ipc->cmd = P8CMD_QUIT;
	sem_post(&ipc->frame_req);
	p8ipc_wait(&ipc->frame_done, 1000);
	int status = 0;
	pid_t w = 0;
	for (int i = 0; i < 100 && w == 0; i++) {
		w = waitpid(child, &status, WNOHANG);
		if (w == 0)
			usleep(10000);
	}
	if (w == 0) {
		printf("spike: pico8_64 still running 1 s after quit; killing it\n");
		kill(child, SIGKILL);
		waitpid(child, &status, 0);
	} else
		printf("spike: pico8_64 exited (%s %d)\n", WIFEXITED(status) ? "status" : "signal",
			   WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	p8ipc_close(ipc);
	unlink(ipc_path);
	return done == frames ? 0 : 1;
}
