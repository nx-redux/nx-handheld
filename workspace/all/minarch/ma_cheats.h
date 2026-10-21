#pragma once

#include <stdlib.h>
#include <stdbool.h>

#include "defines.h"
#include "ma_cheat_mem.h"

struct Cheat {
	const char* name;
	const char* info;
	int enabled;
	const char* code;
	struct CheatMemPatch mem; // a RAM patch instead of a code (mem.memory)
};

struct Cheats {
	size_t count;
	struct Cheat* cheats;
};

#define CHEAT_MAX_DESC_LEN 27
#define CHEAT_MAX_LINE_LEN 52
#define CHEAT_MAX_LINES 3

#define CHEAT_MAX_PATHS 16
#define CHEAT_MAX_DISPLAY_PATHS 8
// the list of displayed paths will be a bit shorter, we cant render that much text
#define CHEAT_MAX_LIST_LENGTH (CHEAT_MAX_DISPLAY_PATHS * MAX_PATH)

extern struct Cheats cheatcodes;

void Cheat_getPaths(char paths[CHEAT_MAX_PATHS][MAX_PATH], int* count);
void Cheats_free();
bool Cheats_load();

struct retro_memory_map;
// The core's memory map (RETRO_ENVIRONMENT_SET_MEMORY_MAPS), NULL on unload.
void Cheats_setMemoryMap(const struct retro_memory_map* mmap);
// Apply the enabled RAM-patch cheats; once per frame, before core.run().
void Cheats_applyMemory(void);
