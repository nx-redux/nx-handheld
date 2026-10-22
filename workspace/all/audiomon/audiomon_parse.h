// audiomon_parse.h — pure parsing helpers of audiomon.c.
//
// No D-Bus/udev/ALSA includes, so they build and run on the host (see
// tests/test_audiomon_parse.c).
#ifndef AUDIOMON_PARSE_H
#define AUDIOMON_PARSE_H

#include <stdio.h>
#include <string.h>

// Extract the MAC address from a BlueZ D-Bus object path.
// Path format: .../dev_AA_BB_CC_DD_EE_FF/...
// Returns length written to out (excluding NUL), or 0 on failure: no "dev_"
// segment, a segment shorter than a full address, or out_size < 18.
static inline int audiomon_path_to_mac(const char* path, char* out, size_t out_size) {
	const char* p = strstr(path, "dev_");
	if (!p || out_size < 18)
		return 0;

	p += 4; // skip "dev_"

	// Copy up to 17 chars (AA_BB_CC_DD_EE_FF), replacing '_' with ':'
	int i = 0;
	while (*p && *p != '/' && i < 17) {
		out[i] = (*p == '_') ? ':' : *p;
		i++;
		p++;
	}
	out[i] = '\0';
	if (i != 17) {
		out[0] = '\0';
		return 0; // cut short: not an address
	}
	return i;
}

#endif
