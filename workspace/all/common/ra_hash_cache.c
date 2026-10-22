#include "ra_hash_cache.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cjson/cJSON.h"

#define RA_HC_MAX_PATH 1024
#define RA_HC_MAX_FILE (16 * 1024 * 1024)
#define RA_HC_TZ_STEP 900		 // FAT time zone offsets come in 15 minute steps
#define RA_HC_TZ_MAX (14 * 3600) // ...of at most 14 hours

// Writes in a burst: the first RA_HC_BURST_SAVES within RA_HC_BURST_MS go to
// disk at once (minarch's forget + store), later ones are held in memory and
// written together once the window has passed (the RA pak's Library run
// stores once per rom: rewriting the whole file each time is quadratic)
#define RA_HC_BURST_SAVES 4
#define RA_HC_BURST_MS 2000

// rcheevos' RC_CONSOLE_ARCADE (this module doesn't include rcheevos)
#define RA_HASH_CACHE_CONSOLE_ARCADE 27

static char hc_roms[RA_HC_MAX_PATH] = {0};

// One console folder's hashes.json, kept parsed between calls so a Library
// run doesn't re-read it per rom. Reloaded when the file changes on disk
// (another writer); `pending` holds the entries changed since the last
// write (an entry, or a JSON null for a forget) so a reload keeps them.
// Callers include minarch's HTTP threads: all of it under hc_mutex.
static pthread_mutex_t hc_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct {
	char dir[RA_HC_MAX_PATH];
	char path[RA_HC_MAX_PATH + 32];
	cJSON* root;	// NULL: nothing cached
	cJSON* pending; // non-NULL: changes not yet on disk
	bool stamped;	// file existed at load / last write; stamp below
	dev_t dev;
	ino_t ino;
	off_t size;
	long long mtime_ns;
	long long burst_start; // ms
	int burst_saves;
} hc_mem;
static bool hc_atexit_set = false;

