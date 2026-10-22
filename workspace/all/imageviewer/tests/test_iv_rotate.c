// Host-compiled unit test for iv_rotate.c (surface rotation + the PNG file
// path; the ffmpeg path is exercised on device).
// Build & run (from workspace/all/imageviewer; links the host SDL2, so no
// -fsanitize=address: the Homebrew sdl2-compat fails to load SDL3 under ASan
// and blocks on an error dialog):
//   cc -std=gnu99 -DUSE_SDL2 -I. -I../common -I$(sdl2-config --prefix)/include iv_rotate.c tests/test_iv_rotate.c \
//      $(sdl2-config --libs) -lSDL2_image -o /tmp/test_iv_rotate && /tmp/test_iv_rotate
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../iv_rotate.h"

static Uint32 px(SDL_Surface* s, int x, int y) {
	return ((Uint32*)((Uint8*)s->pixels + y * s->pitch))[x];
}

// 3x2 ARGB surface with a distinct value per pixel: v = 1 + x + y*3.
static SDL_Surface* make_3x2(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 3, 2, 32, SDL_PIXELFORMAT_ARGB8888);
	for (int y = 0; y < 2; y++)
		for (int x = 0; x < 3; x++)
			((Uint32*)((Uint8*)s->pixels + y * s->pitch))[x] = 0xFF000000 | (1 + x + y * 3);
	return s;
}

int main(void) {
	SDL_Surface* s = make_3x2();
	// 1 2 3
	// 4 5 6

	// Clockwise: 4 1 / 5 2 / 6 3
	SDL_Surface* r = IvRotate_surface(s, 1);
	assert(r && r->w == 2 && r->h == 3);
	assert((px(r, 0, 0) & 0xFF) == 4 && (px(r, 1, 0) & 0xFF) == 1);
	assert((px(r, 0, 2) & 0xFF) == 6 && (px(r, 1, 2) & 0xFF) == 3);
	SDL_FreeSurface(r);

	// Half turn: 6 5 4 / 3 2 1
	r = IvRotate_surface(s, 2);
	assert(r && r->w == 3 && r->h == 2);
	assert((px(r, 0, 0) & 0xFF) == 6 && (px(r, 2, 1) & 0xFF) == 1);
	SDL_FreeSurface(r);

	// Counter-clockwise: 3 6 / 2 5 / 1 4
	r = IvRotate_surface(s, 3);
	assert(r && r->w == 2 && r->h == 3);
	assert((px(r, 0, 0) & 0xFF) == 3 && (px(r, 1, 0) & 0xFF) == 6);
	assert((px(r, 0, 2) & 0xFF) == 1 && (px(r, 1, 2) & 0xFF) == 4);
	SDL_FreeSurface(r);

	assert(IvRotate_surface(s, 0) == NULL);
	assert(IvRotate_surface(s, 4) == NULL);

	// -1 turn is a counter-clockwise quarter, same as 3
	r = IvRotate_surface(s, -1);
	assert(r && r->w == 2 && r->h == 3);
	assert((px(r, 0, 0) & 0xFF) == 3 && (px(r, 1, 2) & 0xFF) == 4);
	SDL_FreeSurface(r);

	// Four clockwise quarters give the original back
	SDL_Surface* cur = s;
	for (int i = 0; i < 4; i++) {
		SDL_Surface* next = IvRotate_surface(cur, 1);
		assert(next);
		if (cur != s)
			SDL_FreeSurface(cur);
		cur = next;
	}
	assert(cur->w == 3 && cur->h == 2);
	for (int y = 0; y < 2; y++)
		for (int x = 0; x < 3; x++)
			assert(px(cur, x, y) == px(s, x, y));
	SDL_FreeSurface(cur);

	// 16-bit surfaces (the device screen format) rotate too, pixel for pixel.
	SDL_Surface* s16 = SDL_CreateRGBSurfaceWithFormat(0, 3, 2, 16, SDL_PIXELFORMAT_RGB565);
	for (int y = 0; y < 2; y++)
		for (int x = 0; x < 3; x++)
			((Uint16*)((Uint8*)s16->pixels + y * s16->pitch))[x] = (Uint16)(0x1000 + 1 + x + y * 3);
	r = IvRotate_surface(s16, 1);
	assert(r && r->w == 2 && r->h == 3 && r->format->format == SDL_PIXELFORMAT_RGB565);
	// Clockwise: 4 1 / 5 2 / 6 3
	assert(((Uint16*)r->pixels)[0] == 0x1004 && ((Uint16*)r->pixels)[1] == 0x1001);
	assert(((Uint16*)((Uint8*)r->pixels + 2 * r->pitch))[0] == 0x1006);
	assert(((Uint16*)((Uint8*)r->pixels + 2 * r->pitch))[1] == 0x1003);
	SDL_FreeSurface(r);
	SDL_FreeSurface(s16);

	// PNG file: rotated in place, no temp file left behind.
	system("rm -rf /tmp/iv-rot-test && mkdir -p /tmp/iv-rot-test");
	assert(IMG_SavePNG(s, "/tmp/iv-rot-test/a.png") == 0);
	assert(IvRotate_file("/tmp/iv-rot-test/a.png", 1));
	assert(access("/tmp/iv-rot-test/.rotate-tmp.png", F_OK) != 0);
	SDL_Surface* loaded = IMG_Load("/tmp/iv-rot-test/a.png");
	SDL_Surface* argb = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
	assert(argb->w == 2 && argb->h == 3);
	assert((px(argb, 0, 0) & 0xFF) == 4 && (px(argb, 1, 2) & 0xFF) == 3);
	SDL_FreeSurface(argb);
	SDL_FreeSurface(loaded);

	// Zero turns is a no-op success; a missing file fails and leaves nothing.
	assert(IvRotate_file("/tmp/iv-rot-test/a.png", 4));
	assert(!IvRotate_file("/tmp/iv-rot-test/missing.png", 1));
	assert(access("/tmp/iv-rot-test/.rotate-tmp.png", F_OK) != 0);
	assert(!IvRotate_file("/tmp/iv-rot-test/noext", 1));

	SDL_FreeSurface(s);
	system("rm -rf /tmp/iv-rot-test");
	printf("test_iv_rotate: all passed\n");
	return 0;
}
