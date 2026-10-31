#include <string.h>

#include "image_probe.h"

static unsigned int read_be16(const unsigned char* p) {
	return ((unsigned int)p[0] << 8) | p[1];
}

static unsigned int read_le16(const unsigned char* p) {
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int read_be32(const unsigned char* p) {
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

static int read_le32(const unsigned char* p) {
	unsigned int v = (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
	return (int)v;
}

static bool set_dims(int width, int height, int* w, int* h) {
	if (width <= 0 || height <= 0)
		return false;
	*w = width;
	*h = height;
	return true;
}

// Marker walk starting past SOI. Segments are skipped with fseek, so a large
// APP1 (EXIF thumbnail) or a run of APP2 (ICC) chunks costs no buffering.
// Stops at the first SOFx (frame header holding the real dimensions), SOS or
// end of file.
static bool probe_jpeg(FILE* f, int* w, int* h) {
	if (fseek(f, 2, SEEK_SET) != 0)
		return false;
	// a valid file reaches SOF in a handful of segments; bound the walk anyway
	for (int segments = 0; segments < 4096; segments++) {
		int c = fgetc(f);
		if (c != 0xFF)
			return false; // not a marker where one is expected
		while (c == 0xFF) // skip 0xFF fill bytes
			c = fgetc(f);
		if (c == EOF)
			return false;
		unsigned char m = (unsigned char)c;

		if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7))
			continue; // standalone markers, no length field
		if (m == 0xDA)
			return false; // SOS: entropy-coded data follows, no more markers

		unsigned char len[2];
		if (fread(len, 1, 2, f) != 2)
			return false;
		unsigned int seg_len = read_be16(len);
		if (seg_len < 2)
			return false;

		if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
			// SOFx: precision(1) height(2) width(2), big-endian
			unsigned char sof[5];
			if (seg_len < 7 || fread(sof, 1, 5, f) != 5)
				return false;
			return set_dims((int)read_be16(sof + 3), (int)read_be16(sof + 1), w, h);
		}

		if (fseek(f, (long)seg_len - 2, SEEK_CUR) != 0)
			return false;
	}
	return false;
}

// WebP: the first chunk after RIFF....WEBP. VP8 (lossy): the key frame's start code, then 14-bit width and height;
// VP8L (lossless): the 0x2F signature, then 14-bit width-1 and height-1; VP8X (extended): 24-bit canvas width-1 and
// height-1.
static bool probe_webp(const unsigned char* buf, int* w, int* h) {
	if (memcmp(buf + 12, "VP8 ", 4) == 0 && buf[23] == 0x9D && buf[24] == 0x01 && buf[25] == 0x2A)
		return set_dims((int)(read_le16(buf + 26) & 0x3FFF), (int)(read_le16(buf + 28) & 0x3FFF), w, h);
	if (memcmp(buf + 12, "VP8L", 4) == 0 && buf[20] == 0x2F) {
		unsigned int bits = buf[21] | buf[22] << 8 | buf[23] << 16 | (unsigned int)buf[24] << 24;
		return set_dims((int)(bits & 0x3FFF) + 1, (int)((bits >> 14) & 0x3FFF) + 1, w, h);
	}
	if (memcmp(buf + 12, "VP8X", 4) == 0)
		return set_dims((int)(buf[24] | buf[25] << 8 | buf[26] << 16) + 1, (int)(buf[27] | buf[28] << 8 | buf[29] << 16) + 1,
						w, h);
	return false;
}

bool ImageProbe_dimensionsFile(FILE* f, int* w, int* h) {
	unsigned char buf[30];
	if (fseek(f, 0, SEEK_SET) != 0)
		return false;
	size_t n = fread(buf, 1, sizeof(buf), f);

	if (n >= 24 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0)
		return set_dims((int)read_be32(buf + 16), (int)read_be32(buf + 20), w, h);
	if (n >= 10 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0))
		return set_dims((int)read_le16(buf + 6), (int)read_le16(buf + 8), w, h);
	if (n >= 26 && buf[0] == 'B' && buf[1] == 'M') {
		int height = read_le32(buf + 22);
		if (height < 0)
			height = -height; // top-down BMPs store a negative height
		return set_dims(read_le32(buf + 18), height, w, h);
	}
	if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xD8)
		return probe_jpeg(f, w, h);
	if (n >= 30 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WEBP", 4) == 0)
		return probe_webp(buf, w, h);

	return false;
}

bool ImageProbe_dimensions(const char* path, int* w, int* h) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;
	bool ok = ImageProbe_dimensionsFile(f, w, h);
	fclose(f);
	return ok;
}

bool ImageProbe_isOversized(const char* path, int max_side) {
	int w = 0, h = 0;
	if (!ImageProbe_dimensions(path, &w, &h))
		return false;
	return w > max_side || h > max_side;
}
