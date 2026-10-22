#pragma once

#include <stddef.h>
#include <stdint.h>

// RetroArch's frontend-side ("handler 1") cheats: no code for the core, just a
// RAM patch the frontend applies every frame. Pure logic, host-tested by
// scripts/tests/test-cheat-mem.sh; ma_cheats.c owns the parsing and memory.

#define CHEAT_MEM_MAX_REGIONS 16

// cheatN_repeat_count is a file value: a huge one would loop for seconds every
// frame. RetroArch's own cheat editor tops out far below this.
#define CHEAT_MEM_MAX_REPEAT 65536

// Most cheats a .cht file may declare (`cheats = N`); the biggest libretro
// database files hold a few thousand. A larger count is treated as corrupt
// instead of being calloc'ed.
#define CHEAT_FILE_MAX_COUNT 16384

// 1 if a file's `cheats = N` count is usable (1..CHEAT_FILE_MAX_COUNT).
int CheatFile_countOk(unsigned long count);

// cheatN_cheat_type values
enum {
	CHEAT_MEM_DISABLED = 0,
	CHEAT_MEM_SET_TO_VALUE,
	CHEAT_MEM_INCREASE_VALUE,
	CHEAT_MEM_DECREASE_VALUE,
	CHEAT_MEM_RUN_NEXT_IF_EQ,
	CHEAT_MEM_RUN_NEXT_IF_NEQ,
	CHEAT_MEM_RUN_NEXT_IF_LT,
	CHEAT_MEM_RUN_NEXT_IF_GT,
};

struct CheatMemPatch {
	int memory;		  // cheatN_handler = 1
	unsigned address; // offset into the concatenated RAM regions
	unsigned value;
	unsigned size;	   // cheatN_memory_search_size: 0-2 = 1/2/4 bits, 3 = 8, 4 = 16, 5 = 32
	unsigned type;	   // CHEAT_MEM_*
	unsigned bit_mask; // cheatN_address_bit_position: bits written for sizes 0-2
	int big_endian;
	unsigned repeat_count; // default 1
	unsigned repeat_add_to_value;
	unsigned repeat_add_to_address; // default 1
};

// The core's system RAM, as RetroArch sees it: the SYSTEM_RAM regions of its
// memory map in order, or else RETRO_MEMORY_SYSTEM_RAM.
typedef struct {
	uint8_t* ptr[CHEAT_MEM_MAX_REGIONS];
	size_t len[CHEAT_MEM_MAX_REGIONS];
	int count;
	size_t total;
} CheatMem;

void CheatMem_reset(CheatMem* mem);
void CheatMem_addRegion(CheatMem* mem, void* ptr, size_t len);

void CheatMemPatch_init(struct CheatMemPatch* p);

// Apply the enabled memory cheats, in file order, once (one frame). A false
// RUN_NEXT_IF_* condition (or one that can't be read) skips the next cheat.
// Returns how many cheats wrote to memory.
int CheatMem_apply(const CheatMem* mem, const struct CheatMemPatch* const* patches, int n);
