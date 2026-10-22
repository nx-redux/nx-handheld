// Host test for ma_alias.c: getAlias reading the map.txt beside a rom.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "defines.h"
#include "ma_alias.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static char root[64];

static void write_map(const char* text) {
	char path[128];
	snprintf(path, sizeof(path), "%s/map.txt", root);
	FILE* f = fopen(path, "w");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

// getAlias for <root>/<file>, alias pre-filled with "unchanged"
static int alias_of(const char* file, char* alias) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s", root, file);
	strcpy(alias, "unchanged");
	return getAlias(path, alias);
}

int main(void) {
	char alias[MAX_PATH];

	strcpy(root, "/tmp/nx_test_alias.XXXXXX");
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}

	CHECK(!alias_of("1941.zip", alias) && !strcmp(alias, "unchanged"), "no map.txt: no alias");

	write_map("1941.zip\t1941: Counter Attack\n\nmslug.zip\tMetal Slug\nnotab line\n");
	CHECK(alias_of("1941.zip", alias) && !strcmp(alias, "1941: Counter Attack"), "tab-separated hit");
	CHECK(alias_of("mslug.zip", alias) && !strcmp(alias, "Metal Slug"), "hit after an empty line");
	CHECK(!alias_of("1942.zip", alias) && !strcmp(alias, "unchanged"), "miss leaves alias as is");
	CHECK(!alias_of("1941", alias), "key must match the whole file name");
	CHECK(!alias_of("notab line", alias), "a line without a tab never matches");

	write_map("a.zip\tAlpha\r\nb.zip\tBeta\r\n");
	CHECK(alias_of("a.zip", alias) && !strcmp(alias, "Alpha"), "CRLF map: no trailing \\r");
	CHECK(alias_of("b.zip", alias) && !strcmp(alias, "Beta"), "CRLF map: last line");

	write_map("c.zip\tGamma");
	CHECK(alias_of("c.zip", alias) && !strcmp(alias, "Gamma"), "last line without a newline");

	write_map("d.zip\tFirst\nd.zip\tSecond\n");
	CHECK(alias_of("d.zip", alias) && !strcmp(alias, "First"), "first match wins");

	write_map("e.zip\t\n");
	CHECK(alias_of("e.zip", alias) && !strcmp(alias, ""), "empty alias");

	// a path with no slash: no folder to look in, never an alias
	strcpy(alias, "unchanged");
	CHECK(!getAlias("1941.zip", alias) && !strcmp(alias, "unchanged"), "path without a slash: no alias");

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	if (system(cmd) != 0)
		printf("warning: could not remove %s\n", root);

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
