// Host test for ma_file_list.c: list_files_in_folder on a temp folder.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ma_file_list.h"

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

static void touch(const char* name) {
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", root, name);
	FILE* f = fopen(path, "w");
	if (f)
		fclose(f);
}

// "a,b,c" for the list (NULL-terminated), checking count matches it
static int list_is(char** list, int count, const char* want) {
	char got[512] = "";
	int n = 0;
	for (; list && list[n]; n++) {
		if (n)
			strcat(got, ",");
		strcat(got, list[n]);
	}
	if (n != count || strcmp(got, want)) {
		printf("  got [%s] (%d entries, count %d), want [%s]\n", got, n, count, want);
		return 0;
	}
	return 1;
}

int main(void) {
	int count = -1;
	char** list;

	strcpy(root, "/tmp/nx_test_file_list.XXXXXX");
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}

	// empty folder: a valid empty list, or just the default
	list = list_files_in_folder(root, &count, NULL, NULL);
	CHECK(count == 0 && (!list || !list[0]), "empty folder: no entries");
	free_file_list(list);
	list = list_files_in_folder(root, &count, "None", NULL);
	CHECK(list_is(list, count, "None"), "empty folder: just the default");
	free_file_list(list);

	touch("b.png");
	touch("A.png");
	touch("c.cfg");
	touch("._b.png");
	touch(".DS_Store");
	touch(".hidden.png");
	touch("noext");
	{
		char sub[256];
		snprintf(sub, sizeof(sub), "%s/dir.png", root);
		mkdir(sub, 0755);
	}

	list = list_files_in_folder(root, &count, NULL, NULL);
	CHECK(list_is(list, count, ".hidden.png,A.png,b.png,c.cfg,noext"),
		  "skips ._* and .DS_Store and folders, sorted");
	free_file_list(list);

	list = list_files_in_folder(root, &count, NULL, ".png");
	CHECK(list_is(list, count, ".hidden.png,A.png,b.png"), "extension filter");
	free_file_list(list);

	list = list_files_in_folder(root, &count, NULL, ".cfg");
	CHECK(list_is(list, count, "c.cfg"), "extension filter, one match");
	free_file_list(list);

	list = list_files_in_folder(root, &count, NULL, ".zip");
	CHECK(count == 0 && (!list || !list[0]), "extension filter, no match");
	free_file_list(list);

	// the default stays first even when a file sorts ahead of it ("A" < "None")
	list = list_files_in_folder(root, &count, "None", ".png");
	CHECK(list_is(list, count, "None,.hidden.png,A.png,b.png"), "default first, files sorted after it");
	free_file_list(list);

	// a missing folder: NULL and a count of 0
	count = 42;
	list = list_files_in_folder("/tmp/nx_test_file_list_missing/none", &count, "None", NULL);
	CHECK(!list && count == 0, "missing folder: NULL, count 0");
	free_file_list(NULL); // safe

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
	if (system(cmd) != 0)
		printf("warning: could not remove %s\n", root);

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
