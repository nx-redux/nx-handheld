// Host test for wiz_sync_util.c: the wizard save sync's validators, rsync progress parsing and
// all-or-nothing staging directory. Run by tests/run_tests.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wiz_sync_util.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (!(cond)) {                 \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static void test_validators(void) {
	CHECK(wiz_conf_value_is_safe("/mnt/SDCARD/Saves/DC"), "conf: plain value");
	CHECK(!wiz_conf_value_is_safe("x\nhosts allow = *") && !wiz_conf_value_is_safe("x\r"), "conf: CR/LF refused");
	CHECK(!wiz_conf_value_is_safe("") && !wiz_conf_value_is_safe(NULL), "conf: empty/NULL refused");

	CHECK(wiz_ip_is_plain("192.168.1.20") && wiz_ip_is_plain("10.0.0.1"), "ip: dotted quads");
	CHECK(!wiz_ip_is_plain("*") && !wiz_ip_is_plain("host.lan") && !wiz_ip_is_plain("10.0.0.0/8") &&
			  !wiz_ip_is_plain("1.2.3.4 5.6.7.8"),
		  "ip: patterns/names/ranges refused");
	CHECK(!wiz_ip_is_plain("...") && !wiz_ip_is_plain("") && !wiz_ip_is_plain(NULL), "ip: no digits/empty/NULL");
	CHECK(!wiz_ip_is_plain("1234567890123456"), "ip: 16 chars refused");

	CHECK(wiz_sync_name_is_safe("vmu_save_A1.bin"), "sync name: VMU save");
	CHECK(!wiz_sync_name_is_safe("..") && !wiz_sync_name_is_safe(".") && !wiz_sync_name_is_safe("-e") &&
			  !wiz_sync_name_is_safe("a/b") && !wiz_sync_name_is_safe("a b") && !wiz_sync_name_is_safe("") &&
			  !wiz_sync_name_is_safe(NULL),
		  "sync name: traversal/option/slash/space/empty refused");
	char n64[65];
	memset(n64, 'a', 64);
	n64[64] = '\0';
	CHECK(!wiz_sync_name_is_safe(n64), "sync name: 64 chars refused");

	char names[4][WIZ_SYNC_NAME_MAX] = {"a.bin", "b.bin", "c.bin", "b.bin"};
	CHECK(wiz_find_duplicate(names, 4) == 3, "duplicate: index of the repeat");
	CHECK(wiz_find_duplicate(names, 3) == -1, "duplicate: a set");
	CHECK(wiz_find_duplicate(names, 0) == -1 && wiz_find_duplicate(names, 1) == -1, "duplicate: empty/one");
}

static void test_progress(void) {
	CHECK(wiz_progress_percent("    131,072 100%   12.50MB/s    0:00:00 (xfr#1, to-chk=0/1)") == 100,
		  "percent: progress2 line");
	CHECK(wiz_progress_percent("     32,768  25%    1.00MB/s    0:00:03") == 25, "percent: partial");
	CHECK(wiz_progress_percent("sending incremental file list") == -1, "percent: banner");
	CHECK(wiz_progress_percent("50 % done") == -1, "percent: no number right before %");
	CHECK(wiz_progress_percent("% 40%") == 40, "percent: first % with digits");
	CHECK(wiz_progress_percent("250%") == 100, "percent: clamped to 100");
	CHECK(wiz_progress_percent("") == -1, "percent: empty");

	char buf[32];
	size_t len;
	strcpy(buf, " 10%\r 20%\r 3");
	len = strlen(buf);
	CHECK(wiz_take_percent(buf, sizeof(buf), &len, -1) == 20, "take: newest complete line, \\r terminated");
	CHECK(len == 2 && !memcmp(buf, " 3", 2), "take: tail kept at the front");
	memcpy(buf + len, "0%\n", 3);
	len += 3;
	CHECK(wiz_take_percent(buf, sizeof(buf), &len, 20) == 30, "take: tail completed by \\n");
	CHECK(len == 0, "take: nothing left");

	strcpy(buf, "banner\n");
	len = strlen(buf);
	CHECK(wiz_take_percent(buf, sizeof(buf), &len, 7) == 7, "take: no percent keeps current");

	memset(buf, 'x', sizeof(buf));
	len = sizeof(buf);
	CHECK(wiz_take_percent(buf, sizeof(buf), &len, 5) == 5 && len == 0, "take: full buffer without terminator dropped");

	strcpy(buf, "abc");
	len = 3;
	CHECK(wiz_take_percent(buf, sizeof(buf), &len, 1) == 1 && len == 3, "take: partial line waits");
}

