#ifndef SETTINGS_KV_H
#define SETTINGS_KV_H

// Header-only readers for one key of a key=value settings file (the shared
// minuisettings.txt), for the small daemons that don't link config.c
// (audiomon, osdmusic). No SDL; host-tested in common/tests/test_settings_kv.c.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Value of `line` when it starts with exactly "key=", else NULL (so "keyX=" and
// "xkey=" don't match "key").
static inline const char* settings_kv_value(const char* line, const char* key) {
	size_t n = strlen(key);
	if (strncmp(line, key, n) != 0 || line[n] != '=')
		return NULL;
	return line + n + 1;
}

// Integer value of `key`, scanf "%i" style (decimal, 0x hex, 0 octal; leading
// spaces skipped): the first "key=" line whose value parses wins. fallback
// when the file or such a line is missing.
static inline int settings_kv_int(const char* path, const char* key, int fallback) {
	FILE* f = fopen(path, "r");
	if (!f)
		return fallback;
	char line[256];
	int value = fallback;
	while (fgets(line, sizeof(line), f)) {
		const char* v = settings_kv_value(line, key);
		if (!v)
			continue;
		char* end;
		long parsed = strtol(v, &end, 0);
		if (end != v) {
			value = (int)parsed;
			break;
		}
	}
	fclose(f);
	return value;
}

// Hex value of `key` written as 0x... / 0X... (a theme color): the first
// "key=" line with such a value wins. false when missing.
static inline bool settings_kv_hex(const char* path, const char* key, unsigned* out) {
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	char line[256];
	bool found = false;
	while (!found && fgets(line, sizeof(line), f)) {
		const char* v = settings_kv_value(line, key);
		found = v && (sscanf(v, "0x%x", out) == 1 || sscanf(v, "0X%x", out) == 1);
	}
	fclose(f);
	return found;
}

#endif // SETTINGS_KV_H
