// Host test for ra_badge_policy.h: failed-badge retry and the LRU pick.
#include <assert.h>
#include <stdio.h>

#include "../ra_badge_policy.h"

int main(void) {
	// a failed badge is tried again after the wait, a limited number of times
	assert(!RA_BadgePolicy_mayRetry(1, 1000, 1000));
	assert(!RA_BadgePolicy_mayRetry(1, 1000, 1000 + RA_BADGE_RETRY_MS - 1));
	assert(RA_BadgePolicy_mayRetry(1, 1000, 1000 + RA_BADGE_RETRY_MS));
	assert(!RA_BadgePolicy_mayRetry(RA_BADGE_MAX_ATTEMPTS, 1000, 1000 + 10 * RA_BADGE_RETRY_MS));
	// SDL_GetTicks wrapping past 2^32 still counts the time between
	assert(RA_BadgePolicy_mayRetry(1, 0xFFFFFF00u, (uint32_t)(0xFFFFFF00u + RA_BADGE_RETRY_MS)));

	// least recently used loaded slot; unloaded (0) and the one being loaded are skipped
	uint32_t used[] = {5, 0, 3, 9, 1};
	assert(RA_BadgePolicy_pickEvict(used, 5, -1) == 4);
	assert(RA_BadgePolicy_pickEvict(used, 5, 4) == 2);
	uint32_t none[] = {0, 0};
	assert(RA_BadgePolicy_pickEvict(none, 2, -1) == -1);
	assert(RA_BadgePolicy_pickEvict(used, 0, -1) == -1);
	printf("ok test_ra_badge_policy\n");
	return 0;
}
