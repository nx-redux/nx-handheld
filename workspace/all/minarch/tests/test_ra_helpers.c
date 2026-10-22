// Host test for ra_helpers.c: RetroAchievements login backoff, request routing,
// memory range checks and the mute file.
#include <stdint.h>
#include <stdio.h>
#include "ra_helpers.h"

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

static FILE* file_with(const char* text) {
	FILE* f = tmpfile();
	if (f) {
		fputs(text, f);
		rewind(f);
	}
	return f;
}

int main(void) {
	// login retry backoff
	CHECK(RA_retryDelayMs(0) == 1000 && RA_retryDelayMs(1) == 2000 && RA_retryDelayMs(2) == 4000 &&
			  RA_retryDelayMs(3) == 8000,
		  "1 s, 2 s, 4 s, 8 s");
	CHECK(RA_retryDelayMs(4) == 8000 && RA_retryDelayMs(100) == 8000, "capped at 8 s");
	CHECK(RA_retryDelayMs(-1) == 1000, "negative attempt: first delay");

	// background requests
	CHECK(RA_isBackgroundRequest("r=achievementsets&u=me&g=1"), "achievementsets is background");
	CHECK(RA_isBackgroundRequest("u=me&r=achievementsets"), "anywhere in the body");
	CHECK(!RA_isBackgroundRequest("r=startsession&g=1") && !RA_isBackgroundRequest(""), "other requests are not");
	CHECK(!RA_isBackgroundRequest(NULL), "NULL body is not");

	// memory ranges
	CHECK(RA_memRangeFits(100, 0, 100) && RA_memRangeFits(100, 99, 1) && RA_memRangeFits(100, 100, 0),
		  "ranges up to the end fit");
	CHECK(!RA_memRangeFits(100, 99, 2) && !RA_memRangeFits(100, 101, 0), "past the end does not");
	CHECK(!RA_memRangeFits(100, UINT32_MAX, 2) && !RA_memRangeFits(100, 4, UINT32_MAX - 1),
		  "address + size wrapping uint32 does not fit");
	CHECK(!RA_memRangeFits(0, 0, 1) && RA_memRangeFits(0, 0, 0), "empty block");

	// mute file
	{
		uint32_t ids[4] = {0};
		FILE* f = file_with("12\n0\nabc\n345\n\n99999\n");
		CHECK(f && RA_readMutedIds(f, ids, 4) == 3 && ids[0] == 12 && ids[1] == 345 && ids[2] == 99999,
			  "positive ids read, 0/garbage/blank skipped");
		if (f)
			fclose(f);
		f = file_with("1\n2\n3\n4\n5\n6\n");
		CHECK(f && RA_readMutedIds(f, ids, 4) == 4 && ids[3] == 4, "capped at max");
		if (f)
			fclose(f);
		f = file_with("7\r\n8");
		CHECK(f && RA_readMutedIds(f, ids, 4) == 2 && ids[0] == 7 && ids[1] == 8, "CRLF and no final newline");
		if (f)
			fclose(f);
		f = file_with("");
		CHECK(f && RA_readMutedIds(f, ids, 4) == 0, "empty file");
		if (f)
			fclose(f);
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
