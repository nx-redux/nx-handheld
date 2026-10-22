// Host test for podcast_rss.c podcast_rss_is_complete: a feed cut off
// mid-download still parses (up to the cut), so refresh must detect the cut
// and keep the saved episode list instead of replacing it with the newest few.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "podcast.h"

static char* build_feed(int episodes, const char* root) {
	size_t cap = 4096 + (size_t)episodes * 512;
	char* xml = malloc(cap);
	size_t n = 0;
	if (strcmp(root, "rss") == 0)
		n += snprintf(xml + n, cap - n, "<?xml version=\"1.0\"?>\n<rss version=\"2.0\"><channel><title>Show</title>\n");
	else
		n += snprintf(xml + n, cap - n, "<?xml version=\"1.0\"?>\n<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Show</title>\n");
	for (int i = 0; i < episodes; i++)
		n += snprintf(xml + n, cap - n,
					  "<item><title>Episode %d</title><guid>ep-%d</guid>"
					  "<enclosure url=\"https://example.com/%d.mp3\" type=\"audio/mpeg\" length=\"1\"/></item>\n",
					  i, i, i);
	if (strcmp(root, "rss") == 0)
		n += snprintf(xml + n, cap - n, "</channel></rss>\n\n");
	else
		n += snprintf(xml + n, cap - n, "</feed>\n");
	return xml;
}

// Publication dates: weekday optional, zone applied, independent of local TZ
static void test_dates(void) {
	const uint32_t t = 1736848800; // 2025-01-14 10:00:00 UTC
	assert(podcast_rss_parse_date("Tue, 14 Jan 2025 10:00:00 GMT") == t);
	assert(podcast_rss_parse_date("14 Jan 2025 10:00:00 +0000") == t);
	assert(podcast_rss_parse_date("Tue, 14 Jan 2025 11:00:00 +0100") == t);
	assert(podcast_rss_parse_date("Tue, 14 Jan 2025 05:00:00 EST") == t);
	assert(podcast_rss_parse_date("Tue, 14 Jan 2025 02:00 PST") == t);
	assert(podcast_rss_parse_date("Tue, 14 jan 25 10:00:00 Z") == t);
	assert(podcast_rss_parse_date("2025-01-14T10:00:00Z") == t);
	assert(podcast_rss_parse_date("2025-01-14T12:00:00.250+02:00") == t);
	assert(podcast_rss_parse_date("2025-01-14") == t - 36000);
	assert(podcast_rss_parse_date("14 Foo 2025") == 0);
	assert(podcast_rss_parse_date("") == 0);
	assert(podcast_rss_parse_date(NULL) == 0);
}

int main(void) {
	setenv("TZ", "Asia/Kuala_Lumpur", 1);
	tzset();
	test_dates();

	char* rss = build_feed(200, "rss");
	int len = (int)strlen(rss);
	assert(podcast_rss_is_complete(rss, len));

	// the cut: the parser still succeeds on the front half...
	int cut = len / 2;
	PodcastFeed feed;
	memset(&feed, 0, sizeof(feed));
	PodcastEpisode* eps = calloc(2000, sizeof(PodcastEpisode));
	int count = 0;
	int r = podcast_rss_parse_with_episodes(rss, cut, &feed, eps, 2000, &count);
	printf("cut feed parse: rc=%d episodes=%d of 200\n", r, count);
	// ...so only the completeness check stands between it and the saved list
	assert(!podcast_rss_is_complete(rss, cut));
	assert(!podcast_rss_is_complete(rss, len - 4)); // "</rss>" itself cut
	assert(!podcast_rss_is_complete(rss, 0));
	assert(!podcast_rss_is_complete(NULL, 10));

	char* atom = build_feed(3, "feed");
	assert(podcast_rss_is_complete(atom, (int)strlen(atom)));
	const char* rdf = "<rdf:RDF><channel/></rdf:RDF>";
	assert(podcast_rss_is_complete(rdf, (int)strlen(rdf)));
	const char* upper = "<RSS><channel/></RSS>";
	assert(podcast_rss_is_complete(upper, (int)strlen(upper)));

	free(eps);
	free(rss);
	free(atom);
	printf("test_podcast_rss: all passed\n");
	return 0;
}
