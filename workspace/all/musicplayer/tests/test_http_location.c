// Host-compiled unit test for HttpLocation_resolve (http_location.h): relative
// redirect targets used to be passed on as is and failed with an empty host.
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_http_location.c -o /tmp/t && /tmp/t
#include <stdio.h>
#include <string.h>

#include "../http_location.h"

static int failures;

static void check(const char* base, const char* location, const char* want) {
	char out[256];
	HttpLocation_resolve(base, location, out, sizeof(out));
	if (strcmp(out, want) != 0) {
		fprintf(stderr, "FAIL %s + %s: got %s, want %s\n", base, location, out, want);
		failures++;
	}
}

int main(void) {
	check("http://a.com/x/y", "https://b.com/z", "https://b.com/z");
	check("http://a.com/x/y", "HTTP://b.com/z", "HTTP://b.com/z");
	check("http://a.com:8000/x/y?q=1", "/live", "http://a.com:8000/live");
	check("https://a.com/x/y", "//cdn.com/s.mp3", "https://cdn.com/s.mp3");
	check("http://a.com/x/y.pls?q=1", "stream.mp3", "http://a.com/x/stream.mp3");
	check("http://a.com/x/y", "?token=2", "http://a.com/x/y?token=2");
	check("http://a.com", "s.mp3", "http://a.com/s.mp3");
	check("http://a.com?x=/y/z", "s.mp3", "http://a.com/s.mp3");
	check("http://a.com", "/s.mp3", "http://a.com/s.mp3");
	check("no-scheme", "/s.mp3", "/s.mp3");

	// cut to fit, always terminated
	char small[12];
	HttpLocation_resolve("http://a.com/x/y", "/a-long-path", small, sizeof(small));
	if (strlen(small) != sizeof(small) - 1) {
		fprintf(stderr, "FAIL truncation: %s\n", small);
		failures++;
	}

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("test_http_location: all passed\n");
	return 0;
}
