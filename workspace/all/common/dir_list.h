#ifndef DIR_LIST_H
#define DIR_LIST_H

#include <stdbool.h>

// One row of a file browser listing (Image Viewer, Media Player).
typedef struct {
	char name[256];
	char path[512];
	bool is_dir;
	long size_bytes; // 0 for dirs and ".."; from stat while listing
} DirListEntry;

// Keeps a (non-directory) file by name, e.g. an extension check.
typedef bool (*DirListFilter)(const char* name);

// List `path` for a browser: every subdirectory plus the files `accept` keeps,
// hidden (dot) entries and paths over 1023 bytes skipped. When `path` is not
// `root`, a ".." row comes first, pointing at the parent of `path` (or at
// `root` when `path` has no '/'); the rest are sorted directories first, then
// case-insensitively by name. Returns a malloc'd array (never zero-sized; free
// it) and sets *count, or NULL with *count = 0 when the directory can't be
// opened or memory runs out. Pure: no logging, no directory creation.
DirListEntry* DirList_load(const char* path, const char* root, DirListFilter accept, int* count);

#endif // DIR_LIST_H
