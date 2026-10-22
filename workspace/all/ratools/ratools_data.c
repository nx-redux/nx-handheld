#include "ratools_data.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <rcheevos/rc_api_runtime.h>
#include <rcheevos/rc_api_user.h>

#include "config.h"
#include "defines.h"
#include "ra_offline.h"
#include "ratools_logic.h"

// nx-redux always runs softcore, where RA suppresses the "Warning: Unknown
// Emulator" achievement in-game - hide it in the browser and counts too.
static bool rat_is_hidden_achievement(uint32_t id) {
	return id == RA_UNKNOWN_EMULATOR_ACHIEVEMENT_ID;
}

// Match what the in-game achievements menu shows
// (RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE): core achievements only, minus the
// Unknown Emulator warning. Unofficial (Flags=5) achievements are excluded.
static bool rat_is_visible_achievement(const rc_api_achievement_definition_t* def) {
	return def->category == RC_ACHIEVEMENT_CATEGORY_CORE &&
		   !rat_is_hidden_achievement(def->id);
}

// A cached server reply (rel: below the RA offline cache) as the input of an rcheevos process_*_server_response;
// *body is the file's malloc'd contents, freed by the caller once processed.
static bool rat_cached_response(const char* rel, rc_api_server_response_t* sr, char** body) {
	size_t len = 0;
	*body = NULL;
	if (!RA_Offline_readCacheFile(rel, body, &len))
		return false;
	memset(sr, 0, sizeof(*sr));
	sr->body = *body;
	sr->body_length = len;
	sr->http_status_code = 200;
	return true;
}

static bool rat_parse_sets(const char* hash, rc_api_fetch_game_sets_response_t* out) {
	char rel[192];
	snprintf(rel, sizeof(rel), "cache/games/%s/sets.json", hash);
	char* body;
	rc_api_server_response_t sr;
	if (!rat_cached_response(rel, &sr, &body))
		return false;

	int rc = rc_api_process_fetch_game_sets_server_response(out, &sr);
	free(body);
	if (rc != RC_OK || !out->response.succeeded) {
		rc_api_destroy_fetch_game_sets_response(out);
		return false;
	}
	return true;
}

// server unlocks for a game (softcore+hardcore merged, with unlock times);
// returns malloc'd array via *out, count as return value
static int rat_server_unlocks(uint32_t session_game_id, RAT_ServerUnlock** out) {
	*out = NULL;
	char rel[128];
	snprintf(rel, sizeof(rel), "cache/sessions/%u.json", session_game_id);
	char* body;
	rc_api_server_response_t sr;
	if (!rat_cached_response(rel, &sr, &body))
		return 0;

	rc_api_start_session_response_t resp;
	int count = 0;
	if (rc_api_process_start_session_server_response(&resp, &sr) == RC_OK &&
		resp.response.succeeded) {
		uint32_t total = resp.num_unlocks + resp.num_hardcore_unlocks;
		if (total > 0) {
			RAT_ServerUnlock* ids = (RAT_ServerUnlock*)malloc(total * sizeof(RAT_ServerUnlock));
			if (ids) {
				// the server reports the "Unknown Emulator" warning
				// (101000001) as unlocked in every softcore session on an
				// unapproved client - filter hidden ids here so they never
				// inflate the unlocked counts
				for (uint32_t i = 0; i < resp.num_unlocks; i++) {
					if (rat_is_hidden_achievement(resp.unlocks[i].achievement_id))
						continue;
					ids[count].id = resp.unlocks[i].achievement_id;
					ids[count].when = resp.unlocks[i].when;
					count++;
				}
				for (uint32_t i = 0; i < resp.num_hardcore_unlocks; i++) {
					if (rat_is_hidden_achievement(resp.hardcore_unlocks[i].achievement_id))
						continue;
					ids[count].id = resp.hardcore_unlocks[i].achievement_id;
					ids[count].when = resp.hardcore_unlocks[i].when;
					count++;
				}
				*out = ids;
			}
		}
	}
	rc_api_destroy_start_session_response(&resp);
	free(body);
	return count;
}

typedef int (*RAT_EntriesReadFn)(RA_PendingUnlock* out, int max);

