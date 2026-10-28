// Host test for shim/clock.c: PICO-8's SDL_GetTicks only advances by time the core granted.
#include <stdio.h>
#include <stdlib.h>

#include "../shim/clock.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

// 17/17/16 as the core sends them, 50 ms per 3 frames
static uint32_t frame_ms(int i) {
	return i % 3 == 2 ? 16 : 17;
}

// A PICO-8-like main loop: SDL_Delay(1) until the next frame is due (fps 60 or 30), then
// one present, which ends the slice. Returns frames presented over `slices` requests.
static int run_cart(int fps, int slices) {
	struct p8clock c = {0, 0};
	uint32_t due_x3 = 0; // next frame due, in 1/3 ms
	uint32_t step_x3 = fps == 60 ? 50 : 100;
	int presented = 0;
	int s = 0;
	p8clock_grant(&c, frame_ms(s));
	while (s < slices) {
		while (c.now * 3 < due_x3) {
			CHECK(c.now <= c.granted);
			if (p8clock_delay(&c, 1)) { // ran out of slice: next request, no frame
				if (++s == slices)
					return presented;
				p8clock_grant(&c, frame_ms(s));
			}
		}
		due_x3 += step_x3;
		presented++;
		if (++s == slices)
			break;
		p8clock_grant(&c, frame_ms(s)); // present: next request
	}
	return presented;
}

int main(void) {
	struct p8clock c = {0, 0};

	// Nothing granted yet: any wait ends the (empty) slice at once
	CHECK(p8clock_delay(&c, 1) == 1 && c.now == 0);

	// A slice starts at the end of the last; delays move within it only
	p8clock_grant(&c, 17);
	CHECK(c.now == 0 && c.granted == 17);
	CHECK(p8clock_delay(&c, 10) == 0 && c.now == 10);
	CHECK(p8clock_delay(&c, 7) == 0 && c.now == 17); // reaching the end exactly is still inside
	CHECK(p8clock_delay(&c, 1) == 1 && c.now == 17); // past it: the slice ends, the clock stays
	p8clock_grant(&c, 16);
	CHECK(c.now == 17 && c.granted == 33);

	// A present early in the slice: the next slice still starts where this one ends
	CHECK(p8clock_delay(&c, 3) == 0 && c.now == 20);
	p8clock_grant(&c, 17);
	CHECK(c.now == 33 && c.granted == 50);
	CHECK(p8clock_delay(&c, 0) == 0 && c.now == 33);

	// The core paused (no requests): time stands still however long PICO-8 waits
	for (int i = 0; i < 1000; i++) {
		if (p8clock_delay(&c, 5))
			break;
	}
	CHECK(c.now == 50 && c.granted == 50);

	// 60 fps carts present every slice; 30 fps ones every other; over a minute
	int f60 = run_cart(60, 3600);
	int f30 = run_cart(30, 3600);
	printf("test_clock: 3600 slices → %d frames at 60 fps, %d at 30 fps\n", f60, f30);
	CHECK(f60 >= 3598 && f60 <= 3600);
	CHECK(f30 >= 1798 && f30 <= 1801);

	puts("test_clock: ok");
	return 0;
}
