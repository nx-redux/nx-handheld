// Host test for the IndicatorAutoHide model (indicator_autohide.h).
#include <assert.h>
#include <stdio.h>

#define INDICATOR_AUTOHIDE_MODEL_ONLY
#include "../indicator_autohide.h"

// One ModuleCommon_PWR_update frame at `now`.
static bool frame(IndicatorAutoHide* s, bool active, uint32_t now) {
	IndicatorAutoHide_before(s, active, now);
	return IndicatorAutoHide_after(s, active, now);
}

int main(void) {
	IndicatorAutoHide s = {0};

	// Held buttons: never forced hidden.
	assert(!frame(&s, true, 100));
	assert(!frame(&s, true, 5000));

	// Released at 6000: visible for 800 ms, then forced hidden for 500 ms.
	assert(!frame(&s, false, 6000));
	assert(s.release_time == 6000);
	assert(!frame(&s, false, 6799));
	assert(frame(&s, false, 6800));
	assert(frame(&s, false, 7299));
	assert(frame(&s, false, 7300)); // last forced frame stops the timer
	assert(s.release_time == 0);
	assert(!frame(&s, false, 7301));

	// Pressed again mid-countdown: the next release restarts the timer.
	assert(!frame(&s, true, 8000));
	assert(!frame(&s, false, 8100));
	assert(!frame(&s, true, 8500));
	assert(frame(&s, true, 8900)); // still counting from 8100, even while held
	assert(!frame(&s, false, 9000));
	assert(s.release_time == 9000);

	// Never pressed: nothing to hide.
	IndicatorAutoHide idle = {0};
	assert(!frame(&idle, false, 100000));

	printf("test_indicator_autohide: OK\n");
	return 0;
}