static long long hc_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long hc_mtime_ns(const struct stat* st) {
#ifdef __APPLE__
	return (long long)st->st_mtimespec.tv_sec * 1000000000LL + st->st_mtimespec.tv_nsec;
#else
	return (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
#endif
}

// Remember the file's identity, to notice another writer replacing it
static void hc_stamp(void) {
	struct stat st;
	hc_mem.stamped = stat(hc_mem.path, &st) == 0;
	if (hc_mem.stamped) {
		hc_mem.dev = st.st_dev;
		hc_mem.ino = st.st_ino;
		hc_mem.size = st.st_size;
		hc_mem.mtime_ns = hc_mtime_ns(&st);
	}
}

static bool hc_stamp_matches(void) {
	struct stat st;
	if (stat(hc_mem.path, &st) != 0)
		return !hc_mem.stamped;
	return hc_mem.stamped && st.st_dev == hc_mem.dev && st.st_ino == hc_mem.ino &&
		   st.st_size == hc_mem.size && hc_mtime_ns(&st) == hc_mem.mtime_ns;
}

static bool hc_flush_locked(void);

static void hc_drop_locked(void) {
	cJSON_Delete(hc_mem.root);
	cJSON_Delete(hc_mem.pending);
	hc_mem.root = NULL;
	hc_mem.pending = NULL;
	hc_mem.dir[0] = '\0';
	hc_mem.path[0] = '\0';
}

static void hc_atexit_flush(void) {
	// a thread may still hold the lock at exit: never wait on it there
	if (pthread_mutex_trylock(&hc_mutex) == 0) {
		hc_flush_locked();
		pthread_mutex_unlock(&hc_mutex);
	}
}

void RA_HashCache_init(const char* roms_root) {
	pthread_mutex_lock(&hc_mutex);
	hc_flush_locked();
	hc_drop_locked();
	hc_mem.burst_saves = 0;
	if (!hc_atexit_set)
		hc_atexit_set = atexit(hc_atexit_flush) == 0;
	snprintf(hc_roms, sizeof(hc_roms), "%s", roms_root ? roms_root : "");
	// a trailing slash would double up when the path is split
	size_t n = strlen(hc_roms);
	while (n > 1 && hc_roms[n - 1] == '/')
		hc_roms[--n] = '\0';
	pthread_mutex_unlock(&hc_mutex);
}

bool RA_HashCache_mtimeMatches(long long cached, long long actual) {
	long long diff = cached > actual ? cached - actual : actual - cached;
	return diff == 0 || (diff % RA_HC_TZ_STEP == 0 && diff <= RA_HC_TZ_MAX);
}

bool RA_HashCache_canStore(const char* hashed_path, uint32_t console_id) {
	const char* ext = hashed_path ? strrchr(hashed_path, '.') : NULL;
	bool archive = ext && (!strcasecmp(ext, ".zip") || !strcasecmp(ext, ".7z"));
	return hashed_path && (!archive || console_id == RA_HASH_CACHE_CONSOLE_ARCADE);
}

/*****************************************************************************
 * Paths
 *****************************************************************************/

// Split rom_path into its console folder's data dir (<roms>/<Folder>/
// .retroachievements), its cache file (hashes.json in there; cache_file may be
// NULL) and its key (the path below <Folder>). False for paths outside a
// visible console folder, with "." / ".." components, or too long for the
// buffers.
static bool hc_split(const char* rom_path, char* cache_dir, size_t dir_size, char* cache_file,
					 size_t file_size, const char** key) {
	if (!hc_roms[0] || !rom_path)
		return false;
	size_t rlen = strlen(hc_roms);
	if (strncmp(rom_path, hc_roms, rlen) != 0 || rom_path[rlen] != '/')
		return false;
	const char* folder = rom_path + rlen + 1;
	const char* slash = strchr(folder, '/');
	if (!slash || slash == folder || folder[0] == '.' || !slash[1])
		return false;
	*key = slash + 1;
	for (const char* c = *key; *c;) {
		size_t len = strcspn(c, "/");
		if (len == 0 || (len == 1 && c[0] == '.') || (len == 2 && c[0] == '.' && c[1] == '.'))
			return false;
		c += len;
		if (*c == '/')
			c++;
	}
	int folder_len = (int)(slash - folder);
	int len = snprintf(cache_dir, dir_size, "%s/%.*s/%s", hc_roms, folder_len, folder, RA_HASH_CACHE_DIR);
	if (len <= 0 || (size_t)len >= dir_size)
		return false;
	if (cache_file) {
		len = snprintf(cache_file, file_size, "%s/%s", cache_dir, RA_HASH_CACHE_FILE);
		if (len <= 0 || (size_t)len >= file_size)
			return false;
	}
	return true;
}

bool RA_HashCache_folderDir(const char* rom_path, char* buf, size_t n) {
	if (!buf || n == 0)
		return false;
	const char* key;
	if (!hc_split(rom_path, buf, n, NULL, 0, &key)) {
		buf[0] = '\0';
		return false;
	}
	return true;
}

/*****************************************************************************
 * File I/O
 *****************************************************************************/

static cJSON* hc_load(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > RA_HC_MAX_FILE) {
		fclose(f);
		return NULL;
	}
	char* buf = (char*)malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[rd] = '\0';
	cJSON* root = cJSON_Parse(buf);
	free(buf);
	if (root && !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return NULL;
	}
	return root;
}

