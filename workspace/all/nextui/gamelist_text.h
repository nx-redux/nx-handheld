#ifndef GAMELIST_TEXT_H
#define GAMELIST_TEXT_H

#include <stdbool.h>
#include <stddef.h>

// gamelist.c's string rules (dialog rows, collection names, the art-fetch title, folder-game files), SDL-free so
// they can be host-tested (common/tests/test_gamelist_text.c).

// Copy src into dst (size > 0), cut short on a UTF-8 character boundary (never half a character), terminated.
void GameListText_copyRow(char* dst, size_t size, const char* src);

// A collection's name: its file name without ".txt" (once, any case; a name that is only ".txt" is kept).
void GameListText_collectionStem(const char* path, char* out, size_t size);

// Width in px of utf8 as it would be drawn.
typedef int (*GameListText_measureFn)(void* ctx, const char* utf8);

// Fit name onto one line of max_w px: the whole name when it fits out (out_size >= 4) and max_w, else its longest
// prefix that ends on a UTF-8 boundary and fits with "..." after it. When not even "..." fits, the name (cut to out
// on a character boundary).
void GameListText_fitTitle(const char* name, char* out, size_t out_size, int max_w, GameListText_measureFn measure,
						   void* ctx);

// True when path (under roms_path) is the folder-named .cue/.m3u of its parent dir (basename minus extension ==
// parent dir name: the file that makes the parent a folder game) and that parent is not a console dir (a direct
// child of roms_path: a stray DC/DC.m3u must not make the whole library a game). parent_out (size) gets the parent.
bool GameListText_folderGameParent(const char* path, const char* roms_path, char* parent_out, size_t size);

#endif
