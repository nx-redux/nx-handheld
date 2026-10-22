#include "../../nextui/homeart_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 100
#define H 80
static unsigned buf[W * H];

static void fill(int x, int y, int w, int h, unsigned c) {
	for (int r = y; r < y + h; r++)
		for (int col = x; col < x + w; col++)
			buf[r * W + col] = c;
}

static int rectIs(HomeArtRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static HomeArtRect trim(void) {
	return HomeArt_trimLetterbox(buf, W, H, W);
}

static void symmetric_rows_trimmed(void) {
	fill(0, 0, W, H, 0xFF000000);
	fill(0, 10, W, 60, 0xFF808080);
	assert(rectIs(trim(), 0, 10, 100, 60));
}

static void asymmetric_kept(void) {
	fill(0, 0, W, H, 0xFF000000);
	fill(0, 10, W, 67, 0xFF808080); // bars 10 top / 3 bottom
	assert(rectIs(trim(), 0, 0, 100, 80));
}

static void near_black_is_bar_but_5_is_not(void) {
	fill(0, 0, W, H, 0xFF040404);
	fill(0, 10, W, 60, 0xFF808080);
	assert(rectIs(trim(), 0, 10, 100, 60)); // 0x04 is black
	buf[9 * W + 50] = 0xFF000005;			// one blue-5 pixel in the top bar's last row
	buf[70 * W + 50] = 0xFF050000;			// and one red-5 pixel in the bottom bar's first row
	assert(rectIs(trim(), 0, 9, 100, 62));
	fill(0, 0, W, H, 0x00000000); // alpha is ignored
	fill(0, 10, W, 60, 0xFF808080);
	assert(rectIs(trim(), 0, 10, 100, 60));
}

static void columns_trimmed(void) {
	fill(0, 0, W, H, 0xFF000000);
	fill(12, 0, 76, H, 0xFF808080);
	assert(rectIs(trim(), 12, 0, 76, 80));
	fill(0, 0, W, H, 0xFF000000); // both axes at once
	fill(12, 10, 76, 60, 0xFF808080);
	assert(rectIs(trim(), 12, 10, 76, 60));
}

static void quarter_rule(void) {
	fill(0, 0, W, H, 0xFF000000);
	fill(0, 32, W, 15, 0xFF808080); // 15 rows < 80/4: bars 32 top / 33 bottom
	assert(rectIs(trim(), 0, 0, 100, 80));
	fill(0, 0, W, H, 0xFF000000);
	fill(0, 30, W, 20, 0xFF808080); // exactly a quarter is kept
	assert(rectIs(trim(), 0, 30, 100, 20));
}

static void no_bars(void) {
	fill(0, 0, W, H, 0xFF808080);
	assert(rectIs(trim(), 0, 0, 100, 80));
	fill(0, 0, W, H, 0xFF000000); // an all-black frame is not trimmed to nothing
	assert(rectIs(trim(), 0, 0, 100, 80));
}

static void pitch_and_tolerance(void) {
	static unsigned wide[64 * 40];
	for (int i = 0; i < 64 * 40; i++)
		wide[i] = 0xFFFFFFFF; // padding beyond w must be ignored
	for (int y = 0; y < 40; y++)
		for (int x = 0; x < 50; x++)
			wide[y * 64 + x] = (y >= 6 && y < 32) ? 0xFF808080 : 0xFF000000; // 6 top / 8 bottom: within 2 px
	HomeArtRect r = HomeArt_trimLetterbox(wide, 50, 40, 64);
	assert(rectIs(r, 0, 6, 50, 26));
	assert(rectIs(HomeArt_trimLetterbox(wide, 0, 0, 64), 0, 0, 0, 0));
}

static int blank(HomeArtRect r) {
	return HomeArt_isBlankFrame(buf, r, W);
}

static void blank_frames(void) {
	HomeArtRect all = {0, 0, W, H};
	fill(0, 0, W, H, 0xFF000000); // all black (Contra's state preview)
	assert(blank(all));
	fill(0, 0, W, H, 0xFF040404); // near black counts as black
	assert(blank(all));
	fill(0, 0, W, H, 0x00000000); // alpha is ignored
	assert(blank(all));
	fill(0, 0, W, H, 0xFF000000); // exactly 2% lit: still blank (160 of 8000)
	fill(0, 0, 16, 10, 0xFFFFFFFF);
	assert(blank(all));
	fill(0, 0, 16, 11, 0xFFFFFFFF); // 176 lit (2.2%): a picture
	assert(!blank(all));
	fill(0, 0, W, H, 0xFF000000); // one channel over 4 makes a pixel lit
	fill(0, 0, 20, 10, 0xFF000005);
	assert(!blank(all));
	fill(0, 0, W, H, 0xFF808080);
	assert(!blank(all));
	// judged inside the kept (trimmed) rect only
	fill(0, 0, W, H, 0xFF808080);
	fill(10, 10, 40, 40, 0xFF000000);
	assert(blank((HomeArtRect){10, 10, 40, 40}));
	assert(!blank((HomeArtRect){10, 10, 41, 40})); // 40 lit of 1640 (2.4%)
	// nothing to judge: blank (no picture to show)
	assert(blank((HomeArtRect){0, 0, 0, 0}));
	assert(HomeArt_isBlankFrame(NULL, all, W));
}

// cropFill's 2x2 halving: rounded per-channel means, no carry between channels, odd edges left out
static void halving(void) {
	enum { P = 6 };
	unsigned src[P * 2] = {
		0xDEADBEEF,
		0x01020304,
		0x01020304,
		0xFFFFFFFF,
		0xFFFFFFFF,
		0x12345678,
		0xDEADBEEF,
		0x01020304,
		0x02030405,
		0x00000000,
		0x00000000,
		0x12345678,
	};
	unsigned dst[3] = {0, 0, 0xCAFEF00D};
	HomeArt_halve(src, P, (HomeArtRect){1, 0, 5, 2}, dst, 3);
	assert(dst[0] == 0x01020304); // (3 * c + c + 1 + 2) >> 2 per channel
	assert(dst[1] == 0x80808080); // two 0xFF and two 0x00: 128, every channel on its own
	assert(dst[2] == 0xCAFEF00D); // the odd fifth column is not read into a third pixel
	unsigned white[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}, out = 0;
	HomeArt_halve(white, 2, (HomeArtRect){0, 0, 2, 2}, &out, 1);
	assert(out == 0xFFFFFFFF); // 4 * 255 + 2 never carries into the next channel
	out = 7;
	HomeArt_halve(white, 2, (HomeArtRect){0, 0, 1, 2}, &out, 1); // under 2 px wide: nothing written
	assert(out == 7);
}

static void cover_math(void) {
	assert(HomeArt_coverScale(200, 100, 100, 100, 1.0f) == 1.0f); // the height decides
	assert(HomeArt_coverScale(100, 200, 100, 100, 1.0f) == 1.0f); // the width decides
	assert(HomeArt_coverScale(400, 400, 100, 50, 1.5f) == 0.375f);
	float ox, oy;
	HomeArt_coverOrigin(200, 100, 100, 100, 1.0f, 0.4f, &ox, &oy);
	assert(ox == 50.0f && oy == 0.0f); // centred across
	HomeArt_coverOrigin(100, 200, 100, 100, 1.0f, 0.4f, &ox, &oy);
	assert(ox == 0.0f && oy == 40.0f); // frame_y of the vertical overflow
	HomeArt_coverOrigin(100, 200, 100, 100, 1.0f, 0.0f, &ox, &oy);
	assert(oy == 0.0f); // a tall picture's top
	// the window stays inside the scaled picture at any zoom
	HomeArt_coverOrigin(300, 200, 100, 100, HomeArt_coverScale(300, 200, 100, 100, 1.2f), 1.0f, &ox, &oy);
	assert(ox >= 0 && oy >= 0 && ox + 100 <= 300 * 0.6f + 0.01f && oy + 100 <= 200 * 0.6f + 0.01f);
}

static void list_paths(void) {
	char path[128], preview[64];
	const char* rom = NULL;
	assert(HomeArt_listPathCont("/sd/.userdata/a.bmp", "/sd/Roms/GB/a.gb", path, sizeof(path)));
	assert(HomeArt_parseListPath(path, preview, sizeof(preview), &rom) == HOMEART_LIST_CONT);
	assert(strcmp(preview, "/sd/.userdata/a.bmp") == 0 && strcmp(rom, "/sd/Roms/GB/a.gb") == 0);
	assert(HomeArt_parseListPath(path, preview, 8, &rom) == HOMEART_LIST_CONT && strcmp(preview, "/sd/.us") == 0);
	assert(HomeArt_listPathAbs("/sd/Roms/GB/a.gb", path, sizeof(path)));
	assert(HomeArt_parseListPath(path, preview, sizeof(preview), &rom) == HOMEART_LIST_ABS);
	assert(strcmp(rom, "/sd/Roms/GB/a.gb") == 0);
	// too long for out: nothing half-written
	assert(!HomeArt_listPathCont("/sd/p.bmp", "/sd/Roms/GB/a.gb", path, 20) && path[0] == '\0');
	assert(!HomeArt_listPathAbs("/sd/Roms/GB/a.gb", path, 10) && path[0] == '\0');
	// a plain file path and malformed ones are no list paths
	assert(HomeArt_parseListPath("/sd/Roms/GB/.media/a.png", preview, sizeof(preview), &rom) == HOMEART_LIST_NONE);
	assert(HomeArt_parseListPath(HOMEART_LIST_PREFIX "cont\nno-rom", preview, sizeof(preview), &rom) ==
		   HOMEART_LIST_NONE);
	assert(HomeArt_parseListPath(HOMEART_LIST_PREFIX "what\nx", preview, sizeof(preview), &rom) == HOMEART_LIST_NONE);
	assert(HomeArt_parseListPath(NULL, preview, sizeof(preview), &rom) == HOMEART_LIST_NONE);
}

int main(void) {
	halving();
	cover_math();
	list_paths();
	// a tall picture (a DS screenshot, 256×384) frames its top; a wide or square one keeps the default
	assert(HomeArt_frameY(256, 384, 0.5f) == 0.0f && HomeArt_frameY(256, 384, 0.4f) == 0.0f);
	assert(HomeArt_frameY(320, 240, 0.5f) == 0.5f && HomeArt_frameY(256, 256, 0.4f) == 0.4f);
	symmetric_rows_trimmed();
	asymmetric_kept();
	near_black_is_bar_but_5_is_not();
	columns_trimmed();
	quarter_rule();
	no_bars();
	pitch_and_tolerance();
	blank_frames();
	printf("test_homeart_model: ok\n");
	return 0;
}
