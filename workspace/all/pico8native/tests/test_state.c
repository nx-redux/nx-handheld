// Host test for save states through the whole shim (shim/state.c): plays the core's side
// of p8ipc against fake_pico8 in its FAKE_STATES mode, under LD_PRELOAD=pico8shim.so with
// a fake libSDL2. fake_pico8 is built without PIE (as with ASLR off for pico8_64, its
// image stays put), while libc, SDL and the shim still move between runs, so a state put
// back into a second run checks the pointer words kept from that run.
// Usage: test_state <shim.so> <fake_pico8>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../p8ipc.h"
#include "../p8state.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

static char root[64], ipc_path[96], home[128], cdata[128];
static pid_t child;
static unsigned frame_no;
static const char *shim_path, *fake_path;
static uint8_t sha[32];

static void kill_child(void) {
	if (child > 0) {
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
		child = 0;
	}
}

static void cleanup(void) {
	kill_child();
	unlink(ipc_path);
	char cmd[96];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", root);
}

static struct p8ipc* start(void) {
	unlink(ipc_path);
	struct p8ipc* ipc = p8ipc_create(ipc_path);
	CHECK(ipc);
	frame_no = 0;
	child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		setenv("P8NATIVE_IPC", ipc_path, 1);
		setenv("P8NATIVE_HOME", home, 1);
		setenv("P8NATIVE_CDATA", cdata, 1);
		setenv("P8NATIVE_STATES", "1", 1);
		setenv("FAKE_FPS", "60", 1);
		setenv("FAKE_STATES", "1", 1);
		setenv("LD_PRELOAD", shim_path, 1);
		personality(ADDR_NO_RANDOMIZE); // as the core does; a container may refuse it
		execl(fake_path, fake_path, (char*)NULL);
		_exit(127);
	}
	return ipc;
}