static int rat_entries_for_hash(RAT_EntriesReadFn read_fn, const char* hash,
								RA_PendingUnlock** out) {
	RA_PendingUnlock* all = (RA_PendingUnlock*)malloc(
		sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	*out = NULL;
	if (!all)
		return 0;
	int total = read_fn(all, RA_OFFLINE_MAX_PENDING);
	int kept = 0;
	for (int i = 0; i < total; i++) {
		if (strcmp(all[i].game_hash, hash) == 0)
			all[kept++] = all[i];
	}
	if (kept == 0) {
		free(all);
		return 0;
	}
	*out = all;
	return kept;
}

// journaled offline unlocks awaiting sync
static int rat_pending_for_hash(const char* hash, RA_PendingUnlock** out) {
	return rat_entries_for_hash(RA_Offline_readJournal, hash, out);
}

// synced offline unlocks the stale session cache does not know about yet.
// Filtered while reading: the confirmed file outgrows the per-read cap.
static int rat_confirmed_for_hash(const char* hash, RA_PendingUnlock** out) {
	*out = NULL;
	RA_PendingUnlock* all = (RA_PendingUnlock*)malloc(
		sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	if (!all)
		return 0;
	int kept = RA_Offline_readConfirmedFor(NULL, hash, all, RA_OFFLINE_MAX_PENDING);
	if (kept == 0) {
		free(all);
		return 0;
	}
	*out = all;
	return kept;
}

static int rat_game_cmp(const void* a, const void* b) {
	return strcasecmp(((const RAT_Game*)a)->title, ((const RAT_Game*)b)->title);
}

// true if the cached rom path for `hash` is an .m3u (the multi-disc entry RAT_listGames keeps)
static bool rat_rom_is_m3u(const char* hash) {
	char rom[512];
	return RA_Offline_getGameRomPath(hash, rom, sizeof(rom)) && strlen(rom) > 4 &&
		   strcasecmp(rom + strlen(rom) - 4, ".m3u") == 0;
}

int RAT_listGames(RAT_Game** out_games) {
	*out_games = NULL;
	const char* games_dir = RA_Offline_gamesDir();
	DIR* d = games_dir ? opendir(games_dir) : NULL;
	if (!d)
		return 0;

	int cap = 64, count = 0;
	RAT_Game* games = (RAT_Game*)malloc(cap * sizeof(RAT_Game));
	// the journal and confirmed files hold every game's entries: read once here and filtered by hash per game
	// (rat_resolve_unlock), not re-read per game
	RA_PendingUnlock* pend = malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	RA_PendingUnlock* conf = malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	if (!games || !pend || !conf) {
		free(games);
		free(pend);
		free(conf);
		closedir(d);
		return 0;
	}
	int pend_count = RA_Offline_readJournal(pend, RA_OFFLINE_MAX_PENDING);
	int conf_count = RA_Offline_readConfirmed(conf, RA_OFFLINE_MAX_PENDING);
	// a confirmed file past the cap was cut to its newest entries: then each game reads its own (filtered while
	// reading), as before, so an older game's synced unlocks still count
	bool conf_full = conf_count >= RA_OFFLINE_MAX_PENDING;

	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;

		rc_api_fetch_game_sets_response_t sets;
		if (!rat_parse_sets(ent->d_name, &sets))
			continue;

		if (count == cap) {
			cap *= 2;
			RAT_Game* bigger = (RAT_Game*)realloc(games, cap * sizeof(RAT_Game));
			if (!bigger) {
				rc_api_destroy_fetch_game_sets_response(&sets);
				break;
			}
			games = bigger;
		}

		RAT_Game* g = &games[count];
		memset(g, 0, sizeof(*g));
		snprintf(g->hash, sizeof(g->hash), "%s", ent->d_name);
		g->game_id = sets.id;
		g->session_game_id = sets.session_game_id;
		snprintf(g->title, sizeof(g->title), "%s", sets.title ? sets.title : "(unknown)");

		RAT_ServerUnlock* server_ids = NULL;
		int server_count = rat_server_unlocks(g->session_game_id, &server_ids);
		RA_PendingUnlock* game_conf = NULL;
		int game_conf_count = conf_full ? rat_confirmed_for_hash(g->hash, &game_conf) : 0;
		const RA_PendingUnlock* use_conf = conf_full ? game_conf : conf;
		int use_conf_count = conf_full ? game_conf_count : conf_count;

		// counts are derived from the same visible-definition set the
		// achievement list shows, so they can never disagree with it
		for (uint32_t s = 0; s < sets.num_sets; s++) {
			for (uint32_t a = 0; a < sets.sets[s].num_achievements; a++) {
				const rc_api_achievement_definition_t* def = &sets.sets[s].achievements[a];
				if (!rat_is_visible_achievement(def))
					continue;
				g->total++;
				time_t when = 0;
				RAT_AchState state = rat_resolve_unlock(def->id, g->hash, server_ids, server_count, pend,
														pend_count, use_conf, use_conf_count, &when);
				if (state != RAT_ACH_LOCKED)
					g->unlocked++;
				if (state == RAT_ACH_PENDING)
					g->pending++;
			}
		}

		free(server_ids);
		free(game_conf);
		rc_api_destroy_fetch_game_sets_response(&sets);
		count++;
	}
	closedir(d);
	free(pend);
	free(conf);

	count = rat_merge_discs(games, count, rat_rom_is_m3u);

	if (count == 0) {
		free(games);
		return 0;
	}
	qsort(games, count, sizeof(RAT_Game), rat_game_cmp);
	*out_games = games;
	return count;
}

int RAT_loadAchievements(const RAT_Game* game, RAT_Achievement** out) {
	*out = NULL;
	rc_api_fetch_game_sets_response_t sets;
	if (!rat_parse_sets(game->hash, &sets))
		return 0;

	int total = 0;
	for (uint32_t s = 0; s < sets.num_sets; s++)
		total += (int)sets.sets[s].num_achievements;
	if (total == 0) {
		rc_api_destroy_fetch_game_sets_response(&sets);
		return 0;
	}

	RAT_Achievement* achs = (RAT_Achievement*)calloc(total, sizeof(RAT_Achievement));
	if (!achs) {
		rc_api_destroy_fetch_game_sets_response(&sets);
		return 0;
	}

	RAT_ServerUnlock* server_ids = NULL;
	int server_count = rat_server_unlocks(game->session_game_id, &server_ids);
	RA_PendingUnlock* pend = NULL;
	int pend_count = rat_pending_for_hash(game->hash, &pend);
	RA_PendingUnlock* conf = NULL;
	int conf_count = rat_confirmed_for_hash(game->hash, &conf);

	int n = 0;
	for (uint32_t s = 0; s < sets.num_sets; s++) {
		for (uint32_t a = 0; a < sets.sets[s].num_achievements; a++) {
			const rc_api_achievement_definition_t* def = &sets.sets[s].achievements[a];
			if (!rat_is_visible_achievement(def))
				continue;
			RAT_Achievement* dst = &achs[n];
			dst->order = n++;
			dst->id = def->id;
			dst->points = def->points;
			snprintf(dst->title, sizeof(dst->title), "%s", def->title ? def->title : "");
			snprintf(dst->description, sizeof(dst->description), "%s",
					 def->description ? def->description : "");
			snprintf(dst->badge_name, sizeof(dst->badge_name), "%s",
					 def->badge_name ? def->badge_name : "");
			dst->type = def->type;
			dst->rarity = def->rarity;
			time_t when = 0;
			dst->state = rat_resolve_unlock(def->id, NULL, server_ids, server_count, pend, pend_count,
											conf, conf_count, &when);
			dst->unlock_time = dst->state == RAT_ACH_LOCKED ? 0 : when;
		}
	}

	free(server_ids);
	free(pend);
	free(conf);
	rc_api_destroy_fetch_game_sets_response(&sets);
	*out = achs;
	return n;
}

bool RAT_progressForHash(const char* hash, int* unlocked, int* total, char* next, size_t next_size) {
	rc_api_fetch_game_sets_response_t sets;
	if (!rat_parse_sets(hash, &sets))
		return false;
	RAT_Game game;
	memset(&game, 0, sizeof(game));
	snprintf(game.hash, sizeof(game.hash), "%s", hash);
	game.game_id = sets.id;
	game.session_game_id = sets.session_game_id;
	rc_api_destroy_fetch_game_sets_response(&sets);

	// RAT_loadAchievements already drops non-core and hidden achievements
	// (rat_is_visible_achievement), the same set RAT_listGames counts
	RAT_Achievement* achs = NULL;
	int n = RAT_loadAchievements(&game, &achs);
	if (n <= 0) {
		free(achs);
		return false;
	}
	const RAT_Achievement* first_locked = RAT_firstLocked(achs, n);
	*unlocked = 0;
	*total = n;
	for (int i = 0; i < n; i++)
		if (achs[i].state != RAT_ACH_LOCKED)
			(*unlocked)++;
	if (next_size) {
		const char* title = first_locked ? first_locked->title : "";
		snprintf(next, next_size, "%s", title);
		size_t len = strlen(next);
		if (strlen(title) > len && len > 0) // truncated: don't end mid-character
			rat_utf8_trim(next, len);
	}
	free(achs);
	return true;
}


void RAT_sortAchievements(RAT_Achievement* achs, int count) {
	rat_sort_achievements_by(achs, count, CFG_getRAAchievementSortOrder());
}

bool RAT_getCachedScore(uint32_t* score, uint32_t* softcore_score) {
	char* body;
	rc_api_server_response_t sr;
	if (!rat_cached_response("cache/login.json", &sr, &body))
		return false;

	rc_api_login_response_t resp;
	bool ok = false;
	if (rc_api_process_login_server_response(&resp, &sr) == RC_OK && resp.response.succeeded) {
		if (score)
			*score = resp.score;
		if (softcore_score)
			*softcore_score = resp.score_softcore;
		ok = true;
	}
	rc_api_destroy_login_response(&resp);
	free(body);
	return ok;
}

typedef struct {
	uint32_t game; // session_game_id: every disc of a multi-disc game shares it (and its unlocks)
	uint32_t id;
} RAT_UnlockKey;

static int rat_unlock_key_cmp(const void* a, const void* b) {
	const RAT_UnlockKey* x = a;
	const RAT_UnlockKey* y = b;
	if (x->game != y->game)
		return x->game < y->game ? -1 : 1;
	return x->id < y->id ? -1 : x->id > y->id;
}

// One pass over cache/games/: each sets.json is parsed once (RAT_listGames + RAT_loadAchievements would
// parse it twice), the journal and confirmed files are read once, and the unlocks are kept as
// (session game, achievement) keys, so the discs of one game (RAT_listGames merges them) count once.
// Reads and parses every cached game: call it from a worker thread only.
int RAT_unlocksSinceCancellable(time_t since, bool (*cancelled)(void)) {
	const char* games_dir = RA_Offline_gamesDir();
	DIR* d = games_dir ? opendir(games_dir) : NULL;
	if (!d)
		return 0;

	RA_PendingUnlock* pend = malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	RA_PendingUnlock* conf = malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	if (!pend || !conf) {
		free(pend);
		free(conf);
		closedir(d);
		return -1;
	}
	int pend_count = RA_Offline_readJournal(pend, RA_OFFLINE_MAX_PENDING);
	int conf_count = RA_Offline_readConfirmed(conf, RA_OFFLINE_MAX_PENDING);

	RAT_UnlockKey* keys = NULL;
	int nkeys = 0, cap = 0;
	bool failed = false; // cancelled or out of memory
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (cancelled && cancelled()) {
			failed = true;
			break;
		}
		if (ent->d_name[0] == '.')
			continue;
		const char* hash = ent->d_name;
		rc_api_fetch_game_sets_response_t sets;
		if (!rat_parse_sets(hash, &sets))
			continue;
		RAT_ServerUnlock* server_ids = NULL;
		int server_count = rat_server_unlocks(sets.session_game_id, &server_ids);

		// the journal and confirmed files hold every game's entries: filtered by hash here
		for (uint32_t s = 0; s < sets.num_sets && !failed; s++) {
			for (uint32_t a = 0; a < sets.sets[s].num_achievements; a++) {
				const rc_api_achievement_definition_t* def = &sets.sets[s].achievements[a];
				if (!rat_is_visible_achievement(def))
					continue;
				time_t when = 0;
				if (rat_resolve_unlock(def->id, hash, server_ids, server_count, pend, pend_count, conf,
									   conf_count, &when) == RAT_ACH_LOCKED ||
					when < since)
					continue;
				if (nkeys == cap) {
					int grown = cap ? cap * 2 : 64;
					RAT_UnlockKey* bigger = realloc(keys, grown * sizeof(RAT_UnlockKey));
					if (!bigger) {
						failed = true; // no silent undercount
						break;
					}
					keys = bigger;
					cap = grown;
				}
				keys[nkeys].game = sets.session_game_id;
				keys[nkeys].id = def->id;
				nkeys++;
			}
		}
		free(server_ids);
		rc_api_destroy_fetch_game_sets_response(&sets);
		if (failed)
			break;
	}
	closedir(d);
	free(pend);
	free(conf);

	int count = -1;
	if (!failed) {
		if (nkeys > 1)
			qsort(keys, nkeys, sizeof(RAT_UnlockKey), rat_unlock_key_cmp);
		count = 0;
		for (int i = 0; i < nkeys; i++) {
			if (i == 0 || rat_unlock_key_cmp(&keys[i - 1], &keys[i]) != 0)
				count++;
		}
	}
	free(keys);
	return count;
}
