#include "scraper_names.h"

#include <stdio.h>
#include <string.h>

#include "utils.h" // trimSortingMeta

void Scraper_extractTag(const char* dirname, char* tag_out, int tag_size) {
	tag_out[0] = '\0';
	const char* open = strrchr(dirname, '(');
	const char* close = strrchr(dirname, ')');
	if (open && close && close > open) {
		int len = (int)(close - open - 1);
		if (len > 0 && len < tag_size) {
			strncpy(tag_out, open + 1, len);
			tag_out[len] = '\0';
		}
	}
}

void Scraper_extractDisplayName(const char* dirname, char* name_out, int name_size) {
	const char* open = strrchr(dirname, '(');
	if (open && open > dirname) {
		int len = (int)(open - dirname);
		while (len > 0 && dirname[len - 1] == ' ')
			len--;
		if (len > 0 && len < name_size) {
			strncpy(name_out, dirname, len);
			name_out[len] = '\0';
		} else {
			snprintf(name_out, name_size, "%s", dirname);
		}
	} else {
		snprintf(name_out, name_size, "%s", dirname);
	}

	// Drop the numeric sort prefix nextui folders use ("1) Game Boy" ->
	// "Game Boy"), the same way the main menu title does (trimSortingMeta).
	char* trimmed = name_out;
	trimSortingMeta(&trimmed);
	if (trimmed != name_out)
		memmove(name_out, trimmed, strlen(trimmed) + 1);
}

void Scraper_mapKeyForGame(const ScanGame* g, char* dir, size_t dir_size,
						   char* key, size_t key_size) {
	if (g->folder_game) {
		// g->path is <parent>/<folder>/<folder>.cue; the listed entry is the
		// folder, so key on the folder name and look in <parent>/map.txt.
		char folder[SCAN_PATH_MAX];
		snprintf(folder, sizeof(folder), "%s", g->path);
		char* slash = strrchr(folder, '/');
		if (slash)
			*slash = '\0'; // -> <parent>/<folder>
		slash = strrchr(folder, '/');
		snprintf(key, key_size, "%s", slash ? slash + 1 : folder);
		if (slash)
			*slash = '\0'; // -> <parent>
		snprintf(dir, dir_size, "%s", folder);
	} else {
		snprintf(key, key_size, "%s", g->filename);
		snprintf(dir, dir_size, "%s", g->path);
		char* slash = strrchr(dir, '/');
		if (slash)
			*slash = '\0'; // -> parent dir of the ROM file
	}
}
