// A PICO-8 cart's label cut out of its cartridge image (cart_label.h). The cut is SDL-free (host-tested with
// -DCART_LABEL_NO_SDL); the loader reads the cart with SDL_image.
#include "cart_label.h"

bool CartLabel_cut(const uint32_t* src, int w, int h, int pitch, uint32_t* dst, int dst_pitch) {
	if (!src || !dst || w != CART_W || h != CART_H)
		return false;
	for (int y = 0; y < CART_LABEL_OUT; y++) {
		const uint32_t* sp = src + (CART_LABEL_Y + y / CART_LABEL_ZOOM) * pitch + CART_LABEL_X;
		uint32_t* dp = dst + y * dst_pitch;
		for (int x = 0; x < CART_LABEL_OUT; x++)
			dp[x] = sp[x / CART_LABEL_ZOOM];
	}
	return true;
}

#ifndef CART_LABEL_NO_SDL
#include "defines.h"
#include "image_load.h"
#include "utils.h"

SDL_Surface* CartLabel_load(const char* path) {
	char cart[MAX_PATH];
	if (!ROM_cartLabelCart(path, cart, sizeof(cart)))
		return Image_load(path);
	SDL_Surface* img = Image_load(cart);
	if (!img)
		return NULL;
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(img);
	if (!argb || argb->w != CART_W || argb->h != CART_H)
		return argb; // not a cart's size: the whole picture
	SDL_Surface* label = SDL_CreateRGBSurfaceWithFormat(0, CART_LABEL_OUT, CART_LABEL_OUT, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!label)
		return argb;
	CartLabel_cut(argb->pixels, argb->w, argb->h, argb->pitch / 4, label->pixels, label->pitch / 4);
	SDL_FreeSurface(argb);
	return label;
}
#endif
