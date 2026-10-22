// Host unit test for ui/confirm_wrap.h (the confirm dialog's subtitle wrap). Built with AddressSanitizer by
// run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ui/confirm_wrap.h"

static int measure_10px(void* ctx, const char* utf8) {
	(void)ctx;
	return (int)strlen(utf8) * 10;
}

int main(void) {
	char lines[4][256];
	assert(ConfirmWrap_lines("aaa bbb ccc", 70, measure_10px, NULL, lines, 4) == 2);
	assert(strcmp(lines[0], "aaa bbb") == 0 && strcmp(lines[1], "ccc") == 0);
	// a word wider than the line gets one of its own, never an empty line before it
	assert(ConfirmWrap_lines("supercalifragilistic short", 50, measure_10px, NULL, lines, 4) == 2);
	assert(strcmp(lines[0], "supercalifragilistic") == 0 && strcmp(lines[1], "short") == 0);
	// past the last line the rest is dropped
	assert(ConfirmWrap_lines("a b c d e f", 10, measure_10px, NULL, lines, 4) == 4);
	assert(strcmp(lines[3], "d") == 0);
	assert(ConfirmWrap_lines("a b c", 10, measure_10px, NULL, lines, 1) == 1 && strcmp(lines[0], "a") == 0);
	// runs of spaces separate words like one
	assert(ConfirmWrap_lines("  a   b  ", 1000, measure_10px, NULL, lines, 4) == 1 && strcmp(lines[0], "a b") == 0);
	assert(ConfirmWrap_lines("", 100, measure_10px, NULL, lines, 4) == 0);
	assert(ConfirmWrap_lines("   ", 100, measure_10px, NULL, lines, 4) == 0);
	// a word longer than a line's buffer is cut to it (ASan)
	char big[700];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	assert(ConfirmWrap_lines(big, 100, measure_10px, NULL, lines, 4) == 1 && strlen(lines[0]) == 255);
	printf("test_confirm_wrap: OK\n");
	return 0;
}
