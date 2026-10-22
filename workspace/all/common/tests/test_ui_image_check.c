// Host test for ui/ui_image_check.h: image completeness and fit.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../ui/ui_image_check.h"

static uint8_t buf[16384];

// A JPEG-ish buffer: SOI, filler (no FF), EOI, then `trailing` bytes after it
static size_t make_jpeg(size_t body, size_t trailing, bool eoi) {
	size_t n = 0;
	buf[n++] = 0xFF;
	buf[n++] = 0xD8;
	for (size_t i = 0; i < body; i++)
		buf[n++] = (uint8_t)(i % 200);
	if (eoi) {
		buf[n++] = 0xFF;
		buf[n++] = 0xD9;
	}
	for (size_t i = 0; i < trailing; i++)
		buf[n++] = 0;
	return n;
}

static size_t make_png(size_t trailing, bool iend) {
	static const uint8_t sig[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	static const uint8_t end[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
	size_t n = 0;
	memcpy(buf, sig, sizeof(sig));
	n += sizeof(sig);
	memset(buf + n, 7, 100);
	n += 100;
	if (iend) {
		memcpy(buf + n, end, sizeof(end));
		n += sizeof(end);
	}
	memset(buf + n, 0, trailing);
	return n + trailing;
}

int main(void) {
	assert(UI_imageComplete(buf, make_jpeg(1000, 0, true)));
	// padding or metadata after the EOI: still complete (was deleted as corrupt)
	assert(UI_imageComplete(buf, make_jpeg(1000, 1, true)));
	assert(UI_imageComplete(buf, make_jpeg(1000, 600, true)));
	// cut off before the EOI
	assert(!UI_imageComplete(buf, make_jpeg(1000, 0, false)));
	assert(!UI_imageComplete(buf, make_jpeg(9000, 0, false)));
	// an EOI further back than the scanned tail is not trusted
	assert(!UI_imageComplete(buf, make_jpeg(100, UI_IMAGE_TAIL_SCAN + 10, true)));

	assert(UI_imageComplete(buf, make_png(0, true)));
	assert(UI_imageComplete(buf, make_png(32, true)));
	assert(!UI_imageComplete(buf, make_png(0, false)));

	assert(!UI_imageComplete(buf, 3));
	assert(UI_imageComplete((const uint8_t*)"GIF89a", 6)); // unknown: assumed complete

	int w, h;
	UI_imageFit(200, 100, 100, 100, &w, &h);
	assert(w == 100 && h == 50);
	UI_imageFit(100, 200, 100, 100, &w, &h);
	assert(w == 50 && h == 100);
	UI_imageFit(100, 100, 80, 60, &w, &h);
	assert(w == 60 && h == 60);
	// an empty image no longer divides by zero
	UI_imageFit(0, 100, 80, 60, &w, &h);
	assert(w == 0 && h == 0);
	UI_imageFit(100, 0, 80, 60, &w, &h);
	assert(w == 0 && h == 0);
	printf("ok test_ui_image_check\n");
	return 0;
}
