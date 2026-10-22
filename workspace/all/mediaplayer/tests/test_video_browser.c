// Host-compiled unit test for video_browser.c: extension detection, subtitle
// discovery and its labels, the HEVC probe and directory listing order.
// Build & run (from workspace/all/mediaplayer; api.h needs the SDL2 headers
// only, nothing links SDL):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -DUSE_SDL2 -I. -I../common -I../../tg5040/platform \
//      -DPLATFORM='"tg5040"' $(sdl2-config --cflags) -I$(sdl2-config --prefix)/include \
//      video_browser.c tests/test_video_browser.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../video_browser.h"

#define DIR_ "/tmp/nx_test_video_browser"

void LOG_note(int level, const char* fmt, ...) {
	(void)level;
	(void)fmt;
}

static void write_file(const char* path, const void* data, size_t len) {
	FILE* f = fopen(path, "wb");
	assert(f);
	if (len)
		fwrite(data, 1, len, f);
	fclose(f);
}

static void touch(const char* path) {
	write_file(path, "", 0);
}

static void test_detect_format(void) {
	assert(VideoBrowser_detectFormat("a.mp4") == VIDEO_FORMAT_MP4);
	assert(VideoBrowser_detectFormat("A.MKV") == VIDEO_FORMAT_MKV);
	assert(VideoBrowser_detectFormat("x.y.Mpeg") == VIDEO_FORMAT_MPG);
	assert(VideoBrowser_detectFormat("clip.3gp") == VIDEO_FORMAT_3GP);
	assert(VideoBrowser_detectFormat(".mp4") == VIDEO_FORMAT_UNKNOWN); // hidden, no name
	assert(VideoBrowser_detectFormat(".hidden") == VIDEO_FORMAT_UNKNOWN);
	assert(VideoBrowser_detectFormat("noext") == VIDEO_FORMAT_UNKNOWN);
	assert(VideoBrowser_detectFormat("a.srt") == VIDEO_FORMAT_UNKNOWN);
	assert(VideoBrowser_detectFormat(NULL) == VIDEO_FORMAT_UNKNOWN);
	assert(VideoBrowser_isVideoFile("a.webm") && !VideoBrowser_isVideoFile("a.txt"));

	char name[16];
	VideoBrowser_getDisplayName("Movie.Name.mkv", name, sizeof(name));
	assert(strcmp(name, "Movie.Name") == 0);
	VideoBrowser_getDisplayName(".mkv", name, sizeof(name));
	assert(strcmp(name, ".mkv") == 0);
	VideoBrowser_getDisplayName("A very long file name.mp4", name, 8);
	assert(strcmp(name, "A very ") == 0);
}

static int find_label(const SubtitleList* list, const char* label) {
	for (int i = 0; i < list->count; i++)
		if (strcmp(list->entries[i].label, label) == 0)
			return i;
	return -1;
}

static void test_subtitles(void) {
	system("rm -rf " DIR_ " && mkdir -p " DIR_);
	touch(DIR_ "/movie.mkv");
	touch(DIR_ "/movie.srt");		 // exact match
	touch(DIR_ "/movie.en.srt");	 // language tag
	touch(DIR_ "/movie(Malay).srt"); // bracketed label
	touch(DIR_ "/movie_english.ass");
	touch(DIR_ "/movieExtra.srt"); // a different title: not matched
	touch(DIR_ "/movie.en.txt");   // not a subtitle extension
	touch(DIR_ "/other.srt");	   // other video's
	touch(DIR_ "/.movie.fr.srt");  // hidden
	touch(DIR_ "/movie.-.ssa");	   // separators only: label = extension

	SubtitleList list;
	VideoBrowser_findSubtitles(DIR_ "/movie.mkv", &list);
	assert(list.count == 5);
	assert(strcmp(list.entries[0].label, "srt") == 0); // exact matches come first
	assert(strcmp(list.entries[0].path, DIR_ "/movie.srt") == 0);
	assert(find_label(&list, "en") >= 0);
	assert(find_label(&list, "Malay") >= 0);
	assert(find_label(&list, "english") >= 0);
	assert(find_label(&list, "ssa") >= 0);
	int srt_count = 0;
	for (int i = 0; i < list.count; i++) {
		assert(strstr(list.entries[i].path, "Extra") == NULL);
		assert(strstr(list.entries[i].path, "other") == NULL);
		if (strcmp(list.entries[i].path, DIR_ "/movie.srt") == 0)
			srt_count++;
	}
	assert(srt_count == 1); // the exact match is not listed again

	// At most MAX_SUBTITLE_FILES
	for (int i = 0; i < MAX_SUBTITLE_FILES + 4; i++) {
		char path[128];
		snprintf(path, sizeof(path), DIR_ "/movie.l%d.srt", i);
		touch(path);
	}
	VideoBrowser_findSubtitles(DIR_ "/movie.mkv", &list);
	assert(list.count == MAX_SUBTITLE_FILES);

	// Long labels are cut to the label field
	system("rm -rf " DIR_ " && mkdir -p " DIR_);
	touch(DIR_ "/m.mp4");
	touch(DIR_ "/m.averyveryveryverylonglanguagenamethatgoeson.srt");
	VideoBrowser_findSubtitles(DIR_ "/m.mp4", &list);
	assert(list.count == 1 && strlen(list.entries[0].label) == sizeof(list.entries[0].label) - 1);

	VideoBrowser_findSubtitles("no-slash.mkv", &list);
	assert(list.count == 0);
	VideoBrowser_findSubtitles(NULL, &list);
	assert(list.count == 0);
	VideoBrowser_findSubtitles(DIR_ "/missing-dir/m.mp4", &list);
	assert(list.count == 0);
}

