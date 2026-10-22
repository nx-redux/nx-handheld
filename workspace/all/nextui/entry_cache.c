// The "path\tname\n" cache files content.c keeps (the rom index and the Consoles list), SDL-free so they can be
// host-tested (common/tests/test_entry_cache.c).

#include "entry_cache.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "emulist_model.h"
#include "utils.h"

int EntryCache_forEachLine(const char* cache_path, const uint64_t* expect_fp, uint64_t* fp_out, EntryCache_lineFn cb,
						   void* ctx) {
	if (fp_out)
		*fp_out = 0;
	FILE* file = fopen(cache_path, "r");
	if (!file)
		return -1;

	int count = 0;
	// sized to what writeEntryCache can emit (two MAX_PATH strings + tab + newline), plus the
	// emulist's count column
	char line[MAX_PATH * 2 + 24];
	bool first = true;
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (first) {
			first = false;
			uint64_t fp = 0;
			bool has_fp = sscanf(line, ENTRY_CACHE_FP_PREFIX "%" SCNx64, &fp) == 1;
			if (expect_fp && (!has_fp || fp != *expect_fp)) {
				fclose(file);
				return -1; // sources changed since this cache was written
			}
			if (has_fp) {
				if (fp_out)
					*fp_out = fp;
				continue;
			}
		}
		if (strlen(line) == 0)
			continue;

		char* tab = strchr(line, '\t');
		if (!tab) {
			fclose(file);
			return -1; // malformed cache, force rescan
		}
		*tab = '\0';
		cb(line, tab + 1, ctx);
		count++;
	}
	fclose(file);
	return count;
}

bool EntryCache_readFp(const char* cache_path, uint64_t* fp) {
	FILE* file = fopen(cache_path, "r");
	if (!file)
		return false;
	char line[64];
	bool ok = fgets(line, sizeof(line), file) != NULL && sscanf(line, ENTRY_CACHE_FP_PREFIX "%" SCNx64, fp) == 1;
	fclose(file);
	return ok;
}

typedef struct {
	char* buf;
	size_t len, cap;
	const char* drop; // rows at or below this path are left out; NULL keeps all
	size_t drop_len;
	int kept, dropped;
	// emulist rows ("name\tcount" after the first tab): lower each count by adjust_dropped when the row
	// collates adjust_folder (Emulist_adjustCount); NULL copies rows through untouched
	const char* adjust_folder;
	int adjust_dropped;
	bool failed;
} CacheRewrite;

static void rewriteCacheCb(char* path, char* name, void* ctx) {
	CacheRewrite* rw = ctx;
	if (rw->failed)
		return;
	if (rw->drop && strncmp(path, rw->drop, rw->drop_len) == 0 &&
		(path[rw->drop_len] == '\0' || path[rw->drop_len] == '/')) {
		rw->dropped++;
		return;
	}
	char row[MAX_PATH * 2 + 24];
	if (rw->adjust_folder) {
		char* row_path;
		char* row_name;
		int count;
		if ((size_t)snprintf(row, sizeof(row), "%s\t%s", path, name) >= sizeof(row) ||
			!Emulist_parseRow(row, &row_path, &row_name, &count)) {
			rw->failed = true; // not a counted row: let the caller invalidate rather than guess
			return;
		}
		Emulist_adjustCount(row_path, rw->adjust_folder, rw->adjust_dropped, &count);
		// re-emit as "path\tname\tcount": the copy below writes "path\t<name>"
		char* tail = row_name + strlen(row_name);
		snprintf(tail, sizeof(row) - (tail - row), "\t%d", count);
		name = row_name;
	}
	size_t need = strlen(path) + strlen(name) + 2;
	while (rw->len + need + 1 > rw->cap) {
		char* grown = realloc(rw->buf, rw->cap * 2);
		if (!grown) {
			rw->failed = true;
			return;
		}
		rw->buf = grown;
		rw->cap *= 2;
	}
	rw->len += snprintf(rw->buf + rw->len, rw->cap - rw->len, "%s\t%s\n", path, name);
	rw->kept++;
}

int EntryCache_rewrite(const char* cache_path, uint64_t new_fp, const char* drop, int* dropped, const char* adjust_folder,
					   int adjust_dropped) {
	CacheRewrite rw = {malloc(16384), 0, 16384, drop, drop ? strlen(drop) : 0, 0, 0, adjust_folder, adjust_dropped, false};
	if (dropped)
		*dropped = 0;
	if (!rw.buf)
		return -1;
	rw.len = snprintf(rw.buf, rw.cap, ENTRY_CACHE_FP_PREFIX "%016" PRIx64 "\n", new_fp);
	int rows = EntryCache_forEachLine(cache_path, NULL, NULL, rewriteCacheCb, &rw);
	if (dropped)
		*dropped = rw.dropped;
	int kept = rw.kept;
	if (rows < 0 || rw.failed || (kept > 0 && !writeFileAtomic(cache_path, rw.buf, rw.len)))
		kept = -1;
	free(rw.buf);
	return kept;
}
