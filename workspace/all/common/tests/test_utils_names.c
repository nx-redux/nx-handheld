// Host test for the name and path helpers in utils.c: cleanName/removeParentheses, replaceString2, getDisplayName,
// getEmuName, urlEncode, json_extract_string, pathRelativeTo, the M3U helpers, the time formatters and the Simple
// Mode PIN. Built with -Isdroot (the card at /tmp/nx_test_sdroot) and AddressSanitizer by run_tests.sh.
#include "../utils.h"
#include "../defines.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void sh(const char* cmd) {
	assert(system(cmd) == 0);
}

static void put(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

static void test_clean_name(void) {
	char out[STR_MAX];
	cleanName(out, "001. Game_Name (USA) [!].gba");
	assert(strcmp(out, "Game Name") == 0);
	cleanName(out, "(Demo).zip");
	assert(strcmp(out, "") == 0);
	cleanName(out, "Mr. Driller.gba");
	assert(strcmp(out, "Mr. Driller") == 0);

	// Regression: a name of STR_MAX+ chars without brackets wrote the NUL one past the 256-byte temp (ASan).
	char longname[400];
	memset(longname, 'a', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	removeParentheses(out, longname);
	assert(strlen(out) == STR_MAX - 2); // trimString keeps len - 1 of the STR_MAX - 1 it is given
	removeParentheses(out, "Game [b] (USA) Extra");
	assert(strcmp(out, "Game   Extra") == 0);
}

static void test_replace_string2(void) {
	char* r = replaceString2("a-b-c", "-", "+");
	assert(strcmp(r, "a+b+c") == 0);
	free(r);
	r = replaceString2("aXYbXY", "XY", "123");
	assert(strcmp(r, "a123b123") == 0);
	free(r);
	r = replaceString2("a--b", "-", NULL);
	assert(strcmp(r, "ab") == 0);
	free(r);
	r = replaceString2("abc", "x", "y");
	assert(strcmp(r, "abc") == 0);
	free(r);
	assert(replaceString2("abc", "", "y") == NULL);
	assert(replaceString2(NULL, "a", "y") == NULL);
}

static void test_display_name(void) {
	char out[MAX_PATH];
	getDisplayName("/mnt/SDCARD/Roms/Pico-8 (P8)/Celeste.p8.png", out);
	assert(strcmp(out, "Celeste") == 0);
	getDisplayName("Game (USA) [b].zip", out);
	assert(strcmp(out, "Game") == 0);
	getDisplayName("Game [b] (USA).zip", out);
	assert(strcmp(out, "Game") == 0);
	getDisplayName("(USA).gba", out); // a name that is all brackets is kept
	assert(strcmp(out, "(USA)") == 0);
	getDisplayName("Game (USA", out); // unbalanced
	assert(strcmp(out, "Game") == 0);
	getDisplayName("Doom.wad.doom", out); // 4-letter extension
	assert(strcmp(out, "Doom") == 0);
	getDisplayName("Archive.tar.gzipped", out); // a 7-letter "extension" is part of the name
	assert(strcmp(out, "Archive.tar.gzipped") == 0);
	getDisplayName("", out);
	assert(strcmp(out, "") == 0);
	// a dot inside the title is not an extension
	getDisplayName("Super Mario Bros. 3.nes", out);
	assert(strcmp(out, "Super Mario Bros. 3") == 0);
	getDisplayName("Super Mario Bros. 3 (USA).nes", out);
	assert(strcmp(out, "Super Mario Bros. 3") == 0);
	getDisplayName("Ys Vol. 2.pce", out);
	assert(strcmp(out, "Ys Vol. 2") == 0);
	getDisplayName("Game 1.11.zip", out);
	assert(strcmp(out, "Game 1.11") == 0);
	getDisplayName("Dr. Mario.gb", out);
	assert(strcmp(out, "Dr. Mario") == 0);
	getDisplayName("Knuckles.32x", out);
	assert(strcmp(out, "Knuckles") == 0);
	getDisplayName("Pack.7z", out);
	assert(strcmp(out, "Pack") == 0);
}

static void test_file_extension(void) {
	assert(isFileExtension(".nes"));
	assert(isFileExtension(".p8"));
	assert(isFileExtension(".7z"));
	assert(isFileExtension(".doom"));
	assert(!isFileExtension(".c")); // one character
	assert(!isFileExtension(".gzipped"));
	assert(!isFileExtension(". 3"));
	assert(!isFileExtension(".11"));
	assert(!isFileExtension(".a-b"));
	assert(!isFileExtension("nes"));
}

static void test_emu_name(void) {
	char out[MAX_PATH];
	getEmuName(ROMS_PATH "/Game Boy Advance (GBA)/Sub/Game.gba", out);
	assert(strcmp(out, "GBA") == 0);
	getEmuName(ROMS_PATH "/Ports/Game.sh", out);
	assert(strcmp(out, "Ports") == 0);
	getEmuName("Sega Genesis (MD", out); // unbalanced '(' keeps the rest
	assert(strcmp(out, "MD") == 0);
	getEmuName("GBA", out);
	assert(strcmp(out, "GBA") == 0);
}

static void test_url_encode(void) {
	char out[64];
	urlEncode("a b&c/\xC3\xA9-_.~", out, sizeof(out));
	assert(strcmp(out, "a+b%26c%2F%C3%A9-_.~") == 0);
	// a tight buffer never splits a %XX escape
	char small[8];
	urlEncode("%%%%%", small, sizeof(small));
	assert(strcmp(small, "%25%25") == 0);
	urlEncode("%%%%%", small, 7);
	assert(strcmp(small, "%25") == 0);
	urlEncode("abcdefgh", small, sizeof(small));
	assert(strcmp(small, "abcd") == 0);
}

static void test_json_extract(void) {
	const char* json = "{\"Success\":true,\"Token\":\"abc123\",\"User\": \"Bob\",\"Bad\":\"open";
	char out[32];
	assert(json_extract_string(json, "Token", out, sizeof(out)) == out && strcmp(out, "abc123") == 0);
	assert(json_extract_string(json, "User", out, sizeof(out)) && strcmp(out, "Bob") == 0);
	assert(json_extract_string(json, "Token", out, 3) && strcmp(out, "ab") == 0);
	assert(json_extract_string(json, "Missing", out, sizeof(out)) == NULL);
	assert(json_extract_string(json, "Bad", out, sizeof(out)) == NULL);		// unterminated value
	assert(json_extract_string(json, "Success", out, sizeof(out)) == NULL); // not a string
	assert(json_extract_string(NULL, "Token", out, sizeof(out)) == NULL);
}

static void test_path_relative(void) {
	sh("rm -rf " SDCARD_PATH "/rel && mkdir -p " SDCARD_PATH "/rel/a/bc/sub " SDCARD_PATH "/rel/a/bd");
	put(SDCARD_PATH "/rel/a/bd/x", "");
	put(SDCARD_PATH "/rel/a/f", "");
	put(SDCARD_PATH "/rel/a/bc/sub/g", "");
	char out[MAX_PATH];
	assert(pathRelativeTo(out, SDCARD_PATH "/rel/a", SDCARD_PATH "/rel/a/f"));
	assert(strcmp(out, "f") == 0);
	assert(pathRelativeTo(out, SDCARD_PATH "/rel/a/bc", SDCARD_PATH "/rel/a/bc/sub/g"));
	assert(strcmp(out, "sub/g") == 0);
	assert(pathRelativeTo(out, SDCARD_PATH "/rel/a/bc/sub", SDCARD_PATH "/rel/a/f"));
	assert(strcmp(out, "../../f") == 0);
	// Regression: siblings sharing a name prefix ("bc" vs "bd") were split mid-segment into "../d/x".
	assert(pathRelativeTo(out, SDCARD_PATH "/rel/a/bc", SDCARD_PATH "/rel/a/bd/x"));
	assert(strcmp(out, "../bd/x") == 0);
	assert(!pathRelativeTo(out, SDCARD_PATH "/rel/nope", SDCARD_PATH "/rel/a/f"));
	assert(out[0] == '\0');

	// Regression: realpath() wrote into MAX_PATH buffers, and a result longer than
	// MAX_PATH overflowed path_out. A 700-byte real path must fail cleanly instead.
	char deep[2048];
	int n = snprintf(deep, sizeof(deep), SDCARD_PATH "/rel/a");
	for (int i = 0; i < 7; i++)
		n += snprintf(deep + n, sizeof(deep) - n, "/%s", "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
	char cmd[2200];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", deep);
	sh(cmd);
	snprintf(deep + n, sizeof(deep) - n, "/f");
	put(deep, "");
	assert(!pathRelativeTo(out, SDCARD_PATH "/rel/a/bc", deep));
	assert(out[0] == '\0');
}

static bool collect(const char* disc_path, int index, void* ctx) {
	char (*seen)[MAX_PATH] = ctx;
	snprintf(seen[index], MAX_PATH, "%s", disc_path);
	return index < 1; // stop after the second disc
}

static bool stop_first(const char* disc_path, int index, void* ctx) {
	(void)disc_path;
	(void)index;
	(*(int*)ctx)++;
	return false;
}

static void test_m3u(void) {
#define GAME SDCARD_PATH "/Roms/PlayStation (PS)/Game"
	sh("rm -rf " SDCARD_PATH "/Roms && mkdir -p '" GAME "'");
	put(GAME "/Game.m3u", "Game (Disc 1).cue\r\n\r\nmissing.cue\nGame (Disc 2).cue\nGame (Disc 3).cue");
	put(GAME "/Game (Disc 1).cue", "");
	put(GAME "/Game (Disc 2).cue", "");
	put(GAME "/Game (Disc 3).cue", "");
	char m3u[MAX_PATH];
	assert(M3U_findForRom(GAME "/Game (Disc 2).cue", m3u, sizeof(m3u)));
	assert(strcmp(m3u, GAME "/Game.m3u") == 0);
	assert(!M3U_findForRom(SDCARD_PATH "/Roms/PlayStation (PS)/Loose.cue", m3u, sizeof(m3u)));
	assert(!M3U_findForRom("nofolder.cue", m3u, sizeof(m3u)));

	char seen[3][MAX_PATH] = {{0}};
	// CRLF and blank lines are skipped, a missing disc is left out, and the callback's false stops the walk
	assert(M3U_forEachDisc(GAME "/Game.m3u", collect, seen) == 2);
	assert(strcmp(seen[0], GAME "/Game (Disc 1).cue") == 0);
	assert(strcmp(seen[1], GAME "/Game (Disc 2).cue") == 0);
	assert(seen[2][0] == '\0');
	int calls = 0;
	assert(M3U_forEachDisc(GAME "/Game.m3u", stop_first, &calls) == 1 && calls == 1);
	assert(M3U_forEachDisc(GAME "/none.m3u", stop_first, &calls) == 0);
#undef GAME
}

static void test_time(void) {
	char buf[32];
	serializeTime(buf, 0);
	assert(strcmp(buf, "0s") == 0);
	serializeTime(buf, 59);
	assert(strcmp(buf, "59s") == 0);
	serializeTime(buf, 60);
	assert(strcmp(buf, "1m 0s") == 0);
	serializeTime(buf, 3599);
	assert(strcmp(buf, "59m 59s") == 0);
	serializeTime(buf, 3600);
	assert(strcmp(buf, "1h 0m") == 0);
	serializeTime(buf, 7322);
	assert(strcmp(buf, "2h 2m") == 0);
	format_time(buf, 0);
	assert(strcmp(buf, "00:00") == 0);
	format_time(buf, 59);
	assert(strcmp(buf, "00:59") == 0);
	format_time(buf, 3599);
	assert(strcmp(buf, "59:59") == 0);
	format_time(buf, 3661);
	assert(strcmp(buf, "1:01:01") == 0);
}

static void test_simple_mode_pin(void) {
	sh("mkdir -p " SHARED_USERDATA_PATH);
	char pin[5] = "xxxx";
	unlink(SIMPLE_MODE_PATH);
	assert(!SimpleMode_readPin(pin));
	put(SIMPLE_MODE_PATH, "1234\r\n");
	assert(SimpleMode_readPin(pin) && strcmp(pin, "1234") == 0);
	assert(SimpleMode_readPin(NULL));
	put(SIMPLE_MODE_PATH, "0987 \t");
	assert(SimpleMode_readPin(pin) && strcmp(pin, "0987") == 0);
	const char* bad[] = {"123", "12a4", "12345", " 1234", "", "12 4"};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		put(SIMPLE_MODE_PATH, bad[i]);
		assert(!SimpleMode_readPin(pin));
	}
	unlink(SIMPLE_MODE_PATH);
}

static void test_media_art_path(void) {
	char out[MAX_PATH];
	ROM_mediaArtVariantPath("/Roms/GBA/Game.gba", "screenshot", out, sizeof(out));
	assert(strcmp(out, "/Roms/GBA/.media/screenshot/Game.png") == 0);
	ROM_mediaArtVariantPath("/Roms/GBA/Game.gba", NULL, out, sizeof(out));
	assert(strcmp(out, "/Roms/GBA/.media/Game.png") == 0);
	// a path too long for MAX_PATH once ".media/<variant>/" is added gives no path, never a cut one
	char rom[MAX_PATH];
	memset(rom, 'a', sizeof(rom) - 1);
	rom[0] = '/';
	rom[10] = '/';
	rom[sizeof(rom) - 1] = '\0';
	ROM_mediaArtVariantPath(rom, "screenshot", out, sizeof(out));
	assert(out[0] == '\0');
	ROM_mediaArtPath(rom, out, sizeof(out));
	assert(out[0] == '\0');
}

int main(void) {
	sh("rm -rf " SDCARD_PATH " && mkdir -p " SDCARD_PATH);
	test_clean_name();
	test_replace_string2();
	test_display_name();
	test_file_extension();
	test_media_art_path();
	test_emu_name();
	test_url_encode();
	test_json_extract();
	test_path_relative();
	test_m3u();
	test_time();
	test_simple_mode_pin();
	sh("rm -rf " SDCARD_PATH);
	printf("test_utils_names: OK\n");
	return 0;
}
