// Host test for ma_cheat_parse.c: RetroArch .cht parsing and the case-folded glob.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ma_cheat_parse.h"

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

// Parses `text` as a .cht file into out; returns CheatParse_file's result.
static int parse(const char* text, struct Cheats* out) {
	FILE* f = tmpfile();
	if (!f)
		return -1;
	fputs(text, f);
	rewind(f);
	int ok = CheatParse_file(f, out, "test.cht", NULL);
	fclose(f);
	return ok;
}

static int streq(const char* a, const char* b) {
	return a && b && !strcmp(a, b);
}

int main(void) {
	struct Cheats c;

	// 1. the count: quoted, unquoted, and not on the first line
	CHECK(parse("cheats = 2\n", &c) == 1 && c.count == 2, "unquoted count");
	CheatParse_free(&c);
	CHECK(parse("cheats = \"15\"\n", &c) == 1 && c.count == 15, "quoted count (#257)");
	CheatParse_free(&c);
	CHECK(parse("cheat0_desc = \"A\"\ncheat0_code = \"1\"\ncheats = 1\n", &c) == 1 && c.count == 1 &&
			  streq(c.cheats[0].name, "A") && streq(c.cheats[0].code, "1"),
		  "count line last (RetroArch cheat search files)");
	CheatParse_free(&c);
	CHECK(parse("cheats_extra = 3\ncheats = 1\n", &c) == 1 && c.count == 1, "a longer key is not the count");
	CheatParse_free(&c);
	CHECK(parse("cheat0_desc = \"A\"\n", &c) == 0 && c.count == 0 && !c.cheats, "no count: rejected");
	CHECK(parse("cheats = 0\n", &c) == 0 && !c.cheats, "count 0: rejected");
	CHECK(parse("cheats = 99999999\n", &c) == 0 && c.count == 0 && !c.cheats, "absurd count: rejected, not allocated");

	// 2. strings: escapes and HTML entities
	CHECK(parse("cheats = 1\ncheat0_desc = \"Say \\\"hi\\\"\"\n", &c) == 1 && streq(c.cheats[0].name, "Say \"hi\""),
		  "escaped quotes decode");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = \"P1 &amp; P2 &lt;x&gt;\"\n", &c) == 1 &&
			  streq(c.cheats[0].name, "P1 & P2 <x>"),
		  "&amp; &lt; &gt; decode");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = \"Pok&eacute;mon\"\n", &c) == 1 && streq(c.cheats[0].name, "Pok\xC3\xA9mon"),
		  "&eacute; decodes to UTF-8");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = \"A &bogus; B\"\n", &c) == 1 && streq(c.cheats[0].name, "A &bogus; B"),
		  "unknown entity kept as is");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = \"&quot;&quot;&quot;\"\n", &c) == 1 && streq(c.cheats[0].name, "\"\"\""),
		  "entities never grow the output");
	CheatParse_free(&c);

	// 3. an unterminated quote is rejected: the cheat falls back to its number
	CHECK(parse("cheats = 1\ncheat0_desc = \"open\ncheat0_code = \"12\n", &c) == 1 &&
			  streq(c.cheats[0].name, "Cheat 1") && !c.cheats[0].code,
		  "unterminated desc and code rejected");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = unquoted\n", &c) == 1 && streq(c.cheats[0].name, "Cheat 1"),
		  "unquoted desc rejected");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = \"\"\n", &c) == 1 && streq(c.cheats[0].name, "Cheat 1"),
		  "empty desc gets the numbered name");
	CheatParse_free(&c);

	// 4. long lines: database codes run to 15,000 chars
	{
		size_t n = 16000;
		char* code = malloc(n + 1);
		char* text = malloc(n + 64);
		memset(code, 'A', n);
		code[n] = '\0';
		snprintf(text, n + 64, "cheats = 1\ncheat0_code = \"%s\"\n", code);
		CHECK(parse(text, &c) == 1 && c.cheats[0].code && strlen(c.cheats[0].code) == n, "16,000-char code parses");
		CheatParse_free(&c);
		free(code);
		free(text);
	}

	// 5. a long description is truncated for the name and kept whole in info
	CHECK(parse("cheats = 1\ncheat0_desc = \"Infinite health for player one and two\"\n", &c) == 1 &&
			  strlen(c.cheats[0].name) == CHEAT_MAX_DESC_LEN - 1 &&
			  !strcmp(c.cheats[0].name + CHEAT_MAX_DESC_LEN - 4, "...") && c.cheats[0].info &&
			  !strncmp(c.cheats[0].info, "Infinite health", 15),
		  "long desc: name truncated, info kept");
	CheatParse_free(&c);

	// 6. indexes out of range are ignored
	CHECK(parse("cheats = 2\ncheat5_desc = \"X\"\ncheat-1_desc = \"Y\"\ncheat1_desc = \"B\"\n", &c) == 1 &&
			  streq(c.cheats[0].name, "Cheat 1") && streq(c.cheats[1].name, "B"),
		  "index >= count and negative index ignored");
	CheatParse_free(&c);

	// 7. duplicate keys replace the previous value (ASan: no leak)
	CHECK(parse("cheats = 1\ncheat0_desc = \"First\"\ncheat0_desc = \"Second\"\n"
				"cheat0_code = \"1\"\ncheat0_code = \"2\"\n",
				&c) == 1 &&
			  streq(c.cheats[0].name, "Second") && streq(c.cheats[0].code, "2"),
		  "duplicate desc/code: last wins");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc = \"An extremely long first description here\"\ncheat0_desc = \"Short\"\n",
				&c) == 1 &&
			  streq(c.cheats[0].name, "Short") && !c.cheats[0].info,
		  "duplicate desc drops the old info");
	CheatParse_free(&c);

	// 8. enable: only the unquoted form (ours) turns a cheat on
	CHECK(parse("cheats = 3\ncheat0_enable = true\ncheat1_enable = \"true\"\ncheat2_enable = false\n", &c) == 1 &&
			  c.cheats[0].enabled == 1 && c.cheats[1].enabled == 0 && c.cheats[2].enabled == 0,
		  "enable = true on, quoted \"true\" ignored");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_enable = maybe\n", &c) == 1 && c.cheats[0].enabled == 0, "bad enable ignored");
	CheatParse_free(&c);

	// 9. RAM-patch fields land in the right CheatMemPatch member
	CHECK(parse("cheats = 1\n"
				"cheat0_handler = \"1\"\n"
				"cheat0_address = \"0x7E0DBE\"\n"
				"cheat0_value = 7\n"
				"cheat0_memory_search_size = \"3\"\n"
				"cheat0_cheat_type = \"1\"\n"
				"cheat0_address_bit_position = \"255\"\n"
				"cheat0_repeat_count = \"4\"\n"
				"cheat0_repeat_add_to_value = \"2\"\n"
				"cheat0_repeat_add_to_address = \"8\"\n"
				"cheat0_big_endian = \"true\"\n",
				&c) == 1 &&
			  c.cheats[0].mem.memory == 1 && c.cheats[0].mem.address == 0x7E0DBE && c.cheats[0].mem.value == 7 &&
			  c.cheats[0].mem.size == 3 && c.cheats[0].mem.type == 1 && c.cheats[0].mem.bit_mask == 255 &&
			  c.cheats[0].mem.repeat_count == 4 && c.cheats[0].mem.repeat_add_to_value == 2 &&
			  c.cheats[0].mem.repeat_add_to_address == 8 && c.cheats[0].mem.big_endian == 1,
		  "RAM-patch fields parsed");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_handler = \"0\"\ncheat0_value = \"x\"\n", &c) == 1 && c.cheats[0].mem.memory == 0 &&
			  c.cheats[0].mem.value == 0 && c.cheats[0].mem.repeat_count == 1 &&
			  c.cheats[0].mem.repeat_add_to_address == 1,
		  "handler 0 is not a RAM patch, defaults kept");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_valuex = 9\ncheat0_value_ = 9\n", &c) == 1 && c.cheats[0].mem.value == 0,
		  "a key with a longer name is not the field");
	CheatParse_free(&c);
	CHECK(parse("cheats = 1\ncheat0_desc\n", &c) == 1 && streq(c.cheats[0].name, "Cheat 1"), "key without value");
	CheatParse_free(&c);

	// 10. caseFoldPattern
	{
		char out[64];
		CheatParse_caseFoldPattern("Of 1", out, sizeof(out));
		CHECK(!strcmp(out, "[oO][fF] 1"), "letters fold, others stay");
		CheatParse_caseFoldPattern("a[b]*?\\", out, sizeof(out));
		CHECK(!strcmp(out, "[aA]\\[[bB]\\]\\*\\?\\\\"), "glob metacharacters escaped");
		CheatParse_caseFoldPattern("abc", out, 9);
		CHECK(!strcmp(out, "[aA][bB]"), "truncated at a whole piece");
		CheatParse_caseFoldPattern("abc", out, 8);
		CHECK(!strcmp(out, "[aA]"), "never a cut piece");
		CheatParse_caseFoldPattern("abc", out, 1);
		CHECK(out[0] == '\0', "room for the terminator only");
		CheatParse_caseFoldPattern("", out, sizeof(out));
		CHECK(out[0] == '\0', "empty name");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
