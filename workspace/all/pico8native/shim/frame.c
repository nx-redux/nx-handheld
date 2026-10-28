#include <string.h>

#include "../p8ipc.h"
#include "frame.h"

static uint32_t expand565(uint16_t v, int bgr) {
	uint32_t hi = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, lo = v & 0x1f;
	hi = (hi << 3) | (hi >> 2);
	g = (g << 2) | (g >> 4);
	lo = (lo << 3) | (lo >> 2);
	return bgr ? (lo << 16) | (g << 8) | hi : (hi << 16) | (g << 8) | lo;
}

// A 32-bit pixel as read (native, little-endian) → 0x00RRGGBB
static uint32_t to_xrgb(uint32_t v, uint32_t format) {
	switch (format) {
	case P8_FMT_ARGB8888:
	case P8_FMT_XRGB8888:
		return v & 0xffffffu;
	case P8_FMT_RGBA8888:
	case P8_FMT_RGBX8888:
		return v >> 8;
	case P8_FMT_ABGR8888:
	case P8_FMT_XBGR8888:
		return ((v & 0xff) << 16) | (v & 0xff00) | ((v >> 16) & 0xff);
	default: // BGRA8888, BGRX8888
		return ((v >> 8) & 0xff) << 16 | ((v >> 16) & 0xff) << 8 | (v >> 24);
	}
}

int frame_convert(const void* pixels, int pitch, uint32_t format, uint32_t* out) {
	int bpp;
	switch (format) {
	case P8_FMT_RGB565:
	case P8_FMT_BGR565:
		bpp = 2;
		break;
	case P8_FMT_XRGB8888:
	case P8_FMT_RGBX8888:
	case P8_FMT_ARGB8888:
	case P8_FMT_RGBA8888:
	case P8_FMT_XBGR8888:
	case P8_FMT_BGRX8888:
	case P8_FMT_ABGR8888:
	case P8_FMT_BGRA8888:
		bpp = 4;
		break;
	default:
		return -1;
	}
	if (!pixels || !out || pitch < P8_W * bpp)
		return -1;

	for (int y = 0; y < P8_H; y++) {
		const uint8_t* row = (const uint8_t*)pixels + (size_t)y * pitch;
		uint32_t* dst = out + y * P8_W;
		if (bpp == 2) {
			int bgr = format == P8_FMT_BGR565;
			for (int x = 0; x < P8_W; x++) {
				uint16_t v;
				memcpy(&v, row + x * 2, 2); // pitch need not keep rows aligned
				dst[x] = expand565(v, bgr);
			}
		} else {
			for (int x = 0; x < P8_W; x++) {
				uint32_t v;
				memcpy(&v, row + x * 4, 4);
				dst[x] = to_xrgb(v, format);
			}
		}
	}
	return 0;
}

const char* frame_format_name(uint32_t format) {
	switch (format) {
	case P8_FMT_RGB565:
		return "RGB565";
	case P8_FMT_BGR565:
		return "BGR565";
	case P8_FMT_XRGB8888:
		return "XRGB8888";
	case P8_FMT_RGBX8888:
		return "RGBX8888";
	case P8_FMT_ARGB8888:
		return "ARGB8888";
	case P8_FMT_RGBA8888:
		return "RGBA8888";
	case P8_FMT_XBGR8888:
		return "XBGR8888";
	case P8_FMT_BGRX8888:
		return "BGRX8888";
	case P8_FMT_ABGR8888:
		return "ABGR8888";
	case P8_FMT_BGRA8888:
		return "BGRA8888";
	default:
		return "unknown";
	}
}
