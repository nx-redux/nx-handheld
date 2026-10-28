#ifndef P8SHIM_FRAME_H
#define P8SHIM_FRAME_H

// PICO-8's 128x128 screen texture → the XRGB8888 block the core hands to minarch.

#include <stdint.h>

// SDL_PIXELFORMAT_* values (SDL_pixels.h); the shim builds without SDL headers
#define P8_FMT_RGB565 0x15151002u
#define P8_FMT_BGR565 0x15551002u
#define P8_FMT_XRGB8888 0x16161804u
#define P8_FMT_RGBX8888 0x16261804u
#define P8_FMT_ARGB8888 0x16362004u
#define P8_FMT_RGBA8888 0x16462004u
#define P8_FMT_XBGR8888 0x16561804u
#define P8_FMT_BGRX8888 0x16661804u
#define P8_FMT_ABGR8888 0x16762004u
#define P8_FMT_BGRA8888 0x16862004u

// Converts one 128x128 frame of rows pitch bytes apart into out[128*128] as 0x00RRGGBB.
// 0 ok, -1 when the format is not one above or pitch is shorter than a row.
int frame_convert(const void* pixels, int pitch, uint32_t format, uint32_t* out);

// Name for the log; "unknown" for any other value
const char* frame_format_name(uint32_t format);

#endif
