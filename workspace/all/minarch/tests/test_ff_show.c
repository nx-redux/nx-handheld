#include <stdio.h>
#include "ma_ffshow.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

int main(void) {
	// 2x..4x (index 1..3): the last frame of each pass, however soon after the last present
	CHECK(FF_showFrame(1, 1, 0), "2x: last frame of a pass is shown right after a present");
	CHECK(!FF_showFrame(1, 0, 100), "2x: an earlier frame of the pass is dropped, however late");
	CHECK(FF_showFrame(3, 1, 16), "4x: last frame of a pass is shown at 16 ms (no 30 ms gap)");
	CHECK(!FF_showFrame(3, 0, 16), "4x: the other frames of the pass are dropped");

	// above 4x: one frame per FF_FRAME_INTERVAL_MS, wherever it falls in the pass
	CHECK(!FF_showFrame(4, 1, FF_FRAME_INTERVAL_MS - 1), "5x: last of pass dropped inside the 30 ms gap");
	CHECK(FF_showFrame(4, 0, FF_FRAME_INTERVAL_MS), "5x: any frame shown once the gap has passed");
	CHECK(FF_showFrame(7, 0, 31) && !FF_showFrame(7, 1, 29), "8x: gap only");

	// None (index 0): passes aren't paced, so the gap applies
	CHECK(!FF_showFrame(0, 1, 10), "None: last of pass dropped inside the gap");
	CHECK(FF_showFrame(0, 0, FF_FRAME_INTERVAL_MS), "None: shown once the gap has passed");

	if (fails)
		printf("%d FAILED\n", fails);
	return fails != 0;
}
