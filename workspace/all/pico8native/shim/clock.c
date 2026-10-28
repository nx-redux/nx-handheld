#include "clock.h"

void p8clock_grant(struct p8clock* c, uint32_t frame_ms) {
	// whatever of the last slice PICO-8 did not wait out is skipped, never carried over
	c->now = c->granted;
	c->granted += frame_ms;
}

int p8clock_delay(struct p8clock* c, uint32_t ms) {
	if (ms > c->granted - c->now) {
		c->now = c->granted;
		return 1;
	}
	c->now += ms;
	return 0;
}
