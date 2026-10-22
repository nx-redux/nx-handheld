// Host-compiled unit test for iptv_text.h: channel-name tag stripping and the
// country-code sanitizer used in the fetch URL and cache path.
// Build & run (from workspace/all/mediaplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_iptv_text.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../iptv_text.h"

static const char* strip(const char* name, int out_sz) {
	static char out[64];
	memset(out, 'X', sizeof(out));
	IPTV_stripNameTags(name, out, out_sz);
	assert(out_sz == (int)sizeof(out) || out[out_sz] == 'X');
	return out;
}

static void test_strip_name_tags(void) {
	assert(strcmp(strip("Foo [Geo-blocked] Bar", 64), "Foo Bar") == 0);
	assert(strcmp(strip("News 24 [Not 24/7]", 64), "News 24") == 0);
	assert(strcmp(strip("[Geo-blocked] Foo", 64), "Foo") == 0); // no leading space left
	assert(strcmp(strip("A [x] [y] B", 64), "A B") == 0);
	assert(strcmp(strip("A [outer [inner] still] B", 64), "A B") == 0); // nested
	assert(strcmp(strip("A [unclosed B", 64), "A") == 0);				// unbalanced: rest dropped
	assert(strcmp(strip("A ] B", 64), "A B") == 0);						// stray close bracket
	assert(strcmp(strip("Plain", 64), "Plain") == 0);
	assert(strcmp(strip("[only]", 64), "") == 0);
	assert(strcmp(strip("Long channel name", 6), "Long") == 0); // cut, then trailing space trimmed
	assert(strcmp(strip("Abcdef", 4), "Abc") == 0);
	assert(strcmp(strip("Abc", 1), "") == 0);
}

static const char* lc(const char* code) {
	static char out[8];
	memset(out, 'X', sizeof(out));
	IPTV_lcCode(code, out, sizeof(out));
	return out;
}

static void test_lc_code(void) {
	assert(strcmp(lc("US"), "us") == 0);
	assert(strcmp(lc("gb"), "gb") == 0);
	assert(strcmp(lc("../x"), "x") == 0);
	assert(strcmp(lc("a;rm -rf /"), "armrf") == 0);
	assert(strcmp(lc("ABCDEFGHIJ"), "abcdefg") == 0); // stays within 7 chars
	assert(strlen(lc("ABCDEFGHIJ")) == 7);
	assert(strcmp(lc(""), "") == 0);
}

int main(void) {
	test_strip_name_tags();
	test_lc_code();
	printf("test_iptv_text: OK\n");
	return 0;
}
