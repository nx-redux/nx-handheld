#include "ra_offline.h"
#include "ra_file.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RA_OFFLINE_MAX_PATH 512

static char ra_root[RA_OFFLINE_MAX_PATH] = {0};
static char ra_games[RA_OFFLINE_MAX_PATH] = {0}; // <root>/cache/games
// rom.txt records paths relative to this (the card), so another OS can resolve them
static char ra_card[RA_OFFLINE_MAX_PATH] = {0};
static RA_NetMode ra_mode = RA_NET_ONLINE;
static char ra_current_hash[64] = {0};
static pthread_mutex_t ra_off_mutex = PTHREAD_MUTEX_INITIALIZER;

/*****************************************************************************
 * Small helpers
 *****************************************************************************/

// cached server responses are small; refuse anything absurd
#define RA_OFFLINE_MAX_FILE (8 * 1024 * 1024)

// hashes / game ids / usernames only: refuse anything that could escape the
// cache dir (server- or caller-supplied strings become path components)
static bool ra_off_safe_component(const char* s) {
	if (!s || !*s)
		return false;
	for (const char* p = s; *p; p++) {
		if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
			return false;
	}
	return true;
}

// a valid cached RA response always carries "Success":true (that is the
// write-time criterion in RA_Offline_cacheResponse) - anything else is
// treated as a cache miss
static bool ra_off_body_valid(const char* body) {
	return body && strstr(body, "\"Success\":true") != NULL;
}

// Extract "key":"value" from a JSON body (RA responses are compact, no
// whitespace around ':'). Good enough for the User field check.
static bool ra_off_json_find_string(const char* body, const char* key,
									char* out, size_t out_size) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	const char* p = strstr(body, pat);
	if (!p)
		return false;
	p += strlen(pat);
	const char* e = strchr(p, '"');
	if (!e)
		return false;
	size_t len = (size_t)(e - p);
	if (len >= out_size)
		len = out_size - 1;
	memcpy(out, p, len);
	out[len] = '\0';
	return true;
}

/*****************************************************************************
 * Public: init / mode / params
 *****************************************************************************/

void RA_Offline_init(const char* root_dir, const char* card_root) {
	if (!root_dir || !*root_dir)
		return;
	snprintf(ra_root, sizeof(ra_root), "%s", root_dir);
	snprintf(ra_games, sizeof(ra_games), "%s/cache/games", root_dir);
	snprintf(ra_card, sizeof(ra_card), "%s", card_root ? card_root : "");
	size_t clen = strlen(ra_card);
	while (clen > 1 && ra_card[clen - 1] == '/')
		ra_card[--clen] = '\0';
	char p[RA_OFFLINE_MAX_PATH];
	mkdir(ra_root, 0755);
	snprintf(p, sizeof(p), "%s/cache", ra_root);
	mkdir(p, 0755);
	mkdir(ra_games, 0755);
	snprintf(p, sizeof(p), "%s/cache/badges", ra_root);
	mkdir(p, 0755);
	snprintf(p, sizeof(p), "%s/cache/sessions", ra_root);
	mkdir(p, 0755);
	snprintf(p, sizeof(p), "%s/pending", ra_root);
	mkdir(p, 0755);
	ra_mode = RA_NET_ONLINE;
	ra_current_hash[0] = '\0';
}

void RA_Offline_setMode(RA_NetMode mode) {
	pthread_mutex_lock(&ra_off_mutex);
	ra_mode = mode;
	pthread_mutex_unlock(&ra_off_mutex);
}

RA_NetMode RA_Offline_getMode(void) {
	pthread_mutex_lock(&ra_off_mutex);
	RA_NetMode mode = ra_mode;
	pthread_mutex_unlock(&ra_off_mutex);
	return mode;
}

const char* RA_Offline_rootDir(void) {
	return ra_root[0] ? ra_root : NULL;
}

const char* RA_Offline_gamesDir(void) {
	return ra_root[0] ? ra_games : NULL;
}

const char* RA_Offline_currentGameHash(void) {
	return ra_current_hash;
}

bool RA_Offline_getParam(const char* post_data, const char* key,
						 char* out, size_t out_size) {
	if (!post_data || !key || !out || out_size == 0)
		return false;
	size_t klen = strlen(key);
	const char* p = post_data;
	while (p && *p) {
		if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
			const char* v = p + klen + 1;
			const char* e = strchr(v, '&');
			size_t len = e ? (size_t)(e - v) : strlen(v);
			if (len >= out_size)
				len = out_size - 1;
			memcpy(out, v, len);
			out[len] = '\0';
			return true;
		}
		p = strchr(p, '&');
		if (p)
			p++;
	}
	return false;
}

/*****************************************************************************
 * Response cache
 *****************************************************************************/

