// Host test for the native PICO-8 runtime lookup (utils.c): a Bios folder holding both pico8_64 and pico8.dat,
// Bios/P8 before Bios/PICO, and the resume tag that keeps native resume markers apart from fake-08's.
#include "../utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void mkfile(const char* dir, const char* name) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE* f = fopen(path, "w");
	assert(f);
	fputs("x", f);
	fclose(f);
}

static void sh(const char* fmt, const char* arg) {
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), fmt, arg);
	assert(system(cmd) == 0);
}

static int endsWith(const char* s, const char* tail) {
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && strcmp(s + a - b, tail) == 0;
}

int main(void) {
	char root[256], p8[512], pico[512], dir[512], tag[64];
	snprintf(root, sizeof(root), "/tmp/nx_test_pico8_runtime_%d", (int)getpid());
	snprintf(p8, sizeof(p8), "%s/Bios/P8", root);
	snprintf(pico, sizeof(pico), "%s/Bios/PICO", root);
	sh("mkdir -p '%s'", p8);
	sh("mkdir -p '%s'", pico);
	PICO8_setRuntimeRoot(root);

	// 1. empty Bios/P8: no runtime, P8 keeps its tag
	assert(!PICO8_findRuntime(dir, sizeof(dir)));
	ROM_resumeTag("P8", tag, sizeof(tag));
	assert(strcmp(tag, "P8") == 0);

	// 2. only pico8_64 in Bios/P8: still no runtime
	PICO8_resetRuntimeCache();
	mkfile(p8, "pico8_64");
	assert(!PICO8_findRuntime(dir, sizeof(dir)));

	// 3. both files in Bios/PICO: found there, P8 resumes get their own tag, other tags pass through
	PICO8_resetRuntimeCache();
	mkfile(pico, "pico8_64");
	mkfile(pico, "pico8.dat");
	assert(PICO8_findRuntime(dir, sizeof(dir)));
	assert(endsWith(dir, "/Bios/PICO"));
	ROM_resumeTag("P8", tag, sizeof(tag));
	assert(strcmp(tag, "P8-native") == 0);
	ROM_resumeTag("GBA", tag, sizeof(tag));
	assert(strcmp(tag, "GBA") == 0);

	// 4. both files in both folders: Bios/P8 wins
	PICO8_resetRuntimeCache();
	mkfile(p8, "pico8.dat");
	assert(PICO8_findRuntime(dir, sizeof(dir)));
	assert(endsWith(dir, "/Bios/P8"));

	// 5. the result is cached: removing the files does not change it until a reset
	sh("rm -rf '%s/Bios'", root);
	assert(PICO8_findRuntime(dir, sizeof(dir)));
	assert(endsWith(dir, "/Bios/P8"));
	PICO8_resetRuntimeCache();
	assert(!PICO8_findRuntime(dir, sizeof(dir)));

	sh("rm -rf '%s'", root);
	puts("test_pico8_runtime: all passed");
	return 0;
}
