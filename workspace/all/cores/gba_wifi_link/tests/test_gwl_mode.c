// Host-compiled tests for gwl_mode.c: the auto-mode rule and the ROM probe.
// Build & run: workspace/all/cores/gba_wifi_link/tests/run_tests.sh
#undef NDEBUG // the checks below must always run
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../gwl_mode.h"

#define LEN 0x200

// A ROM image with a valid header, the given title (12) and game code (4).
static void make_rom(uint8_t* rom, const char* title, const char* code) {
	memset(rom, 0, LEN);
	rom[3] = 0xEA;
	rom[0xB2] = 0x96;
	memcpy(rom + 0xA0, title, strlen(title) > 12 ? 12 : strlen(title));
	memcpy(rom + 0xAC, code, 4);
}

static enum GwlMode mode_of(const char* title, const char* code, size_t size) {
	uint8_t rom[LEN];
	make_rom(rom, title, code);
	return gwl_auto_mode(rom, sizeof rom, size);
}

static void write_file(const char* path, const uint8_t* buf, size_t len) {
	FILE* f = fopen(path, "wb");
	assert(f);
	assert(fwrite(buf, 1, len, f) == len);
	fclose(f);
}

int main(void) {
	// Table entries and the Pokemon branches.
	assert(mode_of("POKEMON FIRE", "BPRE", LEN) == GWL_RFU);
	assert(mode_of("POKEMON LEAF", "BPGE", LEN) == GWL_RFU);
	assert(mode_of("POKEMON EMER", "BPEE", LEN) == GWL_RFU); // table RFU and family
	assert(mode_of("POKEMON RUBY", "AXVE", LEN) == GWL_POKE);
	assert(mode_of("POKEMON SAPP", "AXPE", LEN) == GWL_POKE);
	assert(mode_of("ADVANCEWARS", "AWRE", LEN) == GWL_AW1);
	assert(mode_of("ADVANCEWARS2", "AW2E", LEN) == GWL_AW2);
	assert(mode_of("MARIO GOLF", "BMGE", LEN) == GWL_RFU);
	assert(mode_of("GBA PLAYER", "A88E", LEN) == GWL_NONE);
	assert(mode_of("SOME GAME", "ZZZE", LEN) == GWL_NONE);
	assert(mode_of("NO CODE", "\0\0\0\0", LEN) == GWL_NONE); // becomes UNKN

	// Hacks.
	assert(mode_of("POKEMON HACK", "ZZZE", LEN) == GWL_POKE);		// family title, unknown code
	assert(mode_of("POKEMON FIRE", "BPRX", LEN) == GWL_POKE);		// unknown code, family
	assert(mode_of("POKEMON GOLD", "BPRE", LEN) == GWL_POKE);		// title altered
	assert(mode_of("POKEMON FIRE", "BPRE", 32u << 20) == GWL_POKE); // expanded
	assert(mode_of("POKEMON EMER", "BPEE", 32u << 20) == GWL_POKE); // table RFU overridden
	assert(mode_of("ADVANCEWARS", "AWRE", 32u << 20) == GWL_AW1);	// expanded, not the engine
	{
		uint8_t rom[LEN];
		make_rom(rom, "POKEMON FIRE", "BPRE");
		rom[3] = 0x00;
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_POKE);
		make_rom(rom, "POKEMON FIRE", "BPRE");
		rom[0xB2] = 0x00;
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_POKE);
		// A hack that is not the engine keeps the table's mode.
		make_rom(rom, "MARIO GOLF", "BMGE");
		rom[3] = 0x00;
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_RFU);
		// AXV code but not "POKEMON" title: family by code, title altered.
		make_rom(rom, "RUBY HACK", "AXVE");
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_POKE);
		// Too short for a header.
		assert(gwl_auto_mode(rom, 0x40, LEN) == GWL_NONE);
		assert(gwl_auto_mode(NULL, 0, 0) == GWL_NONE);
	}

	// 128 KB flash makes an unknown or expanded game the Pokemon engine.
	{
		uint8_t rom[LEN];
		make_rom(rom, "SOME GAME", "ZZZE");
		memcpy(rom + 0x100, "FLASH1M_V103", 12);
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_POKE); // unknown + flash 128K
		make_rom(rom, "SOME GAME", "ZZZE");
		memcpy(rom + 0x100, "FLASH_V126", 10);
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_NONE); // 64 KB flash
		make_rom(rom, "SOME GAME", "ZZZE");
		memcpy(rom + 0x100, "SRAM_V113", 9);
		memcpy(rom + 0x120, "FLASH1M_V103", 12);
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_NONE); // SRAM wins
		// Known flash-128K game: the table flag needs a flash marker to count.
		make_rom(rom, "BIXJ GAME", "BIXJ");
		assert(gwl_auto_mode(rom, sizeof rom, 32u << 20) == GWL_NONE);
		memcpy(rom + 0x100, "FLASH1M_V103", 12);
		assert(gwl_auto_mode(rom, sizeof rom, LEN) == GWL_NONE);	   // standard, known
		assert(gwl_auto_mode(rom, sizeof rom, 32u << 20) == GWL_POKE); // expanded
	}
	// The marker scan past the first MiB, 4-aligned like gpSP's.
	{
		size_t n = 2u << 20;
		uint8_t* big = malloc(n);
		assert(big);
		make_rom(big, "SOME GAME", "ZZZE");
		memset(big + LEN, 0, n - LEN);
		memcpy(big + (1u << 20) + 0x100, "FLASH1M_V103", 12);
		assert(gwl_auto_mode(big, n, n) == GWL_POKE);
		assert(gwl_auto_mode(big, 1u << 20, n) == GWL_NONE); // first MiB only
		memset(big + (1u << 20) + 0x100, 0, 12);
		memcpy(big + (1u << 20) + 0x102, "FLASH1M_V103", 12); // unaligned: gpSP misses it
		assert(gwl_auto_mode(big, n, n) == GWL_NONE);
		free(big);
	}

	// Tokens.
	for (int m = GWL_NONE; m <= GWL_AW2; m++)
		assert(gwl_mode_from_token(gwl_mode_token((enum GwlMode)m)) == (enum GwlMode)m);
	assert(!strcmp(gwl_mode_token(GWL_RFU), "rfu"));
	assert(!strcmp(gwl_mode_token(GWL_AW2), "aw2"));
	assert(gwl_mode_from_token("x") == GWL_NONE);
	assert(gwl_mode_from_token(NULL) == GWL_NONE);

	// What two devices may link.
	assert(gwl_mixed_ok("rfu", "rfu", true));
	assert(!gwl_mixed_ok("poke", "poke", true));
	assert(gwl_mixed_ok("poke", "poke", false));
	assert(!gwl_mixed_ok("rfu", "poke", false));
	assert(!gwl_mixed_ok("none", "none", false));
	assert(!gwl_mixed_ok("aw3", "aw3", false));
	assert(!gwl_mixed_ok(NULL, "rfu", false));
	assert(!gwl_mixed_ok("rfu", NULL, false));

	// The probe.
	{
		const char* tmp = getenv("TMPDIR");
		char base[512], path[600];
		snprintf(base, sizeof base, "%s/gwl_mode_test_%d", tmp && *tmp ? tmp : "/tmp", (int)getpid());
		uint8_t rom[LEN];
		make_rom(rom, "POKEMON FIRE", "BPRE");
		snprintf(path, sizeof path, "%s.gba", base);
		write_file(path, rom, sizeof rom);
		assert(gwl_probe_file(path) == GWL_RFU);
		remove(path);
		rom[0] = 'P';
		rom[1] = 'K';
		snprintf(path, sizeof path, "%s.zip", base);
		write_file(path, rom, sizeof rom);
		assert(gwl_probe_file(path) == GWL_NONE);
		remove(path);
		snprintf(path, sizeof path, "%s.short", base);
		write_file(path, rom, 0x40);
		assert(gwl_probe_file(path) == GWL_NONE);
		remove(path);
		assert(gwl_probe_file("/nonexistent/gwl.gba") == GWL_NONE);
		assert(gwl_probe_file(NULL) == GWL_NONE);
	}

	puts("test_gwl_mode: ok");
	return 0;
}
