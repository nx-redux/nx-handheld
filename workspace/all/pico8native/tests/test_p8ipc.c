// Host test for p8ipc.c: core (parent) and shim (child) in lockstep over the shared block.
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../p8ipc.h"

#define ROUNDS 3

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// The shim side: attach, then answer each frame_req with one "frame".
static int shim_child(const char* path) {
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
	char path[64];
	snprintf(path, sizeof(path), "/tmp/p8native_test.%d.ipc", (int)getpid());

	struct p8ipc* ipc = p8ipc_create(path);
	assert(ipc);
	assert(ipc->magic == P8IPC_MAGIC && ipc->version == P8IPC_VERSION);
	assert(ipc->fb_serial == 0 && ipc->cmd == P8CMD_NONE);

	pid_t pid = fork();
	assert(pid >= 0);
	if (pid == 0)
		_exit(shim_child(path));

	// Lockstep: each round the child echoes buttons into fb[0] and bumps fb_serial.
	for (uint32_t round = 1; round <= ROUNDS; round++) {
		ipc->buttons = round;
		sem_post(&ipc->frame_req);
		assert(p8ipc_wait(&ipc->frame_done, 2000) == 0);
		assert(ipc->fb[0] == round);
		assert(ipc->fb_serial == round);
	}

	int status = 0;
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	// Nobody posts: times out with -1, and not much later than asked.
	long t0 = now_ms();
	assert(p8ipc_wait(&ipc->frame_done, 50) == -1);
	long dt = now_ms() - t0;
	assert(dt >= 40 && dt < 100);

	p8ipc_close(ipc);
	unlink(path);

	// A stale or foreign file: wrong magic is refused.
	char bad[64];
	snprintf(bad, sizeof(bad), "/tmp/p8native_bad.%d.ipc", (int)getpid());
	struct p8ipc* b = p8ipc_create(bad);
	assert(b);
	b->magic = 0xdeadbeefu;
	p8ipc_close(b);
	assert(p8ipc_attach(bad) == NULL);
	unlink(bad);

	// A file too short to hold the block is refused too.
	int fd = open(bad, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	assert(fd >= 0);
	assert(write(fd, "P8N1", 4) == 4);
	close(fd);
	assert(p8ipc_attach(bad) == NULL);
	unlink(bad);

	// No file at all.
	assert(p8ipc_attach("/tmp/p8native_missing.ipc") == NULL);

	puts("test_p8ipc: ok");
	return 0;
}
