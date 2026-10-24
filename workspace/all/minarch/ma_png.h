#ifndef MA_PNG_H
#define MA_PNG_H

#include <stdint.h>

// Writes w x h pixels (ARGB8888 in native order, as SDL_PIXELFORMAT_ARGB8888,
// `pitch` bytes per row) to `path` as an 8-bit RGB PNG: no row filter and
// zlib's fastest level. Save-state screenshots are opaque frames that minarch
// writes while quitting; SDL_image's encoder took most of a second for a
// 1280x720 frame on the Smart Pro S. 0 on success, -1 on any error (a partial
// file is removed).
int PNG_saveRGB(const char* path, const uint32_t* pixels, int w, int h, int pitch);

#endif
