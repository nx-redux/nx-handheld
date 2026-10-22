// Host test for xtras_meta.c (Xtras catalog meta.txt, ordering, progress hints). Run by
// tests/run_tests.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xtras_meta.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (!(cond)) {                 \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static bool parse_text(const char* text, AddonEntry* e) {
	char path[] = "/tmp/xtras_meta_XXXXXX";
	int fd = mkstemp(path);
	FILE* f = fdopen(fd, "w");
	fputs(text, f);
	fclose(f);
	memset(e, 0, sizeof(*e));
	bool ok = xtras_meta_parse(path, e);
	unlink(path);
	return ok;
}

static void test_meta(void) {
	AddonEntry e;
	CHECK(parse_text("name=Celeste Classic\ncategory=GAME\ndesc=A=B climb\nrepo=o/r\nsize_mb=12\n"
					 "done_msg=Find it in Emus.\nplatforms=tg5040 tg5050\nversion=1.2\n"
					 "version_source=internal\nasset=x.zip\nfuture_key=1\n=novalue\nno equals\n",
					 &e),
		  "meta: parses");
	CHECK(!strcmp(e.name, "Celeste Classic") && !strcmp(e.category, "GAME"), "meta: name, category");
	CHECK(!strcmp(e.desc, "A=B climb"), "meta: value keeps later '='");
	CHECK(!strcmp(e.repo, "o/r") && e.size_mb == 12, "meta: repo, size_mb");
	CHECK(!strcmp(e.done_msg, "Find it in Emus.") && !strcmp(e.platforms, "tg5040 tg5050"),
		  "meta: done_msg, platforms");
	CHECK(!strcmp(e.version, "1.2") && e.version_internal, "meta: internal version");

	CHECK(parse_text("name=T\ncategory=TOOL\nversion_source=upstream", &e) && !e.version_internal,
		  "meta: last line without newline, upstream version");
	CHECK(!parse_text("name=No category\n", &e), "meta: missing category fails");
	CHECK(!parse_text("category=GAME\n", &e), "meta: missing name fails");
	memset(&e, 0, sizeof(e));
	CHECK(!xtras_meta_parse("/nonexistent/meta.txt", &e), "meta: missing file fails");

	// desc longer than the field is cut, not overflowed
	char text[1200];
	char longdesc[1100];
	memset(longdesc, 'd', sizeof(longdesc) - 1);
	longdesc[sizeof(longdesc) - 1] = '\0';
	snprintf(text, sizeof(text), "name=N\ncategory=GAME\ndesc=%s\n", longdesc);
	CHECK(parse_text(text, &e) && strlen(e.desc) == sizeof(e.desc) - 1, "meta: long desc truncated");
	char longname[400];
	memset(longname, 'n', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	snprintf(text, sizeof(text), "name=%s\ncategory=GAMEGAMEGAMEGAMEGAME\n", longname);
	CHECK(parse_text(text, &e) && strlen(e.name) == META_STR - 1 && strlen(e.category) == sizeof(e.category) - 1,
		  "meta: long name and category truncated");
}

static AddonEntry entry(const char* cat, const char* name) {
	AddonEntry e;
	memset(&e, 0, sizeof(e));
	snprintf(e.category, sizeof(e.category), "%s", cat);
	snprintf(e.name, sizeof(e.name), "%s", name);
	return e;
}

static void test_order(void) {
	AddonEntry a[4] = {entry("TOOL", "alpha"), entry("GAME", "zelda"), entry("GAME", "Apotris"),
					   entry("TOOL", "Beta")};
	qsort(a, 4, sizeof(a[0]), xtras_entry_cmp);
	CHECK(!strcmp(a[0].name, "Apotris") && !strcmp(a[1].name, "zelda"), "order: games first, by name");
	CHECK(!strcmp(a[2].name, "alpha") && !strcmp(a[3].name, "Beta"), "order: tools, case-insensitive");
}

static void test_hint(void) {
	int pct = -1;
	const char* status = NULL;
	const char* stripped = (const char*)1;
	CHECK(xtras_parse_hint("@42 Downloading", &pct, &status, &stripped), "hint: well-formed");
	CHECK(pct == 42 && !strcmp(status, "Downloading") && stripped == NULL, "hint: pct and status");
	CHECK(xtras_parse_hint("@0 Start", &pct, &status, &stripped) && pct == 0, "hint: zero");
	CHECK(xtras_parse_hint("@250 Done", &pct, &status, &stripped) && pct == 100, "hint: clamped to 100");
	CHECK(xtras_parse_hint("@99999999999999999999 x", &pct, &status, &stripped) && pct == 100,
		  "hint: overflowing number clamped");

	CHECK(!xtras_parse_hint("plain text", &pct, &status, &stripped) && stripped == NULL, "hint: ordinary text");
	CHECK(!xtras_parse_hint("@user mention", &pct, &status, &stripped) && stripped == NULL, "hint: @word");
	CHECK(!xtras_parse_hint("@-5 neg", &pct, &status, &stripped) && stripped == NULL, "hint: negative");
	CHECK(!xtras_parse_hint("@50", &pct, &status, &stripped) && stripped && !strcmp(stripped, ""),
		  "hint: no text -> empty remainder");
	CHECK(!xtras_parse_hint("@50 ", &pct, &status, &stripped) && stripped && !strcmp(stripped, ""),
		  "hint: space without text -> empty remainder");
	CHECK(!xtras_parse_hint("@50%done", &pct, &status, &stripped) && stripped && !strcmp(stripped, "%done"),
		  "hint: no space -> remainder after the number");
	CHECK(!xtras_parse_hint("", &pct, &status, &stripped) && stripped == NULL, "hint: empty line");
}

int main(void) {
	test_meta();
	test_order();
	test_hint();
	if (fails) {
		printf("test_xtras_meta: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_xtras_meta: all tests passed\n");
	return 0;
}
