// Host test for image_probe.c: header-only dimension probe.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../image_probe.h"

static char dir[] = "/tmp/nx_test_image_probeXXXXXX";

static void path_for(char* out, size_t n, const char* name) {
	snprintf(out, n, "%s/%s", dir, name);
}

static void put_segment(FILE* f, unsigned char marker, size_t payload) {
	unsigned int len = (unsigned int)payload + 2;
	fputc(0xFF, f);
	fputc(marker, f);
	fputc((len >> 8) & 0xFF, f);
	fputc(len & 0xFF, f);
	for (size_t i = 0; i < payload; i++)
		fputc(i % 251, f); // arbitrary, includes 0xFF-free filler
}

// SOI, `app_segments` APPx segments of `app_len` payload bytes each, SOF0.
static void write_jpeg(const char* path, int app_segments, size_t app_len, int w, int h) {
	FILE* f = fopen(path, "wb");
	assert(f);
	fputc(0xFF, f);
	fputc(0xD8, f);
	for (int i = 0; i < app_segments; i++)
		put_segment(f, i == 0 ? 0xE1 : 0xE2, app_len);
	const unsigned char sof[] = {0xFF, 0xC0, 0x00, 0x11, 0x08,
								 (unsigned char)(h >> 8), (unsigned char)h,
								 (unsigned char)(w >> 8), (unsigned char)w,
								 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
	fwrite(sof, 1, sizeof(sof), f);
	const unsigned char sos[] = {0xFF, 0xDA, 0x00, 0x02, 0x00, 0xFF, 0xD9};
	fwrite(sos, 1, sizeof(sos), f);
	fclose(f);
}

int main(void) {
	if (!mkdtemp(dir))
		return 1;
	char p[256];
	int w = -1, h = -1;

	// small JPEG
	path_for(p, sizeof(p), "small.jpg");
	write_jpeg(p, 1, 14, 640, 480);
	assert(ImageProbe_dimensions(p, &w, &h) && w == 640 && h == 480);

	// 8000x8000 behind a maximal (65533-byte) EXIF APP1: past the old 32 KB
	// fixed buffer, the size must still be found
	path_for(p, sizeof(p), "exif.jpg");
	write_jpeg(p, 1, 65533, 8000, 8000);
	w = h = -1;
	assert(ImageProbe_dimensions(p, &w, &h) && w == 8000 && h == 8000);
	assert(ImageProbe_isOversized(p, IMAGE_PROBE_MAX_SIDE));

	// EXIF plus a run of ICC APP2 chunks (~400 KB of metadata)
	path_for(p, sizeof(p), "icc.jpg");
	write_jpeg(p, 7, 60000, 4000, 3000);
	w = h = -1;
	assert(ImageProbe_dimensions(p, &w, &h) && w == 4000 && h == 3000);
	assert(!ImageProbe_isOversized(p, IMAGE_PROBE_MAX_SIDE));

	// truncated before SOF: no size, and not called oversized
	path_for(p, sizeof(p), "trunc.jpg");
	write_jpeg(p, 1, 1000, 8000, 8000);
	assert(truncate(p, 600) == 0);
	w = h = -1;
	assert(!ImageProbe_dimensions(p, &w, &h) && w == -1 && h == -1);
	assert(!ImageProbe_isOversized(p, IMAGE_PROBE_MAX_SIDE));

	// garbage where a marker belongs
	path_for(p, sizeof(p), "bad.jpg");
	FILE* f = fopen(p, "wb");
	fwrite("\xFF\xD8\x00\x00\x00\x00", 1, 6, f);
	fclose(f);
	assert(!ImageProbe_dimensions(p, &w, &h));

	// PNG 5000x100
	path_for(p, sizeof(p), "wide.png");
	const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
								 0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
								 0x00, 0x00, 0x13, 0x88, 0x00, 0x00, 0x00, 0x64,
								 0x08, 0x06, 0x00, 0x00, 0x00};
	f = fopen(p, "wb");
	fwrite(png, 1, sizeof(png), f);
	fclose(f);
	assert(ImageProbe_dimensions(p, &w, &h) && w == 5000 && h == 100);
	assert(ImageProbe_isOversized(p, IMAGE_PROBE_MAX_SIDE));

	// unknown format / missing file
	path_for(p, sizeof(p), "x.webp");
	f = fopen(p, "wb");
	fwrite("RIFF\0\0\0\0WEBP", 1, 12, f);
	fclose(f);
	assert(!ImageProbe_dimensions(p, &w, &h));
	assert(!ImageProbe_dimensions("/nonexistent/x.png", &w, &h));

	const char* names[] = {"small.jpg", "exif.jpg", "icc.jpg", "trunc.jpg", "bad.jpg", "wide.png", "x.webp"};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		path_for(p, sizeof(p), names[i]);
		unlink(p);
	}
	rmdir(dir);
	printf("test_image_probe: all assertions passed\n");
	return 0;
}
