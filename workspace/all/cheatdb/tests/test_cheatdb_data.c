#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cheatdb_data.h"

static int fails = 0;
#define CHECK(c, m)                  \
	do {                             \
		if (c)                       \
			printf("PASS: %s\n", m); \
		else {                       \
			printf("FAIL: %s\n", m); \
			fails++;                 \
		}                            \
	} while (0)

int main(void) {
	CheatdbPaths p;
	Cheatdb_initPaths(&p);

	CHECK(!Cheatdb_installed(&p), "not installed when no manifest");

	// Extract two mapped systems from the fixture zip (env CHEATDB_TEST_ZIP)
	// into their Cheats/<TAG> dirs, building the manifest from the archive index.
	Cheatdb_truncateManifest(&p);
	char dest[600];
	snprintf(dest, sizeof(dest), "%s/GBA", p.cheats_dir);
	Cheatdb_extractFolder(&p, "Nintendo - Game Boy Advance", dest);
	Cheatdb_appendManifest(&p, "Nintendo - Game Boy Advance", dest);
	snprintf(dest, sizeof(dest), "%s/MD", p.cheats_dir);
	Cheatdb_extractFolder(&p, "Sega - Mega Drive - Genesis", dest);
	Cheatdb_appendManifest(&p, "Sega - Mega Drive - Genesis", dest);

	CHECK(Cheatdb_installed(&p), "installed after extract");
	CHECK(Cheatdb_totalCount(&p) == 2, "total count = 2 pack files");

	// Hand-made file present, NOT in the manifest.
	char handmade[700];
	snprintf(handmade, sizeof(handmade), "%s/GBA/MyHomebrew.cht", p.cheats_dir);
	FILE* f = fopen(handmade, "w");
	if (f) {
		fputs("user cheat\n", f);
		fclose(f);
	}

	Cheatdb_writeDbVersion(&p, "Mon, 14 Sep 2026 18:05:15 GMT");
	char v[128] = {0};
	CHECK(Cheatdb_readDbVersion(&p, v, sizeof(v)) && strcmp(v, "Mon, 14 Sep 2026 18:05:15 GMT") == 0, "db version round-trips");

	Cheatdb_removeAll(&p);
	CHECK(!Cheatdb_installed(&p), "not installed after removeAll");
	f = fopen(handmade, "r");
	CHECK(f != NULL, "hand-made cheat preserved by removeAll");
	if (f)
		fclose(f);

	// A manifest naming more than 64 folders, plus a line longer than removeAll's 600-byte read
	// buffer whose tail is the path of a file that is not in the manifest.
	char sentinel[700];
	snprintf(sentinel, sizeof(sentinel), "%s/keep-me.cht", p.cheats_dir);
	f = fopen(sentinel, "w");
	if (f)
		fclose(f);
	FILE* man = fopen(p.manifest, "w");
	char path[700];
	for (int i = 0; i < 70 && man; i++) {
		snprintf(path, sizeof(path), "%s/T%02d", p.cheats_dir, i);
		mkdir(path, 0755);
		snprintf(path, sizeof(path), "%s/T%02d/x.cht", p.cheats_dir, i);
		f = fopen(path, "w");
		if (f)
			fclose(f);
		fprintf(man, "%s\n", path);
	}
	if (man) {
		char head[600];
		size_t pad = 599 - strlen(p.cheats_dir) - 1;
		memset(head, 'x', pad);
		head[pad] = '\0';
		fprintf(man, "%s/%s%s\n", p.cheats_dir, head, sentinel);
		fclose(man);
	}
	Cheatdb_removeAll(&p);
	int left = 0, dirs_left = 0;
	for (int i = 0; i < 70; i++) {
		snprintf(path, sizeof(path), "%s/T%02d/x.cht", p.cheats_dir, i);
		left += access(path, F_OK) == 0;
		snprintf(path, sizeof(path), "%s/T%02d", p.cheats_dir, i);
		dirs_left += access(path, F_OK) == 0;
		rmdir(path);
	}
	CHECK(left == 0, "removeAll removes every listed file past 64 folders");
	CHECK(dirs_left == 6, "removeAll prunes the first 64 folders and keeps the rest");
	CHECK(access(sentinel, F_OK) == 0, "tail of an over-long manifest line is not removed as a path");
	remove(sentinel);

	// mapping sanity
	CHECK(CHEATDB_MAP_COUNT == 33, "33 tag mappings");
	// one folder per console: a second core's tag has no copy of its own
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++)
		CHECK(strcmp(CHEATDB_MAP[i].tag, "MGBA") && strcmp(CHEATDB_MAP[i].tag, "SUPA") &&
				  strcmp(CHEATDB_MAP[i].tag, "PSX") && strcmp(CHEATDB_MAP[i].tag, "GPGX"),
			  "no per-core copy (MGBA/SUPA/PSX/GPGX read their console's folder)");
	int msx = 0, msx2 = 0;
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++)
		if (strcmp(CHEATDB_MAP[i].tag, "MSX") == 0) {
			msx |= strcmp(CHEATDB_MAP[i].folder, "Microsoft - MSX") == 0;
			msx2 |= strcmp(CHEATDB_MAP[i].folder, "Microsoft - MSX2") == 0;
		}
	CHECK(msx && msx2, "MSX -> Microsoft - MSX and Microsoft - MSX2 (the folders cheats.zip has)");
	const char* psp = NULL;
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++)
		if (strcmp(CHEATDB_MAP[i].tag, "PSP") == 0)
			psp = CHEATDB_MAP[i].folder;
	CHECK(psp && strcmp(psp, "Sony - PlayStation Portable") == 0, "PSP -> Sony - PlayStation Portable");
	const char* wsc = NULL;
	for (int i = 0; i < CHEATDB_MAP_COUNT; i++)
		if (strcmp(CHEATDB_MAP[i].tag, "WSC") == 0)
			wsc = CHEATDB_MAP[i].folder;
	CHECK(wsc && strcmp(wsc, "Bandai - WonderSwan Color") == 0, "WSC -> Bandai - WonderSwan Color");

	// Last-Modified probe: TLS verified against the CA bundle when one is
	// given (the vendored wget needs --check-certificate=on, --ca-certificate
	// alone doesn't verify); no bundle, or one whose path holds a quote, keeps
	// the no-verification fallback. The wget shim logs each call's argv
	// (one line per call) to $WGET_ARGS_LOG.
	const char* log = getenv("WGET_ARGS_LOG");
	char lm[128], line[1024] = {0};
	if (log) {
		remove(log);
		int got = Cheatdb_remoteLastModified("/sd/.system/shared/ssl/ca-certificates.crt", lm, sizeof(lm));
		CHECK(got > 0 && strcmp(lm, "Mon, 14 Sep 2026 18:05:15 GMT") == 0, "Last-Modified parsed");
		FILE* lf = fopen(log, "r");
		if (lf && fgets(line, sizeof(line), lf)) {}
		if (lf)
			fclose(lf);
		CHECK(strstr(line, "--check-certificate=on --ca-certificate=/sd/.system/shared/ssl/ca-certificates.crt") &&
				  !strstr(line, "--no-check-certificate"),
			  "Last-Modified probe verifies TLS against the bundle");

		remove(log);
		Cheatdb_remoteLastModified(NULL, lm, sizeof(lm));
		line[0] = '\0';
		lf = fopen(log, "r");
		if (lf && fgets(line, sizeof(line), lf)) {}
		if (lf)
			fclose(lf);
		CHECK(strstr(line, "--no-check-certificate") && !strstr(line, "--check-certificate=on"),
			  "no bundle -> no-verification fallback");

		remove(log);
		Cheatdb_remoteLastModified("/sd/it's.crt", lm, sizeof(lm));
		line[0] = '\0';
		lf = fopen(log, "r");
		if (lf && fgets(line, sizeof(line), lf)) {}
		if (lf)
			fclose(lf);
		CHECK(strstr(line, "--no-check-certificate") != NULL, "bundle path with a quote is not spliced into the shell");
	} else {
		CHECK(0, "WGET_ARGS_LOG not set (run via scripts/tests/test-cheatdb-data.sh)");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
