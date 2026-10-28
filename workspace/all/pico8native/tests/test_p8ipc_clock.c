// Host test for p8ipc_wait against wall-clock steps, on fake clocks.
// Includes p8ipc.c to swap its clock_gettime/sem_timedwait hooks; no real time passes.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "../p8ipc.c"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

static long mono, real; // fake clocks, ms
static long step_ms;	// added to the wall clock at the step_read'th realtime read
static int step_read, real_reads;
static long post_at = -1; // monotonic time the other side posts, -1 never
static int eintr_left;	  // timedwait calls that return EINTR first
static int waits;

static int fake_clock(clockid_t clk, struct timespec* ts) {
	long ms = mono;
	if (clk == CLOCK_REALTIME) {
		if (++real_reads == step_read)
			real += step_ms;
		ms = real;
	}
	ts->tv_sec = ms / 1000;
	ts->tv_nsec = (ms % 1000) * 1000000L;
	return 0;
}

// Sleeps (in fake time) until the realtime deadline, or until the post lands
static int fake_timedwait(sem_t* sem, const struct timespec* abs) {
	waits++;
	if (eintr_left > 0) {
		eintr_left--;
		mono++;
		real++;
		errno = EINTR;
		return -1;
	}
	long until = abs->tv_sec * 1000L + abs->tv_nsec / 1000000L;
	long dt = until > real ? until - real : 0;
	if (post_at >= 0 && mono + dt >= post_at) {
		long d = post_at > mono ? post_at - mono : 0;
		mono += d;
		real += d;
		return 0;
	}
	mono += dt;
	real += dt;
	errno = ETIMEDOUT;
	return -1;
}

static void reset(long step, int at_read, long post, int eintrs) {
	mono = 5000;
	real = 1700000000000L;
	step_ms = step;
	step_read = at_read;
	real_reads = 0;
	post_at = post;
	eintr_left = eintrs;
	waits = 0;
}

int main(void) {
	p8_clock_gettime = fake_clock;
	p8_sem_timedwait = fake_timedwait;
	sem_t sem;
	CHECK(sem_init(&sem, 0, 0) == 0);

	// No step: times out after exactly the timeout, in 100 ms slices
	reset(0, 0, -1, 0);
	CHECK(p8ipc_wait(&sem, 250) == -1);
	CHECK(mono - 5000 == 250);
	CHECK(waits == 3);

	// ntpd steps the wall clock 10 s forward as the wait starts: the first slice expires at
	// once, but the wait still runs its full 3 s on the monotonic clock
	reset(10000, 1, -1, 0);
	CHECK(p8ipc_wait(&sem, 3000) == -1);
	CHECK(mono - 5000 == 3000);

	// Same step, and the frame arrives 1.5 s in: that's a success, not a timeout
	reset(10000, 1, 5000 + 1500, 0);
	CHECK(p8ipc_wait(&sem, 3000) == 0);
	CHECK(mono - 5000 == 1500);

	// 10 s backward step between slices: the next deadline is rebuilt from the stepped clock,
	// so the wait isn't stretched
	reset(-10000, 2, -1, 0);
	CHECK(p8ipc_wait(&sem, 3000) == -1);
	CHECK(mono - 5000 == 3000);

	// A few signals mid-wait are ridden out
	reset(0, 0, 5000 + 200, 5);
	CHECK(p8ipc_wait(&sem, 3000) == 0);

	// A signal storm gives up instead of spinning
	reset(0, 0, -1, 1000);
	CHECK(p8ipc_wait(&sem, 3000) == -1);
	CHECK(waits == P8IPC_EINTR_RETRIES + 1);

	// Posted right as the timeout runs out still counts
	reset(0, 0, -1, 0);
	sem_post(&sem);
	CHECK(p8ipc_wait(&sem, 0) == 0);

	sem_destroy(&sem);
	puts("test_p8ipc_clock: ok");
	return 0;
}
