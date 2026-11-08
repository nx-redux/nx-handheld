// gwl_mode.c - gpSP's per-game link mode choice (gba_memory.c load_gamepak and
// load_game_config_over), with the ROM-hack and Pokemon-engine rules.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gwl_mode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GWL_GAME_NO_SERIAL (-1)

typedef struct {
	char code[5];
	int serial;
	uint8_t flash128k;
	uint8_t eeprom;
} GwlGame;

static const GwlGame games[] = {
#include "gwl_games.h"
};

#define MIB (1024u * 1024u)
#define HEADER_LEN 0xC0
// A bigger file is not a GBA ROM; keeps the probe's memory bounded.
#define PROBE_MAX (64u * MIB)

enum Backup { BACKUP_UNKNOWN,
			  BACKUP_EEPROM,
			  BACKUP_SRAM,
			  BACKUP_FLASH };

enum {
	SIG_EEPROM = 1,
	SIG_SRAM = 2,
	SIG_FLASH1M = 4,
	SIG_FLASH5 = 8
};

static bool has_signature(const uint8_t* rom, size_t size, const char* sig) {
	size_t n = strlen(sig);
	if (size < n)
		return false;
	for (size_t i = 0; i + n <= size; i++)
		if (memcmp(rom + i, sig, n) == 0)
			return true;
	return false;
}

// gpSP's whole-ROM marker scan: 4-aligned, per 1 MiB chunk, so a marker
// straddling a chunk edge or in a chunk's last 10 bytes is missed.
static unsigned scan_signatures(const uint8_t* rom, size_t len) {
	unsigned found = 0;
	for (size_t base = 0; base < len; base += MIB) {
		size_t chunk = len - base > MIB ? MIB : len - base;
		const uint8_t* c = rom + base;
		for (size_t i = 0; chunk > 10 && i < chunk - 10; i += 4) {
			if (c[i] == 'E' && !(found & SIG_EEPROM) && !memcmp(c + i, "EEPROM_V", 8))
				found |= SIG_EEPROM;
			else if (c[i] == 'S' && !(found & SIG_SRAM) && !memcmp(c + i, "SRAM_V", 6))
				found |= SIG_SRAM;
			else if (c[i] == 'F' && !(found & SIG_FLASH1M) && !memcmp(c + i, "FLASH1M_V", 9))
				found |= SIG_FLASH1M;
			else if (c[i] == 'F' && !(found & SIG_FLASH5) &&
					 (!memcmp(c + i, "FLASH512_V", 10) || !memcmp(c + i, "FLASH_V", 7)))
				found |= SIG_FLASH5;
		}
		if (found == (SIG_EEPROM | SIG_SRAM | SIG_FLASH1M | SIG_FLASH5))
			break;
	}
	return found;
}

static bool pokemon_family(const uint8_t* rom) {
	return !memcmp(rom + 0xA0, "POKEMON", 7) ||
		   !memcmp(rom + 0xAC, "AXV", 3) || !memcmp(rom + 0xAC, "AXP", 3) ||
		   !memcmp(rom + 0xAC, "BPE", 3) || !memcmp(rom + 0xAC, "BPR", 3) ||
		   !memcmp(rom + 0xAC, "BPG", 3);
}

// gpSP's detect_backup_subcircuit: only the outcome "128 KB flash" matters.
static void detect_backup(const uint8_t* rom, size_t len, enum Backup* backup, bool* flash128) {
	size_t first = len < MIB ? len : MIB;
	bool eeprom = has_signature(rom, first, "EEPROM_V");
	bool sram = has_signature(rom, first, "SRAM_V");
	bool flash1m = has_signature(rom, first, "FLASH1M_V");
	bool flash5 = has_signature(rom, first, "FLASH512_V") || has_signature(rom, first, "FLASH_V");
	unsigned file = 0;

	if (!eeprom && !sram && !flash1m && !flash5 && pokemon_family(rom)) {
		*backup = BACKUP_FLASH;
		*flash128 = true;
		return;
	}
	if (!eeprom && !sram && !flash1m && !flash5)
		file = scan_signatures(rom, len);
	if (eeprom || (file & SIG_EEPROM))
		*backup = BACKUP_EEPROM;
	else if (sram || (file & SIG_SRAM))
		*backup = BACKUP_SRAM;
	else if (flash1m || (file & SIG_FLASH1M)) {
		*backup = BACKUP_FLASH;
		*flash128 = true;
	} else if (flash5 || (file & SIG_FLASH5)) {
		*backup = BACKUP_FLASH;
		*flash128 = false;
	}
}

enum GwlMode gwl_auto_mode(const uint8_t* rom, size_t len, size_t rom_size) {
	if (!rom || len < HEADER_LEN)
		return GWL_NONE;

	bool nonstandard = rom[3] != 0xEA || rom[0xB2] != 0x96;

