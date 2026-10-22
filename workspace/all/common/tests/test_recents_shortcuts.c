// Host test for nextui/recents.c and nextui/shortcuts.c against a fixture card (-Isdroot: the card at
// /tmp/nx_test_sdroot; -Istubs: api.h's logging only). Covers the Recently Played add/bump/evict rules and file
// load (long lines, missing games, multi-disc dedupe, the change-disc handoff), and the pins' load, limits, order
// and in-place rename. Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defines.h"
#include "utils.h"
#include "../../nextui/recents.h"
#include "../../nextui/shortcuts.h"

#define GB "/Roms/GB (GB)"
#define DISC "/Roms/PS (PS)/Game/Game (Disc "

static void sh(const char* cmd) {
	assert(system(cmd) == 0);
}

static void put(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

static int count_lines(const char* path) {
	FILE* f = fopen(path, "r");
	assert(f);
	int n = 0, c;
	while ((c = fgetc(f)) != EOF)
		n += c == '\n';
	fclose(f);
	return n;
}

static void setup_card(void) {
	sh("rm -rf " SDCARD_PATH " && mkdir -p " SHARED_USERDATA_PATH "/.minui '" SDCARD_PATH GB "' '" SDCARD_PATH
	   "/Roms/NES (FC)' '" SDCARD_PATH "/Roms/PS (PS)/Game' " SDCARD_PATH "/Tools");
	char path[MAX_PATH];
	const char* files[] = {GB "/a.gb", GB "/b.gb", "/Roms/NES (FC)/n.nes", DISC "1).cue", DISC "2).cue",
						   "/Roms/PS (PS)/Game/Game.m3u"};
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		snprintf(path, sizeof(path), SDCARD_PATH "%s", files[i]);
		put(path, "");
	}
	for (int i = 0; i < 30; i++) {
		snprintf(path, sizeof(path), SDCARD_PATH GB "/g%02d.gb", i);
		put(path, "");
		snprintf(path, sizeof(path), SDCARD_PATH "/Tools/T%d.pak", i);
		put(path, "");
	}
}

static int has_emu(char* emu_name) {
	return strcmp(emu_name, "GB") == 0 || strcmp(emu_name, "PS") == 0;
}

static int has_m3u(char* rom_path, char* m3u_path) {
	return M3U_findForRom(rom_path, m3u_path, MAX_PATH);
}

static void add(const char* sd_relative, const char* alias) {
	char path[MAX_PATH], name[MAX_PATH];
	snprintf(path, sizeof(path), SDCARD_PATH "%s", sd_relative);
	snprintf(name, sizeof(name), "%s", alias ? alias : "");
	Recents_add(path, alias ? name : NULL);
}

static const char* recent_path(int i) {
	Recent* r = Recents_at(i);
	assert(r);
	return r->path;
}

static void test_recents_add(void) {
	Recents_init();
	Recents_setHasEmu(has_emu);
	Recents_setHasM3u(has_m3u);
	add(GB "/a.gb", NULL);
	assert(Recents_count() == 1 && strcmp(recent_path(0), GB "/a.gb") == 0 && Recents_at(0)->available);
	add(GB "/b.gb", NULL);
	assert(strcmp(recent_path(0), GB "/b.gb") == 0);
	// a game already listed is bumped to the top, its alias refreshed
	add(GB "/a.gb", "Alpha");
	assert(Recents_count() == 2 && strcmp(recent_path(0), GB "/a.gb") == 0);
	assert(strcmp(Recents_at(0)->alias, "Alpha") == 0);
	add(GB "/a.gb", NULL); // no alias given: the old one stays
	assert(strcmp(Recents_at(0)->alias, "Alpha") == 0);
	char outside[] = "/elsewhere/x.gb";
	Recents_add(outside, NULL); // only card paths
	assert(Recents_count() == 2);
	// MAX_RECENTS: the oldest go
	char rel[64];
	for (int i = 0; i < 30; i++) {
		snprintf(rel, sizeof(rel), GB "/g%02d.gb", i);
		add(rel, NULL);
	}
	assert(Recents_count() == MAX_RECENTS);
	assert(strcmp(recent_path(0), GB "/g29.gb") == 0 && strcmp(recent_path(MAX_RECENTS - 1), GB "/g06.gb") == 0);
	assert(count_lines(RECENT_PATH) == MAX_RECENTS);
	// rename: the entry keeps its place under a new alias; remove by path
	char g10[] = GB "/g10.gb";
	char alias[] = "Ten";
	Recents_updateAlias(g10, alias);
	assert(strcmp(Recents_at(19)->alias, "Ten") == 0);
	Recents_removeByPath(g10);
	assert(Recents_count() == MAX_RECENTS - 1 && strcmp(recent_path(19), GB "/g09.gb") == 0);
	Recents_quit();
}

