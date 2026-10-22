// Host test for scraper_names.c: the Artwork Manager's system tag, display name and map.txt key.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../scraper/scraper_names.h"

static void tag(const char* dir, const char* want) {
	char out[16];
	Scraper_extractTag(dir, out, sizeof(out));
	if (strcmp(out, want) != 0) {
		printf("FAIL: tag of '%s' = '%s', want '%s'\n", dir, out, want);
		assert(0);
	}
}

static void display(const char* dir, const char* want) {
	char out[64];
	Scraper_extractDisplayName(dir, out, sizeof(out));
	if (strcmp(out, want) != 0) {
		printf("FAIL: name of '%s' = '%s', want '%s'\n", dir, out, want);
		assert(0);
	}
}

int main(void) {
	tag("Game Boy (GB)", "GB");
	tag("01) Game Boy (GB)", "GB");
	tag("Arcade (FBN) (MAME)", "MAME");
	tag("Game Boy", "");
	tag("Game Boy ()", "");
	tag("Game Boy (GB", "");
	tag("Game Boy GB)", "");
	tag("X (THISTAGISWAYTOOLONG)", ""); // does not fit: none rather than a cut tag

	display("Game Boy (GB)", "Game Boy");
	display("01) Game Boy (GB)", "Game Boy");
	display("1)   Sega Genesis   (MD)", "Sega Genesis");
	display("Game Boy", "Game Boy");
	display("(GB)", "(GB)");		 // nothing in front of the tag: the whole name
	display("2048 (PORTS)", "2048"); // digits without ')' are not a sort prefix
	char small[5];
	Scraper_extractDisplayName("Game Boy (GB)", small, sizeof(small));
	assert(strcmp(small, "Game") == 0); // too long for out: the whole name, cut

	// a ROM keys on its file name in its own folder
	ScanGame g;
	memset(&g, 0, sizeof(g));
	snprintf(g.path, sizeof(g.path), "/sd/Roms/Game Boy (GB)/Tetris.gb");
	snprintf(g.filename, sizeof(g.filename), "Tetris.gb");
	char dir[256], key[128];
	Scraper_mapKeyForGame(&g, dir, sizeof(dir), key, sizeof(key));
	assert(strcmp(dir, "/sd/Roms/Game Boy (GB)") == 0 && strcmp(key, "Tetris.gb") == 0);

	// a folder game keys on the folder name, in the folder's parent map.txt
	memset(&g, 0, sizeof(g));
	snprintf(g.path, sizeof(g.path), "/sd/Roms/PlayStation (PS)/FF7/FF7.m3u");
	snprintf(g.filename, sizeof(g.filename), "FF7.m3u");
	g.folder_game = true;
	Scraper_mapKeyForGame(&g, dir, sizeof(dir), key, sizeof(key));
	assert(strcmp(dir, "/sd/Roms/PlayStation (PS)") == 0 && strcmp(key, "FF7") == 0);

	printf("test_scraper_names: all tests passed\n");
	return 0;
}
