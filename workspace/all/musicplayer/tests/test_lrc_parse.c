// Host-compiled unit test for the LRC parser (lrc_parse.c): lines with
// several timestamps used to keep only the first, and a ".5" fraction read as
// 50 ms instead of 500 ms.
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. lrc_parse.c tests/test_lrc_parse.c -o /tmp/t && /tmp/t
#include <stdio.h>
#include <string.h>

#include "../lrc_parse.h"

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

static LyricLine lines[16];

static void test_basic_and_fractions(void) {
	int n = LrcParse_text("[ar:Someone]\n[00:01.5]a\r\n[00:02.50]b\n[00:03.500]c\n[00:04]d\n[01:05.07]e\n", lines, 16);
	CHECK(n == 5);
	CHECK(lines[0].time_ms == 1500 && strcmp(lines[0].text, "a") == 0);
	CHECK(lines[1].time_ms == 2500 && strcmp(lines[1].text, "b") == 0);
	CHECK(lines[2].time_ms == 3500 && strcmp(lines[2].text, "c") == 0);
	CHECK(lines[3].time_ms == 4000 && strcmp(lines[3].text, "d") == 0);
	CHECK(lines[4].time_ms == 65070 && strcmp(lines[4].text, "e") == 0);
}

static void test_multiple_timestamps(void) {
	int n = LrcParse_text("[00:10.00]verse\n[00:05.00][00:20.00]chorus\n[00:15.00]bridge\n", lines, 16);
	CHECK(n == 4);
	CHECK(lines[0].time_ms == 5000 && strcmp(lines[0].text, "chorus") == 0);
	CHECK(lines[1].time_ms == 10000 && strcmp(lines[1].text, "verse") == 0);
	CHECK(lines[2].time_ms == 15000 && strcmp(lines[2].text, "bridge") == 0);
	CHECK(lines[3].time_ms == 20000 && strcmp(lines[3].text, "chorus") == 0);
}

static void test_skips_and_caps(void) {
	// empty lyric lines and malformed tags are skipped
	int n = LrcParse_text("[00:01.00]\n[xx:01]bad\n[00:02.00\nhello\n[00:03.00]ok", lines, 16);
	CHECK(n == 1 && lines[0].time_ms == 3000 && strcmp(lines[0].text, "ok") == 0);

	// max_lines holds even when one line carries many timestamps
	n = LrcParse_text("[00:01][00:02][00:03][00:04]x\n", lines, 3);
	CHECK(n == 3);

	// an over-long line is cut on a UTF-8 character boundary
	char buf[400];
	strcpy(buf, "[00:01.00]");
	for (int i = 0; i < 130; i++)
		strcat(buf, "\xC3\xA9"); // é, 2 bytes
	n = LrcParse_text(buf, lines, 16);
	CHECK(n == 1 && strlen(lines[0].text) == 254);
}

int main(void) {
	test_basic_and_fractions();
	test_multiple_timestamps();
	test_skips_and_caps();
	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("test_lrc_parse: all passed\n");
	return 0;
}
