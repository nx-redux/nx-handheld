#ifndef PATH_LIST_H
#define PATH_LIST_H
// The "path[<TAB>label]" line files behind Recently Played (recents.txt) and the pins (shortcuts.txt).
#include "types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Gives one item's path and its label (NULL for none).
typedef void (*PathListGetFn)(void* item, const char** path, const char** label);

// Writes one "path[\tlabel]\n" line per item via writeFileAtomic (tmp + fsync + rename), so a crash or a full
// card mid-write can't truncate the list. False when out of memory or the write fails.
bool PathList_save(const char* file, Array* items, PathListGetFn get);

// Reads the next non-blank line of f into line (size bytes) and splits it at its first tab: *label is NULL when
// there is none. A line too long for the buffer is skipped whole (reading its tail as an entry would be bogus,
// and a re-save would keep only the cut-down head) and counted in *skipped when that is non-NULL. False at end
// of file.
bool PathList_nextLine(FILE* f, char* line, size_t size, char** path, char** label, int* skipped);

#endif