static void put(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

static int has(const char* path, const char* text) {
	char buf[64] = {0};
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	return strcmp(buf, text) == 0;
}

static void test_staging(void) {
	char root[] = "/tmp/wiz_stage_XXXXXX";
	if (!mkdtemp(root)) {
		CHECK(0, "mkdtemp");
		return;
	}
	char stage[512], p[600];
	wiz_stage_path(stage, sizeof(stage), root);
	CHECK(strstr(stage, "/" WIZ_SYNC_STAGE_DIR) != NULL, "stage: path inside fetch_to");

	// a leftover staging dir from a killed run is purged
	mkdir(stage, 0755);
	snprintf(p, sizeof(p), "%s/stale.bin", stage);
	put(p, "stale");
	CHECK(wiz_stage_prepare(root) == 0, "stage: prepare over a leftover");
	CHECK(access(p, F_OK) != 0, "stage: leftover file purged");

	// the existing local save stays until commit
	snprintf(p, sizeof(p), "%s/a.bin", root);
	put(p, "old-a");
	char sp[600];
	snprintf(sp, sizeof(sp), "%s/a.bin", stage);
	put(sp, "new-a");
	snprintf(sp, sizeof(sp), "%s/b.bin", stage);
	put(sp, "new-b");
	CHECK(has(p, "old-a"), "stage: fetch_to untouched before commit");
	char names[2][WIZ_SYNC_NAME_MAX] = {"a.bin", "b.bin"};
	CHECK(wiz_stage_commit(root, names, 2) == 0, "stage: commit");
	CHECK(has(p, "new-a"), "stage: commit replaced the old save");
	snprintf(p, sizeof(p), "%s/b.bin", root);
	CHECK(has(p, "new-b"), "stage: commit added the new save");
	wiz_stage_purge(root);
	CHECK(access(stage, F_OK) != 0, "stage: purge removes the dir");

	// a failed pull: purge leaves fetch_to as it was
	CHECK(wiz_stage_prepare(root) == 0, "stage: prepare again");
	snprintf(sp, sizeof(sp), "%s/a.bin", stage);
	put(sp, "half");
	wiz_stage_purge(root);
	snprintf(p, sizeof(p), "%s/a.bin", root);
	CHECK(has(p, "new-a"), "stage: failure leaves fetch_to untouched");

	// a name missing from staging fails the commit but the others still land
	CHECK(wiz_stage_prepare(root) == 0, "stage: prepare third");
	snprintf(sp, sizeof(sp), "%s/b.bin", stage);
	put(sp, "newer-b");
	CHECK(wiz_stage_commit(root, names, 2) == -1, "stage: missing name fails the commit");
	snprintf(p, sizeof(p), "%s/b.bin", root);
	CHECK(has(p, "newer-b"), "stage: best-effort rename of the rest");
	wiz_stage_purge(root);

	// no staging dir: commit fails
	CHECK(wiz_stage_commit(root, names, 2) == -1, "stage: commit without a staging dir");

	// a non-empty subdirectory in staging survives purge, so prepare fails closed
	mkdir(stage, 0755);
	snprintf(sp, sizeof(sp), "%s/sub", stage);
	mkdir(sp, 0755);
	char inner[700];
	snprintf(inner, sizeof(inner), "%s/x", sp);
	put(inner, "x");
	CHECK(wiz_stage_prepare(root) == -1, "stage: prepare fails closed on an unpurgeable dir");
	unlink(inner);
	rmdir(sp);
	rmdir(stage);

	snprintf(p, sizeof(p), "%s/a.bin", root);
	unlink(p);
	snprintf(p, sizeof(p), "%s/b.bin", root);
	unlink(p);
	CHECK(rmdir(root) == 0, "stage: nothing else left behind");
}

int main(void) {
	test_validators();
	test_progress();
	test_staging();
	if (fails) {
		printf("test_wiz_sync_util: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_wiz_sync_util: all tests passed\n");
	return 0;
}