static void test_recents_load(void) {
	Recents_init();
	unlink(CHANGE_DISC_PATH);
	char longline[1200];
	memset(longline, 'x', 1023);
	snprintf(longline + 1023, sizeof(longline) - 1023, "%s", GB "/b.gb\n");
	char file[4096];
	snprintf(file, sizeof(file),
			 GB "/a.gb\tAlpha\r\n\n" GB "/gone.gb\n%s/Roms/NES (FC)/n.nes\n" DISC "1).cue\n" DISC "2).cue\n" GB
				"/b.gb\n",
			 longline);
	put(RECENT_PATH, file);
	assert(Recents_load() == 1);
	// a missing game, the over-long line (its tail is no entry) and a second disc of one game are dropped
	assert(Recents_count() == 4);
	assert(strcmp(recent_path(0), GB "/a.gb") == 0 && strcmp(Recents_at(0)->alias, "Alpha") == 0);
	assert(strcmp(recent_path(1), "/Roms/NES (FC)/n.nes") == 0 && !Recents_at(1)->available);
	assert(strcmp(recent_path(2), DISC "1).cue") == 0 && strcmp(recent_path(3), GB "/b.gb") == 0);
	assert(count_lines(RECENT_PATH) == 4); // and the file was cleaned up
	Entry* first = Recents_firstRom();
	assert(first && strcmp(first->path, SDCARD_PATH GB "/a.gb") == 0 && strcmp(first->name, "Alpha") == 0);
	Entry_free(first);

	// a disc change: the new disc leads and replaces the game's other disc
	put(CHANGE_DISC_PATH, SDCARD_PATH DISC "2).cue");
	assert(Recents_load() == 1);
	assert(Recents_count() == 4 && strcmp(recent_path(0), DISC "2).cue") == 0);
	assert(strcmp(recent_path(1), GB "/a.gb") == 0 && strcmp(recent_path(3), GB "/b.gb") == 0);
	assert(access(CHANGE_DISC_PATH, F_OK) != 0);

	// only unavailable games: nothing to show
	put(RECENT_PATH, "/Roms/NES (FC)/n.nes\n");
	assert(Recents_load() == 0 && Recents_count() == 1 && !Recents_firstRom());

	// more lines than MAX_RECENTS: capped
	FILE* f = fopen(RECENT_PATH, "w");
	for (int i = 0; i < 30; i++)
		fprintf(f, GB "/g%02d.gb\n", i);
	fclose(f);
	assert(Recents_load() == 1 && Recents_count() == MAX_RECENTS);
	assert(count_lines(RECENT_PATH) == MAX_RECENTS);
	Recents_quit();
}

static Entry* entry(const char* sd_relative, int type) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path), SDCARD_PATH "%s", sd_relative);
	return Entry_new(path, type);
}

static bool can_add(const char* sd_relative, int type) {
	Entry* e = entry(sd_relative, type);
	bool ok = Shortcuts_canAdd(e);
	Entry_free(e);
	return ok;
}

static void pin(const char* sd_relative, int type, bool add) {
	Entry* e = entry(sd_relative, type);
	if (add)
		Shortcuts_add(e);
	else
		Shortcuts_remove(e);
	Entry_free(e);
}

