#ifndef SCRAPER_NAMES_H
#define SCRAPER_NAMES_H

#include <stddef.h>

#include "scraper_scan.h"

// Name helpers of the Artwork Manager's system and ROM lists, split out of scraper.c for host tests.

// "Game Boy (GB)" -> "GB": the text inside the last (...). "" when there is none, it is empty, or
// it does not fit tag_size.
void Scraper_extractTag(const char* dirname, char* tag_out, int tag_size);

// "01) Game Boy (GB)" -> "Game Boy": the folder name before its last " (", without nextui's
// numeric sort prefix. The whole name when it has no tag.
void Scraper_extractDisplayName(const char* dirname, char* name_out, int name_size);

// Fill dir + key with the parent directory and basename nextui uses to alias this game in a
// map.txt (see mapAliasFor): a folder game keys on its folder name in the folder's parent.
void Scraper_mapKeyForGame(const ScanGame* g, char* dir, size_t dir_size, char* key, size_t key_size);

#endif
