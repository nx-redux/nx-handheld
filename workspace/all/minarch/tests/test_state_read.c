// Host test for ma_state_read.c: RASTATE header size and State_readData.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ma_state_read.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

// A memory stream handing out at most `chunk` bytes per read; fails with -1
// once `fail_at` bytes were read (when fail_at is set).
typedef struct {
	const uint8_t* data;
	size_t len, pos, chunk, fail_at;
	int reads;
} Stream;

static int64_t mem_read(void* stream, void* buf, size_t len) {
	Stream* s = stream;
	s->reads++;
	if (s->fail_at && s->pos >= s->fail_at)
		return -1;
	size_t n = s->len - s->pos;
	if (n > len)
		n = len;
	if (s->chunk && n > s->chunk)
		n = s->chunk;
	memcpy(buf, s->data + s->pos, n);
	s->pos += n;
	return (int64_t)n;
}

// An endless stream of 'z' (a corrupt state that never ends)
static int64_t endless_read(void* stream, void* buf, size_t len) {
	(void)stream;
	memset(buf, 'z', len);
	return (int64_t)len;
}

static int all_are(const uint8_t* p, size_t n, uint8_t v) {
	for (size_t i = 0; i < n; i++)
		if (p[i] != v)
			return 0;
	return 1;
}

int main(void) {
	// State_memSize
	{
		uint8_t h[RASTATE_HEADER_SIZE] = {'R', 'A', 'S', 'T', 'A', 'T', 'E', 1, 'M', 'E', 'M', ' ', 0x78, 0x56, 0x34, 0x12};
		CHECK(State_memSize(h) == 0x12345678, "RASTATE MEM size, little endian");
		h[7] = 9;
		CHECK(State_memSize(h) == 0x12345678, "any version byte");
		h[8] = 'X';
		CHECK(State_memSize(h) == 0, "not a MEM block: 0");
		uint8_t raw[RASTATE_HEADER_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
		CHECK(State_memSize(raw) == 0, "raw core data: 0");
	}

	uint8_t data[1000];
	for (size_t i = 0; i < sizeof(data); i++)
		data[i] = (uint8_t)(i % 251 + 1);
	size_t out = 0;
	uint8_t* buf;

	// want known: exactly want bytes, even if the stream has more
	{
		Stream s = {data, sizeof(data), 0, 0, 0, 0};
		buf = State_readData(mem_read, &s, 100, 300, &out);
		CHECK(buf && out == 300 && !memcmp(buf, data, 300) && s.pos == 300, "want: reads exactly want bytes");
		free(buf);
	}
	// want smaller than min_size: zero-padded to min_size
	{
		Stream s = {data, sizeof(data), 0, 0, 0, 0};
		buf = State_readData(mem_read, &s, 512, 200, &out);
		CHECK(buf && out == 512 && !memcmp(buf, data, 200) && all_are(buf + 200, 312, 0), "short state zero-padded to min_size");
		free(buf);
	}
	// raw state (want 0): reads to the end, growing past min_size
	{
		Stream s = {data, sizeof(data), 0, 64, 0, 0};
		buf = State_readData(mem_read, &s, 100, 0, &out);
		CHECK(buf && out == 1000 && !memcmp(buf, data, 1000), "raw state grows past min_size (chunked reads)");
		free(buf);
	}
	// raw state shorter than min_size
	{
		Stream s = {data, 50, 0, 0, 0, 0};
		buf = State_readData(mem_read, &s, 256, 0, &out);
		CHECK(buf && out == 256 && !memcmp(buf, data, 50) && all_are(buf + 50, 206, 0), "raw short state padded");
		free(buf);
	}
	// raw state of exactly min_size: one extra read finds the end
	{
		Stream s = {data, 128, 0, 0, 0, 0};
		buf = State_readData(mem_read, &s, 128, 0, &out);
		CHECK(buf && out == 128 && !memcmp(buf, data, 128), "raw state of exactly min_size");
		free(buf);
	}
	// want larger than the stream: what is there, padded to min_size
	{
		Stream s = {data, 100, 0, 0, 0, 0};
		buf = State_readData(mem_read, &s, 50, 400, &out);
		CHECK(buf && out == 100 && !memcmp(buf, data, 100), "stream shorter than want: data size");
		free(buf);
	}
	// reader errors: NULL, nothing leaked (ASan)
	{
		Stream s = {data, sizeof(data), 0, 64, 1, 0};
		out = 77;
		buf = State_readData(mem_read, &s, 100, 0, &out);
		CHECK(!buf && out == 77, "reader error: NULL, out_size untouched");
		Stream s2 = {data, sizeof(data), 0, 64, 300, 0};
		buf = State_readData(mem_read, &s2, 100, 0, &out);
		CHECK(!buf, "reader error after growing: NULL");
	}
	// size caps
	{
		Stream s = {data, sizeof(data), 0, 0, 0, 0};
		CHECK(!State_readData(mem_read, &s, 100, STATE_READ_MAX + 1, &out) && s.reads == 0, "want over 256 MB: NULL, no read");
		CHECK(!State_readData(mem_read, &s, STATE_READ_MAX + 1, 0, &out), "min_size over 256 MB: NULL");
		buf = State_readData(endless_read, NULL, 1 << 20, 0, &out);
		CHECK(!buf, "endless raw state stops at 256 MB: NULL");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
