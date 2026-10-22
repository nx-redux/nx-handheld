// Host test for ma_rewind_budget.c: the rewind buffer is cut to what RAM allows.
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I workspace/all/minarch \
//      workspace/all/minarch/ma_rewind_budget.c workspace/all/minarch/tests/test_rewind_budget.c \
//      -o /tmp/test_rewind_budget && /tmp/test_rewind_budget
#include <stdio.h>
#include <string.h>
#include "ma_rewind_budget.h"

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

#define MB (1024ULL * 1024)

static unsigned long long parse(const char* text) {
	FILE* f = fmemopen((void*)text, strlen(text), "r");
	unsigned long long v = RewindBudget_parseMeminfo(f);
	fclose(f);
	return v;
}

int main(void) {
	// a small state and a 64 MB buffer fit easily
	CHECK(RewindBudget_capacity(64 * MB, 200 * 1024, 3, 600 * MB) == 64 * MB, "small state keeps the asked buffer");
	// unknown RAM: no limit
	CHECK(RewindBudget_capacity(256 * MB, 40 * MB, 4, 0) == 256 * MB, "unknown RAM keeps the asked buffer");
	// 30 MB state (PPSSPP): 9 copies = 270 MB of the 300 MB budget leaves 30 MB
	CHECK(RewindBudget_capacity(256 * MB, 30 * MB, 4, 600 * MB) == 30 * MB, "large state cuts the buffer to what is left");
	// the cut is whole MB
	size_t cut = RewindBudget_capacity(256 * MB, 30 * MB + 12345, 4, 600 * MB);
	CHECK(cut > 0 && cut % MB == 0 && cut < 30 * MB, "a cut buffer is a whole number of MB");
	// 40 MB state: 9 copies = 360 MB > 300 MB budget, rewind off
	CHECK(RewindBudget_capacity(64 * MB, 40 * MB, 4, 600 * MB) == 0, "no room for the state copies turns rewind off");
	// less than 1 MB left over turns rewind off too
	CHECK(RewindBudget_capacity(64 * MB, 33 * MB + 300 * 1024, 4, 600 * MB) == 0, "under 1 MB left turns rewind off");
	// exactly fitting keeps it
	CHECK(RewindBudget_capacity(30 * MB, 30 * MB, 4, 600 * MB) == 30 * MB, "an exact fit keeps the asked buffer");

	CHECK(parse("MemTotal:        1013760 kB\nMemFree:          100000 kB\nMemAvailable:     614400 kB\n") == 600 * MB,
		  "MemAvailable parsed in bytes");
	CHECK(parse("MemTotal:        1013760 kB\nMemFree:          100000 kB\n") == 0, "no MemAvailable gives 0");
	CHECK(parse("garbage\n") == 0, "unrelated text gives 0");

	printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
	return fails ? 1 : 0;
}
