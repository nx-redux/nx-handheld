// Host test for the PICO-8 cart label cut (cart_label.c, SDL-free part): the 128x128 picture at (16, 24) of a 160x205
// cart, each pixel a CART_LABEL_ZOOM block; any other size is refused and dst left alone.
#include "../cart_label.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// A pixel that names its own place, so a wrong offset or stride shows.
static uint32_t at(int x, int y) {
	return 0xFF000000u | ((uint32_t)y << 12) | (uint32_t)x;
}

int main(void) {
	int pitch = CART_W + 3; // a padded row: the pitch, not the width, steps the rows
	uint32_t* cart = malloc(sizeof(uint32_t) * pitch * CART_H);
	assert(cart);
	for (int y = 0; y < CART_H; y++)
		for (int x = 0; x < pitch; x++)
			cart[y * pitch + x] = x < CART_W ? at(x, y) : 0xDEADBEEFu;

	int dpitch = CART_LABEL_OUT + 5;
	uint32_t* dst = malloc(sizeof(uint32_t) * dpitch * CART_LABEL_OUT);
	assert(dst);
	for (int i = 0; i < dpitch * CART_LABEL_OUT; i++)
		dst[i] = 0x12345678u;

	assert(CART_LABEL_OUT == 768);
	assert(CartLabel_cut(cart, CART_W, CART_H, pitch, dst, dpitch));
	for (int y = 0; y < CART_LABEL_OUT; y++) {
		for (int x = 0; x < CART_LABEL_OUT; x++)
			assert(dst[y * dpitch + x] == at(CART_LABEL_X + x / CART_LABEL_ZOOM, CART_LABEL_Y + y / CART_LABEL_ZOOM));
		for (int x = CART_LABEL_OUT; x < dpitch; x++)
			assert(dst[y * dpitch + x] == 0x12345678u); // the row's padding untouched
	}
	// the corners: the label's first and last pixels, nothing of the cart's frame around it
	assert(dst[0] == at(16, 24));
	assert(dst[(CART_LABEL_OUT - 1) * dpitch + CART_LABEL_OUT - 1] == at(16 + 127, 24 + 127));

	// not a cart's size (a hand-made or broken .p8.png): refused, dst untouched
	for (int i = 0; i < dpitch * CART_LABEL_OUT; i++)
		dst[i] = 0x12345678u;
	assert(!CartLabel_cut(cart, CART_W - 1, CART_H, pitch, dst, dpitch));
	assert(!CartLabel_cut(cart, CART_W, CART_H + 1, pitch, dst, dpitch));
	assert(!CartLabel_cut(cart, 128, 128, pitch, dst, dpitch));
	assert(!CartLabel_cut(NULL, CART_W, CART_H, pitch, dst, dpitch));
	assert(!CartLabel_cut(cart, CART_W, CART_H, pitch, NULL, dpitch));
	for (int i = 0; i < dpitch * CART_LABEL_OUT; i++)
		assert(dst[i] == 0x12345678u);

	free(cart);
	free(dst);
	printf("test_cart_label: ok\n");
	return 0;
}
