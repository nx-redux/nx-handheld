#include "ma_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put32(unsigned char* p, uint32_t v) {
	p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v;
}

// One chunk: length, type, data, CRC over type + data
static int writeChunk(FILE* f, const char* type, const unsigned char* data, uint32_t len) {
	unsigned char head[8];
	put32(head, len);
	memcpy(head + 4, type, 4);
	uLong crc = crc32(0L, (const Bytef*)type, 4);
	if (len)
		crc = crc32(crc, data, len);
	unsigned char tail[4];
	put32(tail, (uint32_t)crc);
	return fwrite(head, 1, 8, f) == 8 && (!len || fwrite(data, 1, len, f) == len) && fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

int PNG_saveRGB(const char* path, const uint32_t* pixels, int w, int h, int pitch) {
	if (!path || !pixels || w <= 0 || h <= 0 || pitch < w * 4)
		return -1;
	FILE* f = fopen(path, "wb");
	if (!f)
		return -1;

	size_t row_len = 1 + (size_t)w * 3; // filter byte + RGB
	unsigned char* row = malloc(row_len);
	size_t out_cap = 256 * 1024;
	unsigned char* out = malloc(out_cap);
	z_stream z;
	memset(&z, 0, sizeof(z));
	int ok = row && out && deflateInit(&z, Z_BEST_SPEED) == Z_OK;

	static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	unsigned char ihdr[13];
	put32(ihdr, w);
	put32(ihdr + 4, h);
	ihdr[8] = 8;  // bits per channel
	ihdr[9] = 2;  // RGB
	ihdr[10] = 0; // deflate
	ihdr[11] = 0; // adaptive filtering (each row says "none")
	ihdr[12] = 0; // no interlace
	ok = ok && fwrite(signature, 1, 8, f) == 8 && writeChunk(f, "IHDR", ihdr, 13) == 0;

	// rows go through deflate; every time the output buffer fills it becomes one IDAT
	for (int y = 0; ok && y <= h; y++) {
		int flush = y == h ? Z_FINISH : Z_NO_FLUSH;
		if (y < h) {
			const uint32_t* src = (const uint32_t*)((const unsigned char*)pixels + (size_t)y * pitch);
			unsigned char* dst = row;
			*dst++ = 0;
			for (int x = 0; x < w; x++) {
				uint32_t p = src[x];
				*dst++ = p >> 16, *dst++ = p >> 8, *dst++ = p;
			}
			z.next_in = row;
			z.avail_in = row_len;
		} else {
			z.next_in = NULL;
			z.avail_in = 0;
		}
		int ret;
		do {
			z.next_out = out;
			z.avail_out = out_cap;
			ret = deflate(&z, flush);
			if (ret == Z_STREAM_ERROR) {
				ok = 0;
				break;
			}
			uint32_t have = out_cap - z.avail_out;
			if (have && writeChunk(f, "IDAT", out, have) != 0)
				ok = 0;
		} while (ok && z.avail_out == 0);
		if (flush == Z_FINISH && ret != Z_STREAM_END)
			ok = 0;
	}
	ok = ok && writeChunk(f, "IEND", NULL, 0) == 0;

	deflateEnd(&z);
	free(row);
	free(out);
	if (fclose(f) != 0)
		ok = 0;
	if (!ok)
		remove(path);
	return ok ? 0 : -1;
}
