#ifndef OSDMUSIC_CONFIG_H
#define OSDMUSIC_CONFIG_H

// Flat-JSON integer lookup for the stock widget's config.json (osdmusic.c),
// split out so it can be unit-tested on the host: see
// tests/test_osdmusic_config.c.

#include <stdio.h>
#include <string.h>

// The integer after "key": in json, or fallback when the key is missing or
// its value is not a number. Not a JSON parser: the first "key" followed by a
// colon wins (the same text as a string value is passed over).
static inline int osdmusic_config_int(const char* json, const char* key, int fallback) {
	char needle[64];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char* at = json;
	while ((at = strstr(at, needle)) != NULL) {
		at += strlen(needle);
		while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')
			at++;
		if (*at != ':')
			continue; // a string value, not the key
		int value = fallback;
		if (sscanf(at + 1, " %d", &value) != 1)
			return fallback;
		return value;
	}
	return fallback;
}

#endif
