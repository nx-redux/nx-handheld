// Host-compiled unit test for the iTunes / Apple charts JSON handling in
// podcast_search.c, with the network fetch stubbed to serve canned bodies:
// premium and feedless results dropped, artwork upscaled, caps, lookup
// fallbacks and the charts premium filter.
// Build & run (from workspace/all/musicplayer; api.h needs the SDL2 headers
// only, nothing links SDL):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -DUSE_SDL2 -I. -I../common -I../include \
//      -I../../tg5040/platform -DPLATFORM='"tg5040"' $(sdl2-config --cflags) -I$(sdl2-config --prefix)/include \
//      podcast_search.c ../include/parson/parson.c tests/test_podcast_search.c -o /tmp/t && /tmp/t
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../podcast.h"

int podcast_search_itunes(const char* query, PodcastSearchResult* results, int max_results);
int podcast_search_lookup_full(const char* itunes_id, char* feed_url, int feed_url_size,
							   char* artwork_url, int artwork_url_size);
int podcast_charts_fetch(const char* country_code, PodcastChartItem* top, int* top_count,
						 PodcastChartItem* new_items, int* new_count, int max_items);
int podcast_charts_filter_premium(PodcastChartItem* items, int count, int max_items);

static const char* fetch_body; // NULL = the fetch fails
static char fetch_url[4096];

int wget_fetch_alloc(const char* url, int max_size, unsigned char** out) {
	snprintf(fetch_url, sizeof(fetch_url), "%s", url);
	*out = NULL;
	if (!fetch_body)
		return -1;
	int len = (int)strlen(fetch_body);
	*out = malloc(len + 1);
	memcpy(*out, fetch_body, len + 1);
	return len;
}

void urlEncode(const char* src, char* dst, size_t dst_size) {
	size_t j = 0;
	for (; *src && j + 4 < dst_size; src++) {
		if (*src == ' ')
			j += snprintf(dst + j, dst_size - j, "%%20");
		else
			dst[j++] = *src;
	}
	dst[j] = '\0';
}

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

static PodcastSearchResult results[8];

static void test_search(void) {
	fetch_body = "{\"resultCount\":5,\"results\":["
				 "{\"trackId\":101,\"trackName\":\"Free Show\",\"artistName\":\"Ann\","
				 "\"feedUrl\":\"https://f/1.xml\",\"artworkUrl100\":\"https://a/x/100x100bb.jpg\","
				 "\"primaryGenreName\":\"News\",\"trackPrice\":0},"
				 "{\"trackId\":102,\"trackName\":\"Paid\",\"feedUrl\":\"https://f/2.xml\",\"trackPrice\":1.99},"
				 "{\"trackId\":103,\"trackName\":\"No Feed\"},"
				 "{\"trackId\":104,\"trackName\":\"Empty Feed\",\"feedUrl\":\"\"},"
				 "{\"trackId\":1234567890,\"trackName\":\"Second\",\"feedUrl\":\"https://f/5.xml\","
				 "\"artworkUrl100\":\"https://a/y/60x60bb.jpg\"}]}";
	memset(results, 0x7A, sizeof(results));
	CHECK(podcast_search_itunes("hello world", results, 8) == 2);
	CHECK(strstr(fetch_url, "term=hello%20world&media=podcast&limit=8") != NULL);
	CHECK(strcmp(results[0].itunes_id, "101") == 0);
	CHECK(strcmp(results[0].title, "Free Show") == 0 && strcmp(results[0].author, "Ann") == 0);
	CHECK(strcmp(results[0].feed_url, "https://f/1.xml") == 0);
	CHECK(strcmp(results[0].artwork_url, "https://a/x/400x400bb.jpg") == 0);
	CHECK(strcmp(results[0].genre, "News") == 0);
	CHECK(strcmp(results[1].itunes_id, "1234567890") == 0);
	CHECK(strcmp(results[1].artwork_url, "https://a/y/60x60bb.jpg") == 0); // no 100x100 to upscale
	CHECK(results[1].author[0] == '\0' && results[1].genre[0] == '\0');

	// max_results caps both the request and the kept results
	CHECK(podcast_search_itunes("q", results, 1) == 1);
	CHECK(strstr(fetch_url, "limit=1") != NULL);
	CHECK(podcast_search_itunes("q", results, 200) == 2);
	CHECK(strstr(fetch_url, "limit=50") != NULL);

	fetch_body = "{\"resultCount\":0}";
	CHECK(podcast_search_itunes("q", results, 8) == 0);
	fetch_body = "<html>rate limited</html>";
	CHECK(podcast_search_itunes("q", results, 8) == -1);
	fetch_body = "[1,2]";
	CHECK(podcast_search_itunes("q", results, 8) == -1);
	fetch_body = NULL;
	CHECK(podcast_search_itunes("q", results, 8) == -1);
	CHECK(podcast_search_itunes(NULL, results, 8) == -1);
	CHECK(podcast_search_itunes("q", results, 0) == -1);
}

static void test_long_fields_terminated(void) {
	static char body[8192];
	char title[600];
	memset(title, 'T', sizeof(title) - 1);
	title[sizeof(title) - 1] = '\0';
	snprintf(body, sizeof(body), "{\"results\":[{\"trackName\":\"%s\",\"feedUrl\":\"https://f/%s\"}]}", title, title);
	fetch_body = body;
	CHECK(podcast_search_itunes("q", results, 8) == 1);
	CHECK(strlen(results[0].title) == PODCAST_MAX_TITLE - 1);
	CHECK(strlen(results[0].feed_url) == PODCAST_MAX_URL - 1);
}

