// Host-compiled unit test for the HLS playlist parser in radio_hls.c: the
// kept segment window of long playlists, the live start index and the
// playlist fetch helper.
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. radio_hls.c tag_meta.c tests/test_radio_hls_playlist.c -o /tmp/t && /tmp/t
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../radio_hls.h"
#include "../radio_net.h"

static const char* fetch_body;

// Serves fetch_body, cut at buffer_size - 1 like the real fetch
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size, char* content_type, int ct_size) {
	if (!fetch_body)
		return -1;
	int len = (int)strlen(fetch_body);
	if (len > buffer_size - 1)
		len = buffer_size - 1;
	memcpy(buffer, fetch_body, len);
	return len;
}

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

static HLSContext ctx;

// A media playlist of count segments numbered from first_seq
static char* make_playlist(int count, int first_seq, bool endlist) {
	size_t size = 256 + (size_t)count * 64;
	char* s = malloc(size);
	int n = snprintf(s, size, "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:%d\n", first_seq);
	for (int i = 0; i < count; i++)
		n += snprintf(s + n, size - n, "#EXTINF:6.0,\nseg%d.aac\n", first_seq + i);
	if (endlist)
		snprintf(s + n, size - n, "#EXT-X-ENDLIST\n");
	return s;
}

static void parse(const char* body, int last_played) {
	memset(&ctx, 0, sizeof(ctx));
	ctx.last_played_sequence = last_played;
	radio_hls_parse_playlist(&ctx, body, "http://h/live/");
}

static void test_live_window_keeps_newest(void) {
	// a 2-hour live window: the newest segments are kept, not the oldest
	char* body = make_playlist(1200, 5000, false);
	parse(body, -1);
	CHECK(ctx.is_live);
	CHECK(ctx.segment_count == HLS_MAX_SEGMENTS);
	CHECK(ctx.media_sequence == 5000 + 1200 - HLS_MAX_SEGMENTS);
	CHECK(strcmp(ctx.segments[HLS_MAX_SEGMENTS - 1].url, "http://h/live/seg6199.aac") == 0);
	CHECK(strcmp(ctx.segments[0].url, "http://h/live/seg6136.aac") == 0);
	CHECK(!ctx.has_more);
	// and playback starts near the live edge
	CHECK(radio_hls_live_start_index(&ctx) == HLS_MAX_SEGMENTS - 3);
	free(body);
}

static void test_short_live_and_vod_start(void) {
	char* body = make_playlist(6, 10, false);
	parse(body, -1);
	CHECK(ctx.segment_count == 6 && ctx.media_sequence == 10);
	CHECK(radio_hls_live_start_index(&ctx) == 3);
	free(body);
	body = make_playlist(2, 10, false);
	parse(body, -1);
	CHECK(radio_hls_live_start_index(&ctx) == 0);
	free(body);
	body = make_playlist(30, 0, true);
	parse(body, -1);
	CHECK(!ctx.is_live && radio_hls_live_start_index(&ctx) == 0);
	free(body);
}

static void test_vod_windows(void) {
	// a finished 150-segment playlist is played 64 at a time, in order
	char* body = make_playlist(150, 100, true);
	parse(body, -1);
	CHECK(!ctx.is_live);
	CHECK(ctx.segment_count == HLS_MAX_SEGMENTS && ctx.media_sequence == 100);
	CHECK(ctx.has_more);
	// re-read after playing the first window
	parse(body, 100 + HLS_MAX_SEGMENTS - 1);
	CHECK(ctx.media_sequence == 100 + HLS_MAX_SEGMENTS);
	CHECK(strcmp(ctx.segments[0].url, "http://h/live/seg164.aac") == 0);
	CHECK(ctx.has_more);
	parse(body, 100 + 2 * HLS_MAX_SEGMENTS - 1);
	CHECK(ctx.segment_count == 150 - 2 * HLS_MAX_SEGMENTS);
	CHECK(strcmp(ctx.segments[ctx.segment_count - 1].url, "http://h/live/seg249.aac") == 0);
	CHECK(!ctx.has_more);
	free(body);
}

static void test_window_skip(void) {
	CHECK(radio_hls_window_skip(10, 0, true, -1) == 0);
	CHECK(radio_hls_window_skip(100, 0, true, -1) == 100 - HLS_MAX_SEGMENTS);
	CHECK(radio_hls_window_skip(100, 0, false, -1) == 0);
	CHECK(radio_hls_window_skip(100, 0, false, 9) == 10);
	CHECK(radio_hls_window_skip(10, 0, false, 99) == 10); // all played
	CHECK(radio_hls_window_skip(10, 50, false, 3) == 0);  // behind the list
}

static void test_master_base_url(void) {
	// master -> variant: the variant's base URL is used for its segments
	// (ctx->base_url is passed back in as base_url: no overlapping copy)
	fetch_body = "#EXTM3U\n#EXTINF:6,\na.aac\n#EXTINF:6,\nb.aac\n";
	memset(&ctx, 0, sizeof(ctx));
	ctx.last_played_sequence = -1;
	radio_hls_parse_playlist(&ctx, "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttp://v/x/index.m3u8\n", "http://m/");
	CHECK(ctx.segment_count == 2);
	CHECK(strcmp(ctx.segments[1].url, "http://v/x/b.aac") == 0);
	fetch_body = NULL;
}

static void test_fetch_drops_cut_line(void) {
	// a playlist bigger than the read buffer loses its partial last line
	size_t size = HLS_PLAYLIST_BUF_SIZE + 4096;
	char* big = malloc(size);
	size_t n = 0;
	for (int i = 0; n + 64 < size; i++)
		n += snprintf(big + n, size - n, "#EXTINF:6.0,\nhttp://h/seg-%08d.aac\n", i);
	fetch_body = big;
	char* got = radio_hls_fetch_playlist("http://h/p.m3u8");
	CHECK(got != NULL);
	if (got) {
		size_t len = strlen(got);
		CHECK(len < HLS_PLAYLIST_BUF_SIZE);
		CHECK(len > 0 && got[len - 1] == '\n');
		free(got);
	}
	fetch_body = NULL;
	CHECK(radio_hls_fetch_playlist("http://h/p.m3u8") == NULL);
	free(big);
}

int main(void) {
	test_live_window_keeps_newest();
	test_short_live_and_vod_start();
	test_vod_windows();
	test_window_skip();
	test_master_base_url();
	test_fetch_drops_cut_line();
	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("test_radio_hls_playlist: all passed\n");
	return 0;
}
