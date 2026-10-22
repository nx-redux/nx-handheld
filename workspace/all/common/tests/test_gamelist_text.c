// Host test for nextui/gamelist_text.c: dialog-row copies, collection stems, the art-fetch title fit and the
// folder-game file check. Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../nextui/gamelist_text.h"

static void test_copy_row(void) {
	char out[8];
	GameListText_copyRow(out, 4, "abc"); // exact fit
	assert(strcmp(out, "abc") == 0);
	GameListText_copyRow(out, 3, "abc");
	assert(strcmp(out, "ab") == 0);
	GameListText_copyRow(out, 3, "a\xC3\xA9"); // e-acute would be cut in half: left out whole
	assert(strcmp(out, "a") == 0);
	GameListText_copyRow(out, 4, "a\xC3\xA9");
	assert(strcmp(out, "a\xC3\xA9") == 0);
	GameListText_copyRow(out, 1, "abc");
	assert(strcmp(out, "") == 0);
	out[0] = 'x';
	GameListText_copyRow(out, 0, "abc"); // no room at all: untouched
	assert(out[0] == 'x');
}

static void test_collection_stem(void) {
	char out[64];
	GameListText_collectionStem("/sd/Collections/Favs.txt", out, sizeof(out));
	assert(strcmp(out, "Favs") == 0);
	GameListText_collectionStem("/sd/Collections/x.txt.txt", out, sizeof(out)); // stripped once
	assert(strcmp(out, "x.txt") == 0);
	GameListText_collectionStem("/sd/Collections/.txt", out, sizeof(out)); // only the extension: kept
	assert(strcmp(out, ".txt") == 0);
	GameListText_collectionStem("Favs.TXT", out, sizeof(out));
	assert(strcmp(out, "Favs") == 0);
	GameListText_collectionStem("/sd/Collections/RPGs", out, sizeof(out));
	assert(strcmp(out, "RPGs") == 0);
}

static int measure_10px(void* ctx, const char* utf8) {
	(*(int*)ctx)++;
	return (int)strlen(utf8) * 10;
}

static void test_fit_title(void) {
	char out[256];
	int calls = 0;
	GameListText_fitTitle("Hello", out, sizeof(out), 60, measure_10px, &calls);
	assert(strcmp(out, "Hello") == 0 && calls == 1);
	GameListText_fitTitle("Hello World", out, sizeof(out), 60, measure_10px, &calls);
	assert(strcmp(out, "Hel...") == 0);
	// UTF-8: the cut lands between characters, never inside one
	GameListText_fitTitle("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", out, sizeof(out), 70, measure_10px, &calls);
	assert(strcmp(out, "\xC3\xA9\xC3\xA9...") == 0);
	GameListText_fitTitle("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", out, sizeof(out), 60, measure_10px, &calls);
	assert(strcmp(out, "\xC3\xA9...") == 0);
	// nothing fits, not even "...": the name
	GameListText_fitTitle("Hello World", out, sizeof(out), 20, measure_10px, &calls);
	assert(strcmp(out, "Hello World") == 0);
	// a name longer than out is never shown whole or cut without its "...": the cut fits out with it
	char longname[300];
	memset(longname, 'a', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	GameListText_fitTitle(longname, out, sizeof(out), 1000000, measure_10px, &calls);
	assert(strlen(out) == 255 && strcmp(out + 252, "...") == 0);
	// a 2-byte character straddling the cut is left out whole
	memset(longname, 'a', sizeof(longname) - 1);
	longname[251] = (char)0xC3;
	longname[252] = (char)0xA9;
	GameListText_fitTitle(longname, out, sizeof(out), 1000000, measure_10px, &calls);
	assert(strlen(out) == 254 && strcmp(out + 251, "...") == 0);
}

static void test_folder_game(void) {
	char parent[256];
	assert(GameListText_folderGameParent("/sd/Roms/PS (PS)/Game/Game.cue", "/sd/Roms", parent, sizeof(parent)));
	assert(strcmp(parent, "/sd/Roms/PS (PS)/Game") == 0);
	assert(GameListText_folderGameParent("/sd/Roms/PS/My.Game/My.Game.m3u", "/sd/Roms", parent, sizeof(parent)));
	assert(strcmp(parent, "/sd/Roms/PS/My.Game") == 0);
	assert(GameListText_folderGameParent("/sd/Roms/PS/Game/Game.CUE", "/sd/Roms", parent, sizeof(parent)));
	// a playlist named after its console is no folder game (deleting "the game" would delete the library)
	assert(!GameListText_folderGameParent("/sd/Roms/DC/DC.m3u", "/sd/Roms", parent, sizeof(parent)));
	assert(!GameListText_folderGameParent("/sd/Roms/PS/Game/Other.cue", "/sd/Roms", parent, sizeof(parent)));
	assert(!GameListText_folderGameParent("/sd/Roms/PS/Game/Game.bin", "/sd/Roms", parent, sizeof(parent)));
	assert(!GameListText_folderGameParent("/elsewhere/Game/Game.cue", "/sd/Roms", parent, sizeof(parent)));
	assert(!GameListText_folderGameParent("Game.cue", "Game.cue", parent, sizeof(parent)));
}

int main(void) {
	test_copy_row();
	test_collection_stem();
	test_fit_title();
	test_folder_game();
	printf("test_gamelist_text: OK\n");
	return 0;
}
