#ifndef ENTRY_CACHE_H
#define ENTRY_CACHE_H

#include <stdbool.h>
#include <stdint.h>

// content.c's "path\tname\n" cache files (the rom index; the Consoles list, whose rows are "path\tname\tcount"),
// headed by an ENTRY_CACHE_FP_PREFIX line carrying the fingerprint of the sources they were built from. SDL-free so
// they can be host-tested (common/tests/test_entry_cache.c).

#define ENTRY_CACHE_FP_PREFIX "#fp="

typedef void (*EntryCache_lineFn)(char* path, char* name, void* ctx);

// Walks the "path\tname\n" lines, calling cb(path, name, ctx) for each (name is the rest of the line after the
// first tab; blank lines are skipped, CRLF read as LF). Returns the line count, or -1 when the cache is missing or
// malformed (a line without a tab), or when expect_fp is set and the recorded source fingerprint differs (stale, or a
// pre-fingerprint file). fp_out (may be NULL) receives the recorded fingerprint, 0 when there is none.
int EntryCache_forEachLine(const char* cache_path, const uint64_t* expect_fp, uint64_t* fp_out, EntryCache_lineFn cb,
						   void* ctx);

// The fingerprint header of a cache file; false when the file is missing or has none.
bool EntryCache_readFp(const char* cache_path, uint64_t* fp);

// Rewrite cache_path stamped with new_fp, leaving out the rows at or below `drop` (a path; *dropped, may be NULL,
// gets how many) and, with adjust_folder set, lowering Consoles-list counts by adjust_dropped where the row collates
// adjust_folder (Emulist_adjustCount; a row that isn't "path\tname\tcount" fails the rewrite). Returns the rows
// kept, or -1 (file untouched) when it can't be read or written; an empty result is not written.
int EntryCache_rewrite(const char* cache_path, uint64_t new_fp, const char* drop, int* dropped, const char* adjust_folder,
					   int adjust_dropped);

#endif
