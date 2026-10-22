// Host test for proc_util.h (pidof over a fake /proc).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../proc_util.h"

#define PROC "/tmp/nx_fx_proc_util"

static void write_file(const char* path, const char* body) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(body, f);
	fclose(f);
}

int main(void) {
	assert(system("rm -rf " PROC " && mkdir -p " PROC "/1 " PROC "/42 " PROC "/self " PROC "/77") == 0);
	write_file(PROC "/1/comm", "init\n");
	write_file(PROC "/42/comm", "nextui.elf\n");
	write_file(PROC "/self/comm", "osdmusic.elf\n"); // not a pid directory
	// 77 has no comm (the process exited mid-scan)

	assert(proc_running_in(PROC, "nextui.elf"));
	assert(proc_running_in(PROC, "init"));
	assert(!proc_running_in(PROC, "nextui")); // exact names only
	assert(!proc_running_in(PROC, "osdmusic.elf"));
	assert(!proc_running_in(PROC "/missing", "init"));

	assert(system("rm -rf " PROC) == 0);
	printf("test_proc_util: OK\n");
	return 0;
}
