#ifndef __BT_ADDR_H__
#define __BT_ADDR_H__

// Bluetooth address check for generic_bt.c. Addresses are parsed out of
// `bluetoothctl devices` output, where a remote device's name is printed raw
// (it may hold a newline and a fake "Device ..." line), and then go into
// `sh -c` command lines, so only a strict XX:XX:XX:XX:XX:XX is accepted.
// Pure, unit-tested on the host: see tests/test_bt_addr.c.

#include <stdbool.h>
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

#endif