static void stop(struct p8ipc* ipc) {
	ipc->cmd = P8CMD_QUIT;
	sem_post(&ipc->frame_req);
	CHECK(p8ipc_wait(&ipc->frame_done, 3000) == 0);
	int status = 0;
	pid_t w = 0;
	for (int i = 0; i < 100 && w == 0; i++) {
		w = waitpid(child, &status, WNOHANG);
		if (w == 0)
			usleep(10000);
	}
	CHECK(w == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	child = 0;
	p8ipc_close(ipc);
}

static void step(struct p8ipc* ipc) {
	ipc->buttons = 0;
	ipc->frame_ms = frame_no++ % 3 == 2 ? 16 : 17;
	ipc->cmd = P8CMD_NONE;
	sem_post(&ipc->frame_req);
	CHECK(p8ipc_wait(&ipc->frame_done, 3000) == 0);
}

static int32_t command(struct p8ipc* ipc, uint32_t cmd) {
	ipc->cmd_result = 0;
	ipc->cmd = cmd;
	sem_post(&ipc->frame_req);
	CHECK(p8ipc_wait(&ipc->frame_done, 10000) == 0);
	CHECK(ipc->cmd == P8CMD_NONE);
	return ipc->cmd_result;
}

// What a frame shows: frame number, clock, and the heap / machine / big-block checksums
struct seen {
	uint32_t frame, ticks, list, machine, big;
};

static struct seen seen(struct p8ipc* ipc) {
	return (struct seen){ipc->fb[0] & 0xffffff, ipc->fb[1] & 0xffffff, ipc->fb[4] & 0xffffff, ipc->fb[5] & 0xffffff,
						 ipc->fb[6] & 0xffffff};
}

static int same(struct seen a, struct seen b) {
	return a.frame == b.frame && a.ticks == b.ticks && a.list == b.list && a.machine == b.machine && a.big == b.big;
}

static void run_until_ready(struct p8ipc* ipc) {
	for (int i = 0; i < 100 && !ipc->state_ready; i++)
		step(ipc);
	CHECK(ipc->state_ready);
}

// SAVE with the header the core fills; returns the state's size
static size_t save(struct p8ipc* ipc, uint8_t* out) {
	struct p8state_hdr h;
	CHECK(p8state_hdr_init(&h, sha, "/roms/fake.p8") == 0);
	memcpy(ipc->state, &h, sizeof(h));
	int32_t n = command(ipc, P8CMD_SAVE);
	if (n <= 0)
		fprintf(stderr, "save failed: %s\n", ipc->reason);
	CHECK(n > (int32_t)sizeof(h));
	struct p8state_hdr got;
	const char* why = NULL;
	CHECK(p8state_check(ipc->state, (size_t)n, P8IPC_STATE_MAX, sha, "fake.p8", &got, &why) == 0);
	CHECK(sizeof(got) + got.len == (size_t)n);
	memcpy(out, ipc->state, (size_t)n);
	return (size_t)n;
}

// The clock a state keeps is a sum of the pattern from 0: its phase is the pattern's
static unsigned phase_of(uint32_t ticks) {
	return ticks % 50 == 0 ? 0 : ticks % 50 == 17 ? 1
												  : 2;
}

static int32_t load(struct p8ipc* ipc, const uint8_t* state, size_t n) {
	memcpy(ipc->state, state, n);
	ipc->state_len = (uint32_t)n;
	ipc->reason[0] = '\0';
	return command(ipc, P8CMD_LOAD);
}

int main(int argc, char** argv) {
	CHECK(argc == 3);
	shim_path = argv[1];
	fake_path = argv[2];
	for (int i = 0; i < 32; i++)
		sha[i] = (uint8_t)(i * 5);
	snprintf(root, sizeof(root), "/tmp/nx_test_state.%d", (int)getpid());
	snprintf(ipc_path, sizeof(ipc_path), "/tmp/p8native_test_state.%d.ipc", (int)getpid());
	snprintf(home, sizeof(home), "%s/home", root);
	snprintf(cdata, sizeof(cdata), "%s/cdata", root);
	atexit(cleanup);
	CHECK(mkdir(root, 0755) == 0 && mkdir(home, 0755) == 0);

	static uint8_t saved[P8IPC_STATE_MAX], other[P8IPC_STATE_MAX];

	// Run 1: not ready before the cart starts; then save, run on, load, continue exactly
	struct p8ipc* ipc = start();
	step(ipc);
	CHECK(!ipc->state_ready && ipc->state_available);
	struct p8state_hdr h;
	CHECK(p8state_hdr_init(&h, sha, "fake.p8") == 0);
	memcpy(ipc->state, &h, sizeof(h));
	CHECK(command(ipc, P8CMD_SAVE) == 0 && strstr(ipc->reason, "not started"));

	run_until_ready(ipc);
	for (int i = 0; i < 20; i++)
		step(ipc);
	size_t n = save(ipc, saved);
	struct p8state_hdr sh;
	memcpy(&sh, saved, sizeof(sh));
	unsigned save_frame_no = frame_no;
	step(ipc);
	struct seen after_save = seen(ipc);
	step(ipc);
	struct seen after_save2 = seen(ipc);
	printf("test_state: state of %zu bytes at ticks %u (frame %u)\n", n, sh.ticks, after_save.frame - 1);

	for (int i = 0; i < 40; i++)
		step(ipc);
	CHECK(!same(seen(ipc), after_save));
	// a second state, later: different, for the damaged-state checks below
	size_t n_other = save(ipc, other);
	CHECK(n_other != n || memcmp(other, saved, n) != 0);

	CHECK(load(ipc, saved, n) == 1);
	frame_no = phase_of(sh.ticks); // as the core: the 17/17/16 pattern goes on from the state's clock
	CHECK(frame_no % 3 == save_frame_no % 3);
	step(ipc);
	CHECK(same(seen(ipc), after_save)); // frame, clock, heap, machine, big block: all as then
	step(ipc);
	CHECK(same(seen(ipc), after_save2));

	// Damaged states leave PICO-8 as it was: a flipped byte in the packed pages, cut short
	step(ipc);
	struct seen before_bad = seen(ipc);
	unsigned bad_frame_no = frame_no;
	saved[n - 10] ^= 0x5a;
	int32_t r = load(ipc, saved, n);
	saved[n - 10] ^= 0x5a;
	CHECK(r == 0 && ipc->reason[0]);
	printf("test_state: damaged state refused: %s\n", ipc->reason);
	struct p8state_hdr cut;
	memcpy(&cut, saved, sizeof(cut));
	cut.len -= 100;
	memcpy(other, saved, n);
	memcpy(other, &cut, sizeof(cut));
	CHECK(load(ipc, other, n - 100) == 0 && ipc->reason[0]);
	step(ipc);
	CHECK(seen(ipc).frame == before_bad.frame + 1 && seen(ipc).list != 0);
	(void)bad_frame_no;
	stop(ipc);

	// Run 2: a fresh process, where SDL, libc and the shim may sit elsewhere. The state
	// loads once its cart runs and continues exactly; the SDL handles it keeps are this run's.
	ipc = start();
	run_until_ready(ipc);
	step(ipc);
	CHECK(load(ipc, saved, n) == 1);
	frame_no = phase_of(sh.ticks);
	step(ipc);
	CHECK(same(seen(ipc), after_save));
	step(ipc);
	CHECK(same(seen(ipc), after_save2));
	for (int i = 0; i < 30; i++) // and keeps running: the arena it allocates from is the state's
		step(ipc);
	CHECK(seen(ipc).frame == after_save2.frame + 30);
	stop(ipc);

	printf("test_state: ok\n");
	return 0;
}
