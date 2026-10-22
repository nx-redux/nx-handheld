#ifndef RA_BADGE_POLICY_H
#define RA_BADGE_POLICY_H
// ra_badges.c's retry and memory rules, without SDL, so the host tests can
// cover them.
#include <stdbool.h>
#include <stdint.h>

// A failed badge download is tried again after this long, this many times in
// all per game load: one network blip no longer leaves it blank until the
// next game, and a badge the server never serves stops costing requests.
#define RA_BADGE_RETRY_MS 30000
#define RA_BADGE_MAX_ATTEMPTS 3

// Full-size (64x64 RGBA, 16 KB) badges kept decoded at once. A 1000
// achievement list scrolled end to end otherwise holds them all (~32 MB with
// the locked variants) until the game changes. The least recently drawn one
// is dropped and reloaded from disk when it's needed again.
#define RA_BADGE_MAX_LOADED 128

// Whether a badge whose download failed `failures` times, the last at
// failed_at (ms), may be fetched again at now (ms)
static inline bool RA_BadgePolicy_mayRetry(int failures, uint32_t failed_at, uint32_t now) {
	return failures < RA_BADGE_MAX_ATTEMPTS && (uint32_t)(now - failed_at) >= RA_BADGE_RETRY_MS;
}

// Index of the least recently used of n slots (last_used[i] == 0: nothing
// loaded there, never picked), or -1 when none is loaded. skip (-1 for none)
// is never picked: the entry being loaded.
static inline int RA_BadgePolicy_pickEvict(const uint32_t* last_used, int n, int skip) {
	int best = -1;
	for (int i = 0; i < n; i++) {
		if (i == skip || last_used[i] == 0)
			continue;
		if (best < 0 || last_used[i] < last_used[best])
			best = i;
	}
	return best;
}

#endif
