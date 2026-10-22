// Host-compiled unit test for ffplay_parse.h: filtergraph single-quote
// escaping and the fatal open-error line match of the captured stderr.
// Build & run (from workspace/all/mediaplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_ffplay_parse.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../ffplay_parse.h"

static void test_escape(void) {
	char out[64];
	FfplayParse_escapeFilterSquote("a'b", out, sizeof(out));
	assert(strcmp(out, "a'\\''b") == 0);
	FfplayParse_escapeFilterSquote("/Videos/plain.srt", out, sizeof(out));
	assert(strcmp(out, "/Videos/plain.srt") == 0);
	FfplayParse_escapeFilterSquote("''", out, sizeof(out));
	assert(strcmp(out, "'\\'''\\''") == 0);
	FfplayParse_escapeFilterSquote("", out, sizeof(out));
	assert(out[0] == '\0');

	// Many quotes near the end of a small buffer: never past out_size, always
	// terminated, and a quote is written whole or not at all
	char small[10];
	memset(small, 'X', sizeof(small));
	FfplayParse_escapeFilterSquote("ab''''''''", small, 8);
	assert(strlen(small) < 8);
	assert(strcmp(small, "ab'\\''") == 0);
	assert(small[8] == 'X' && small[9] == 'X');

	memset(small, 'X', sizeof(small));
	FfplayParse_escapeFilterSquote("abcdefghijkl", small, 8);
	assert(strcmp(small, "abcd") == 0); // stops while an escape could still fit
	assert(small[8] == 'X');

	small[0] = 'X';
	FfplayParse_escapeFilterSquote("abc", small, 0); // nothing written
	assert(small[0] == 'X');
	FfplayParse_escapeFilterSquote("abc", small, 1);
	assert(small[0] == '\0');
}

static void test_fatal_line(void) {
	const char* url = "https://cdn.example.com/live/index.m3u8";
	assert(FfplayParse_isFatalLine("https://cdn.example.com/live/index.m3u8: Server returned 403 Forbidden\n", url));
	assert(FfplayParse_isFatalLine("https://cdn.example.com/live/index.m3u8: Invalid data found when processing input\n", url));
	// benign mid-stream warning
	assert(!FfplayParse_isFatalLine("[https @ 0x55d0] Stream ends prematurely at 1234, should be 5678\n", url));
	// the URL is a prefix of a longer one
	assert(!FfplayParse_isFatalLine("https://cdn.example.com/live/index.m3u8?token=1: error\n", url));
	assert(!FfplayParse_isFatalLine("https://cdn.example.com/live/index.m3u8\n", url));
	assert(!FfplayParse_isFatalLine("https://cdn.example.com/live/index.m3u", url));
	// an empty URL never matches
	assert(!FfplayParse_isFatalLine(": something\n", ""));
	assert(FfplayParse_isFatalLine("/mnt/SDCARD/Videos/a b.mkv: No such file or directory\n", "/mnt/SDCARD/Videos/a b.mkv"));
}

int main(void) {
	test_escape();
	test_fatal_line();
	printf("test_ffplay_parse: OK\n");
	return 0;
}
