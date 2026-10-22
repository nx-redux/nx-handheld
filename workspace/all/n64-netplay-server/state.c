/*
 * state.c — pure helpers over the server state (declared in server.h), kept
 * out of main.c so the unit tests link them without the socket loop.
 */

#include <stdint.h>

#include "server.h"

int ms_registered_count(const MsServer* s) {
	int n = 0;
	for (int i = 0; i < MS_MAX_PLAYERS; ++i) {
		if (s->players[i].reg_id != 0)
			++n;
	}
	return n;
}

int ms_conn_limit(const MsServer* s) {
	int limit = s->expected_players + MS_CONN_SLACK;
	return limit < MS_MAX_CONNS ? limit : MS_MAX_CONNS;
}

int ms_uint_larger(uint32_t a, uint32_t b) {
	return (uint32_t)(b - a) > UINT32_MAX / 2;
}
