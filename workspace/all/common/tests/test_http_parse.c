// Host test for http_parse.h: curl's "<body>\n<status>" output split.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../http_parse.h"

static int split(const char* in, char* buf, size_t* size) {
	*size = strlen(in);
	memcpy(buf, in, *size + 1);
	return HTTP_splitStatus(buf, size);
}

int main(void) {
	char buf[256];
	size_t n;

	assert(split("{\"Success\":true}\n200", buf, &n) == 200);
	assert(n == 16 && !strcmp(buf, "{\"Success\":true}"));

	// an empty body
	assert(split("\n404", buf, &n) == 404 && n == 0 && buf[0] == '\0');

	// a body with newlines of its own: only the last line is the status
	assert(split("a\nb\n\n302", buf, &n) == 302 && n == 4 && !strcmp(buf, "a\nb\n"));

	// no status line: left as it was
	assert(split("no newline", buf, &n) == -1 && n == 10 && !strcmp(buf, "no newline"));
	// "000" (no connection) is not a status
	assert(split("\n000", buf, &n) == -1 && n == 4);
	assert(split("body\ncurl: (28) timed out", buf, &n) == -1);
	assert(HTTP_splitStatus(NULL, &n) == -1);
	n = 0;
	assert(HTTP_splitStatus(buf, &n) == -1);

	// transport failures are named; anything else still gives a message
	assert(strstr(HTTP_curlErrorText(28), "timed out"));
	assert(strstr(HTTP_curlErrorText(60), "certificate"));
	assert(HTTP_curlErrorText(9999)[0]);
	printf("ok test_http_parse\n");
	return 0;
}