static void test_shortcuts(void) {
	char longline[1200];
	memset(longline, 'x', 1023);
	snprintf(longline + 1023, sizeof(longline) - 1023, "%s", "/Tools/T9.pak\n");
	char file[4096];
	snprintf(file, sizeof(file), GB "/a.gb\tAlpha\n/Tools/T0.pak\n" GB "/gone.gb\tGone\n%s/Tools/T1.pak\tOne\r\n",
			 longline);
	put(SHORTCUTS_PATH, file);
	Shortcuts_init();
	// stored order kept; the missing pin and the over-long line dropped
	assert(Shortcuts_getCount() == 3);
	assert(strcmp(Shortcuts_getPath(0), GB "/a.gb") == 0 && strcmp(Shortcuts_getName(0), "Alpha") == 0);
	assert(strcmp(Shortcuts_getPath(1), "/Tools/T0.pak") == 0 && Shortcuts_getName(1) == NULL);
	assert(strcmp(Shortcuts_getPath(2), "/Tools/T1.pak") == 0 && strcmp(Shortcuts_getName(2), "One") == 0);
	assert(!Shortcuts_getPath(3) && !Shortcuts_getPath(-1));
	assert(count_lines(SHORTCUTS_PATH) == 3);
	assert(Shortcuts_exists(GB "/a.gb") && !Shortcuts_exists(GB "/b.gb"));

	// tools: 9 while a game is pinned
	char rel[64];
	for (int i = 2; i < 9; i++) {
		assert(can_add("/Tools/T9.pak", ENTRY_PAK));
		snprintf(rel, sizeof(rel), "/Tools/T%d.pak", i);
		pin(rel, ENTRY_PAK, true);
	}
	assert(Shortcuts_getCount() == 10);
	assert(!can_add("/Tools/T9.pak", ENTRY_PAK) && can_add(GB "/b.gb", ENTRY_ROM));
	pin("/Tools/T9.pak", ENTRY_PAK, true); // refused, not an eviction
	assert(Shortcuts_getCount() == 10 && !Shortcuts_exists("/Tools/T9.pak"));
	// 8 with no game pinned
	pin(GB "/a.gb", ENTRY_ROM, false);
	pin("/Tools/T8.pak", ENTRY_PAK, false);
	assert(!can_add("/Tools/T9.pak", ENTRY_PAK));
	pin("/Tools/T7.pak", ENTRY_PAK, false);
	assert(can_add("/Tools/T9.pak", ENTRY_PAK));
	// 12 pins at most
	for (int i = 0; Shortcuts_getCount() < MAX_SHORTCUTS; i++) {
		snprintf(rel, sizeof(rel), GB "/g%02d.gb", i);
		pin(rel, ENTRY_ROM, true);
	}
	assert(!can_add(GB "/b.gb", ENTRY_ROM));
	pin(GB "/g00.gb", ENTRY_ROM, true); // already pinned: no duplicate
	assert(Shortcuts_getCount() == MAX_SHORTCUTS);

	// a renamed pin keeps its slot
	assert(strcmp(Shortcuts_getPath(1), "/Tools/T1.pak") == 0);
	assert(Shortcuts_replacePath("/Tools/T1.pak", "/Tools/T20.pak", "Twenty"));
	assert(strcmp(Shortcuts_getPath(1), "/Tools/T20.pak") == 0 && strcmp(Shortcuts_getName(1), "Twenty") == 0);
	assert(Shortcuts_replacePath("/Tools/T20.pak", "/Tools/Renamed.pak", NULL));
	assert(strcmp(Shortcuts_getName(1), "Twenty") == 0);
	assert(!Shortcuts_replacePath("/Tools/nope.pak", "/Tools/T22.pak", NULL));

	// validate drops pins whose file is gone (Renamed.pak is not on the card)
	assert(Shortcuts_validate() == 1 && Shortcuts_getCount() == MAX_SHORTCUTS - 1);
	assert(!Shortcuts_exists("/Tools/Renamed.pak") && Shortcuts_validate() == 0);
	// the saved file reloads to the same pins
	int count = Shortcuts_getCount();
	Shortcuts_quit();
	Shortcuts_init();
	assert(Shortcuts_getCount() == count);
	Shortcuts_quit();

	assert(strcmp(Shortcuts_getPakBasename("/Tools/tg5040/Files.pak"), "Files") == 0);
	assert(strcmp(Shortcuts_getPakBasename("Plain"), "Plain") == 0);
	assert(Shortcuts_isInToolsFolder(SDCARD_PATH "/Tools/x.pak") && !Shortcuts_isInToolsFolder(SDCARD_PATH GB));
}

int main(void) {
	setup_card();
	test_recents_add();
	test_recents_load();
	test_shortcuts();
	sh("rm -rf " SDCARD_PATH);
	printf("test_recents_shortcuts: OK\n");
	return 0;
}