// Whole-file rewrite via hashes.json.tmp + rename, so a power cut never leaves a torn file
static bool hc_save(const char* dir, const char* path, cJSON* root) {
	mkdir(dir, 0755);
	char* text = cJSON_PrintUnformatted(root);
	if (!text)
		return false;
	char tmp[RA_HC_MAX_PATH + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	bool ok = false;
	FILE* f = fopen(tmp, "wb");
	if (f) {
		size_t len = strlen(text);
		ok = fwrite(text, 1, len, f) == len;
		fflush(f);
		fsync(fileno(f));
		ok = (fclose(f) == 0) && ok;
		ok = ok && rename(tmp, path) == 0;
		if (!ok)
			remove(tmp);
	}
	cJSON_free(text);
	if (!ok)
		fprintf(stderr, "[RA] hash cache: could not write %s\n", path);
	return ok;
}

/*****************************************************************************
 * Entries
 *****************************************************************************/

// 32 hex digits, lowercased into out (33 bytes)
static bool hc_valid_hash(const char* s, char* out) {
	if (!s || strlen(s) != 32)
		return false;
	for (int i = 0; i < 32; i++) {
		if (!isxdigit((unsigned char)s[i]))
			return false;
		out[i] = (char)tolower((unsigned char)s[i]);
	}
	out[32] = '\0';
	return true;
}

static bool hc_number(const cJSON* entry, const char* name, double* out) {
	const cJSON* item = cJSON_GetObjectItemCaseSensitive(entry, name);
	if (!cJSON_IsNumber(item) || item->valuedouble < 0)
		return false;
	*out = item->valuedouble;
	return true;
}

// Set (or add) a numeric field, keeping the entry's other keys
static void hc_set_number(cJSON* entry, const char* name, double value) {
	cJSON* item = cJSON_GetObjectItemCaseSensitive(entry, name);
	if (cJSON_IsNumber(item))
		cJSON_SetNumberValue(item, value);
	else if (item)
		cJSON_ReplaceItemInObjectCaseSensitive(entry, name, cJSON_CreateNumber(value));
	else
		cJSON_AddNumberToObject(entry, name, value);
}

static cJSON* hc_files(cJSON* root, bool create) {
	cJSON* files = cJSON_GetObjectItemCaseSensitive(root, "files");
	if (cJSON_IsObject(files) || !create)
		return cJSON_IsObject(files) ? files : NULL;
	files = cJSON_CreateObject();
	if (cJSON_GetObjectItemCaseSensitive(root, "files"))
		cJSON_ReplaceItemInObjectCaseSensitive(root, "files", files);
	else
		cJSON_AddItemToObject(root, "files", files);
	return files;
}

/*****************************************************************************
 * In-memory copy (hold hc_mutex)
 *****************************************************************************/

// Apply the not-yet-written changes to root (after a reload from disk)
static void hc_apply_pending(cJSON* root) {
	if (!hc_mem.pending)
		return;
	cJSON* files = hc_files(root, true);
	if (!files)
		return;
	for (cJSON* item = hc_mem.pending->child; item; item = item->next) {
		cJSON_DeleteItemFromObjectCaseSensitive(files, item->string);
		if (!cJSON_IsNull(item)) {
			cJSON* copy = cJSON_Duplicate(item, true);
			if (copy)
				cJSON_AddItemToObject(files, item->string, copy);
		}
	}
	hc_set_number(root, "version", 1);
}

// The parsed hashes.json at path (dir = its folder), read again only when the
// file changed. create: an empty root for a missing or corrupt file. NULL on
// a miss (create false) or out of memory.
static cJSON* hc_get_root(const char* dir, const char* path, bool create) {
	if (hc_mem.path[0] && strcmp(hc_mem.path, path) != 0) {
		hc_flush_locked(); // another console folder: finish the old one
		hc_drop_locked();
	}
	if (!hc_mem.path[0]) {
		snprintf(hc_mem.dir, sizeof(hc_mem.dir), "%s", dir);
		snprintf(hc_mem.path, sizeof(hc_mem.path), "%s", path);
	}
	if (hc_mem.root && !hc_stamp_matches()) {
		cJSON_Delete(hc_mem.root);
		hc_mem.root = NULL;
	}
	if (!hc_mem.root) {
		hc_mem.root = hc_load(path); // missing or corrupt: start over
		hc_stamp();
		if (hc_mem.root)
			hc_apply_pending(hc_mem.root);
		else if (hc_mem.pending) {
			hc_mem.root = cJSON_CreateObject();
			if (hc_mem.root)
				hc_apply_pending(hc_mem.root);
		}
	}
	if (!hc_mem.root && create)
		hc_mem.root = cJSON_CreateObject();
	return hc_mem.root;
}

// Write the cached root out, if anything is pending
static bool hc_flush_locked(void) {
	if (!hc_mem.pending || !hc_mem.path[0])
		return true;
	// another writer replaced the file meanwhile: merge onto its version
	if (!hc_mem.root || !hc_stamp_matches()) {
		cJSON_Delete(hc_mem.root);
		hc_mem.root = hc_load(hc_mem.path);
		if (!hc_mem.root)
			hc_mem.root = cJSON_CreateObject();
		if (!hc_mem.root)
			return false;
		hc_apply_pending(hc_mem.root);
	}
	bool ok = hc_save(hc_mem.dir, hc_mem.path, hc_mem.root);
	hc_stamp();
	cJSON_Delete(hc_mem.pending);
	hc_mem.pending = NULL; // a failed write is not retried: it's a cache
	return ok;
}

// Record key's new entry (NULL = forgotten) and write it out now, or with
// the rest of a burst
static bool hc_commit(const char* key, const cJSON* entry) {
	if (!hc_mem.pending)
		hc_mem.pending = cJSON_CreateObject();
	if (hc_mem.pending) {
		cJSON* copy = entry ? cJSON_Duplicate(entry, true) : cJSON_CreateNull();
		cJSON_DeleteItemFromObjectCaseSensitive(hc_mem.pending, key);
		if (copy)
			cJSON_AddItemToObject(hc_mem.pending, key, copy);
	}
	long long now = hc_now_ms();
	if (now - hc_mem.burst_start >= RA_HC_BURST_MS) {
		hc_mem.burst_start = now;
		hc_mem.burst_saves = 0;
	}
	if (hc_mem.burst_saves >= RA_HC_BURST_SAVES)
		return true; // held: written with the burst's next save or flush
	hc_mem.burst_saves++;
	return hc_flush_locked();
}

/*****************************************************************************
 * Public
 *****************************************************************************/

void RA_HashCache_flush(void) {
	pthread_mutex_lock(&hc_mutex);
	hc_flush_locked();
	pthread_mutex_unlock(&hc_mutex);
}


bool RA_HashCache_lookup(const char* rom_path, uint32_t console_id, char hash_out[33],
						 uint32_t* game_id) {
	char dir[RA_HC_MAX_PATH], path[RA_HC_MAX_PATH + 32];
	const char* key;
	if (!hash_out || !hc_split(rom_path, dir, sizeof(dir), path, sizeof(path), &key))
		return false;
	struct stat st;
	if (stat(rom_path, &st) != 0)
		return false;
	pthread_mutex_lock(&hc_mutex);
	// held writes whose burst is over go out now
	if (hc_mem.pending && hc_now_ms() - hc_mem.burst_start >= RA_HC_BURST_MS)
		hc_flush_locked();
	cJSON* root = hc_get_root(dir, path, false);
	if (!root) {
		pthread_mutex_unlock(&hc_mutex);
		return false;
	}

	bool hit = false;
	const cJSON* entry = cJSON_GetObjectItemCaseSensitive(hc_files(root, false), key);
	double size, mtime, console, game = 0;
	const cJSON* hash = cJSON_GetObjectItemCaseSensitive(entry, "hash");
	char clean[33];
	if (cJSON_IsObject(entry) && hc_number(entry, "size", &size) &&
		hc_number(entry, "mtime", &mtime) && hc_number(entry, "console", &console) &&
		cJSON_IsString(hash) && hc_valid_hash(hash->valuestring, clean) &&
		(uint32_t)console == console_id && (long long)size == (long long)st.st_size &&
		RA_HashCache_mtimeMatches((long long)mtime, (long long)st.st_mtime)) {
		hc_number(entry, "game", &game); // absent = unknown to RA
		memcpy(hash_out, clean, sizeof(clean));
		if (game_id)
			*game_id = (uint32_t)game;
		hit = true;
	}
	pthread_mutex_unlock(&hc_mutex);
	return hit;
}

bool RA_HashCache_store(const char* rom_path, uint32_t console_id, const char* hash,
						uint32_t game_id) {
	char dir[RA_HC_MAX_PATH], path[RA_HC_MAX_PATH + 32];
	const char* key;
	char clean[33];
	if (!hc_valid_hash(hash, clean) || !hc_split(rom_path, dir, sizeof(dir), path, sizeof(path), &key))
		return false;
	struct stat st;
	if (stat(rom_path, &st) != 0)
		return false;

	pthread_mutex_lock(&hc_mutex);
	cJSON* root = hc_get_root(dir, path, true);
	cJSON* files = root ? hc_files(root, true) : NULL;
	if (!files) {
		pthread_mutex_unlock(&hc_mutex);
		return false;
	}
	cJSON* entry = cJSON_GetObjectItemCaseSensitive(files, key);

	// already identical: skip the write (every launch would rewrite otherwise).
	// An mtime the validity rule accepts counts as the same, so two OSes that
	// read FAT times in different time zones don't keep rewriting each other.
	double size, mtime, console, game;
	const cJSON* old_hash = cJSON_GetObjectItemCaseSensitive(entry, "hash");
	if (cJSON_IsObject(entry) && hc_number(entry, "size", &size) &&
		hc_number(entry, "mtime", &mtime) && hc_number(entry, "console", &console) &&
		hc_number(entry, "game", &game) && cJSON_IsString(old_hash) &&
		(long long)size == (long long)st.st_size &&
		RA_HashCache_mtimeMatches((long long)mtime, (long long)st.st_mtime) &&
		(uint32_t)console == console_id && (uint32_t)game == game_id &&
		!strcmp(old_hash->valuestring, clean)) {
		pthread_mutex_unlock(&hc_mutex);
		return true;
	}

	if (!cJSON_IsObject(entry)) {
		cJSON* fresh = cJSON_CreateObject();
		if (entry)
			cJSON_ReplaceItemInObjectCaseSensitive(files, key, fresh);
		else
			cJSON_AddItemToObject(files, key, fresh);
		entry = fresh;
	}
	hc_set_number(root, "version", 1);
	hc_set_number(entry, "size", (double)st.st_size);
	hc_set_number(entry, "mtime", (double)st.st_mtime);
	hc_set_number(entry, "console", console_id);
	cJSON* h = cJSON_GetObjectItemCaseSensitive(entry, "hash");
	if (h)
		cJSON_ReplaceItemInObjectCaseSensitive(entry, "hash", cJSON_CreateString(clean));
	else
		cJSON_AddStringToObject(entry, "hash", clean);
	hc_set_number(entry, "game", game_id);

	bool ok = hc_commit(key, entry);
	pthread_mutex_unlock(&hc_mutex);
	return ok;
}

void RA_HashCache_forget(const char* rom_path) {
	char dir[RA_HC_MAX_PATH], path[RA_HC_MAX_PATH + 32];
	const char* key;
	if (!hc_split(rom_path, dir, sizeof(dir), path, sizeof(path), &key))
		return;
	pthread_mutex_lock(&hc_mutex);
	cJSON* root = hc_get_root(dir, path, false);
	cJSON* files = root ? hc_files(root, false) : NULL;
	if (cJSON_GetObjectItemCaseSensitive(files, key)) {
		cJSON_DeleteItemFromObjectCaseSensitive(files, key);
		hc_commit(key, NULL);
	}
	pthread_mutex_unlock(&hc_mutex);
}
