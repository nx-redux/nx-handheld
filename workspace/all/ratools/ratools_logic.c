#include "ratools_logic.h"

#include <stdlib.h>
#include <string.h>

#include "config.h"

bool rat_server_find(const RAT_ServerUnlock* ids, int count, uint32_t id,
					 time_t* out_when) {
	for (int i = 0; i < count; i++) {
		if (ids[i].id == id) {
			if (out_when)
				*out_when = ids[i].when;
			return true;
		}
	}
	return false;
}

RAT_AchState rat_resolve_unlock(uint32_t id, const char* hash, const RAT_ServerUnlock* srv,
								int nsrv, const RA_PendingUnlock* pend, int npend,
								const RA_PendingUnlock* conf, int nconf, time_t* when) {
	if (rat_server_find(srv, nsrv, id, when))
		return RAT_ACH_UNLOCKED;
	for (int i = 0; i < npend; i++) {
		if (pend[i].achievement_id == id && (!hash || strcmp(pend[i].game_hash, hash) == 0)) {
			*when = pend[i].when;
			return RAT_ACH_PENDING;
		}
	}
	for (int i = 0; i < nconf; i++) {
		if (conf[i].achievement_id == id && (!hash || strcmp(conf[i].game_hash, hash) == 0)) {
			*when = conf[i].when;
			return RAT_ACH_UNLOCKED;
		}
	}
	return RAT_ACH_LOCKED;
}

// A cut at byte `len` can split a UTF-8 sequence: back off to the last sequence boundary so the
// string never ends in a partial character.
void rat_utf8_trim(char* s, size_t len) {
	size_t i = len;
	size_t cont = 0;
	while (i > 0 && cont < 3 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
		i--;
		cont++;
	}
	if (i == 0) {
		s[len - cont] = '\0'; // only continuation bytes: nothing to anchor them
		return;
	}
	unsigned char lead = (unsigned char)s[i - 1];
	size_t need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2
								 : lead >= 0xC0	  ? 1
												  : 0;
	if (lead >= 0xC0 && cont < need)
		s[i - 1] = '\0'; // incomplete: drop the lead byte and its continuation bytes
	else if (lead < 0xC0 && cont > 0)
		s[i] = '\0'; // stray continuation bytes after ASCII
}

// Multi-disc games: every disc (and the .m3u, which hashes as disc 1)
// resolves to its own hash but the same RA game, and all of them are
// cached so minarch can identify any disc offline. Show the game once:
// keep the entry whose rom is the .m3u when there is one (its art is the
// folder art), otherwise the first seen, carrying over the highest unlock
// count (server unlocks are shared through the session cache; only
// offline-pending unlocks are hash-specific, and those follow the disc
// that was actually played).
int rat_merge_discs(RAT_Game* games, int count, bool (*is_m3u)(const char* hash)) {
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count;) {
			if (games[j].session_game_id != games[i].session_game_id) {
				j++;
				continue;
			}
			bool j_is_m3u = is_m3u(games[j].hash);
			RAT_Game* keep = &games[i];
			RAT_Game* drop = &games[j];
			if (j_is_m3u) {
				RAT_Game tmp = games[i];
				games[i] = games[j];
				games[j] = tmp;
			}
			if (drop->unlocked > keep->unlocked) {
				keep->unlocked = drop->unlocked;
				keep->pending = drop->pending;
			}
			memmove(&games[j], &games[j + 1], (size_t)(count - j - 1) * sizeof(RAT_Game));
			count--;
		}
	}
	return count;
}

/*****************************************************************************
 * Sorting - mirrors the in-game menu's comparators (ma_menu.c
 * ach_sort_achievements): pending-sync counts as unlocked, display order is
 * the server's list order (RAT_Achievement.order), "won by" is the rarity percentage.
 *****************************************************************************/

static int rats_unlocked(const RAT_Achievement* a) {
	return a->state != RAT_ACH_LOCKED;
}

static int rats_cmp_unlocked_first(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	if (rats_unlocked(A) != rats_unlocked(B))
		return rats_unlocked(B) - rats_unlocked(A);
	return 0;
}
static int rats_cmp_display_first(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	return A->order - B->order;
}
static int rats_cmp_display_last(const void* a, const void* b) {
	return -rats_cmp_display_first(a, b);
}
static int rats_cmp_won_by_most(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	if (A->rarity != B->rarity)
		return (B->rarity - A->rarity) > 0 ? 1 : -1;
	return 0;
}
static int rats_cmp_won_by_least(const void* a, const void* b) {
	return -rats_cmp_won_by_most(a, b);
}
static int rats_cmp_points_most(const void* a, const void* b) {
	const RAT_Achievement* A = (const RAT_Achievement*)a;
	const RAT_Achievement* B = (const RAT_Achievement*)b;
	return (int)B->points - (int)A->points;
}
static int rats_cmp_points_least(const void* a, const void* b) {
	return -rats_cmp_points_most(a, b);
}
static int rats_cmp_title_az(const void* a, const void* b) {
	return strcmp(((const RAT_Achievement*)a)->title, ((const RAT_Achievement*)b)->title);
}
static int rats_cmp_title_za(const void* a, const void* b) {
	return -rats_cmp_title_az(a, b);
}
static int rats_cmp_type_asc(const void* a, const void* b) {
	return (int)((const RAT_Achievement*)a)->type - (int)((const RAT_Achievement*)b)->type;
}
static int rats_cmp_type_desc(const void* a, const void* b) {
	return -rats_cmp_type_asc(a, b);
}

void rat_sort_achievements_by(RAT_Achievement* achs, int count, int order) {
	if (!achs || count <= 1)
		return;

	int (*compare)(const void*, const void*) = NULL;
	switch (order) {
	case RA_SORT_UNLOCKED_FIRST:
		compare = rats_cmp_unlocked_first;
		break;
	case RA_SORT_DISPLAY_ORDER_FIRST:
		compare = rats_cmp_display_first;
		break;
	case RA_SORT_DISPLAY_ORDER_LAST:
		compare = rats_cmp_display_last;
		break;
	case RA_SORT_WON_BY_MOST:
		compare = rats_cmp_won_by_most;
		break;
	case RA_SORT_WON_BY_LEAST:
		compare = rats_cmp_won_by_least;
		break;
	case RA_SORT_POINTS_MOST:
		compare = rats_cmp_points_most;
		break;
	case RA_SORT_POINTS_LEAST:
		compare = rats_cmp_points_least;
		break;
	case RA_SORT_TITLE_AZ:
		compare = rats_cmp_title_az;
		break;
	case RA_SORT_TITLE_ZA:
		compare = rats_cmp_title_za;
		break;
	case RA_SORT_TYPE_ASC:
		compare = rats_cmp_type_asc;
		break;
	case RA_SORT_TYPE_DESC:
		compare = rats_cmp_type_desc;
		break;
	default:
		return;
	}
	qsort(achs, count, sizeof(RAT_Achievement), compare);
}
