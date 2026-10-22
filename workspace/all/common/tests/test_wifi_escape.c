// Host unit test for wifi_escape.h (generic_wifi.c's wpa_cli escaper). Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../wifi_escape.h"

static void check(const char* src, size_t dest_len, const char* want) {
	char* dest = malloc(dest_len); // exact size: an over-write fails loudly
	assert(dest);
	WifiEscape_wpa(dest, src, dest_len);
	assert(strcmp(dest, want) == 0);
	free(dest);
}

int main(void) {
	check("Home WiFi", 64, "Home WiFi");
	check("a\"b\\c", 64, "a\\\"b\\\\c");
	check("Bob's", 64, "Bob'\\''s");
	check("", 1, "");
	check("abc", 1, "");
	check("abc", 3, "ab");
	// Regression: a '"' or '\' with room for only one more byte was written unescaped
	check("ab\"", 4, "ab");
	check("ab\"", 5, "ab\\\"");
	check("a\\", 3, "a");
	// a quote that doesn't fit whole is left out with everything after it, never half-written
	check("a'b", 5, "a");
	check("a'b", 6, "a'\\''");
	check("a'b", 7, "a'\\''b");
	// Regression: tiny buffers (dest_len < 5) underflowed the quote's room check and wrote 4 bytes
	check("'", 2, "");
	check("'", 4, "");
	check("'", 5, "'\\''");
	char untouched[1] = {'x'};
	WifiEscape_wpa(untouched, "abc", 0);
	assert(untouched[0] == 'x');
	printf("test_wifi_escape: OK\n");
	return 0;
}
