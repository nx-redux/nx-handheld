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
// sem_timedwait only takes a CLOCK_REALTIME deadline (no sem_clockwait in the device's glibc 2.28)
// and ntpd can step that clock mid-game. The timeout is kept on CLOCK_MONOTONIC instead, waiting
// in slices of at most this long, each with a fresh realtime deadline.
#define P8IPC_SLICE_MS 100

// Swappable so the host test can step a fake wall clock
static int (*p8_clock_gettime)(clockid_t, struct timespec*) = clock_gettime;
static int (*p8_sem_timedwait)(sem_t*, const struct timespec*) = sem_timedwait;

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

static long clock_ms(clockid_t clk) {
	struct timespec ts;
	p8_clock_gettime(clk, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int p8ipc_wait(sem_t* sem, int timeout_ms) {
	long start = clock_ms(CLOCK_MONOTONIC);
	int eintr = 0;
	for (;;) {
		long left = timeout_ms - (clock_ms(CLOCK_MONOTONIC) - start);
		if (left <= 0)
			return sem_trywait(sem) == 0 ? 0 : -1; // a post right at the deadline still counts
		long slice = left < P8IPC_SLICE_MS ? left : P8IPC_SLICE_MS;
		struct timespec ts;
		p8_clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += slice / 1000;
		ts.tv_nsec += (slice % 1000) * 1000000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		if (p8_sem_timedwait(sem, &ts) == 0)
			return 0;
		// a forward step expires the slice early: the monotonic check above decides if time is really up
		if (errno == EINTR) {
			if (++eintr > P8IPC_EINTR_RETRIES)
				return -1;
		} else if (errno != ETIMEDOUT)
			return -1;
	}
}
