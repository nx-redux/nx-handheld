// Host test for emu_overlay_cfg.c's emu_ovl_cfg_write_ini edge cases (emu-options/tests covers a section missing
// from the file): a dirty key replaced in place, a missing key appended at the end of its section, a final line
// without '\n', and a line longer than the 1024-byte inspection buffer copied through whole. Built with
// AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu_overlay_cfg.h"

#define ROOT "/tmp/nx_test_emu_ovl_write_ini"

static const char* SCHEMA = "{ \"emulator\": \"tc\", \"config_section\": \"config\", \"sections\": [\n"
							"  { \"name\": \"Main\", \"items\": [\n"
							"    { \"key\": \"tc_a\", \"label\": \"A\", \"type\": \"int\", \"min\": 0, \"max\": 9 },\n"
							"    { \"key\": \"tc_b\", \"label\": \"B\", \"type\": \"int\", \"min\": 0, \"max\": 9 } ] },\n"
							"  { \"name\": \"Other\", \"ini_section\": \"other\", \"items\": [\n"
							"    { \"key\": \"tc_c\", \"label\": \"C\", \"type\": \"int\", \"min\": 0, \"max\": 9 } ] } ] }\n";

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(content, f);
	fclose(f);
}

static char* slurp(const char* path) {
	static char buf[8192];
	FILE* f = fopen(path, "r");
	assert(f);
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void stage(EmuOvlConfig* cfg, const char* section, const char* key, int value) {
	EmuOvlItem* item = emu_ovl_cfg_find_item(cfg, section, key, NULL, NULL);
	assert(item);
	item->staged_value = value;
	item->dirty = true;
}

int main(void) {
	assert(system("rm -rf " ROOT " && mkdir -p " ROOT) == 0);
	write_file(ROOT "/schema.json", SCHEMA);
	static EmuOvlConfig cfg;
	assert(emu_ovl_cfg_load(&cfg, ROOT "/schema.json") == 0);

	char longval[1500];
	memset(longval, 'v', sizeof(longval) - 1);
	longval[sizeof(longval) - 1] = '\0';
	static char ini[4096], want[4096];
	snprintf(ini, sizeof(ini), "; top\n[config]\ntc_a = 1\nlong = %s\n[other]\nlast = 1", longval);
	write_file(ROOT "/core.ini", ini);
	stage(&cfg, "config", "tc_a", 5); // replaced in place
	stage(&cfg, "config", "tc_b", 7); // not in the file: appended before the next section
	stage(&cfg, "other", "tc_c", 3);  // appended after a final line that has no '\n'
	assert(emu_ovl_cfg_write_ini(&cfg, ROOT "/core.ini") == 0);
	snprintf(want, sizeof(want), "; top\n[config]\ntc_a = 5\nlong = %s\ntc_b = 7\n[other]\nlast = 1\ntc_c = 3\n",
			 longval);
	assert(strcmp(slurp(ROOT "/core.ini"), want) == 0);

	// nothing dirty: the file is left as it is
	for (int s = 0; s < cfg.section_count; s++)
		for (int i = 0; i < cfg.sections[s].item_count; i++)
			cfg.sections[s].items[i].dirty = false;
	write_file(ROOT "/core.ini", "x = 1");
	assert(emu_ovl_cfg_write_ini(&cfg, ROOT "/core.ini") == 0);
	assert(strcmp(slurp(ROOT "/core.ini"), "x = 1") == 0);
	// a missing file is an error, not an empty write
	stage(&cfg, "config", "tc_a", 2);
	assert(emu_ovl_cfg_write_ini(&cfg, ROOT "/missing.ini") == -1);

	emu_ovl_cfg_free(&cfg);
	assert(system("rm -rf " ROOT) == 0);
	printf("test_emu_ovl_write_ini: OK\n");
	return 0;
}
