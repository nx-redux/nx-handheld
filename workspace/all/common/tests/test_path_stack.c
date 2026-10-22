// Host test for nextui/path_stack.c: launcher.c's direct-child check and pathToStack's segment walk.
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../../nextui/path_stack.h"

#define PLAT "tg5040"

static void test_direct_child(void) {
	assert(PathStack_isDirectChild("/sd/Roms", "/sd/Roms/GBA", PLAT));
	assert(PathStack_isDirectChild("/sd/Roms", "/sd/Roms/GBA/", PLAT)); // a trailing slash behaves
	assert(!PathStack_isDirectChild("/sd/Roms", "/sd/Roms/GBA/Sub", PLAT));
	assert(!PathStack_isDirectChild("/sd/Roms", "/sd/Roms", PLAT));
	assert(!PathStack_isDirectChild("/sd/Rom", "/sd/Roms/GBA", PLAT)); // "/a" vs "/ab"
	assert(!PathStack_isDirectChild("/sd/Roms", "/sd/RomsX/GBA", PLAT));
	// PLATFORM and Roms segments aren't levels
	assert(PathStack_isDirectChild("/sd/Roms", "/sd/Roms/GBA/" PLAT, PLAT));
	assert(PathStack_isDirectChild("/sd", "/sd/Roms/GBA", PLAT));
	assert(!PathStack_isDirectChild("/sd/Roms", "/sd/Roms/" PLAT, PLAT));
	assert(!PathStack_isDirectChild("/sd/Roms", "/sd/Roms/" PLAT "x/a", PLAT)); // only the exact name
	// an empty segment stops the count
	assert(!PathStack_isDirectChild("/sd/Roms", "/sd/Roms//GBA", PLAT));
}

typedef struct {
	int n;
	char paths[8][256];
	bool merge[8];
} Seen;

static void record(void* ctx, const char* dir_path, bool merge) {
	Seen* seen = ctx;
	assert(seen->n < 8);
	snprintf(seen->paths[seen->n], sizeof(seen->paths[0]), "%s", dir_path);
	seen->merge[seen->n] = merge;
	seen->n++;
}

static void count_longest(void* ctx, const char* dir_path, bool merge) {
	(void)merge;
	size_t* longest = ctx;
	*longest = strlen(dir_path);
}

static void test_walk(void) {
	Seen s = {0};
	assert(PathStack_walk("/sd/Roms", "/sd/Roms/GBA/Sub", PLAT, record, &s) == 2);
	assert(strcmp(s.paths[0], "/sd/Roms/GBA") == 0 && !s.merge[0]);
	assert(strcmp(s.paths[1], "/sd/Roms/GBA/Sub") == 0 && !s.merge[1]);

	// a PLATFORM segment merges into the directory before it
	memset(&s, 0, sizeof(s));
	assert(PathStack_walk("/sd/Roms", "/sd/Roms/GBA/" PLAT "/x", PLAT, record, &s) == 3);
	assert(strcmp(s.paths[1], "/sd/Roms/GBA/" PLAT) == 0 && s.merge[1]);
	assert(strcmp(s.paths[2], "/sd/Roms/GBA/" PLAT "/x") == 0 && !s.merge[2]);

	// the base itself: nothing to walk
	memset(&s, 0, sizeof(s));
	assert(PathStack_walk("/sd/Roms", "/sd/Roms", PLAT, record, &s) == 0 && s.n == 0);
	// a base with its trailing slash gains no second one
	assert(PathStack_walk("/sd/", "/sd/Tools", PLAT, record, &s) == 1 && strcmp(s.paths[0], "/sd/Tools") == 0);
	// an empty segment ends the walk; a trailing slash is not one more level
	memset(&s, 0, sizeof(s));
	assert(PathStack_walk("/sd", "/sd/a//b", PLAT, record, &s) == 1);
	memset(&s, 0, sizeof(s));
	assert(PathStack_walk("/sd", "/sd/a/", PLAT, record, &s) == 1);
	// an over-long path stops where the directory no longer fits in PATH_MAX
	static char longpath[3 * 4096];
	strcpy(longpath, "/sd");
	for (int i = 0; i < 2000; i++)
		strcat(longpath, "/abcd");
	size_t longest = 0;
	int walked = PathStack_walk("/sd", longpath, PLAT, count_longest, &longest);
	assert(walked > 0 && walked < 2000 && longest < PATH_MAX && longest + 5 >= PATH_MAX);
}

int main(void) {
	test_direct_child();
	test_walk();
	printf("test_path_stack: OK\n");
	return 0;
}
