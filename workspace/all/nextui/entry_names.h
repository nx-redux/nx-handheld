#ifndef ENTRY_NAMES_H
#define ENTRY_NAMES_H

#include <stdbool.h>
#include "types.h"

// The naming rules behind content.c's Directory_index (map.txt aliases, letter-jump groups, same-name run labels)
// and the Game Switcher's duplicate labels, SDL-free so they can be host-tested (common/tests/test_entry_names.c).

// The letter-jump group of a name, in EntryArray_sort's order (strcasecmp: bytes compared after tolower): 0 for what
// sorts before 'a' (digits, most punctuation), 1-26 for a-z, 27 for what sorts after 'z' ("{|}~" and every UTF-8
// lead byte), so each group is one run of the sorted list.
int EntryNames_indexChar(const char* name);

// One row of the letter-jump index, rows taken in list order: a row whose group differs from the last row's (*alpha)
// starts a group at that row in alphas, unless alphas is full, when it joins the last group (a list not in
// EntryNames_indexChar's order could start more groups than alphas holds). Returns the row's group index.
int EntryNames_alphaStep(IntArray* alphas, int* alpha, int* index, int row, const char* name);

// Stem of a path: its basename with trailing 1-4 letter extensions removed, matching getDisplayName()'s rule so
// multi-extensions (e.g. .p8.png) collapse; the basename when stripping would leave nothing. out is MAX_PATH.
void EntryNames_fileStem(const char* path, char* out);

// Parse a map.txt: one "<key>\t<display name>" line per entry; blank lines, lines without a tab and empty names are
// skipped, the name ends at a further tab and loses trailing blanks. NULL if the file cannot be opened, else a Hash
// the caller must Hash_free.
Hash* EntryNames_readMap(const char* map_path);

// A run [i, j) (j - i >= 2) of entries sharing one display name, from sibling Roms/<Console> (<TAG>) folders.
typedef struct {
	bool same_file;	   // every row is the same file name (per-core copies of one rom)
	bool tags_differ;  // the rows come from more than one core tag
	bool stems_unique; // (not same_file) no two rows share a file stem
} EntryNamesRun;

EntryNamesRun EntryNames_runInfo(Array* entries, int i, int j);

// Sets each run row's unique label:
//   same file across the run  -> "<name> (<tag>)"
//   files differ, tags differ -> "<stem> (<tag>)" (the file name instead of the stem when stems collide)
//   files differ, same tag    -> "<stem>"         (likewise)
void EntryNames_labelRun(Array* entries, int i, int j, EntryNamesRun run);

// The Game Switcher's rows: a name shared with another row gets "<name> (<tag>)", or "<name> (<file name>)" when a
// row of the same name has the same tag. Rows with a unique name are left alone.
void EntryNames_labelSwitcher(Array* entries);

#endif