	char code[4];
	memcpy(code, rom + 0xAC, 4);
	bool valid = false;
	for (int i = 0; i < 4; i++)
		if ((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9'))
			valid = true;
	if (!valid)
		memcpy(code, "UNKN", 4);

	// load_game_config_over visits every entry with this code.
	bool known = false, flash128 = false;
	int serial = GWL_GAME_NO_SERIAL; // gpSP sets it from the first FLAGS_SERIAL entry only
	enum Backup backup = BACKUP_UNKNOWN;
	for (size_t i = 0; i < sizeof games / sizeof games[0]; i++) {
		if (memcmp(games[i].code, code, 4) != 0)
			continue;
		known = true;
		if (games[i].flash128k)
			flash128 = true;
		if (games[i].eeprom)
			backup = BACKUP_EEPROM;
		if (serial == GWL_GAME_NO_SERIAL)
			serial = games[i].serial;
	}

	// The table's flash flag alone does not make it 128 KB flash: the backup
	// type must still come out as flash, which the marker scan decides.
	if (backup == BACKUP_UNKNOWN)
		detect_backup(rom, len < rom_size ? len : rom_size, &backup, &flash128);
	bool flash128_used = backup == BACKUP_FLASH && flash128;
	bool family = pokemon_family(rom);
	bool engine = family || flash128_used;

	bool title_altered = false;
	if (family) {
		static const char* const titles[] = {"POKEMON FIRE", "POKEMON LEAF", "POKEMON EMER", "POKEMON RUBY", "POKEMON SAPP"};
		title_altered = true;
		for (size_t i = 0; i < sizeof titles / sizeof titles[0]; i++)
			if (!memcmp(rom + 0xA0, titles[i], 12))
				title_altered = false;
	}
	bool expanded = rom_size > 16u * MIB;
	bool hack = nonstandard || !known || title_altered || (expanded && engine);

	// The Pokemon branches come after the table's pick and replace it.
	if (hack && engine)
		return GWL_POKE;
	if (!hack && family)
		return (!memcmp(code, "AXV", 3) || !memcmp(code, "AXP", 3)) ? GWL_POKE : GWL_RFU;
	return serial == GWL_GAME_NO_SERIAL ? GWL_NONE : (enum GwlMode)serial;
}

enum GwlMode gwl_probe_file(const char* path) {
	FILE* f = path ? fopen(path, "rb") : NULL;
	if (!f)
		return GWL_NONE;
	enum GwlMode m = GWL_NONE;
	uint8_t* buf = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long size = ftell(f);
		if (size >= HEADER_LEN && fseek(f, 0, SEEK_SET) == 0) {
			size_t len = (size_t)size > PROBE_MAX ? PROBE_MAX : (size_t)size;
			buf = malloc(len);
			if (buf && fread(buf, 1, len, f) == len && memcmp(buf, "PK", 2) != 0)
				m = gwl_auto_mode(buf, len, (size_t)size);
		}
	}
	free(buf);
	fclose(f);
	return m;
}

static const char* const tokens[] = {"none", "rfu", "poke", "aw1", "aw2"};

const char* gwl_mode_token(enum GwlMode m) {
	return (unsigned)m < sizeof tokens / sizeof tokens[0] ? tokens[m] : tokens[0];
}

enum GwlMode gwl_mode_from_token(const char* t) {
	if (t)
		for (size_t i = 1; i < sizeof tokens / sizeof tokens[0]; i++)
			if (strcmp(t, tokens[i]) == 0)
				return (enum GwlMode)i;
	return GWL_NONE;
}

bool gwl_mixed_ok(const char* our_wl, const char* peer_wl, bool usb) {
	if (!our_wl || !peer_wl || strcmp(our_wl, peer_wl) != 0 || gwl_mode_from_token(our_wl) == GWL_NONE)
		return false;
	return !usb || strcmp(our_wl, "rfu") == 0;
}

enum GwlMode gwl_mgba_wifi_mode(const char* option, enum GwlMode auto_mode) {
	static const struct {
		const char* option;
		enum GwlMode mode;
	} pinned[] = {{"rfu", GWL_RFU}, {"mul_poke", GWL_POKE}, {"mul_aw1", GWL_AW1}, {"mul_aw2", GWL_AW2}};
	if (!option)
		return GWL_NONE;
	if (strcmp(option, "auto") == 0)
		return auto_mode;
	if (strcmp(option, "auto_cable") == 0)
		return auto_mode == GWL_RFU ? GWL_RFU : GWL_NONE;
	for (size_t i = 0; i < sizeof pinned / sizeof pinned[0]; i++)
		if (strcmp(option, pinned[i].option) == 0)
			return pinned[i].mode;
	return GWL_NONE;
}

enum GwlLinkPick gwl_mgba_pick(const char* option, enum GwlMode auto_mode) {
	if (gwl_mgba_wifi_mode(option, auto_mode) != GWL_NONE)
		return GWL_PICK_WIFI;
	if (option && (strcmp(option, "disabled") == 0 || strcmp(option, "auto") == 0))
		return GWL_PICK_NONE;
	return GWL_PICK_CABLE;
}
