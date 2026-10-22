/*
 * Pure tests of the server's timers and helpers: ms_tick_buffer hysteresis,
 * ms_tick_sweep timeouts, ms_uint_larger wrap and ms_conn_limit. Links
 * udp.c + state.c with the two main.c helpers stubbed; no sockets.
 * Run by tests/run_tests.sh.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../server.h"

long long ms_now_ms(void) {
	return 0;
}

void ms_log(const char* fmt, ...) {
	(void)fmt;
}

static MsServer srv; /* large (input rings): static, not on the stack */

static void sample(int seat, double lag, double health, int n) {
	srv.players[seat].lag_sum = lag * n;
	srv.players[seat].health_sum = health * n;
	srv.players[seat].sample_n = (uint32_t)n;
}

static void test_uint_larger(void) {
	assert(ms_uint_larger(5, 4));
	assert(!ms_uint_larger(4, 5));
	assert(!ms_uint_larger(7, 7));
	assert(ms_uint_larger(0, UINT32_MAX));			/* wrapped: 0 follows 0xFFFFFFFF */
	assert(ms_uint_larger(3, UINT32_MAX - 2));		/* across the wrap */
	assert(!ms_uint_larger(UINT32_MAX - 2, 3));		/* and the other way */
	assert(ms_uint_larger(UINT32_MAX / 2 + 1, 1));	/* just under half the ring ahead */
	assert(!ms_uint_larger(UINT32_MAX / 2 + 2, 0)); /* more than half ahead reads as behind */
}

static void test_conn_limit(void) {
	memset(&srv, 0, sizeof(srv));
	srv.expected_players = 2;
	assert(ms_conn_limit(&srv) == 2 + MS_CONN_SLACK);
	srv.expected_players = MS_MAX_CONNS;
	assert(ms_conn_limit(&srv) == MS_MAX_CONNS);
}

static void test_tick_buffer(void) {
	memset(&srv, 0, sizeof(srv));
	srv.buffer_target = 2;
	srv.buffer_size = 3;

	/* no samples: unchanged */
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 3);

	/* inside the +-0.75 dead band: unchanged */
	sample(0, 0.0, 2.7, 10);
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 3);
	sample(0, 0.0, 1.3, 10);
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 3);

	/* too much health: shrink; too little: grow */
	sample(0, 0.0, 2.8, 10);
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 2);
	sample(0, 0.0, 1.2, 10);
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 3);

	/* the seat with the lowest lag leads; the other's health is not used */
	sample(0, 2.0, 0.0, 10); /* behind, starving */
	sample(1, 0.5, 5.0, 10); /* lead, plenty */
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 2);

	/* accumulators reset after every tick */
	for (int i = 0; i < MS_MAX_PLAYERS; ++i)
		assert(srv.players[i].sample_n == 0 && srv.players[i].lag_sum == 0.0 && srv.players[i].health_sum == 0.0);

	/* never below 0 */
	srv.buffer_size = 0;
	sample(0, 0.0, 9.0, 1);
	ms_tick_buffer(&srv);
	assert(srv.buffer_size == 0);
}

static void test_tick_sweep(void) {
	memset(&srv, 0, sizeof(srv));
	srv.players[0].reg_id = 0x11;
	srv.players[1].reg_id = 0x22;

	/* nothing ever registered (the flag): no sweep, no exit */
	assert(ms_tick_sweep(&srv) == 0);
	assert(srv.players[0].reg_id == 0x11);

	srv.ever_registered = 1;
	srv.players[0].alive = 1; /* P1 asked for input; P2 did not */
	assert(ms_tick_sweep(&srv) == 0);
	assert(srv.players[0].reg_id == 0x11 && srv.players[0].alive == 0); /* kept, alive re-armed */
	assert(srv.players[1].reg_id == 0);									/* freed */
	assert(srv.status == (1u << 2));									/* P2 disconnect bit */

	/* next sweep without a request from P1: last seat gone, server exits */
	assert(ms_tick_sweep(&srv) == 1);
	assert(srv.players[0].reg_id == 0);
	assert(srv.status == ((1u << 1) | (1u << 2)));
	assert(ms_registered_count(&srv) == 0);
}

int main(void) {
	test_uint_larger();
	test_conn_limit();
	test_tick_buffer();
	test_tick_sweep();
	printf("test_state: all tests passed\n");
	return 0;
}
