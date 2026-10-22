// Host test for the pure prefetch-scan and achievement-list helpers (ratools_scan.c,
// ratools_logic.c). Run by tests/run_tests.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "ratools_logic.h"
#include "ratools_scan.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (!(cond)) {                 \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static void test_dir_tag(void) {
	char tag[16];
	CHECK(rat_dir_tag("Game Boy (GB)", tag, sizeof(tag)) && !strcmp(tag, "GB"), "dir tag GB");
	CHECK(rat_dir_tag("01) Game Boy (GB)", tag, sizeof(tag)) && !strcmp(tag, "GB"), "dir tag uses the last (");
	CHECK(rat_dir_tag("Arcade (FBN) (MAME)", tag, sizeof(tag)) && !strcmp(tag, "MAME"), "dir tag last group");
	CHECK(!rat_dir_tag("Game Boy", tag, sizeof(tag)), "no tag");
	CHECK(!rat_dir_tag("Game Boy ()", tag, sizeof(tag)), "empty tag");
	CHECK(!rat_dir_tag("Game Boy (GB", tag, sizeof(tag)), "unclosed tag");
	char small[4];
	CHECK(rat_dir_tag("X (ABCDEF)", small, sizeof(small)) && !strcmp(small, "ABC"), "tag truncated to n");
}

static void test_extensions(void) {
	CHECK(rat_skip_extension("README"), "no extension skipped");
	CHECK(rat_skip_extension("game.SRM"), "srm skipped, any case");
	CHECK(rat_skip_extension("Track 02.raw") && rat_skip_extension("disc.sub"), "raw/sub skipped");
	CHECK(!rat_skip_extension("game.gba") && !rat_skip_extension("disc.bin"), "roms kept");
	CHECK(rat_is_track_file("Track 01.BIN") && rat_is_track_file("disc.img"), "bin/img are tracks");
	CHECK(!rat_is_track_file("disc.cue") && !rat_is_track_file("noext"), "cue/no ext not tracks");
	CHECK(rat_is_archive("a.ZIP") && rat_is_archive("a.7z"), "zip/7z archives");
	CHECK(!rat_is_archive("a.zipx") && !rat_is_archive("zip"), "not archives");
}

static void test_entry_is_rom(void) {
	CHECK(rat_entry_is_rom("Pokemon.gba", 256), "plain rom");
	CHECK(rat_entry_is_rom("sub/dir/Pokemon.gba", 256), "nested rom");
	CHECK(!rat_entry_is_rom("sub/dir/", 256), "directory");
	CHECK(!rat_entry_is_rom("", 256), "empty");
	CHECK(!rat_entry_is_rom("readme.txt", 256), "skipped extension");
	CHECK(!rat_entry_is_rom("inner.zip", 256), "nested archive");
	CHECK(!rat_entry_is_rom("a.zip/rom.7z", 256), "nested archive by base name");
	CHECK(rat_entry_is_rom("dir.txt/rom.gba", 256), "only the base name's extension counts");
	CHECK(!rat_entry_is_rom("Pokemon.gba", 11), "path that does not fit n");
	CHECK(rat_entry_is_rom("Pokemon.gba", 12), "path that just fits n");
}

static FILE* listing(const char* text) {
	FILE* f = tmpfile();
	fputs(text, f);
	rewind(f);
	return f;
}

static void test_7z_listing(void) {
	char entry[256];
	long long size = -1;

	// a directory, a skipped file, then the rom as the last entry with no trailing blank line
	FILE* f = listing("Path = Game\nSize = 0\nAttributes = D_ -rwxr-xr-x\n\n"
					  "Path = Game/readme.txt\nSize = 10\nAttributes = A_ -rw-r--r--\n\n"
					  "Path = Game/Game (USA).gba\nSize = 16777216\nAttributes = A_ -rw-r--r--");
	CHECK(rat_7z_pick_entry(f, entry, sizeof(entry), &size), "7z: last entry without blank line");
	CHECK(!strcmp(entry, "Game/Game (USA).gba"), "7z: entry path");
	CHECK(size == 16777216, "7z: size");
	fclose(f);

	// the first rom wins; CRLF lines; Size of a skipped entry is not carried over
	f = listing("Path = a.nes\r\nSize = 40976\r\nAttributes = A\r\n\r\n"
				"Path = b.nes\r\nSize = 1\r\n\r\n");
	CHECK(rat_7z_pick_entry(f, entry, sizeof(entry), &size) && !strcmp(entry, "a.nes") && size == 40976,
		  "7z: first rom, CRLF");
	fclose(f);

	f = listing("Path = x.txt\nSize = 99\n\nPath = y.sfc\n\n");
	CHECK(rat_7z_pick_entry(f, entry, sizeof(entry), &size) && !strcmp(entry, "y.sfc") && size == 0,
		  "7z: size resets per entry");
	fclose(f);

	// a path too long for entry is skipped, not truncated
	f = listing("Path = a-very-long-name.gba\n\nPath = s.gba\n\n");
	CHECK(rat_7z_pick_entry(f, entry, 8, &size) && !strcmp(entry, "s.gba"), "7z: over-long path skipped");
	fclose(f);

	f = listing("Path = only.zip\n\nPath = notes.txt\n");
	CHECK(!rat_7z_pick_entry(f, entry, sizeof(entry), &size), "7z: no rom");
	fclose(f);

	f = listing("");
	CHECK(!rat_7z_pick_entry(f, entry, sizeof(entry), &size), "7z: empty listing");
	fclose(f);
}

