#ifndef __RATOOLS_LOGIC_H__
#define __RATOOLS_LOGIC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "ra_offline.h"
#include "ratools_data.h"

// Pure parts of ratools_data.c (no rcheevos, no file reads): host-testable.

typedef struct {
	uint32_t id;
	time_t when;
} RAT_ServerUnlock;

bool rat_server_find(const RAT_ServerUnlock* ids, int count, uint32_t id, time_t* out_when);

// The unlock state of achievement `id`: a server unlock, else an offline one awaiting sync (pending),
// else a synced offline one the stale session cache predates (confirmed, unlocked). `hash` filters the
// pending/confirmed entries by game; NULL when the arrays are already filtered. *when = the unlock
// time (untouched while locked).
RAT_AchState rat_resolve_unlock(uint32_t id, const char* hash, const RAT_ServerUnlock* srv, int nsrv,
								const RA_PendingUnlock* pend, int npend, const RA_PendingUnlock* conf, int nconf,
								time_t* when);

// A cut at byte `len` can split a UTF-8 sequence: back off to the last sequence boundary so the
// string never ends in a partial character.
void rat_utf8_trim(char* s, size_t len);

// Collapse the discs of one multi-disc game (same session_game_id) into one entry: the .m3u's when
// is_m3u(hash) names one, else the first seen, carrying the highest unlocked/pending counts.
// Returns the new count.
int rat_merge_discs(RAT_Game* games, int count, bool (*is_m3u)(const char* hash));

// RAT_sortAchievements with an explicit RA_SORT_* order (unknown orders leave the array as is).
void rat_sort_achievements_by(RAT_Achievement* achs, int count, int order);

#endif
