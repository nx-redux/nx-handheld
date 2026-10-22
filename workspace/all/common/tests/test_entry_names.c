// Host test for nextui/entry_names.c: Directory_index's letter-jump groups, file stems, map.txt parsing and
// same-name run labels, and the Game Switcher's duplicate labels. Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defines.h"
#include "../../nextui/entry_names.h"

#define MAP "/tmp/nx_test_entry_names_map.txt"

static void test_index_char(void) {
	assert(EntryNames_indexChar("apple") == 1 && EntryNames_indexChar("Zed") == 26);
	assert(EntryNames_indexChar("1942") == 0 && EntryNames_indexChar("_x") == 0 && EntryNames_indexChar("") == 0);
	assert(EntryNames_indexChar("{x") == 27 && EntryNames_indexChar("~x") == 27);
	assert(EntryNames_indexChar("\xC3\x89tude") == 27); // a UTF-8 lead byte sorts after 'z'

	// groups never go backwards over a sorted list, so each group is one run
	const char* names[] = {"Zelda", "apple", "1942", "\xC3\x89t\xC3\xA9", "banana", "_test", "~tilde", "Apple 2", "[b]"};
	Array* entries = Array_new();
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		EntryArray_push(entries, Entry_newNamed("/x/f", ENTRY_ROM, names[i]));
	EntryArray_sort(entries);
	for (int i = 1; i < entries->count; i++)
		assert(EntryNames_indexChar(((Entry*)entries->items[i - 1])->name) <=
			   EntryNames_indexChar(((Entry*)entries->items[i])->name));
	EntryArray_free(entries);
}

static void test_alpha_step(void) {
	IntArray alphas;
	IntArray_init(&alphas);
	int alpha = -1, index = 0;
	assert(EntryNames_alphaStep(&alphas, &alpha, &index, 0, "apple") == 0);
	assert(EntryNames_alphaStep(&alphas, &alpha, &index, 1, "avocado") == 0);
	assert(EntryNames_alphaStep(&alphas, &alpha, &index, 2, "banana") == 1);
	assert(alphas.count == 2 && alphas.items[0] == 0 && alphas.items[1] == 2);

	// an unsorted list starts more groups than alphas holds: the overflow joins the last group
	IntArray_init(&alphas);
	alpha = -1, index = 0;
	for (int i = 0; i < 40; i++) {
		int g = EntryNames_alphaStep(&alphas, &alpha, &index, i, i % 2 ? "b" : "a");
		assert(g == (i < INT_ARRAY_MAX ? i : INT_ARRAY_MAX - 1));
	}
	assert(alphas.count == INT_ARRAY_MAX && alphas.items[INT_ARRAY_MAX - 1] == INT_ARRAY_MAX - 1);
}

static void test_file_stem(void) {
	char out[MAX_PATH];
	EntryNames_fileStem("/x/Celeste.p8.png", out);
	assert(strcmp(out, "Celeste") == 0);
	EntryNames_fileStem("/x/.png", out); // only extensions: the basename comes back
	assert(strcmp(out, ".png") == 0);
	EntryNames_fileStem("/x/a.b.c", out); // a one-letter "extension" stays
	assert(strcmp(out, "a.b.c") == 0);
	EntryNames_fileStem("Game.tar.gzipped", out);
	assert(strcmp(out, "Game.tar.gzipped") == 0);
}

static void test_read_map(void) {
	assert(EntryNames_readMap("/tmp/nx_test_entry_names_missing.txt") == NULL);
	FILE* f = fopen(MAP, "w");
	assert(f);
	fputs("a.gba\tAlpha\r\nb.gba\tBeta\textra column\n\nc.gba\t   \nnotab\nd.gba\tDelta  \t\ne.gba\t\n", f);
	fclose(f);
	Hash* map = EntryNames_readMap(MAP);
	assert(map);
	assert(strcmp(Hash_get(map, "a.gba"), "Alpha") == 0); // CRLF
	assert(strcmp(Hash_get(map, "b.gba"), "Beta") == 0);  // a third column dropped
	assert(Hash_get(map, "c.gba") == NULL);				  // blank name
	assert(Hash_get(map, "notab") == NULL);
	assert(strcmp(Hash_get(map, "d.gba"), "Delta") == 0); // trailing blanks
	assert(Hash_get(map, "e.gba") == NULL);				  // empty name
	Hash_free(map);
	unlink(MAP);
}

