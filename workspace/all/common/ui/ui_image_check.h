#ifndef UI_IMAGE_CHECK_H
#define UI_IMAGE_CHECK_H
// The pure parts of ui_image.c (no SDL), so the host tests can cover them.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// How far from the end the image's end marker may sit: encoders and servers
// append padding, metadata or a second copy of the trailer after it
#define UI_IMAGE_TAIL_SCAN 4096

// Fit img_w x img_h into max_w x max_h preserving aspect ratio. An empty
// image (either side <= 0) fits as 0 x 0.
static inline void UI_imageFit(int img_w, int img_h, int max_w, int max_h, int* out_w, int* out_h) {
	if (img_w <= 0 || img_h <= 0) {
		*out_w = 0;
		*out_h = 0;
		return;
	}
	double aspect_ratio = (double)img_h / img_w;
	int new_w = max_w;
	int new_h = (int)(new_w * aspect_ratio);

	if (new_h > max_h) {
		new_h = max_h;
		new_w = (int)(new_h / aspect_ratio);
	}

	*out_w = new_w;
	*out_h = new_h;
}

// True when the last UI_IMAGE_TAIL_SCAN bytes of data hold marker
static inline bool UI_imageTailHas(const uint8_t* data, size_t size, const uint8_t* marker,
								   size_t marker_len) {
	if (size < marker_len)
		return false;
	size_t from = size > UI_IMAGE_TAIL_SCAN ? size - UI_IMAGE_TAIL_SCAN : 0;
	for (size_t i = size - marker_len + 1; i-- > from;) {
		if (memcmp(data + i, marker, marker_len) == 0)
			return true;
	}
	return false;
}

// True when data looks like a fully-downloaded image: a JPEG (FF D8) with its
// EOI (FF D9), a PNG with its IEND chunk, near the end. Unknown formats are
// assumed complete.
static inline bool UI_imageComplete(const uint8_t* data, size_t size) {
	if (size < 4)
		return false;
	if (data[0] == 0xFF && data[1] == 0xD8) {
		// FF never appears unstuffed in entropy-coded data, so an FF D9 near
		// the end is the EOI, not image data
		static const uint8_t eoi[] = {0xFF, 0xD9};
		return UI_imageTailHas(data, size, eoi, sizeof(eoi));
	}
	if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
		// "IEND" + its fixed CRC
		static const uint8_t iend[] = {'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
		return size >= 8 && UI_imageTailHas(data, size, iend, sizeof(iend));
	}
	return true;
}

#endif
