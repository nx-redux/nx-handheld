// Host-compiled unit test for gametimedb.c's path helpers: the ROM path
// stored relative to Roms/ (__ensure_rel_path) and the PortMaster exclusion.
// On the host /mnt/SDCARD/Roms does not exist, so realpath fails and the
// string fallbacks are what runs (the same ones the device uses for paths
// that don't resolve).
// Build & run (from workspace/all/libgametimedb):
//   cc -std=gnu99 -Wall -Werror -Wno-deprecated-declarations -fsanitize=address -g -I. -I../common \
//      -I../../tg5040/platform -DPLATFORM='"tg5040"' gametimedb.c ../common/utils.c tests/test_gametimedb_paths.c \
//      -lsqlite3 -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <defines.h>

#include "../gametimedb.h"

void __ensure_rel_path(char* rel_path, const char* rom_path);

static const char* rel(const char* rom_path) {
	static char out[MAX_PATH + 16];
	memset(out, 'X', sizeof(out));
	__ensure_rel_path(out, rom_path);
	assert(out[MAX_PATH] == 'X'); // never past MAX_PATH
	return out;
}

static void test_rel_path(void) {
	assert(strcmp(rel("/mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb"), "Game Boy (GB)/Tetris.gb") == 0);
	assert(strcmp(rel("../../Roms/Game Boy (GB)/Tetris.gb"), "Game Boy (GB)/Tetris.gb") == 0);
	assert(strcmp(rel("/mnt/SDCARD/.system/../../Roms/X/y.gb"), "X/y.gb") == 0);
	// outside Roms: unchanged
	assert(strcmp(rel("/mnt/SDCARD/Other/z.gb"), "/mnt/SDCARD/Other/z.gb") == 0);
	assert(strcmp(rel("/mnt/SDCARD/Roms"), "/mnt/SDCARD/Roms") == 0);

	// A path longer than MAX_PATH (gametimectl takes it from argv) is cut,
	// not copied past the buffer
	char long_path[MAX_PATH * 2];
	int n = snprintf(long_path, sizeof(long_path), "/mnt/SDCARD/Roms/X/");
	memset(long_path + n, 'a', sizeof(long_path) - n - 4);
	strcpy(long_path + sizeof(long_path) - 4, ".gb");
	assert(strlen(rel(long_path)) == MAX_PATH - 1);
	char long_rel[MAX_PATH * 2];
	snprintf(long_rel, sizeof(long_rel), "../../Roms/%s", long_path + n);
	assert(strlen(rel(long_rel)) == MAX_PATH - 1);
}

static void test_excluded(void) {
	assert(play_activity_is_excluded("/mnt/SDCARD/Roms/Ports (PORTS)/PortMaster.sh"));
	assert(play_activity_is_excluded("/mnt/SDCARD/Roms/Ports (PORTS)/portmaster.SH"));
	assert(play_activity_is_excluded("/mnt/SDCARD/Tools/tg5040/PortMaster.pak/launch.sh"));
	assert(play_activity_is_excluded("/x/PORTMASTER.PAK"));
	assert(!play_activity_is_excluded("/mnt/SDCARD/Roms/Ports (PORTS)/Doom.sh"));
	assert(!play_activity_is_excluded("/mnt/SDCARD/Roms/Ports (PORTS)/PortMaster.sh.bak"));
	assert(!play_activity_is_excluded("portmaster"));
	assert(!play_activity_is_excluded(""));
	assert(!play_activity_is_excluded(NULL));

	// Longer than MAX_PATH: the suffix is still seen
	char long_path[MAX_PATH * 2];
	memset(long_path, 'a', sizeof(long_path));
	strcpy(long_path + sizeof(long_path) - 15, "/PortMaster.sh");
	assert(play_activity_is_excluded(long_path));
	// ... and a substring inside the first MAX_PATH - 1 bytes
	memset(long_path, 'a', sizeof(long_path));
	memcpy(long_path, "/PortMaster.pak/", 16);
	long_path[sizeof(long_path) - 1] = '\0';
	assert(play_activity_is_excluded(long_path));
}

int main(void) {
	test_rel_path();
	test_excluded();
	printf("test_gametimedb_paths: all tests passed\n");
	return 0;
}