static Array* run_of(const char* const* paths, int n, const char* name) {
	Array* entries = Array_new();
	for (int i = 0; i < n; i++)
		EntryArray_push(entries, Entry_newNamed(paths[i], ENTRY_ROM, name));
	return entries;
}

static void check_run(const char* const* paths, int n, const char* const* want) {
	Array* entries = run_of(paths, n, "Tetris");
	EntryNamesRun run = EntryNames_runInfo(entries, 0, n);
	EntryNames_labelRun(entries, 0, n, run);
	for (int i = 0; i < n; i++)
		assert(strcmp(((Entry*)entries->items[i])->unique, want[i]) == 0);
	EntryArray_free(entries);
}

static void test_run_labels(void) {
	// the same file from two cores: the name and its core's tag
	const char* same[] = {ROMS_PATH "/Game Boy (GB)/Tetris.gb", ROMS_PATH "/Game Boy (SGB)/Tetris.gb"};
	const char* same_want[] = {"Tetris (GB)", "Tetris (SGB)"};
	check_run(same, 2, same_want);
	// different files from different cores: the stem and the tag
	const char* diff[] = {ROMS_PATH "/Game Boy (GB)/Tetris.gb", ROMS_PATH "/Game Boy (SGB)/Tetris (Rev 1).gb"};
	const char* diff_want[] = {"Tetris (GB)", "Tetris (Rev 1) (SGB)"};
	check_run(diff, 2, diff_want);
	// different files from one folder: the stems alone
	const char* one[] = {ROMS_PATH "/Game Boy (GB)/Tetris.gb", ROMS_PATH "/Game Boy (GB)/Tetris (1).gb"};
	const char* one_want[] = {"Tetris", "Tetris (1)"};
	check_run(one, 2, one_want);
	// colliding stems: the whole run keeps file names
	const char* clash[] = {ROMS_PATH "/Game Boy (GB)/Tetris.gb", ROMS_PATH "/Game Boy Color (GBC)/Tetris.gbc",
						   ROMS_PATH "/Game Boy (GB)/Tetris (Alt).gb"};
	const char* clash_want[] = {"Tetris.gb (GB)", "Tetris.gbc (GBC)", "Tetris (Alt).gb (GB)"};
	check_run(clash, 3, clash_want);

	Array* entries = run_of(clash, 3, "Tetris");
	EntryNamesRun run = EntryNames_runInfo(entries, 0, 3);
	assert(!run.same_file && run.tags_differ && !run.stems_unique);
	EntryArray_free(entries);
}

static void test_switcher_labels(void) {
	Array* entries = Array_new();
	EntryArray_push(entries, Entry_newNamed(ROMS_PATH "/Game Boy (GB)/Tetris.gb", ENTRY_ROM, "Tetris"));
	EntryArray_push(entries, Entry_newNamed(ROMS_PATH "/Nintendo (FC)/Tetris.nes", ENTRY_ROM, "Tetris"));
	EntryArray_push(entries, Entry_newNamed(ROMS_PATH "/Game Boy (GB)/Mario.gb", ENTRY_ROM, "Mario"));
	EntryArray_push(entries, Entry_newNamed(ROMS_PATH "/Doom (DOOM)/doom1.wad", ENTRY_ROM, "Doom"));
	EntryArray_push(entries, Entry_newNamed(ROMS_PATH "/Doom (DOOM)/doom2.wad", ENTRY_ROM, "Doom"));
	EntryNames_labelSwitcher(entries);
	const char* want[] = {"Tetris (GB)", "Tetris (FC)", NULL, "Doom (doom1.wad)", "Doom (doom2.wad)"};
	for (int i = 0; i < 5; i++) {
		const char* got = ((Entry*)entries->items[i])->unique;
		assert(want[i] ? got && strcmp(got, want[i]) == 0 : got == NULL);
	}
	EntryArray_free(entries);
}

int main(void) {
	test_index_char();
	test_alpha_step();
	test_file_stem();
	test_read_map();
	test_run_labels();
	test_switcher_labels();
	printf("test_entry_names: OK\n");
	return 0;
}