static void test_hevc(void) {
	system("rm -rf " DIR_ " && mkdir -p " DIR_);
	static unsigned char buf[300 * 1024];
	memset(buf, 0, sizeof(buf));
	memcpy(buf + 1000, "avc1", 4);
	write_file(DIR_ "/avc.mp4", buf, 4096);
	assert(!VideoBrowser_isHEVC(DIR_ "/avc.mp4"));

	memcpy(buf + 2000, "hvc1", 4);
	write_file(DIR_ "/hvc.mp4", buf, 4096);
	assert(VideoBrowser_isHEVC(DIR_ "/hvc.mp4"));

	memset(buf, 0, sizeof(buf));
	memcpy(buf + 100, "V_MPEGH/ISO/HEVC", 16);
	write_file(DIR_ "/a.mkv", buf, 200);
	assert(VideoBrowser_isHEVC(DIR_ "/a.mkv"));

	// past the 256 KB probe window: not seen
	memset(buf, 0, sizeof(buf));
	memcpy(buf + 280 * 1024, "hev1", 4);
	write_file(DIR_ "/late.mp4", buf, sizeof(buf));
	assert(!VideoBrowser_isHEVC(DIR_ "/late.mp4"));

	// a tag ending exactly at the end of a short file is still found
	write_file(DIR_ "/tail.mp4", "xxhev1", 6);
	assert(VideoBrowser_isHEVC(DIR_ "/tail.mp4"));
	assert(!VideoBrowser_isHEVC(DIR_ "/missing.mp4"));
}

static void test_load_directory(void) {
	system("rm -rf " DIR_ " && mkdir -p " DIR_ "/sub/Zdir " DIR_ "/sub/adir");
	touch(DIR_ "/sub/b.mp4");
	touch(DIR_ "/sub/A.mkv");
	touch(DIR_ "/sub/notes.txt");
	touch(DIR_ "/sub/.hidden.mp4");

	VideoBrowserContext ctx = {0};
	VideoBrowser_loadDirectory(&ctx, DIR_ "/sub", DIR_);
	assert(ctx.entry_count == 5);
	assert(strcmp(ctx.entries[0].name, "..") == 0 && strcmp(ctx.entries[0].path, DIR_) == 0);
	assert(strcmp(ctx.entries[1].name, "adir") == 0 && ctx.entries[1].is_dir);
	assert(strcmp(ctx.entries[2].name, "Zdir") == 0 && ctx.entries[2].is_dir);
	assert(strcmp(ctx.entries[3].name, "A.mkv") == 0 && ctx.entries[3].format == VIDEO_FORMAT_MKV);
	assert(strcmp(ctx.entries[4].name, "b.mp4") == 0);

	// At the root: no ".." entry
	VideoBrowser_loadDirectory(&ctx, DIR_, DIR_);
	assert(ctx.entry_count == 1 && strcmp(ctx.entries[0].name, "sub") == 0);

	VideoBrowser_loadDirectory(&ctx, DIR_ "/missing", DIR_);
	assert(ctx.entry_count == 0 && ctx.entries == NULL);
	VideoBrowser_freeEntries(&ctx);
}

int main(void) {
	test_detect_format();
	test_subtitles();
	test_hevc();
	test_load_directory();
	system("rm -rf " DIR_);
	printf("test_video_browser: OK\n");
	return 0;
}
