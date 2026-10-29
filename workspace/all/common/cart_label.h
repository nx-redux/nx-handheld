#ifndef CART_LABEL_H
#define CART_LABEL_H

#include <stdbool.h>
#include <stdint.h>

// A PICO-8 .p8.png cart's label: the 128x128 picture printed on the 160x205 cartridge image, at (16, 24). A screenshot
// slot shows it in place of the whole cart (ROM_cartLabelPath, utils.h), cut out and scaled up nearest-neighbour by
// CART_LABEL_ZOOM, so the views' smooth scalers start from pixels big enough to stay sharp at the screen's height.
#define CART_W 160
#define CART_H 205
#define CART_LABEL_X 16
#define CART_LABEL_Y 24
#define CART_LABEL_SIDE 128
#define CART_LABEL_ZOOM 6 // 768 px: the 1024x768 screen's height, just over the 1280x720 one's
#define CART_LABEL_OUT (CART_LABEL_SIDE * CART_LABEL_ZOOM)

// SDL-free core (host-tested by tests/test_cart_label.c with -DCART_LABEL_NO_SDL).

// The label of a w x h cart (32-bit pixels, pitches in pixels) into dst, CART_LABEL_OUT square, each label pixel a
// CART_LABEL_ZOOM block. false (dst untouched) when the image is not a cart's size.
bool CartLabel_cut(const uint32_t* src, int w, int h, int pitch, uint32_t* dst, int dst_pitch);

#ifndef CART_LABEL_NO_SDL
#include "sdl.h"

// IMG_Load for any art path a view may get: a label path (ROM_cartLabelPath) reads its cart and gives the label
// (ARGB8888, CART_LABEL_OUT square), or the whole cart when the image is not a cart's size (a hand-made or broken
// one); any other path is IMG_Load's. NULL on failure; the caller frees the result.
SDL_Surface* CartLabel_load(const char* path);
#endif

#endif
