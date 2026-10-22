// Host test for nextui/line_rewrite.c: the map.txt / collection line filter.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../nextui/line_rewrite.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

// filter `in` and compare with `want`; want_changed checks the flag too
static int filter_is(const char* in, const char* key, bool map_key, const char* rep, bool append, const char* want,
					 bool want_changed) {
	size_t out_len = 0;
	bool changed = !want_changed;
	char* out = LineRewrite_filter(in, strlen(in), key, map_key, rep, append, &out_len, &changed);
	int ok = out && out_len == strlen(want) && memcmp(out, want, out_len) == 0 && changed == want_changed;
	if (!ok)
		printf("  got [%s] changed=%d\n", out ? out : "(null)", changed);
	free(out);
	return ok;
}

int main(void) {
	// collections: whole-line match, others byte for byte
	CHECK(filter_is("/Roms/GB/a.gb\n/Roms/GB/b.gb\n", "/Roms/GB/a.gb", false, NULL, false, "/Roms/GB/b.gb\n", true),
		  "collection line dropped");
	CHECK(filter_is("/Roms/GB/a.gb\r\n/Roms/GB/b.gb\r\n", "/Roms/GB/b.gb", false, NULL, false, "/Roms/GB/a.gb\r\n", true),
		  "CRLF line matched, others kept verbatim");
	CHECK(filter_is("/Roms/GB/a.gb\n", "/Roms/GB/a", false, NULL, false, "/Roms/GB/a.gb\n", false), "prefix is not a match");
	CHECK(filter_is("/Roms/GB/a.gb", "/Roms/GB/a.gb", false, NULL, false, "", true), "last line without newline");

	// map.txt: key before the tab
	CHECK(filter_is("a.gb\tAlpha\nb.gb\tBeta\n", "a.gb", true, NULL, false, "b.gb\tBeta\n", true), "map key dropped");
	CHECK(filter_is("a.gb\tAlpha\n", "a.gb\tAlpha", true, NULL, false, "a.gb\tAlpha\n", false), "key never spans the tab");
	CHECK(filter_is("a.gb\nb.gb\tBeta\n", "a.gb", true, NULL, false, "a.gb\nb.gb\tBeta\n", false), "line without a tab kept");
	CHECK(filter_is("a.gb\tAlpha\nb.gb\tBeta\n", "b.gb", true, "b.gb\tBravo\n", true, "a.gb\tAlpha\nb.gb\tBravo\n", true),
		  "alias replaced in place");
	CHECK(filter_is("a.gb\tAlpha", "c.gb", true, "c.gb\tCharlie\n", true, "a.gb\tAlpha\nc.gb\tCharlie\n", true),
		  "alias appended after a last line without newline");
	CHECK(filter_is("", "c.gb", true, "c.gb\tCharlie\n", true, "c.gb\tCharlie\n", true), "alias into an empty map");

	// a line far past the old 512-byte fgets buffer stays whole
	size_t big = 3000;
	char* in = malloc(big + 64);
	memset(in, 'x', big);
	strcpy(in + big, "\tLong\nk\tv\n");
	char* want = malloc(big + 64);
	memset(want, 'x', big);
	strcpy(want + big, "\tLong\n");
	CHECK(filter_is(in, "k", true, NULL, false, want, true), "long line kept whole");
	free(in);
	free(want);

	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
