// Host-compiled unit test for Playlist_buildFromDirectory (playlist.c): order
// selected -> after -> before -> subdirectories, symlinks and non-audio files
// skipped, the depth cap and the PLAYLIST_MAX_TRACKS cap; plus next/prev.
// Build & run (from workspace/all/musicplayer; api.h needs the SDL2 headers
// only, nothing links SDL):
//   cc -std=gnu99 -Wall -Werror -Wno-typedef-redefinition -fsanitize=address -g -DUSE_SDL2 -I. -I../common \
//      -I../../tg5040/platform -DPLATFORM='"tg5040"' $(sdl2-config --cflags) -I$(sdl2-config --prefix)/include \
//      playlist.c music_format.c tests/test_playlist_dir.c -o /tmp/t && /tmp/t
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../playlist.h"

#define ROOT "/tmp/nx_test_playlist_dir"

void LOG_note(int level, const char* fmt, ...) {
	(void)level;
	(void)fmt;
}

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

static void touch(const char* path) {
	FILE* f = fopen(path, "w");
	if (f)
		fclose(f);
}

static const char* name_at(PlaylistContext* ctx, int i) {
	const PlaylistTrack* t = Playlist_getTrack(ctx, i);
	return t ? t->name : "";
}

static void test_order(void) {
	system("rm -rf " ROOT " && mkdir -p " ROOT "/b_sub " ROOT "/A_sub " ROOT "/.hidden");
	touch(ROOT "/c.mp3");
	touch(ROOT "/A.flac");
	touch(ROOT "/b.ogg");
	touch(ROOT "/d.MP3");
	touch(ROOT "/cover.jpg");	// not audio
	touch(ROOT "/.secret.mp3"); // hidden
	touch(ROOT "/A_sub/x.wav");
	touch(ROOT "/b_sub/y.m4a");
	touch(ROOT "/.hidden/z.mp3");
	symlink(ROOT "/c.mp3", ROOT "/link.mp3"); // symlinks are skipped
	symlink(ROOT, ROOT "/b_sub/loop");		  // so a loop can't recurse

	PlaylistContext ctx = {0};
	CHECK(Playlist_buildFromDirectory(&ctx, ROOT, ROOT "/c.mp3") == 6);
	// selected, files after it, files before it (case-insensitive), then subdirs
	CHECK(strcmp(name_at(&ctx, 0), "c.mp3") == 0);
	CHECK(strcmp(name_at(&ctx, 1), "d.MP3") == 0);
	CHECK(strcmp(name_at(&ctx, 2), "A.flac") == 0);
	CHECK(strcmp(name_at(&ctx, 3), "b.ogg") == 0);
	CHECK(strcmp(name_at(&ctx, 4), "x.wav") == 0);
	CHECK(strcmp(name_at(&ctx, 5), "y.m4a") == 0);
	CHECK(strcmp(Playlist_getTrack(&ctx, 4)->path, ROOT "/A_sub/x.wav") == 0);
	CHECK(Playlist_getTrack(&ctx, 1)->format == AUDIO_FORMAT_MP3);
	CHECK(Playlist_getCurrentIndex(&ctx) == 0);

	// No start track (or one not in the folder): from the first file
	CHECK(Playlist_buildFromDirectory(&ctx, ROOT, NULL) == 6);
	CHECK(strcmp(name_at(&ctx, 0), "A.flac") == 0 && strcmp(name_at(&ctx, 3), "d.MP3") == 0);
	CHECK(Playlist_buildFromDirectory(&ctx, ROOT, ROOT "/nope.mp3") == 6);
	CHECK(strcmp(name_at(&ctx, 0), "A.flac") == 0);

	// next/prev don't wrap
	CHECK(Playlist_prev(&ctx) == -1);
	CHECK(Playlist_next(&ctx) == 1);
	ctx.current_index = 5;
	CHECK(Playlist_next(&ctx) == -1);
	CHECK(Playlist_prev(&ctx) == 4);

	CHECK(Playlist_buildFromDirectory(&ctx, ROOT "/nope", NULL) == -1);
	Playlist_free(&ctx);
}

static void test_depth_cap(void) {
	system("rm -rf " ROOT " && mkdir -p " ROOT);
	char dir[512] = ROOT;
	touch(ROOT "/0.mp3");
	for (int i = 1; i <= PLAYLIST_MAX_DEPTH + 2; i++) {
		size_t len = strlen(dir);
		snprintf(dir + len, sizeof(dir) - len, "/d%d", i);
		char cmd[600], file[600];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
		system(cmd);
		snprintf(file, sizeof(file), "%s/%d.mp3", dir, i);
		touch(file);
	}
	PlaylistContext ctx = {0};
	// the start folder plus PLAYLIST_MAX_DEPTH levels below it
	CHECK(Playlist_buildFromDirectory(&ctx, ROOT, NULL) == PLAYLIST_MAX_DEPTH + 1);
	CHECK(strcmp(name_at(&ctx, PLAYLIST_MAX_DEPTH), "10.mp3") == 0);
	Playlist_free(&ctx);
}

static void test_track_cap(void) {
	system("rm -rf " ROOT " && mkdir -p " ROOT "/sub");
	for (int i = 0; i < PLAYLIST_MAX_TRACKS - 10; i++) {
		char file[128];
		snprintf(file, sizeof(file), ROOT "/%04d.mp3", i);
		touch(file);
	}
	for (int i = 0; i < 20; i++) {
		char file[128];
		snprintf(file, sizeof(file), ROOT "/sub/%04d.mp3", i);
		touch(file);
	}
	PlaylistContext ctx = {0};
	CHECK(Playlist_buildFromDirectory(&ctx, ROOT, NULL) == PLAYLIST_MAX_TRACKS);
	CHECK(strcmp(name_at(&ctx, PLAYLIST_MAX_TRACKS - 1), "0009.mp3") == 0);
	Playlist_free(&ctx);
}

int main(void) {
	test_order();
	test_depth_cap();
	test_track_cap();
	system("rm -rf " ROOT);
	if (failures) {
		fprintf(stderr, "test_playlist_dir: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_playlist_dir: all tests passed\n");
	return 0;
}
