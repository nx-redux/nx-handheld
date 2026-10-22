#ifndef __BT_ADDR_H__
#define __BT_ADDR_H__

// Bluetooth address check for generic_bt.c. Addresses are parsed out of
// `bluetoothctl devices` output, where a remote device's name is printed raw
// (it may hold a newline and a fake "Device ..." line), and then go into
// `sh -c` command lines, so only a strict XX:XX:XX:XX:XX:XX is accepted.
// Pure, unit-tested on the host: see tests/test_bt_addr.c.

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static inline bool BtAddr_isValid(const char* s) {
	if (!s)
		return false;
	for (int i = 0; i < 17; i++) {
		unsigned char c = (unsigned char)s[i];
		if (i % 3 == 2) {
			if (c != ':')
				return false;
		} else if (!isxdigit(c)) {
			return false;
		}
	}
	return s[17] == '\0';
}

// One `bluetoothctl devices` line: "Device XX:XX:XX:XX:XX:XX Name". False unless it starts "Device " and carries a
// valid address (BtAddr_isValid), which goes to addr. name (may be NULL) gets what follows "Device " (7) and the
// address with its space (18), cut to name_size - 1 bytes, or "" when the line ends before it.
static inline bool BtAddr_parseDeviceLine(const char* line, char addr[18], char* name, size_t name_size) {
	if (name && name_size > 0)
		name[0] = '\0';
	addr[0] = '\0';
	if (!line || strncmp(line, "Device ", 7) != 0)
		return false;
	if (sscanf(line, "Device %17s", addr) != 1 || !BtAddr_isValid(addr))
		return false;
	// bounds-checked so a line with no trailing space can't over-read
	if (name && name_size > 0 && strlen(line) >= 25 && line[25]) {
		strncpy(name, line + 25, name_size - 1);
		name[name_size - 1] = '\0';
	}
	return true;
}

#endif
