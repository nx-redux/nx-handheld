#ifndef __WIFI_ESCAPE_H__
#define __WIFI_ESCAPE_H__

// Pure escaper behind generic_wifi.c's wpa_cli commands, split out so it can be unit-tested on the host: see
// tests/test_wifi_escape.c. The SSID or passphrase goes inside wpa_cli's double quotes, themselves inside a
// single-quoted shell argument: '"' and '\' gain a backslash (for wpa_cli), and '\'' becomes '\'' (close, escaped
// quote, reopen) for the shell. A value that does not fit is cut before the first character that would not fit
// whole, so an escape is never split (a lone backslash or quote would change the command).

#include <stddef.h>

static inline void WifiEscape_wpa(char* dest, const char* src, size_t dest_len) {
	if (dest_len == 0)
		return;
	size_t j = 0;
	for (size_t i = 0; src[i] != '\0'; i++) {
		char c = src[i];
		if (c == '"' || c == '\\') {
			if (j + 2 >= dest_len)
				break;
			dest[j++] = '\\';
			dest[j++] = c;
		} else if (c == '\'') {
			if (j + 4 >= dest_len)
				break;
			dest[j++] = '\''; // close single quote
			dest[j++] = '\\'; // escape
			dest[j++] = '\''; // the actual quote
			dest[j++] = '\''; // open single quote again
		} else {
			if (j + 1 >= dest_len)
				break;
			dest[j++] = c;
		}
	}
	dest[j] = '\0';
}

#endif
