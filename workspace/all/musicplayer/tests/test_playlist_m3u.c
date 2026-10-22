// Host-compiled unit test for the .m3u playlist file helpers in
// playlist_m3u.c: load (EXTINF names, missing files skipped), add (dedup,
// batch dedup), contains, and remove (EXTINF pair, index into the loaded list).
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. -I../common -I../../tg5040/platform \
//      -DPLATFORM='"tg5040"' playlist_m3u.c music_format.c tests/test_playlist_m3u.c -o /tmp/t && /tmp/t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../playlist_m3u.h"

#define DIR_ "/tmp/nx_test_playlist_m3u"
#define M3U DIR_ "/list.m3u"

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

static void write_file(const char* path, const char* body) {
	FILE* f = fopen(path, "w");
	if (f) {
		fputs(body, f);
		fclose(f);
	}
}

static char* read_file(const char* path) {
	static char buf[4096];
	buf[0] = '\0';
	FILE* f = fopen(path, "r");
	if (f) {
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = '\0';
		fclose(f);
	}
	return buf;
}

static PlaylistTrack tracks[PLAYLIST_MAX_TRACKS];

static void setup(void) {
	system("rm -rf " DIR_ " && mkdir -p " DIR_);
	touch(DIR_ "/a.mp3");
	touch(DIR_ "/b.flac");
	touch(DIR_ "/c.ogg");
}

static void test_load(void) {
	setup();
	write_file(M3U, "#EXTM3U\n"
					"#EXTINF:123,Song A\r\n" DIR_ "/a.mp3\r\n"
					"\n"
					"#EXTINF:0,Gone\n" DIR_ "/missing.mp3\n" // skipped, name not carried over
			   DIR_ "/b.flac\n"
					"# a comment\n"
					"#EXTINF:0\n" DIR_ "/c.ogg\n"); // no comma: no name
	int count = -1;
	CHECK(M3U_loadTracks(M3U, tracks, PLAYLIST_MAX_TRACKS, &count) == 0);
	CHECK(count == 3);
	CHECK(strcmp(tracks[0].path, DIR_ "/a.mp3") == 0 && strcmp(tracks[0].name, "Song A") == 0);
	CHECK(tracks[0].format == AUDIO_FORMAT_MP3);
	CHECK(strcmp(tracks[1].path, DIR_ "/b.flac") == 0 && strcmp(tracks[1].name, "b.flac") == 0);
	CHECK(tracks[1].format == AUDIO_FORMAT_FLAC);
	CHECK(strcmp(tracks[2].name, "c.ogg") == 0);

	// max caps the count
	CHECK(M3U_loadTracks(M3U, tracks, 2, &count) == 0 && count == 2);

	CHECK(M3U_loadTracks(DIR_ "/nope.m3u", tracks, PLAYLIST_MAX_TRACKS, &count) == -1 && count == 0);
}

static void test_add_and_contains(void) {
	setup();
	write_file(M3U, "#EXTM3U\n");
	CHECK(M3U_addTrack(M3U, DIR_ "/a.mp3", "Song A") == 0);
	CHECK(M3U_addTrack(M3U, DIR_ "/a.mp3", "Again") == -1); // duplicate
	CHECK(M3U_addTrack(M3U, DIR_ "/b.flac", NULL) == 0);	// name falls back to the path
	CHECK(M3U_containsTrack(M3U, DIR_ "/a.mp3"));
	CHECK(!M3U_containsTrack(M3U, DIR_ "/c.ogg"));
	CHECK(!M3U_containsTrack(M3U, "Song A")); // EXTINF lines are not paths
	CHECK(strcmp(read_file(M3U), "#EXTM3U\n#EXTINF:0,Song A\n" DIR_ "/a.mp3\n#EXTINF:0," DIR_ "/b.flac\n" DIR_ "/b.flac\n") == 0);

	PlaylistTrack batch[4];
	memset(batch, 0, sizeof(batch));
	snprintf(batch[0].path, sizeof(batch[0].path), "%s", DIR_ "/a.mp3"); // already there
	snprintf(batch[1].path, sizeof(batch[1].path), "%s", DIR_ "/c.ogg");
	snprintf(batch[1].name, sizeof(batch[1].name), "%s", "Song C");
	snprintf(batch[2].path, sizeof(batch[2].path), "%s", DIR_ "/c.ogg"); // twice in the batch
	// batch[3] has an empty path: skipped
	CHECK(M3U_addTracks(M3U, batch, 4) == 1);
	int count = 0;
	M3U_loadTracks(M3U, tracks, PLAYLIST_MAX_TRACKS, &count);
	CHECK(count == 3 && strcmp(tracks[2].name, "Song C") == 0);

	CHECK(M3U_addTracks(M3U, batch, 0) == -1);
	CHECK(M3U_addTrack(NULL, DIR_ "/a.mp3", NULL) == -1);
}

static void test_remove(void) {
	setup();
	write_file(M3U, "#EXTM3U\n"
					"#EXTINF:0,A\n" DIR_ "/a.mp3\n"
					"#EXTINF:0,B\n" DIR_ "/b.flac\n" DIR_ "/c.ogg\n");
	CHECK(M3U_removeTrack(M3U, 1) == 0); // B and its EXTINF line
	CHECK(strcmp(read_file(M3U), "#EXTM3U\n#EXTINF:0,A\n" DIR_ "/a.mp3\n" DIR_ "/c.ogg\n") == 0);
	CHECK(access(M3U ".tmp", F_OK) != 0);

	CHECK(M3U_removeTrack(M3U, 1) == 0); // c.ogg has no EXTINF: a.mp3's stays
	CHECK(strcmp(read_file(M3U), "#EXTM3U\n#EXTINF:0,A\n" DIR_ "/a.mp3\n") == 0);

	CHECK(M3U_removeTrack(M3U, 5) == -1); // out of range: file unchanged
	CHECK(M3U_removeTrack(M3U, -1) == -1);
	CHECK(strcmp(read_file(M3U), "#EXTM3U\n#EXTINF:0,A\n" DIR_ "/a.mp3\n") == 0);
	CHECK(M3U_removeTrack(DIR_ "/nope.m3u", 0) == -1);
}

// The playlist screen removes by the index into M3U_loadTracks' list, which
// leaves out missing files: a missing track before it must not shift the
// index onto the wrong track.
static void test_remove_skips_missing_files(void) {
	setup();
	write_file(M3U, "#EXTM3U\n"
					"#EXTINF:0,Gone\n" DIR_ "/missing.mp3\n"
					"#EXTINF:0,A\n" DIR_ "/a.mp3\n"
					"#EXTINF:0,B\n" DIR_ "/b.flac\n");
	int count = 0;
	M3U_loadTracks(M3U, tracks, PLAYLIST_MAX_TRACKS, &count);
	CHECK(count == 2 && strcmp(tracks[1].name, "B") == 0);
	CHECK(M3U_removeTrack(M3U, 1) == 0); // "B" as listed
	M3U_loadTracks(M3U, tracks, PLAYLIST_MAX_TRACKS, &count);
	CHECK(count == 1 && strcmp(tracks[0].name, "A") == 0);
	CHECK(M3U_containsTrack(M3U, DIR_ "/missing.mp3"));
}

int main(void) {
	test_load();
	test_add_and_contains();
	test_remove();
	test_remove_skips_missing_files();
	system("rm -rf " DIR_);
	if (failures) {
		fprintf(stderr, "test_playlist_m3u: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_playlist_m3u: all tests passed\n");
	return 0;
}