// Map a request (by its post_data) to a cache path relative to root.
// Returns false for non-cacheable requests. Also updates ra_current_hash
// for gameid/achievementsets requests.
static bool ra_off_cache_relpath(const char* post_data, char* relpath, size_t size) {
	char r[32], m[64], g[32];
	if (!RA_Offline_getParam(post_data, "r", r, sizeof(r)))
		return false;

	if (strcmp(r, "login2") == 0) {
		snprintf(relpath, size, "cache/login.json");
		return true;
	}
	if (strcmp(r, "gameid") == 0 || strcmp(r, "achievementsets") == 0) {
		if (!RA_Offline_getParam(post_data, "m", m, sizeof(m)) || !ra_off_safe_component(m))
			return false;
		snprintf(ra_current_hash, sizeof(ra_current_hash), "%s", m);
		snprintf(relpath, size, "cache/games/%s/%s.json", m,
				 (r[0] == 'g') ? "gameid" : "sets");
		return true;
	}
	if (strcmp(r, "startsession") == 0) {
		if (!RA_Offline_getParam(post_data, "g", g, sizeof(g)) || !ra_off_safe_component(g))
			return false;
		snprintf(relpath, size, "cache/sessions/%s.json", g);
		return true;
	}
	return false;
}

static void ra_off_confirmed_prune(const char* user, const char* session_body);

