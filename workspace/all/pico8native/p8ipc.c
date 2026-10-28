#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "p8ipc.h"

// Signals can land mid-wait (SIGCHLD, SIGALRM...); retry this many times before giving up
#define P8IPC_EINTR_RETRIES 16

static struct p8ipc* map_fd(int fd) {
	void* p = mmap(NULL, sizeof(struct p8ipc), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	return p == MAP_FAILED ? NULL : p;
}

struct p8ipc* p8ipc_create(const char* path) {
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return NULL;
	// O_TRUNC drops any stale block from a dead run; the new size reads as zeros
	if (ftruncate(fd, sizeof(struct p8ipc)) != 0) {
		close(fd);
		unlink(path);
		return NULL;
	}
	struct p8ipc* ipc = map_fd(fd);
	close(fd);
	if (!ipc) {
		unlink(path);
		return NULL;
	}
	if (sem_init(&ipc->frame_req, 1, 0) != 0 || sem_init(&ipc->frame_done, 1, 0) != 0) {
		munmap(ipc, sizeof(*ipc));
		unlink(path);
		return NULL;
	}
	ipc->version = P8IPC_VERSION;
	// magic last: an attacher that sees it can trust the rest is set up
	__atomic_store_n(&ipc->magic, P8IPC_MAGIC, __ATOMIC_RELEASE);
	return ipc;
}

struct p8ipc* p8ipc_attach(const char* path) {
	int fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	struct stat st;
	// too short means stale or foreign; mapping it would fault past the end
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(struct p8ipc)) {
		close(fd);
		return NULL;
	}
	struct p8ipc* ipc = map_fd(fd);
	close(fd);
	if (!ipc)
		return NULL;
	if (__atomic_load_n(&ipc->magic, __ATOMIC_ACQUIRE) != P8IPC_MAGIC || ipc->version != P8IPC_VERSION) {
		munmap(ipc, sizeof(*ipc));
		return NULL;
	}
	return ipc;
}

// Unmap only: the core owns the file and unlinks it; the sems live in the file
void p8ipc_close(struct p8ipc* ipc) {
	if (ipc)
		munmap(ipc, sizeof(*ipc));
}

int p8ipc_wait(sem_t* sem, int timeout_ms) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts); // sem_timedwait takes an absolute CLOCK_REALTIME deadline
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	for (int i = 0; i <= P8IPC_EINTR_RETRIES; i++) {
		if (sem_timedwait(sem, &ts) == 0)
			return 0;
		if (errno != EINTR)
			return -1;
	}
	return -1;
}
