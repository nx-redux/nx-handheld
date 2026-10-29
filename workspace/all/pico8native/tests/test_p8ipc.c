// Host test for p8ipc.c: core (parent) and shim (child) in lockstep over the shared block.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../p8ipc.h"

// Always evaluated (unlike assert), and exit() so the atexit cleanup still runs
#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

#define ROUNDS 3

static char path[64];
static char bad[64];

static void cleanup(void) {
	unlink(path);
	unlink(bad);
}

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// The shim side: attach, then answer each frame_req with one "frame".
static int shim_child(void) {
	struct p8ipc* ipc = p8ipc_attach(path);
	if (!ipc)
		return 10;
	for (int i = 0; i < ROUNDS; i++) {
		if (p8ipc_wait(&ipc->frame_req, 2000) != 0)
			return 11;
		ipc->fb[0] = ipc->buttons;
		ipc->fb_serial++;
		sem_post(&ipc->frame_done);
	}
	p8ipc_close(ipc);
	return 0;
}

int main(void) {
	snprintf(path, sizeof(path), "/tmp/p8native_test.%d.ipc", (int)getpid());
	snprintf(bad, sizeof(bad), "/tmp/p8native_bad.%d.ipc", (int)getpid());
	atexit(cleanup);

	struct p8ipc* ipc = p8ipc_create(path);
	CHECK(ipc);
	CHECK(ipc->magic == P8IPC_MAGIC && ipc->version == P8IPC_VERSION);
	CHECK(ipc->fb_serial == 0 && ipc->cmd == P8CMD_NONE);

	pid_t pid = fork();
	CHECK(pid >= 0);
	if (pid == 0)
		_exit(shim_child()); // _exit: the child must not run the parent's cleanup

	// Lockstep: each round the child echoes buttons into fb[0] and bumps fb_serial.
	for (uint32_t round = 1; round <= ROUNDS; round++) {
		ipc->buttons = round;
		sem_post(&ipc->frame_req);
		int r = p8ipc_wait(&ipc->frame_done, 2000);
		CHECK(r == 0);
		CHECK(ipc->fb[0] == round);
		CHECK(ipc->fb_serial == round);
	}

	int status = 0;
	pid_t w = waitpid(pid, &status, 0);
	CHECK(w == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	// Nobody posts: times out with -1, and not much later than asked.
	long t0 = now_ms();
	int r = p8ipc_wait(&ipc->frame_done, 50);
	long dt = now_ms() - t0;
	CHECK(r == -1);
	CHECK(dt >= 40 && dt < 100);

	p8ipc_close(ipc);
	unlink(path);

	// A stale or foreign file: wrong magic is refused.
	struct p8ipc* b = p8ipc_create(bad);
	CHECK(b);
	b->magic = 0xdeadbeefu;
	p8ipc_close(b);
	CHECK(p8ipc_attach(bad) == NULL);
	unlink(bad);

	// A file too short to hold the block is refused too.
	int fd = open(bad, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	CHECK(fd >= 0);
	ssize_t n = write(fd, "P8N1", 4);
	close(fd);
	CHECK(n == 4);
	CHECK(p8ipc_attach(bad) == NULL);
	unlink(bad);

	// No file at all.
	CHECK(p8ipc_attach("/tmp/p8native_missing.ipc") == NULL);

	puts("test_p8ipc: ok");
	return 0;
}
