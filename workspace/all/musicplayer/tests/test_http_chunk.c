// Host unit test for http_chunk.h (radio_net.c chunked decoding). Built with
// AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "http_chunk.h"

static void test_parse_size(void) {
	assert(HttpChunk_parseSize("1a") == 0x1a);
	assert(HttpChunk_parseSize("FF;name=value") == 0xff);
	assert(HttpChunk_parseSize("0") == 0);
	assert(HttpChunk_parseSize("") == -1);
	assert(HttpChunk_parseSize("zz") == -1);
	assert(HttpChunk_parseSize("-10") == -1);
	// Regression: these used to narrow to a negative int read length.
	assert(HttpChunk_parseSize("80000000") == -1);
	assert(HttpChunk_parseSize("ffffffffffffffff") == -1);
	assert(HttpChunk_parseSize("7fffffff") == INT_MAX);
}

static void test_span(void) {
	assert(HttpChunk_span(100, 0, 1000) == 100);
	assert(HttpChunk_span(100, 40, 1000) == 60);
	assert(HttpChunk_span(100, 0, 30) == 30);
	assert(HttpChunk_span(INT_MAX, 0, 256) == 256);
	assert(HttpChunk_span(10, 20, 256) == 0);
	assert(HttpChunk_span(10, 0, -5) == 0);
}

// Simulated decode into a small buffer: no write may pass its end.
static void test_decode_bounded(void) {
	const char* sizes[] = {"10", "7fffffff", "0"};
	char buf[24];
	int total = 0;
	for (int i = 0; i < 3 && total < (int)sizeof(buf) - 1; i++) {
		long size = HttpChunk_parseSize(sizes[i]);
		if (size <= 0)
			break;
		long done = 0;
		while (done < size && total < (int)sizeof(buf) - 1) {
			int n = HttpChunk_span(size, done, (long)sizeof(buf) - 1 - total);
			assert(n > 0);
			memset(buf + total, 'x', n);
			total += n;
			done += n;
		}
	}
	buf[total] = '\0';
	assert(total == (int)sizeof(buf) - 1);
}

int main(void) {
	test_parse_size();
	test_span();
	test_decode_bounded();
	printf("test_http_chunk: OK\n");
	return 0;
}