static void launch_core(const char* script, char* out, size_t n) {
	char path[] = "/tmp/rat_launch_XXXXXX";
	int fd = mkstemp(path);
	FILE* f = fdopen(fd, "w");
	fputs(script, f);
	fclose(f);
	rat_launch_core(path, out, n);
	unlink(path);
}

static void test_launch_core(void) {
	char core[64];
	launch_core("#!/bin/sh\nEMU_EXE=gpsp\nminarch.elf \"$CORES_PATH/${EMU_EXE}_libretro.so\"\n", core,
				sizeof(core));
	CHECK(!strcmp(core, "gpsp"), "EMU_EXE plain");
	launch_core("  EMU_EXE=\"mgba\" ; foo\n", core, sizeof(core));
	CHECK(!strcmp(core, "mgba"), "EMU_EXE quoted, indented, ;");
	launch_core("EMU_EXE='pcsx_rearmed'\n", core, sizeof(core));
	CHECK(!strcmp(core, "pcsx_rearmed"), "EMU_EXE single-quoted");
	launch_core("EMU_EXE=$1\nminarch.elf \"/cores/fceumm_libretro.so\" \"$1\"\n", core, sizeof(core));
	CHECK(!strcmp(core, "fceumm"), "EMU_EXE with $ falls back to the .so name");
	launch_core("minarch.elf $CORES/${CORE}_libretro.so\n", core, sizeof(core));
	CHECK(!strcmp(core, ""), ".so name from a variable is ignored");
	launch_core("exec ./drastic \"$1\"\n", core, sizeof(core));
	CHECK(!strcmp(core, ""), "standalone emulator: empty");
	launch_core("EMU_EXE=snes9x2005_plus\n", core, 8);
	CHECK(!strcmp(core, ""), "EMU_EXE too long for out: empty");
	rat_launch_core("/nonexistent/launch.sh", core, sizeof(core));
	CHECK(!strcmp(core, ""), "missing script: empty");
}

static void test_resolve_unlock(void) {
	RAT_ServerUnlock srv[] = {{10, 1000}};
	RA_PendingUnlock pend[2] = {0}, conf[2] = {0};
	pend[0].achievement_id = 10;
	strcpy(pend[0].game_hash, "aaa");
	pend[0].when = 2000;
	pend[1].achievement_id = 20;
	strcpy(pend[1].game_hash, "bbb");
	pend[1].when = 2100;
	conf[0].achievement_id = 20;
	strcpy(conf[0].game_hash, "aaa");
	conf[0].when = 3000;
	conf[1].achievement_id = 30;
	strcpy(conf[1].game_hash, "aaa");
	conf[1].when = 3100;

	time_t when = 0;
	CHECK(rat_resolve_unlock(10, "aaa", srv, 1, pend, 2, conf, 2, &when) == RAT_ACH_UNLOCKED && when == 1000,
		  "server beats pending");
	when = 0;
	CHECK(rat_resolve_unlock(20, "bbb", srv, 1, pend, 2, conf, 2, &when) == RAT_ACH_PENDING && when == 2100,
		  "pending beats confirmed");
	when = 0;
	CHECK(rat_resolve_unlock(20, "aaa", srv, 1, pend, 2, conf, 2, &when) == RAT_ACH_UNLOCKED && when == 3000,
		  "hash filter: another game's pending is ignored, confirmed counts");
	when = 7;
	CHECK(rat_resolve_unlock(30, "bbb", srv, 1, pend, 2, conf, 2, &when) == RAT_ACH_LOCKED && when == 7,
		  "hash filter on confirmed; when untouched while locked");
	CHECK(rat_resolve_unlock(30, NULL, NULL, 0, pend, 2, conf, 2, &when) == RAT_ACH_UNLOCKED && when == 3100,
		  "NULL hash: no filter");
	CHECK(rat_server_find(srv, 1, 10, NULL), "server_find with NULL when");
}

static void trim(const char* in, size_t len, const char* want, const char* msg) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%s", in);
	buf[len] = '\0';
	rat_utf8_trim(buf, len);
	CHECK(!strcmp(buf, want), msg);
}

