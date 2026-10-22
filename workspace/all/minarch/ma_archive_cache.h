#ifndef MA_ARCHIVE_CACHE_H
#define MA_ARCHIVE_CACHE_H
// Where a zip/7z rom is unpacked in /tmp, kept free of minarch globals so the
// host tests (tests/test_archive_cache.c) can cover it.
//
// Each archive gets its own folder, named after a hash of its path, size and
// mtime. A cached rom therefore always came from this exact archive: matching
// cached files by name let "Double Dragon" pick up the extracted
// "Double Dragon II" (issue #222).
#include <stddef.h>
#include <stdio.h>

// Writes <root>/<key> into out, where key identifies archive_path by path,
// size and mtime. Returns 0, or -1 if the archive can't be stat'ed or the
// path does not fit.
int ArchiveCache_dir(char* out, size_t size, const char* root, const char* archive_path);

// Looks in dir for a non-empty regular file ending in one of extensions
// (NULL-terminated, without dots, e.g. {"nes", "fds", NULL}) and writes its
// path into out. Returns 1 if found, 0 if not.
int ArchiveCache_find(const char* dir, char** extensions, char* out, size_t size);

// /tmp is RAM: only the current game's folder is kept. Removes every entry
// under <root>/<tag>/ except keep (a path made by ArchiveCache_dir), along
// with anything else found there. Returns how many entries were removed.
int ArchiveCache_evictOthers(const char* root, const char* keep);

// Room the unpacked rom must leave free in /tmp, on top of its own size.
#define ARCHIVE_CACHE_RESERVE (48ULL * 1024 * 1024)

// 1 if `needed` bytes plus ARCHIVE_CACHE_RESERVE fit in `available` bytes.
int ArchiveCache_fits(unsigned long long available, unsigned long long needed);

// Free bytes for unprivileged writes on the filesystem holding path, or 0 if
// it can't be read.
unsigned long long ArchiveCache_available(const char* path);

// Sums the "Size = N" lines of a `7z l -slt` listing (the unpacked size of
// every entry). Returns the total, or -1 if the listing has none.
long long ArchiveCache_sum7zListing(FILE* listing);
#endif
