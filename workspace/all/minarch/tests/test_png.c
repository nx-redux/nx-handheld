// Host test for ma_png.c: an image written by PNG_saveRGB decodes back
// (zlib inflate of the IDAT stream) to the same RGB, rows unfiltered.
#include "../ma_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int fails = 0;
#define CHECK(c, msg)                  \
	do {                               \
		if (!(c)) {                    \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		} else                         \
			printf("PASS: %s\n", msg); \
	} while (0)

static uint32_t get32(const unsigned char* p) {
	return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

int main(void) {
	const int w = 37, h = 23, pitch = w * 4 + 12; // odd size, padded rows
	uint32_t* px = calloc(h, pitch);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			((uint32_t*)((unsigned char*)px + y * pitch))[x] = 0xFF000000u | (x * 7) << 16 | (y * 11) << 8 | ((x ^ y) & 0xFF);
	const char* path = "/tmp/nx_test_png.png";
	CHECK(PNG_saveRGB(path, px, w, h, pitch) == 0, "writes the file");

	FILE* f = fopen(path, "rb");
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	rewind(f);
	unsigned char* d = malloc(n);
	fread(d, 1, n, f);
	fclose(f);
	CHECK(n > 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0, "PNG signature");

	// walk the chunks: CRCs, IHDR fields, concatenated IDAT
	unsigned char* idat = NULL;
	size_t idat_len = 0;
	int crc_ok = 1, ihdr_ok = 0, iend = 0;
	for (long i = 8; i + 12 <= n;) {
		uint32_t len = get32(d + i);
		const unsigned char* type = d + i + 4;
		if (crc32(crc32(0L, type, 4), type + 4, len) != get32(type + 4 + len))
			crc_ok = 0;
		if (!memcmp(type, "IHDR", 4))
			ihdr_ok = get32(type + 4) == (uint32_t)w && get32(type + 8) == (uint32_t)h && type[12] == 8 && type[13] == 2;
		if (!memcmp(type, "IDAT", 4)) {
			idat = realloc(idat, idat_len + len);
			memcpy(idat + idat_len, type + 4, len);
			idat_len += len;
		}
		if (!memcmp(type, "IEND", 4))
			iend = 1;
		i += 12 + len;
	}
	CHECK(crc_ok, "every chunk CRC matches");
	CHECK(ihdr_ok, "IHDR: size, 8-bit RGB");
	CHECK(iend, "ends with IEND");

	uLongf raw_len = (uLongf)h * (1 + w * 3);
	unsigned char* raw = malloc(raw_len);
	CHECK(uncompress(raw, &raw_len, idat, idat_len) == Z_OK && raw_len == (uLongf)h * (1 + w * 3), "IDAT inflates to h rows");
	int same = 1;
	for (int y = 0; y < h; y++) {
		const unsigned char* r = raw + y * (1 + w * 3);
		if (r[0] != 0)
			same = 0;
		for (int x = 0; x < w; x++) {
			uint32_t p = ((uint32_t*)((unsigned char*)px + y * pitch))[x];
			if (r[1 + x * 3] != (unsigned char)(p >> 16) || r[2 + x * 3] != (unsigned char)(p >> 8) || r[3 + x * 3] != (unsigned char)p)
				same = 0;
		}
	}
	CHECK(same, "pixels round-trip, rows unfiltered");
	CHECK(PNG_saveRGB("/nonexistent-dir/x.png", px, w, h, pitch) == -1, "unwritable path fails");
	remove(path);
	free(raw);
	free(idat);
	free(d);
	free(px);
	printf(fails ? "test_png: %d FAILED\n" : "test_png: OK\n", fails);
	return fails != 0;
}
