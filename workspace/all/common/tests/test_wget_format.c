// Host test for the download-progress formatters in wget_fetch.h.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../wget_fetch.h"

static const char* speed(int bps) {
	static char buf[32];
	strcpy(buf, "junk");
	wget_format_speed(bps, buf, sizeof(buf));
	return buf;
}

static const char* eta(int sec) {
	static char buf[32];
	strcpy(buf, "junk");
	wget_format_eta(sec, buf, sizeof(buf));
	return buf;
}

int main(void) {
	assert(strcmp(speed(0), "") == 0);
	assert(strcmp(speed(-5), "") == 0);
	assert(strcmp(speed(512), "512 B/s") == 0);
	assert(strcmp(speed(1023), "1023 B/s") == 0);
	assert(strcmp(speed(1024), "1 KB/s") == 0);
	assert(strcmp(speed(1536 * 10), "15 KB/s") == 0);
	assert(strcmp(speed(1024 * 1024), "1.0 MB/s") == 0);
	assert(strcmp(speed(1024 * 1024 * 3 / 2), "1.5 MB/s") == 0);

	assert(strcmp(eta(0), "") == 0);
	assert(strcmp(eta(-1), "") == 0);
	assert(strcmp(eta(42), "42s") == 0);
	assert(strcmp(eta(60), "1:00") == 0);
	assert(strcmp(eta(125), "2:05") == 0);
	assert(strcmp(eta(3600), "1:00:00") == 0);
	assert(strcmp(eta(3723), "1:02:03") == 0);

	printf("test_wget_format: OK\n");
	return 0;
}
