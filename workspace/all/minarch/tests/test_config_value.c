// Host test for ma_config_value.c: Config_getValue on cfg text.
#include <stdio.h>
#include <string.h>
#include "ma_config_value.h"

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

int main(void) {
	char v[256];
	int lock;

	CHECK(Config_getValue("a = 1\nkey = hello\nb = 2\n", "key", v, NULL) && !strcmp(v, "hello"), "plain key");
	CHECK(Config_getValue("key = first", "key", v, NULL) && !strcmp(v, "first"), "key on the first line, no newline");
	CHECK(!Config_getValue("a = 1\n", "key", v, NULL), "missing key");
	CHECK(!Config_getValue("", "key", v, NULL), "empty cfg");

	// anchoring: substrings inside other keys or values never match
	CHECK(!Config_getValue("key_suffix = x\n", "key", v, NULL), "longer key (suffix) does not match");
	CHECK(!Config_getValue("prefix_key = x\n", "key", v, NULL), "longer key (prefix) does not match");
	CHECK(!Config_getValue("other = key = x\n", "key", v, NULL), "key inside a value does not match");
	CHECK(Config_getValue("my_key = 1\nkey = 2\n", "key", v, NULL) && !strcmp(v, "2"), "skips a false hit, finds the line");
	CHECK(!Config_getValue("key=1\n", "key", v, NULL), "needs \" = \"");
	CHECK(!Config_getValue(" key = 1\n", "key", v, NULL), "indented key does not match");

	// the '-' lock prefix
	lock = 0;
	CHECK(Config_getValue("a = 1\n-key = 5\n", "key", v, &lock) && !strcmp(v, "5") && lock == 1, "-key sets lock");
	lock = 0;
	CHECK(Config_getValue("key = 5\n", "key", v, &lock) && lock == 0, "no prefix leaves lock");
	lock = 7;
	CHECK(Config_getValue("key = 5\n", "key", v, &lock) && lock == 7, "lock is only ever set, not cleared");
	CHECK(Config_getValue("-key = 5\n", "key", v, NULL) && !strcmp(v, "5"), "-key with a NULL lock");
	lock = 0;
	CHECK(!Config_getValue("x-key = 5\n", "key", v, &lock) && lock == 0, "'-' mid-line is not a lock marker");

	// line endings
	CHECK(Config_getValue("key = crlf\r\nb = 2\r\n", "key", v, NULL) && !strcmp(v, "crlf"), "CRLF: no trailing \\r");
	CHECK(Config_getValue("a = 1\r\nkey = x\r\n", "key", v, NULL) && !strcmp(v, "x"), "CRLF: anchored after \\r\\n");
	CHECK(Config_getValue("key = \nb = 2\n", "key", v, NULL) && !strcmp(v, ""), "empty value");
	CHECK(Config_getValue("key = a b = c\n", "key", v, NULL) && !strcmp(v, "a b = c"), "value keeps spaces and '='");

	// truncation
	{
		char cfg[600];
		memcpy(cfg, "key = ", 6);
		memset(cfg + 6, 'x', 400);
		strcpy(cfg + 406, "\nnext = 1\n");
		memset(v, 'Z', sizeof(v));
		CHECK(Config_getValue(cfg, "key", v, NULL) && strlen(v) == 255 && v[0] == 'x' && v[254] == 'x',
			  "long value truncated to 255 chars, terminated");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
