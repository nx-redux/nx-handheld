#ifndef P8SHIM_CLOCK_H
#define P8SHIM_CLOCK_H

// PICO-8's SDL_GetTicks clock, run on time the core grants instead of wall time.
//
// Each frame_req grants a slice of frame_ms. The slice starts where the last one ended,
// so after N requests the clock has reached at most the sum of their frame_ms: pause,
// fast-forward and 30 fps carts all follow minarch. Inside a slice PICO-8's own
// SDL_Delay calls move the clock (it waits for its next frame that way); a delay that
// would run past the slice's end ends the slice instead, without a new frame.

#include <stdint.h>

struct p8clock {
	uint32_t now;	  /* what SDL_GetTicks returns */
	uint32_t granted; /* sum of frame_ms of all slices so far; now never passes it */
};

// A frame_req arrived: a new slice of frame_ms starts at the end of the previous one
void p8clock_grant(struct p8clock* c, uint32_t frame_ms);

// SDL_Delay(ms) on PICO-8's main thread. 0: the clock moved by ms. 1: that would pass the
// slice's end, so the clock stops there and the caller ends the slice (waits for frame_req).
int p8clock_delay(struct p8clock* c, uint32_t ms);

#endif
