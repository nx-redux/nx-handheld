// Host test for dir_list.c: the browser listing shared by the Image Viewer and
// the Media Player.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "../dir_list.h"

#define ROOT "/tmp/nx_fx_dir_list"

static bool keep_txt(const char* name) {
	const char* dot = strrchr(name, '.');
	return dot && dot != name && strcasecmp(dot, ".txt") == 0;
}

static void write_file(const char* path, const char* data) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(data, f);
	fclose(f);
}

int main(void) {
	assert(system("rm -rf " ROOT " && mkdir -p " ROOT "/sub/beta " ROOT "/sub/Alpha.txt " ROOT "/.hid") == 0);
	write_file(ROOT "/sub/b.TXT", "hello");
	write_file(ROOT "/sub/a.txt", "");
	write_file(ROOT "/sub/c.bin", "x");	 // filtered out
	write_file(ROOT "/sub/.d.txt", "x"); // hidden

	int count = -1;
	DirListEntry* e = DirList_load(ROOT "/sub", ROOT, keep_txt, &count);
	assert(e && count == 5);
	// ".." first, pointing at the parent.
	assert(strcmp(e[0].name, "..") == 0 && e[0].is_dir && strcmp(e[0].path, ROOT) == 0);
	// Directories (even with a matching extension) before files, case-insensitive.
	assert(strcmp(e[1].name, "Alpha.txt") == 0 && e[1].is_dir && e[1].size_bytes == 0);
	assert(strcmp(e[2].name, "beta") == 0 && e[2].is_dir);
	assert(strcmp(e[3].name, "a.txt") == 0 && !e[3].is_dir && e[3].size_bytes == 0);
	assert(strcmp(e[4].name, "b.TXT") == 0 && e[4].size_bytes == 5);
	assert(strcmp(e[4].path, ROOT "/sub/b.TXT") == 0);
	free(e);

	// At the root: no "..", hidden directory skipped.
	e = DirList_load(ROOT, ROOT, keep_txt, &count);
	assert(e && count == 1 && strcmp(e[0].name, "sub") == 0);
	free(e);

	// Empty directory: still a valid (one-element) allocation, zero rows.
	assert(system("mkdir -p " ROOT "/empty") == 0);
	e = DirList_load(ROOT "/empty", ROOT "/empty", keep_txt, &count);
	assert(e && count == 0);
	free(e);

	// A relative path with no '/': ".." points at root.
	assert(chdir(ROOT) == 0);
	e = DirList_load("sub", ROOT, keep_txt, &count);
	assert(e && count == 5 && strcmp(e[0].path, ROOT) == 0);
	free(e);

	// Missing directory -> NULL, count 0.
	count = 7;
	assert(DirList_load(ROOT "/missing", ROOT, keep_txt, &count) == NULL && count == 0);

	printf("test_dir_list: OK\n");
	return 0;
}
