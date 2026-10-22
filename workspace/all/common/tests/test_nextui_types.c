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

int main(void) {
	push_grows_and_reports();
	unshift_keeps_order();
	entry_push_skips_null();
	printf("test_nextui_types: ok\n");
	return 0;
}
