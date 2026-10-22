// Host test for nextui/entry_cache.c: the rom index / Consoles cache reader and its in-place rewrite (Delete Rom's
// row drop and count adjust). Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../nextui/entry_cache.h"

#define CACHE "/tmp/nx_test_entry_cache.txt"

static void put(const char* text) {
	FILE* f = fopen(CACHE, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

static char* slurp(void) {
	static char buf[4096];
	FILE* f = fopen(CACHE, "r");
	assert(f);
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

typedef struct {
	int n;
	char rows[4][64];
} Seen;

static void record(char* path, char* name, void* ctx) {
	Seen* seen = ctx;
	assert(seen->n < 4);
	snprintf(seen->rows[seen->n++], sizeof(seen->rows[0]), "%s|%s", path, name);
}

static void test_for_each_line(void) {
	put("#fp=00000000000000ab\nA\ta\r\n\nB\tb\tc\n");
	Seen seen = {0};
	uint64_t fp = 1;
	assert(EntryCache_forEachLine(CACHE, NULL, &fp, record, &seen) == 2 && fp == 0xab);
	assert(strcmp(seen.rows[0], "A|a") == 0 && strcmp(seen.rows[1], "B|b\tc") == 0); // CRLF, blank, 2nd tab
	uint64_t want = 0xab;
	memset(&seen, 0, sizeof(seen));
	assert(EntryCache_forEachLine(CACHE, &want, NULL, record, &seen) == 2);
	want = 0xac;
	assert(EntryCache_forEachLine(CACHE, &want, NULL, record, &seen) == -1); // stale
	assert(EntryCache_readFp(CACHE, &fp) && fp == 0xab);

	put("A\ta\n"); // no fingerprint: fine unchecked, stale when one is expected
	memset(&seen, 0, sizeof(seen));
	assert(EntryCache_forEachLine(CACHE, NULL, &fp, record, &seen) == 1 && fp == 0);
	assert(EntryCache_forEachLine(CACHE, &want, NULL, record, &seen) == -1);
	assert(!EntryCache_readFp(CACHE, &fp));

	put("#fp=1\nA\ta\nno tab here\n"); // malformed
	memset(&seen, 0, sizeof(seen));
	assert(EntryCache_forEachLine(CACHE, NULL, NULL, record, &seen) == -1);
	unlink(CACHE);
	assert(EntryCache_forEachLine(CACHE, NULL, &fp, record, &seen) == -1 && fp == 0);
}

// found: 999 for the last row seen and no row 500
static void find_500_999(char* path, char* name, void* ctx) {
	(void)path;
	int* found = ctx;
	if (strcmp(name, "Game 0500") == 0)
		*found = -1000000;
	else if (strcmp(name, "Game 0999") == 0)
		*found += 999;
}

static void test_rewrite_drop(void) {
	put("#fp=1\n/R/GB (GB)/a.gb\tA\n/R/GB (GB)/Sub\tS\n/R/GB (GB)/Sub/b.gb\tB\n/R/GB (GB)/Subway.gb\tW\n");
	int dropped = -1;
	// the folder and what is in it go; a sibling that only starts with its name stays
	assert(EntryCache_rewrite(CACHE, 2, "/R/GB (GB)/Sub", &dropped, NULL, 0) == 2 && dropped == 2);
	assert(strcmp(slurp(), "#fp=0000000000000002\n/R/GB (GB)/a.gb\tA\n/R/GB (GB)/Subway.gb\tW\n") == 0);
	// everything dropped: nothing written (the file keeps its rows)
	assert(EntryCache_rewrite(CACHE, 3, "/R", &dropped, NULL, 0) == 0 && dropped == 2);
	assert(strstr(slurp(), "#fp=0000000000000002\n"));
	// a cache bigger than the rewrite's first buffer grows it
	FILE* f = fopen(CACHE, "w");
	assert(f);
	fputs("#fp=1\n", f);
	for (int i = 0; i < 1000; i++)
		fprintf(f, "/R/GB (GB)/game %04d.gb\tGame %04d\n", i, i);
	fclose(f);
	assert(EntryCache_rewrite(CACHE, 7, "/R/GB (GB)/game 0500.gb", &dropped, NULL, 0) == 999 && dropped == 1);
	int found = 0;
	assert(EntryCache_forEachLine(CACHE, NULL, NULL, find_500_999, &found) == 999 && found == 999);
	unlink(CACHE);
	assert(EntryCache_rewrite(CACHE, 3, "/R", &dropped, NULL, 0) == -1 && dropped == 0);
}

static void test_rewrite_counts(void) {
	put("#fp=1\n/R/GB (GB)\tGame Boy\t5\n/R/GBA (GBA)\tGame Boy Advance\t3\n/R/Ports\tPorts\t1\n");
	// the GB row collates the folder a game left: its count drops; the GBA row is another console
	assert(EntryCache_rewrite(CACHE, 4, NULL, NULL, "/R/GB (GB)", 2) == 3);
	assert(strcmp(slurp(), "#fp=0000000000000004\n/R/GB (GB)\tGame Boy\t3\n/R/GBA (GBA)\tGame Boy Advance\t3\n"
						   "/R/Ports\tPorts\t1\n") == 0);
	// never below zero
	assert(EntryCache_rewrite(CACHE, 5, NULL, NULL, "/R/Ports (PORTS)", 9) == 3);
	assert(strstr(slurp(), "/R/Ports\tPorts\t0\n"));
	// a row without its count fails the rewrite and leaves the file as it was
	put("#fp=1\n/R/GB (GB)\tGame Boy\n");
	assert(EntryCache_rewrite(CACHE, 6, NULL, NULL, "/R/GB (GB)", 1) == -1);
	assert(strcmp(slurp(), "#fp=1\n/R/GB (GB)\tGame Boy\n") == 0);
	unlink(CACHE);
}

int main(void) {
	test_for_each_line();
	test_rewrite_drop();
	test_rewrite_counts();
	printf("test_entry_cache: OK\n");
	return 0;
}