static void test_utf8_trim(void) {
	trim("abc", 3, "abc", "ascii untouched");
	trim("ab\xc3\xa9", 3, "ab", "2-byte cut after the lead");
	trim("ab\xc3\xa9", 4, "ab\xc3\xa9", "complete 2-byte kept");
	trim("a\xe2\x82\xac", 3, "a", "3-byte cut after 1 continuation");
	trim("a\xe2\x82\xac", 4, "a\xe2\x82\xac", "complete 3-byte kept");
	trim("a\xf0\x9f\x98\x80", 4, "a", "4-byte cut after 2 continuations");
	trim("a\xf0\x9f\x98\x80", 5, "a\xf0\x9f\x98\x80", "complete 4-byte kept");
	trim("ab\x80\x80", 4, "ab", "stray continuation bytes after ascii");
	trim("\x80\x80", 2, "", "only continuation bytes");
}

static RAT_Game game(const char* hash, uint32_t session, int unlocked, int pending) {
	RAT_Game g;
	memset(&g, 0, sizeof(g));
	snprintf(g.hash, sizeof(g.hash), "%s", hash);
	snprintf(g.title, sizeof(g.title), "%s", hash);
	g.session_game_id = session;
	g.unlocked = unlocked;
	g.pending = pending;
	return g;
}

static bool m3u_hash(const char* hash) {
	return strcmp(hash, "m3u") == 0;
}

static void test_merge_discs(void) {
	RAT_Game g[5] = {game("disc1", 7, 3, 0), game("other", 9, 1, 0), game("m3u", 7, 2, 0),
					 game("disc2", 7, 5, 2), game("solo", 11, 0, 0)};
	int n = rat_merge_discs(g, 5, m3u_hash);
	CHECK(n == 3, "merge: three games left");
	CHECK(!strcmp(g[0].hash, "m3u"), "merge: .m3u entry kept");
	CHECK(g[0].unlocked == 5 && g[0].pending == 2, "merge: highest unlocked and its pending carried");
	CHECK(!strcmp(g[1].hash, "other") && !strcmp(g[2].hash, "solo"), "merge: other games untouched, in order");

	RAT_Game h[2] = {game("d1", 4, 6, 1), game("d2", 4, 2, 2)};
	n = rat_merge_discs(h, 2, m3u_hash);
	CHECK(n == 1 && !strcmp(h[0].hash, "d1") && h[0].unlocked == 6 && h[0].pending == 1,
		  "merge: no m3u keeps the first, lower counts not carried");
	CHECK(rat_merge_discs(h, 0, m3u_hash) == 0, "merge: empty");
}

static RAT_Achievement ach(int order, uint32_t points, const char* title, RAT_AchState st, float rarity,
						   uint32_t type) {
	RAT_Achievement a;
	memset(&a, 0, sizeof(a));
	a.order = order;
	a.points = points;
	snprintf(a.title, sizeof(a.title), "%s", title);
	a.state = st;
	a.rarity = rarity;
	a.type = type;
	return a;
}

static void sorted(int order, const char* want, const char* msg) {
	RAT_Achievement a[3] = {ach(2, 5, "B", RAT_ACH_LOCKED, 50.0f, 1), ach(0, 25, "C", RAT_ACH_PENDING, 1.5f, 3),
							ach(1, 10, "A", RAT_ACH_UNLOCKED, 10.0f, 2)};
	rat_sort_achievements_by(a, 3, order);
	char got[4] = {a[0].title[0], a[1].title[0], a[2].title[0], 0};
	CHECK(!strcmp(got, want), msg);
}

static void test_sort(void) {
	sorted(RA_SORT_DISPLAY_ORDER_FIRST, "CAB", "sort: display order");
	sorted(RA_SORT_DISPLAY_ORDER_LAST, "BAC", "sort: display order reversed");
	sorted(RA_SORT_POINTS_MOST, "CAB", "sort: points most");
	sorted(RA_SORT_POINTS_LEAST, "BAC", "sort: points least");
	sorted(RA_SORT_TITLE_AZ, "ABC", "sort: title a-z");
	sorted(RA_SORT_TITLE_ZA, "CBA", "sort: title z-a");
	sorted(RA_SORT_WON_BY_MOST, "BAC", "sort: won by most");
	sorted(RA_SORT_WON_BY_LEAST, "CAB", "sort: won by least");
	sorted(RA_SORT_TYPE_ASC, "BAC", "sort: type asc");
	sorted(RA_SORT_TYPE_DESC, "CAB", "sort: type desc");
	sorted(-1, "BCA", "sort: unknown order leaves the list");
	RAT_Achievement a[3] = {ach(0, 0, "L", RAT_ACH_LOCKED, 0, 0), ach(1, 0, "P", RAT_ACH_PENDING, 0, 0),
							ach(2, 0, "U", RAT_ACH_UNLOCKED, 0, 0)};
	rat_sort_achievements_by(a, 3, RA_SORT_UNLOCKED_FIRST);
	CHECK(a[2].title[0] == 'L', "sort: unlocked first (pending counts as unlocked)");
}

int main(void) {
	test_dir_tag();
	test_extensions();
	test_entry_is_rom();
	test_7z_listing();
	test_launch_core();
	test_resolve_unlock();
	test_utf8_trim();
	test_merge_discs();
	test_sort();
	if (fails) {
		printf("test_ratools_scan: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_ratools_scan: all tests passed\n");
	return 0;
}