void RA_Offline_cacheResponse(const char* post_data, const char* body, size_t body_len) {
	if (!ra_root[0] || !post_data || !body || body_len == 0)
		return;
	// don't clobber good cache with server errors
	if (!ra_off_body_valid(body))
		return;

	// callers include detached HTTP worker threads: serialize only the
	// ra_current_hash update (inside ra_off_cache_relpath); the fsync'd file
	// write stays OUTSIDE the lock so the main thread's RA_Offline_getMode
	// can't stall behind slow SD writes (per-thread tmp + rename keeps
	// same-path writers safe)
	pthread_mutex_lock(&ra_off_mutex);
	char relpath[192];
	bool cacheable = ra_off_cache_relpath(post_data, relpath, sizeof(relpath));
	pthread_mutex_unlock(&ra_off_mutex);
	if (!cacheable)
		return;

	char path[RA_OFFLINE_MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s", ra_root, relpath);

	// ensure the per-game directory exists (games/<hash>/)
	char* last_slash = strrchr(path, '/');
	if (last_slash) {
		*last_slash = '\0';
		mkdir(path, 0755);
		*last_slash = '/';
	}

	RA_File_writeAtomic(path, body, body_len);

	// a fresh online session lists every unlock the server has for this
	// game, so confirmed entries it already covers are redundant
	char r[32], u[64];
	if (RA_Offline_getParam(post_data, "r", r, sizeof(r)) && strcmp(r, "startsession") == 0 &&
		RA_Offline_getParam(post_data, "u", u, sizeof(u)))
		ra_off_confirmed_prune(u, body);
}

bool RA_Offline_readCacheFile(const char* relpath, char** out_body, size_t* out_len) {
	if (!ra_root[0] || !relpath || !out_body)
		return false;
	char path[RA_OFFLINE_MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s", ra_root, relpath);
	return RA_File_read(path, RA_OFFLINE_MAX_FILE, out_body, out_len);
}

bool RA_Offline_hasLoginCache(void) {
	if (!ra_root[0])
		return false;
	char path[RA_OFFLINE_MAX_PATH];
	snprintf(path, sizeof(path), "%s/cache/login.json", ra_root);
	struct stat st;
	return stat(path, &st) == 0 && st.st_size > 0;
}

/*****************************************************************************
 * Game rom path (for box art lookup by the achievements browser)
 *****************************************************************************/

void RA_Offline_setGameRomPath(const char* game_hash, const char* rom_path) {
	if (!ra_root[0] || !ra_off_safe_component(game_hash) || !rom_path || !*rom_path)
		return;

	char dir[RA_OFFLINE_MAX_PATH];
	snprintf(dir, sizeof(dir), "%s/%s", ra_games, game_hash);
	mkdir(dir, 0755);

	// a path on the card is stored relative to it ("Roms/GBA/x.gba")
	size_t clen = strlen(ra_card);
	if (clen && strncmp(rom_path, ra_card, clen) == 0 && rom_path[clen] == '/' && rom_path[clen + 1])
		rom_path += clen + 1;

	char path[RA_OFFLINE_MAX_PATH + 16];
	snprintf(path, sizeof(path), "%s/rom.txt", dir);
	RA_File_writeAtomic(path, rom_path, strlen(rom_path));
}

bool RA_Offline_getGameRomPath(const char* game_hash, char* out, size_t out_size) {
	if (!ra_root[0] || !ra_off_safe_component(game_hash) || !out || out_size == 0)
		return false;

	char path[RA_OFFLINE_MAX_PATH + 16];
	snprintf(path, sizeof(path), "%s/%s/rom.txt", ra_games, game_hash);

	char* body = NULL;
	size_t len = 0;
	if (!RA_File_read(path, RA_OFFLINE_MAX_FILE, &body, &len) || !body)
		return false;

	// strip a single trailing newline, if any (RA_File_writeAtomic
	// writes exactly what it's given, but be defensive)
	while (len > 0 && (body[len - 1] == '\n' || body[len - 1] == '\r'))
		body[--len] = '\0';

	if (len == 0) {
		free(body);
		return false;
	}

	// relative = card-relative; absolute = an older record
	if (body[0] != '/' && ra_card[0])
		snprintf(out, out_size, "%s/%s", ra_card, body);
	else
		snprintf(out, out_size, "%s", body);
	free(body);
	return true;
}

/*****************************************************************************
 * Unlock journal
 *****************************************************************************/

static void ra_off_journal_path(char* p, size_t n) {
	snprintf(p, n, "%s/pending/unlocks.jsonl", ra_root);
}

// synced entries land here (see RA_Offline_sync) so a cached session body
// that predates the sync doesn't let an already-earned achievement
// re-trigger offline. Same line format as the journal.
static void ra_off_confirmed_path(char* p, size_t n) {
	snprintf(p, n, "%s/pending/confirmed.jsonl", ra_root);
}

static bool ra_off_journal_parse_line(const char* line, RA_PendingUnlock* out) {
	long long t = 0;
	char buf[512];
	memset(out, 0, sizeof(*out));
	// copy and strip newline
	strncpy(buf, line, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';
	int n = sscanf(buf, "{\"u\":\"%63[^\"]\",\"a\":%u,\"m\":\"%63[^\"]\",\"t\":%lld}",
				   out->username, &out->achievement_id, out->game_hash, &t);
	if (n != 4 || out->achievement_id == 0)
		return false;
	out->when = (time_t)t;
	return true;
}

typedef void (*RA_OffPathFn)(char* p, size_t n);

// caller must hold ra_off_mutex
static bool ra_off_unlocks_contain_locked(RA_OffPathFn path_fn, const char* user, uint32_t aid,
										  const char* hash) {
	char path[RA_OFFLINE_MAX_PATH];
	path_fn(path, sizeof(path));
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	char line[512];
	RA_PendingUnlock e;
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		if (ra_off_journal_parse_line(line, &e) &&
			strcasecmp(e.username, user) == 0 && e.achievement_id == aid &&
			strcmp(e.game_hash, hash) == 0) {
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

// Append-only, locked, dedup, durable fsync. Used for the journal (an
// unlock earned offline) and the confirmed file (RA_Offline_sync, when a
// submit succeeds). The confirmed file is only pruned of entries a fresh
// online session already lists (ra_off_confirmed_prune): entries are tiny
// and the cached server body + skip-if-present check in
// ra_off_build_session_body supersede the rest.
static void ra_off_unlocks_append(RA_OffPathFn path_fn, const char* user, uint32_t aid,
								  const char* hash, time_t when) {
	if (!ra_root[0] || !user || !*user || aid == 0 || !hash || !*hash)
		return;
	pthread_mutex_lock(&ra_off_mutex);
	if (!ra_off_unlocks_contain_locked(path_fn, user, aid, hash)) {
		char path[RA_OFFLINE_MAX_PATH];
		path_fn(path, sizeof(path));
		FILE* f = fopen(path, "a");
		if (f) {
			fprintf(f, "{\"u\":\"%s\",\"a\":%u,\"m\":\"%s\",\"t\":%lld}\n",
					user, aid, hash, (long long)when);
			fflush(f);
			fsync(fileno(f)); // survive battery pulls
			fclose(f);
		}
	}
	pthread_mutex_unlock(&ra_off_mutex);
}

// Rewrite an unlock file without the entries drop() matches. Unparseable
// lines, and the pieces of a line longer than the read buffer, are kept
// verbatim: never silently delete data we didn't understand. Atomic (temp +
// fsync + rename), and the file is left alone when nothing was dropped.
static void ra_off_unlocks_filter(RA_OffPathFn path_fn,
								  bool (*drop)(const RA_PendingUnlock* e, const void* ctx),
								  const void* ctx) {
	pthread_mutex_lock(&ra_off_mutex);
	char path[RA_OFFLINE_MAX_PATH], tmp[RA_OFFLINE_MAX_PATH + 8];
	path_fn(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* in = fopen(path, "r");
	FILE* out = in ? fopen(tmp, "w") : NULL;
	if (!out) {
		if (in)
			fclose(in);
		pthread_mutex_unlock(&ra_off_mutex);
		return;
	}
	bool failed = false;
	int dropped = 0;
	char line[512];
	RA_PendingUnlock e;
	bool line_start = true; // fgets hands a line longer than the buffer over in pieces
	while (fgets(line, sizeof(line), in)) {
		size_t len = strlen(line);
		bool whole = line_start && ((len > 0 && line[len - 1] == '\n') || feof(in));
		line_start = len > 0 && line[len - 1] == '\n';
		// only a whole line is an entry: a piece of a longer one that happens to read as one is kept with the rest
		if (whole && ra_off_journal_parse_line(line, &e) && drop(&e, ctx)) {
			dropped++;
			continue;
		}
		if (fputs(line, out) == EOF) {
			failed = true;
			break;
		}
	}
	fclose(in);
	if (!failed && (fflush(out) != 0 || fsync(fileno(out)) != 0))
		failed = true;
	if (fclose(out) != 0)
		failed = true;
	if (failed || dropped == 0 || rename(tmp, path) != 0)
		remove(tmp);
	pthread_mutex_unlock(&ra_off_mutex);
}

int RA_Offline_readJournal(RA_PendingUnlock* out, int max) {
	if (!ra_root[0] || !out || max <= 0)
		return 0;
	pthread_mutex_lock(&ra_off_mutex);
	char path[RA_OFFLINE_MAX_PATH];
	ra_off_journal_path(path, sizeof(path));
	FILE* f = fopen(path, "r");
	int count = 0;
	if (f) {
		char line[512];
		while (count < max && fgets(line, sizeof(line), f)) {
			if (ra_off_journal_parse_line(line, &out[count]))
				count++;
			// malformed lines are skipped here but preserved on rewrite
		}
		fclose(f);
	}
	pthread_mutex_unlock(&ra_off_mutex);
	return count;
}

int RA_Offline_pendingCount(void) {
	if (!ra_root[0])
		return 0;
	pthread_mutex_lock(&ra_off_mutex);
	char path[RA_OFFLINE_MAX_PATH];
	ra_off_journal_path(path, sizeof(path));
	FILE* f = fopen(path, "r");
	int count = 0;
	if (f) {
		char line[512];
		RA_PendingUnlock e;
		while (fgets(line, sizeof(line), f)) {
			if (ra_off_journal_parse_line(line, &e))
				count++;
		}
		fclose(f);
	}
	pthread_mutex_unlock(&ra_off_mutex);
	return count;
}

// journal entries for a specific (user, hash) — used by the session merge
static int ra_off_journal_entries_for(const char* user, const char* hash,
									  RA_PendingUnlock* out, int max) {
	int total = RA_Offline_readJournal(out, max);
	int kept = 0;
	for (int i = 0; i < total; i++) {
		if (strcasecmp(out[i].username, user) == 0 && strcmp(out[i].game_hash, hash) == 0)
			out[kept++] = out[i];
	}
	return kept;
}

// Keeps the newest `max` entries (ring buffer), returned oldest first.
static int ra_off_read_confirmed(RA_PendingUnlock* out, int max) {
	if (!ra_root[0] || !out || max <= 0)
		return 0;
	pthread_mutex_lock(&ra_off_mutex);
	char path[RA_OFFLINE_MAX_PATH];
	ra_off_confirmed_path(path, sizeof(path));
	FILE* f = fopen(path, "r");
	long total = 0;
	if (f) {
		char line[512];
		RA_PendingUnlock e;
		while (fgets(line, sizeof(line), f)) {
			if (ra_off_journal_parse_line(line, &e))
				out[total++ % max] = e;
		}
		fclose(f);
	}
	pthread_mutex_unlock(&ra_off_mutex);
	if (total <= max)
		return (int)total;
	// wrapped: rotate so the oldest kept entry comes first
	int head = (int)(total % max);
	if (head) {
		RA_PendingUnlock* tmp = (RA_PendingUnlock*)malloc(sizeof(RA_PendingUnlock) * head);
		if (tmp) {
			memcpy(tmp, out, sizeof(RA_PendingUnlock) * head);
			memmove(out, out + head, sizeof(RA_PendingUnlock) * (max - head));
			memcpy(out + (max - head), tmp, sizeof(RA_PendingUnlock) * head);
			free(tmp);
		}
	}
	return max;
}

// confirmed entries for a specific (user, hash) — used by the session merge.
// NOT counted by RA_Offline_pendingCount: these are already-synced.

// Replace the unsigned integer value of "key":<digits> inside body (compact
// server JSON). Returns a malloc'd copy with the replacement applied, or
// NULL if the key was not found / allocation failed.
static char* ra_off_replace_uint_field(const char* body, const char* key, uint32_t value) {
	char pat[48];
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char* p = strstr(body, pat);
	if (!p)
		return NULL;
	const char* digits = p + strlen(pat);
	const char* end = digits;
	while (*end >= '0' && *end <= '9')
		end++;
	if (end == digits)
		return NULL;

	size_t pre = (size_t)(digits - body);
	char num[16];
	int num_len = snprintf(num, sizeof(num), "%u", value);
	size_t tail = strlen(end);
	char* out = (char*)malloc(pre + (size_t)num_len + tail + 1);
	if (!out)
		return NULL;
	memcpy(out, body, pre);
	memcpy(out + pre, num, (size_t)num_len);
	memcpy(out + pre + num_len, end, tail + 1);
	return out;
}

void RA_Offline_updateCachedScores(uint32_t score, uint32_t softcore_score) {
	if (!ra_root[0])
		return;
	char* body = NULL;
	size_t len = 0;
	if (!RA_Offline_readCacheFile("cache/login.json", &body, &len))
		return;

	char* s1 = ra_off_replace_uint_field(body, "Score", score);
	const char* cur = s1 ? s1 : body;
	char* s2 = ra_off_replace_uint_field(cur, "SoftcoreScore", softcore_score);
	const char* final_body = s2 ? s2 : cur;

	if (s1 || s2) {
		char path[RA_OFFLINE_MAX_PATH];
		snprintf(path, sizeof(path), "%s/cache/login.json", ra_root);
		RA_File_writeAtomic(path, final_body, strlen(final_body));
	}

	free(s1);
	free(s2);
	free(body);
}

int RA_Offline_readConfirmed(RA_PendingUnlock* out, int max) {
	return ra_off_read_confirmed(out, max);
}

// Filters while reading, so `max` bounds this game's matches rather than the
// whole file: an old file past `max` lines must not hide newer entries.
// NULL user matches any user.
static int ra_off_confirmed_entries_for(const char* user, const char* hash,
										RA_PendingUnlock* out, int max) {
	if (!ra_root[0] || !hash || !out || max <= 0)
		return 0;
	pthread_mutex_lock(&ra_off_mutex);
	char path[RA_OFFLINE_MAX_PATH];
	ra_off_confirmed_path(path, sizeof(path));
	FILE* f = fopen(path, "r");
	int kept = 0;
	if (f) {
		char line[512];
		while (kept < max && fgets(line, sizeof(line), f)) {
			if (ra_off_journal_parse_line(line, &out[kept]) &&
				(!user || strcasecmp(out[kept].username, user) == 0) &&
				strcmp(out[kept].game_hash, hash) == 0)
				kept++;
		}
		fclose(f);
	}
	pthread_mutex_unlock(&ra_off_mutex);
	return kept;
}

/*****************************************************************************
 * Offline request handling
 *****************************************************************************/

static bool ra_off_serve(const char* body, char** out_body, size_t* out_len, int* out_status) {
	size_t l = strlen(body);
	char* b = (char*)malloc(l + 1);
	if (!b)
		return false;
	memcpy(b, body, l + 1);
	*out_body = b;
	*out_len = l;
	*out_status = 200;
	return true;
}

static bool ra_off_serve_owned(char* body, size_t len, char** out_body,
							   size_t* out_len, int* out_status) {
	*out_body = body;
	*out_len = len;
	*out_status = 200;
	return true;
}

// true if the base session body already lists this achievement id, i.e.
// contains the exact token "ID":<id> followed by ',' or '}' (so "ID":12
// doesn't false-match a lookup for "ID":1).
static bool ra_off_id_in_body(const char* body, uint32_t id) {
	if (!body)
		return false;
	char pat[32];
	int patlen = snprintf(pat, sizeof(pat), "\"ID\":%u", id);
	const char* p = body;
	while ((p = strstr(p, pat)) != NULL) {
		char after = p[patlen];
		if (after == ',' || after == '}')
			return true;
		p += patlen;
	}
	return false;
}

int RA_Offline_readConfirmedFor(const char* user, const char* hash,
								RA_PendingUnlock* out, int max) {
	return ra_off_confirmed_entries_for(user, hash, out, max);
}

typedef struct {
	const char* user;
	const char* session_body;
} RA_OffPruneCtx;

static bool ra_off_prune_drop(const RA_PendingUnlock* e, const void* ctx) {
	const RA_OffPruneCtx* c = (const RA_OffPruneCtx*)ctx;
	return strcasecmp(e->username, c->user) == 0 && ra_off_id_in_body(c->session_body, e->achievement_id);
}

// Drop confirmed entries for `user` whose achievement the server's session
// body already lists as unlocked (achievement IDs are unique across RA, so
// no hash/game mapping is needed).
static void ra_off_confirmed_prune(const char* user, const char* session_body) {
	if (!ra_root[0] || !user || !*user || !session_body)
		return;
	RA_OffPruneCtx ctx = {user, session_body};
	ra_off_unlocks_filter(ra_off_confirmed_path, ra_off_prune_drop, &ctx);
}

// Copy a startsession body with the journal + confirmed unlocks for
// (user, hash) injected into "Unlocks" (see ra_off_id_in_body for the
// dedup-against-base check). *out_added gets the number injected.
static char* ra_off_merge_unlocks(const char* base, size_t base_len, const char* user,
								  const char* hash, size_t* out_len, int* out_added) {
	// journal (not-yet-synced) + confirmed (synced but the cached session
	// predates the sync) entries for this (user, hash), deduped against each
	// other and against anything the cached base already lists.
	RA_PendingUnlock* jentries = NULL;
	RA_PendingUnlock* centries = NULL;
	int jn = 0, cn = 0;
	if (user && *user && hash && *hash) {
		jentries = (RA_PendingUnlock*)malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
		centries = (RA_PendingUnlock*)malloc(sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
		if (jentries)
			jn = ra_off_journal_entries_for(user, hash, jentries, RA_OFFLINE_MAX_PENDING);
		if (centries)
			cn = ra_off_confirmed_entries_for(user, hash, centries, RA_OFFLINE_MAX_PENDING);
	}

	size_t ent_cap = (size_t)(jn + cn) * 64 + 1;
	char* ent = (char*)malloc(ent_cap);
	size_t ent_len = 0;
	int emitted = 0;
	if (ent) {
		ent[0] = '\0';
		for (int i = 0; i < jn; i++) {
			if (ra_off_id_in_body(base, jentries[i].achievement_id))
				continue;
			ent_len += (size_t)snprintf(ent + ent_len, ent_cap - ent_len,
										"%s{\"ID\":%u,\"When\":%lld}", emitted ? "," : "",
										jentries[i].achievement_id,
										(long long)jentries[i].when);
			emitted++;
		}
		for (int i = 0; i < cn; i++) {
			bool dup = ra_off_id_in_body(base, centries[i].achievement_id);
			for (int j = 0; !dup && j < jn; j++) {
				if (jentries[j].achievement_id == centries[i].achievement_id)
					dup = true;
			}
			if (dup)
				continue;
			ent_len += (size_t)snprintf(ent + ent_len, ent_cap - ent_len,
										"%s{\"ID\":%u,\"When\":%lld}", emitted ? "," : "",
										centries[i].achievement_id,
										(long long)centries[i].when);
			emitted++;
		}
	}
	free(jentries);
	free(centries);
	if (!ent)
		return NULL;

	char* out = (char*)malloc(base_len + ent_len + 32);
	if (!out) {
		free(ent);
		return NULL;
	}

	size_t len = 0;
	if (ent_len == 0) {
		memcpy(out, base, base_len);
		len = base_len;
	} else {
		const char* marker = "\"Unlocks\":[";
		const char* ins = strstr(base, marker);
		if (ins) {
			size_t pre = (size_t)(ins - base) + strlen(marker);
			memcpy(out, base, pre);
			len = pre;
			memcpy(out + len, ent, ent_len);
			len += ent_len;
			if (base[pre] != ']')
				out[len++] = ',';
			memcpy(out + len, base + pre, base_len - pre);
			len += base_len - pre;
		} else {
			const char* brace = strchr(base, '{');
			size_t pre = brace ? (size_t)(brace - base) + 1 : 0;
			memcpy(out, base, pre);
			len = pre;
			len += (size_t)sprintf(out + len, "\"Unlocks\":[%s],", ent);
			memcpy(out + len, base + pre, base_len - pre);
			len += base_len - pre;
		}
	}
	out[len] = '\0';
	free(ent);
	if (out_len)
		*out_len = len;
	if (out_added)
		*out_added = emitted;
	return out;
}

// Copy the current game hash. HTTP worker threads update it (under
// ra_off_mutex, in cacheResponse and handleRequest), so it is read under the
// lock too.
static void ra_off_hash_get(char* out, size_t size) {
	pthread_mutex_lock(&ra_off_mutex);
	snprintf(out, size, "%s", ra_current_hash);
	pthread_mutex_unlock(&ra_off_mutex);
}

// Build an offline startsession body: the cached server response (if any)
// with the unlocks for (user, current game hash) merged in.
static char* ra_off_build_session_body(const char* g, const char* user, size_t* out_len) {
	char rel[128];
	snprintf(rel, sizeof(rel), "cache/sessions/%s.json", g);
	char* cached = NULL;
	size_t cached_len = 0;
	RA_Offline_readCacheFile(rel, &cached, &cached_len); // miss is fine
	if (cached && !ra_off_body_valid(cached)) {
		free(cached); // corrupt/truncated cache file: treat as cache-miss
		cached = NULL;
	}

	static const char* kEmpty = "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}";
	const char* base = cached ? cached : kEmpty;
	size_t base_len = cached ? cached_len : strlen(kEmpty);
	char hash[64];
	ra_off_hash_get(hash, sizeof(hash));
	char* out = ra_off_merge_unlocks(base, base_len, user, hash, out_len, NULL);
	free(cached);
	return out;
}

char* RA_Offline_mergeSession(const char* post_data, const char* body, size_t body_len,
							  size_t* out_len) {
	char r[32], u[64], m[64];
	if (!ra_root[0] || !post_data || !ra_off_body_valid(body) ||
		!RA_Offline_getParam(post_data, "r", r, sizeof(r)) || strcmp(r, "startsession") != 0 ||
		!RA_Offline_getParam(post_data, "u", u, sizeof(u)))
		return NULL;
	// HTTP worker thread: copy the hash under the lock cacheResponse takes
	if (!RA_Offline_getParam(post_data, "m", m, sizeof(m)) || !ra_off_safe_component(m))
		ra_off_hash_get(m, sizeof(m));
	int added = 0;
	char* out = ra_off_merge_unlocks(body, body_len, u, m, out_len, &added);
	if (out && added == 0) {
		free(out);
		out = NULL;
	}
	return out;
}

bool RA_Offline_handleRequest(const char* post_data, char** out_body,
							  size_t* out_len, int* out_status) {
	if (RA_Offline_getMode() != RA_NET_OFFLINE || !ra_root[0] || !post_data || !out_body ||
		!out_len || !out_status)
		return false;

	char r[32];
	if (!RA_Offline_getParam(post_data, "r", r, sizeof(r)))
		return false;

	char m[64] = {0}, g[32] = {0}, u[64] = {0}, a[16] = {0};

	if (strcmp(r, "login2") == 0) {
		char* cached = NULL;
		size_t clen = 0;
		if (!RA_Offline_readCacheFile("cache/login.json", &cached, &clen) ||
			!ra_off_body_valid(cached)) {
			free(cached);
			return ra_off_serve("{\"Success\":false,\"Error\":\"Offline: no cached login\"}",
								out_body, out_len, out_status);
		}
		char cached_user[64];
		if (RA_Offline_getParam(post_data, "u", u, sizeof(u)) &&
			ra_off_json_find_string(cached, "User", cached_user, sizeof(cached_user)) &&
			strcasecmp(u, cached_user) != 0) {
			free(cached);
			return ra_off_serve("{\"Success\":false,\"Error\":\"Offline: cached login is for a different user\"}",
								out_body, out_len, out_status);
		}
		return ra_off_serve_owned(cached, clen, out_body, out_len, out_status);
	}

	if (strcmp(r, "gameid") == 0 || strcmp(r, "achievementsets") == 0) {
		if (!RA_Offline_getParam(post_data, "m", m, sizeof(m)) || !ra_off_safe_component(m))
			return ra_off_serve("{\"Success\":false,\"Error\":\"Offline: bad request\"}",
								out_body, out_len, out_status);
		pthread_mutex_lock(&ra_off_mutex);
		snprintf(ra_current_hash, sizeof(ra_current_hash), "%s", m);
		pthread_mutex_unlock(&ra_off_mutex);
		char rel[192];
		snprintf(rel, sizeof(rel), "cache/games/%s/%s.json", m,
				 (r[0] == 'g') ? "gameid" : "sets");
		char* cached = NULL;
		size_t clen = 0;
		if (RA_Offline_readCacheFile(rel, &cached, &clen)) {
			if (ra_off_body_valid(cached))
				return ra_off_serve_owned(cached, clen, out_body, out_len, out_status);
			free(cached); // corrupt/truncated cache file: treat as cache-miss
		}
		if (r[0] == 'g') // unknown game — rc_client reports "not recognized" cleanly
			return ra_off_serve("{\"Success\":true,\"GameID\":0}", out_body, out_len, out_status);
		return ra_off_serve("{\"Success\":false,\"Error\":\"Offline: no cached achievement data\"}",
							out_body, out_len, out_status);
	}

	if (strcmp(r, "startsession") == 0) {
		if (!RA_Offline_getParam(post_data, "g", g, sizeof(g)) || !ra_off_safe_component(g))
			return ra_off_serve("{\"Success\":false,\"Error\":\"Offline: bad request\"}",
								out_body, out_len, out_status);
		RA_Offline_getParam(post_data, "u", u, sizeof(u));
		size_t len = 0;
		char* body = ra_off_build_session_body(g, u, &len);
		if (!body)
			return ra_off_serve("{\"Success\":true}", out_body, out_len, out_status);
		return ra_off_serve_owned(body, len, out_body, out_len, out_status);
	}

	if (strcmp(r, "ping") == 0)
		return ra_off_serve("{\"Success\":true}", out_body, out_len, out_status);

	if (strcmp(r, "awardachievement") == 0) {
		if (RA_Offline_getParam(post_data, "a", a, sizeof(a)) &&
			RA_Offline_getParam(post_data, "u", u, sizeof(u))) {
			uint32_t aid = (uint32_t)strtoul(a, NULL, 10);
			if (!RA_Offline_getParam(post_data, "m", m, sizeof(m)) || !ra_off_safe_component(m))
				ra_off_hash_get(m, sizeof(m));
			ra_off_unlocks_append(ra_off_journal_path, u, aid, m, time(NULL));
			char body[96];
			snprintf(body, sizeof(body), "{\"Success\":true,\"AchievementID\":%u}", aid);
			return ra_off_serve(body, out_body, out_len, out_status);
		}
		return ra_off_serve("{\"Success\":false,\"Error\":\"Offline: bad award request\"}",
							out_body, out_len, out_status);
	}

	// everything else (submitlbentry, lbinfo, ...): clean failure
	return ra_off_serve("{\"Success\":false,\"Error\":\"Offline mode\"}",
						out_body, out_len, out_status);
}

bool RA_Offline_journalAward(const char* post_data) {
	char r[32], u[64], a[16], m[64];
	if (!post_data || !RA_Offline_getParam(post_data, "r", r, sizeof(r)) ||
		strcmp(r, "awardachievement") != 0)
		return false;
	if (!RA_Offline_getParam(post_data, "u", u, sizeof(u)) ||
		!RA_Offline_getParam(post_data, "a", a, sizeof(a)) ||
		!RA_Offline_getParam(post_data, "m", m, sizeof(m)) || !ra_off_safe_component(m))
		return false;
	uint32_t aid = (uint32_t)strtoul(a, NULL, 10);
	if (aid == 0 || !u[0])
		return false;
	ra_off_unlocks_append(ra_off_journal_path, u, aid, m, time(NULL));
	return true;
}

// bounded substring search: response bodies are not guaranteed NUL-terminated
static bool ra_off_body_has(const char* body, size_t len, const char* needle) {
	size_t n = strlen(needle);
	if (!body || n == 0 || len < n)
		return false;
	for (size_t i = 0; i + n <= len; i++) {
		if (body[i] == needle[0] && memcmp(body + i, needle, n) == 0)
			return true;
	}
	return false;
}

RA_AwardOutcome RA_Offline_classifyAwardResponse(int http_status, const char* body,
												 size_t body_len) {
	if (!body || body_len == 0)
		return RA_AWARD_RETRY;
	if (http_status == 200 && ra_off_body_has(body, body_len, "\"Success\":true"))
		return RA_AWARD_ACCEPTED;
	// "User already has this achievement awarded." (rcheevos treats it as success)
	if (http_status == 200 && ra_off_body_has(body, body_len, "User already has"))
		return RA_AWARD_ACCEPTED;
	if (http_status == 404 && ra_off_body_has(body, body_len, "\"Code\":\"not_found\""))
		return RA_AWARD_REJECTED;
	return RA_AWARD_RETRY;
}

typedef struct {
	const RA_PendingUnlock* entries;
	const bool* synced;
	int count;
} RA_OffSyncedCtx;

static bool ra_off_synced_drop(const RA_PendingUnlock* e, const void* ctx) {
	const RA_OffSyncedCtx* c = (const RA_OffSyncedCtx*)ctx;
	for (int i = 0; i < c->count; i++) {
		if (c->synced[i] && strcasecmp(c->entries[i].username, e->username) == 0 &&
			c->entries[i].achievement_id == e->achievement_id &&
			strcmp(c->entries[i].game_hash, e->game_hash) == 0)
			return true;
	}
	return false;
}

// Rewrite the journal without the entries marked in synced[] (entries as
// read; synced[i] marks entry i for removal).
static void ra_off_journal_rewrite(const RA_PendingUnlock* entries, const bool* synced,
								   int count) {
	RA_OffSyncedCtx ctx = {entries, synced, count};
	ra_off_unlocks_filter(ra_off_journal_path, ra_off_synced_drop, &ctx);
}

void RA_Offline_removePending(const char* user, uint32_t achievement_id,
							  const char* game_hash) {
	if (!ra_root[0] || !user || !*user || achievement_id == 0 || !game_hash || !*game_hash)
		return;
	RA_PendingUnlock e;
	memset(&e, 0, sizeof(e));
	snprintf(e.username, sizeof(e.username), "%s", user);
	snprintf(e.game_hash, sizeof(e.game_hash), "%s", game_hash);
	e.achievement_id = achievement_id;
	bool drop = true;
	ra_off_journal_rewrite(&e, &drop, 1);
}

/*****************************************************************************
 * Sync engine
 *****************************************************************************/

static void ra_off_lastsync_path(char* p, size_t n) {
	snprintf(p, n, "%s/pending/lastsync.txt", ra_root);
}

static void ra_off_write_lastsync(time_t t) {
	char path[RA_OFFLINE_MAX_PATH];
	ra_off_lastsync_path(path, sizeof(path));
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "%lld\n", (long long)t);
	RA_File_writeAtomic(path, buf, (size_t)len);
}

time_t RA_Offline_lastSyncTime(void) {
	if (!ra_root[0])
		return 0;
	char path[RA_OFFLINE_MAX_PATH];
	ra_off_lastsync_path(path, sizeof(path));
	char* body = NULL;
	if (!RA_File_read(path, RA_OFFLINE_MAX_FILE, &body, NULL))
		return 0;
	long long t = atoll(body);
	free(body);
	return (time_t)(t > 0 ? t : 0);
}

int RA_Offline_sync(const char* username, time_t now, RA_SubmitFn submit,
					RA_SyncProgressFn progress, void* userdata) {
	if (!ra_root[0] || !username || !*username || !submit)
		return -1;

	RA_PendingUnlock* all = (RA_PendingUnlock*)malloc(
		sizeof(RA_PendingUnlock) * RA_OFFLINE_MAX_PENDING);
	if (!all)
		return -1;
	int total = RA_Offline_readJournal(all, RA_OFFLINE_MAX_PENDING);

	// usernames are case-insensitive: entries carry the server's
	// case-corrected name, callers pass the one typed in Settings (#161)
	int mine = 0;
	for (int i = 0; i < total; i++) {
		if (strcasecmp(all[i].username, username) == 0)
			mine++;
	}
	if (mine == 0) {
		free(all);
		ra_off_write_lastsync(now);
		return 0;
	}

	bool* synced = (bool*)calloc((size_t)total, sizeof(bool));
	if (!synced) {
		free(all);
		return -1;
	}

	int done = 0, ok = 0, rejected = 0;
	for (int i = 0; i < total; i++) {
		if (strcasecmp(all[i].username, username) != 0)
			continue;
		uint32_t secs = (now > all[i].when) ? (uint32_t)(now - all[i].when) : 1;
		int rc = submit(&all[i], secs, userdata);
		if (rc == 0) {
			synced[i] = true;
			ok++;
			// record before the journal rewrite so a cached session that
			// predates this sync can't re-trigger the achievement offline
			ra_off_unlocks_append(ra_off_confirmed_path, all[i].username, all[i].achievement_id,
								  all[i].game_hash, all[i].when);
		} else if (rc == 1) {
			// rejected for good (achievement removed): stop resubmitting it
			// forever, but it was never unlocked so it isn't confirmed
			synced[i] = true;
			rejected++;
		}
		done++;
		if (progress)
			progress(done, mine, userdata);
	}

	if (ok > 0 || rejected > 0)
		ra_off_journal_rewrite(all, synced, total);
	if (ok > 0)
		ra_off_write_lastsync(now);

	free(synced);
	free(all);
	return ok;
}
