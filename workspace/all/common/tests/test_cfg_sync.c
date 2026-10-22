// Host test for config.c CFG_sync: the settings-file merge keeps every known
// key (no fixed line-table cap: one used to stop saving keys past line 96),
// keeps unknown lines verbatim and collapses duplicate known keys.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"

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

static char dir[] = "/tmp/nx_test_cfg_sync_XXXXXX";
static char path[600];

static char* slurp(void) {
	FILE* f = fopen(path, "r");
	if (!f)
		return NULL;
	static char buf[65536];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static int count_lines(const char* s) {
	int n = 0;
	for (; *s; s++)
		if (*s == '\n')
			n++;
	return n;
}

static int count_key(const char* s, const char* key) {
	int n = 0;
	size_t kl = strlen(key);
	for (const char* p = s; p && *p;) {
		if (strncmp(p, key, kl) == 0)
			n++;
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return n;
}

int main(void) {
	if (!mkdtemp(dir)) {
		printf("FAIL: mkdtemp\n");
		return 1;
	}
	setenv("SHARED_USERDATA_PATH", dir, 1);
	snprintf(path, sizeof(path), "%s/minuisettings.txt", dir);

	// fresh file: the canonical serialization, one key per line
	CFG_setShowClock(true);
	char* fresh = slurp();
	CHECK(fresh != NULL, "fresh settings file written");
	if (!fresh)
		return 1;
	char* canon = strdup(fresh);
	int keys = count_lines(canon);
	printf("serialized keys: %d\n", keys);
	CHECK(keys > 50, "serialization emits every setting");
	CHECK(count_key(canon, "showclock=1") == 1, "changed value written");
	CHECK(count_key(canon, "hintlabels=") == 1, "last serialized key present");

	// a file with many foreign keys around ours (other tools park keys here)
	// and a duplicate of a known key
	FILE* f = fopen(path, "w");
	for (int i = 0; i < 150; i++)
		fprintf(f, "foreign_key_%d=%d\n", i, i);
	fputs("showclock=0\n", f);
	fputs("showclock=0\n", f);
	fclose(f);
	CFG_setShowClock(false);
	CFG_setShowClock(true); // sync with a real change
	char* merged = slurp();
	CHECK(count_key(merged, "foreign_key_") == 150, "unknown lines kept");
	CHECK(count_key(merged, "foreign_key_149=149") == 1, "unknown values kept verbatim");
	CHECK(count_key(merged, "showclock=") == 1 && count_key(merged, "showclock=1") == 1,
		  "duplicate known key collapsed to the current value");
	CHECK(count_lines(merged) == 150 + keys, "every known key merged in, none dropped");
	// every canonical line is present in the merge
	int missing = 0;
	for (char* line = canon; *line;) {
		char* nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) + 1 : strlen(line);
		char want[256];
		snprintf(want, sizeof(want), "%.*s", (int)len, line);
		if (!strstr(merged, want))
			missing++;
		line += len;
	}
	CHECK(missing == 0, "all canonical lines in the merged file");

	free(canon);
	unlink(path);
	rmdir(dir);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