static void test_lookup(void) {
	char feed[64], art[64];
	fetch_body = "{\"results\":[{\"feedUrl\":\"https://f/1.xml\",\"artworkUrl600\":\"https://a/600.jpg\","
				 "\"artworkUrl100\":\"https://a/100x100bb.jpg\"}]}";
	art[0] = '\0';
	CHECK(podcast_search_lookup_full("42", feed, sizeof(feed), art, sizeof(art)) == 0);
	CHECK(strstr(fetch_url, "lookup?id=42") != NULL);
	CHECK(strcmp(feed, "https://f/1.xml") == 0 && strcmp(art, "https://a/600.jpg") == 0);

	fetch_body = "{\"results\":[{\"feedUrl\":\"https://f/1.xml\",\"artworkUrl100\":\"https://a/100x100bb.jpg\"}]}";
	art[0] = '\0';
	CHECK(podcast_search_lookup_full("42", feed, sizeof(feed), art, sizeof(art)) == 0);
	CHECK(strcmp(art, "https://a/400x400bb.jpg") == 0);

	char tiny[8];
	CHECK(podcast_search_lookup_full("42", tiny, sizeof(tiny), NULL, 0) == 0);
	CHECK(strcmp(tiny, "https:/") == 0);

	fetch_body = "{\"results\":[{\"trackName\":\"no feed\"}]}";
	CHECK(podcast_search_lookup_full("42", feed, sizeof(feed), NULL, 0) == -1);
	fetch_body = "{\"results\":[]}";
	CHECK(podcast_search_lookup_full("42", feed, sizeof(feed), NULL, 0) == -1);
	fetch_body = NULL;
	CHECK(podcast_search_lookup_full("42", feed, sizeof(feed), NULL, 0) == -1);
}

static PodcastChartItem charts[8];

static void test_charts(void) {
	fetch_body = "{\"feed\":{\"results\":["
				 "{\"id\":\"11\",\"name\":\"One\",\"artistName\":\"A1\",\"artworkUrl100\":\"https://a/100x100bb.png\","
				 "\"genres\":[{\"name\":\"Comedy\"},{\"name\":\"Other\"}]},"
				 "{\"id\":\"22\",\"name\":\"Two\",\"genres\":[]},"
				 "{\"id\":\"33\",\"name\":\"Three\"}]}}";
	int top = -1;
	CHECK(podcast_charts_fetch("us", charts, &top, NULL, NULL, 8) == 0);
	CHECK(strstr(fetch_url, "/api/v2/us/podcasts/top/8/podcasts.json") != NULL);
	CHECK(top == 3);
	CHECK(strcmp(charts[0].itunes_id, "11") == 0 && strcmp(charts[0].title, "One") == 0);
	CHECK(strcmp(charts[0].genre, "Comedy") == 0);
	CHECK(strcmp(charts[0].artwork_url, "https://a/400x400bb.png") == 0);
	CHECK(charts[1].genre[0] == '\0' && charts[2].author[0] == '\0');

	CHECK(podcast_charts_fetch("us", charts, &top, NULL, NULL, 2) == 0 && top == 2);
	CHECK(podcast_charts_fetch("us", charts, &top, NULL, NULL, 500) == 0);
	CHECK(strstr(fetch_url, "/top/100/") != NULL);

	fetch_body = "{\"feed\":{}}";
	CHECK(podcast_charts_fetch("us", charts, &top, NULL, NULL, 8) == -1 && top == 0);
	fetch_body = NULL;
	CHECK(podcast_charts_fetch("us", charts, &top, NULL, NULL, 8) == -1);
}

static void test_charts_filter_premium(void) {
	memset(charts, 0, sizeof(charts));
	const char* ids[] = {"11", "22", "33", "44"};
	for (int i = 0; i < 4; i++) {
		snprintf(charts[i].itunes_id, sizeof(charts[i].itunes_id), "%s", ids[i]);
		snprintf(charts[i].title, sizeof(charts[i].title), "Show %s", ids[i]);
	}
	// 11 free, 22 paid, 33 missing (no feed), 44 free
	fetch_body = "{\"results\":["
				 "{\"trackId\":44,\"feedUrl\":\"https://f/44.xml\",\"trackPrice\":0},"
				 "{\"trackId\":22,\"feedUrl\":\"https://f/22.xml\",\"trackPrice\":0.99},"
				 "{\"trackId\":33},"
				 "{\"trackId\":11,\"feedUrl\":\"https://f/11.xml\"}]}";
	CHECK(podcast_charts_filter_premium(charts, 4, 8) == 2);
	CHECK(strstr(fetch_url, "lookup?id=11,22,33,44") != NULL);
	CHECK(strcmp(charts[0].itunes_id, "11") == 0 && strcmp(charts[0].feed_url, "https://f/11.xml") == 0);
	CHECK(strcmp(charts[1].itunes_id, "44") == 0 && strcmp(charts[1].title, "Show 44") == 0);
	CHECK(strcmp(charts[1].feed_url, "https://f/44.xml") == 0);

	// A failed lookup keeps the list as it was
	fetch_body = NULL;
	CHECK(podcast_charts_filter_premium(charts, 2, 8) == 2);
	CHECK(podcast_charts_filter_premium(charts, 0, 8) == 0);
}

int main(void) {
	test_search();
	test_long_fields_terminated();
	test_lookup();
	test_charts();
	test_charts_filter_premium();
	if (failures) {
		fprintf(stderr, "test_podcast_search: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_podcast_search: all tests passed\n");
	return 0;
}
