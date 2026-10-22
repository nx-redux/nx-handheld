// Host-compiled unit test for IvView_srcRect (iv_view_math.h): the shown part
// of the image never leaves the image at any zoom, for images smaller and
// larger than the screen, wherever the pan center is.
// Build & run (from workspace/all/imageviewer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_iv_view_math.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>

#include "../iv_view_math.h"

static void check_inside(IvRect r, int img_w, int img_h) {
	assert(r.x >= 0 && r.y >= 0);
	assert(r.w > 0 && r.h > 0);
	assert(r.x + r.w <= img_w && r.y + r.h <= img_h);
}

static void test_sweep(void) {
	const int screens[][2] = {{1024, 768}, {1280, 720}};
	const int images[][2] = {{320, 240}, {1024, 768}, {4000, 3000}, {500, 4000}, {4000, 10}};
	const float scales[] = {0.08f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
	for (int s = 0; s < 2; s++)
		for (int i = 0; i < 5; i++)
			for (int z = 0; z < 8; z++) {
				int iw = images[i][0], ih = images[i][1];
				const int centers[][2] = {{0, 0}, {iw / 2, ih / 2}, {iw, ih}, {-500, iw * 3}, {iw * 3, -500}};
				for (int c = 0; c < 5; c++) {
					IvRect r = IvView_srcRect(screens[s][0], screens[s][1], scales[z], iw, ih,
											  centers[c][0], centers[c][1]);
					check_inside(r, iw, ih);
				}
			}
}

static void test_values(void) {
	// Fit-scale image smaller than the screen: the whole image
	IvRect r = IvView_srcRect(1024, 768, 1.0f, 320, 240, 160, 120);
	assert(r.x == 0 && r.y == 0 && r.w == 320 && r.h == 240);

	// 2x on a 4000x3000 image centered: a 512x384 window around the center
	r = IvView_srcRect(1024, 768, 2.0f, 4000, 3000, 2000, 1500);
	assert(r.w == 512 && r.h == 384 && r.x == 2000 - 256 && r.y == 1500 - 192);

	// Center near the bottom-right corner: pushed back inside
	r = IvView_srcRect(1024, 768, 2.0f, 4000, 3000, 3990, 2990);
	assert(r.x == 4000 - 512 && r.y == 3000 - 384);

	// Center near the top-left corner: pinned at 0
	r = IvView_srcRect(1280, 720, 4.0f, 4000, 3000, 10, 10);
	assert(r.x == 0 && r.y == 0 && r.w == 320 && r.h == 180);

	// Wider than the screen but shorter: full height, panned width
	r = IvView_srcRect(1280, 720, 1.0f, 4000, 300, 0, 150);
	assert(r.x == 0 && r.y == 0 && r.w == 1280 && r.h == 300);
}

int main(void) {
	test_sweep();
	test_values();
	printf("test_iv_view_math: all assertions passed\n");
	return 0;
}
