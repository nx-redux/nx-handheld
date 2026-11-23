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
	// the User-Agent: release tag, platform, the core as one product token
	char ua[160];
	HTTP_formatUserAgent(ua, sizeof(ua), "v2.5.1", "tg5040", "melonDS DS", "1.4.0");
	assert(!strcmp(ua, "NXRedux/v2.5.1 (tg5040) melonDS_DS/1.4.0"));
	HTTP_formatUserAgent(ua, sizeof(ua), "v2.5.1", "tg5040", "Flycast", "v2.5 4cbc7d4");
	assert(!strcmp(ua, "NXRedux/v2.5.1 (tg5040) Flycast/v2.5"));
	HTTP_formatUserAgent(ua, sizeof(ua), "v2.5.1", "tg5040", "mGBA", " 0.10.3");
	assert(!strcmp(ua, "NXRedux/v2.5.1 (tg5040) mGBA/0.10.3"));
	HTTP_formatUserAgent(ua, sizeof(ua), "v2.5.1", "tg5040", " Beetle PSX HW ", "");
	assert(!strcmp(ua, "NXRedux/v2.5.1 (tg5040) Beetle_PSX_HW"));
	HTTP_formatUserAgent(ua, sizeof(ua), "v2.5.1", "tg5050", "", "1.0");
	assert(!strcmp(ua, "NXRedux/v2.5.1 (tg5050)"));
	HTTP_formatUserAgent(ua, sizeof(ua), "v2.5.1", "tg5050", NULL, NULL);
	assert(!strcmp(ua, "NXRedux/v2.5.1 (tg5050)"));
	// a small buffer stays terminated
	HTTP_formatUserAgent(ua, 12, "v2.5.1", "tg5040", "mGBA", "0.10.3");
	assert(strlen(ua) == 11);

	return 0;
}
