// Host test for Netplay_frameInWindow: which received input frames the ring keeps.
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include "../netplay.h"

int main(void) {
	assert(Netplay_frameInWindow(100, 100)); // the frame about to run
	assert(Netplay_frameInWindow(102, 100)); // the usual latency lead
	assert(Netplay_frameInWindow(100 + NETPLAY_FRAME_BUFFER_SIZE - 1, 100));
	assert(!Netplay_frameInWindow(100 + NETPLAY_FRAME_BUFFER_SIZE, 100)); // would alias frame 100's slot
	assert(!Netplay_frameInWindow(99, 100));							  // already run
	assert(!Netplay_frameInWindow(0, 100));
	// across the uint32_t wrap
	assert(Netplay_frameInWindow(1, 0xFFFFFFFEu));
	assert(!Netplay_frameInWindow(0xFFFFFFFDu, 0xFFFFFFFEu));
	printf("test_frame_window: OK\n");
	return 0;
}
