// Host test for nextui/types.c's growable Array and its Entry helpers: a push that can't grow the array or an
// Entry that couldn't be allocated must not leave a NULL row behind (rows are dereferenced unchecked).
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../nextui/types.h"

static void push_grows_and_reports(void) {
	Array* a = Array_new();
	assert(a);
	static int vals[100];
	for (int i = 0; i < 100; i++)
		assert(Array_push(a, &vals[i]));
	assert(a->count == 100 && a->items[0] == &vals[0] && a->items[99] == &vals[99]);
	Array_free(a);
}

static void unshift_keeps_order(void) {
	Array* a = Array_new();
	static int vals[20];
	for (int i = 0; i < 20; i++)
		assert(Array_unshift(a, &vals[i]));
	assert(a->count == 20 && a->items[0] == &vals[19] && a->items[19] == &vals[0]);
	Array_free(a);
}

static void entry_push_skips_null(void) {
	Array* a = Array_new();
	assert(!EntryArray_push(a, NULL));
	assert(a->count == 0);
	assert(EntryArray_push(a, Entry_newNamed("/mnt/SDCARD/Roms/GB/Tetris.gb", ENTRY_ROM, "Tetris")));
	assert(a->count == 1 && strcmp(((Entry*)a->items[0])->name, "Tetris") == 0);
	EntryArray_free(a);
}

static void remove_and_pop_edges(void) {
	Array* a = Array_new();
	static int vals[4];
	assert(Array_pop(a) == NULL); // empty
	Array_remove(a, &vals[0]);	  // empty: nothing to do
	for (int i = 0; i < 4; i++)
		assert(Array_push(a, &vals[i]));
	Array_remove(a, NULL);
	Array_remove(a, &a); // not a member
	assert(a->count == 4);
	Array_remove(a, &vals[0]); // first
	assert(a->count == 3 && a->items[0] == &vals[1]);
	Array_remove(a, &vals[3]); // last
	assert(a->count == 2 && a->items[1] == &vals[2]);
	assert(Array_pop(a) == &vals[2] && Array_pop(a) == &vals[1] && Array_pop(a) == NULL);
	assert(Array_unshift(a, &vals[0]) && a->count == 1 && a->items[0] == &vals[0]); // unshift on empty
	Array_free(a);
}

static void hash_rules(void) {
	Hash* h = Hash_new();
	assert(Hash_get(h, "a.gba") == NULL);
	Hash_set(h, "a.gba", "Alpha");
	Hash_set(h, "A.gba", "Upper"); // keys are case-sensitive
	assert(strcmp(Hash_get(h, "a.gba"), "Alpha") == 0 && strcmp(Hash_get(h, "A.gba"), "Upper") == 0);
	Hash_set(h, "a.gba", "Again"); // overwrite in place
	assert(strcmp(Hash_get(h, "a.gba"), "Again") == 0);
	char key[16];
	for (int i = 0; i < 1000; i++) { // many keys share buckets
		snprintf(key, sizeof(key), "k%d", i);
		Hash_set(h, key, key);
	}
	assert(strcmp(Hash_get(h, "k999"), "k999") == 0 && strcmp(Hash_get(h, "a.gba"), "Again") == 0);
	Hash_free(h);
	Hash_free(NULL);
}

// EntryArray_sort is a total order: case-insensitive by name, then case-sensitive, then by path, so same-name rows
// stay adjacent and keep one order however qsort meets them
static void sort_total_order(void) {
	const char* rows[][2] = {{"/b/Tetris.gb", "Tetris"}, {"/a/tetris.gb", "tetris"}, {"/c/TETRIS.gb", "TETRIS"}, {"/a/Tetris.gb", "Tetris"}, {"/z/Alpha.gb", "alpha"}, {"/y/Zelda.gb", "Zelda"}};
	const char* want[] = {"/z/Alpha.gb", "/c/TETRIS.gb", "/a/Tetris.gb", "/b/Tetris.gb", "/a/tetris.gb", "/y/Zelda.gb"};
	for (int pass = 0; pass < 2; pass++) {
		Array* a = Array_new();
		for (int i = 0; i < 6; i++) {
			int k = pass ? 5 - i : i; // the input order doesn't matter
			EntryArray_push(a, Entry_newNamed(rows[k][0], ENTRY_ROM, rows[k][1]));
		}
		EntryArray_sort(a);
		for (int i = 0; i < 6; i++)
			assert(strcmp(((Entry*)a->items[i])->path, want[i]) == 0);
		EntryArray_free(a);
	}
}

int main(void) {
	push_grows_and_reports();
	remove_and_pop_edges();
	hash_rules();
	sort_total_order();
	unshift_keeps_order();
	entry_push_skips_null();
	printf("test_nextui_types: ok\n");
	return 0;
}
