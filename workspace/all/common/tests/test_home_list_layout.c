// Home's List rows and geometry (nextui/home_list_layout.h): the row order, the selection clamp, the strip and the
// list's top, the Continue tag.
#include "../../nextui/home_list_layout.h"
#include <assert.h>
#include <stdio.h>

// Continue, then the games in pin order, then the tools.
static void row_order(void) {
	assert(HomeList_count(0, 0) == 1);
	assert(HomeList_count(3, 2) == 6);
	HomeListItem it = HomeList_item(0, 3, 2);
	assert(it.kind == HOMELIST_CONTINUE && it.ref == 0);
	it = HomeList_item(1, 3, 2);
	assert(it.kind == HOMELIST_GAME && it.ref == 0);
	it = HomeList_item(3, 3, 2);
	assert(it.kind == HOMELIST_GAME && it.ref == 2);
	it = HomeList_item(4, 3, 2);
	assert(it.kind == HOMELIST_TOOL && it.ref == 0);
	it = HomeList_item(5, 3, 2);
	assert(it.kind == HOMELIST_TOOL && it.ref == 1);
	// tools only
	it = HomeList_item(1, 0, 2);
	assert(it.kind == HOMELIST_TOOL && it.ref == 0);
	// out of range reads as the nearest row
	it = HomeList_item(9, 1, 1);
	assert(it.kind == HOMELIST_TOOL && it.ref == 0);
	it = HomeList_item(-1, 1, 1);
	assert(it.kind == HOMELIST_CONTINUE);
}

static void clamp(void) {
	assert(HomeList_clampSel(4, 2, 0) == 2);
	assert(HomeList_clampSel(-3, 2, 0) == 0);
	assert(HomeList_clampSel(1, 2, 0) == 1);
	assert(HomeList_clampSel(5, 0, 0) == 0);
}

// The Brick: bar 66, 8 dp = 11, 12 dp = 17, a 33 px strip font (ascent 31, descent 8), lines 36 apart.
static void geometry(void) {
	HomeListOpts o = {66, 11, 17, 66 + 17, 31, 8, 36};
	HomeListGeom g;
	HomeList_compute(0, &o, &g);
	assert(g.strip_lines == 0 && g.list_top == 83);
	HomeList_compute(1, &o, &g);
	assert(g.strip_lines == 1 && g.strip_base[0] == 66 + 11 + 31);
	assert(g.list_top == g.strip_base[0] + 8 + 17);
	HomeList_compute(2, &o, &g);
	assert(g.strip_lines == 2 && g.strip_base[1] == g.strip_base[0] + 36);
	assert(g.list_top == g.strip_base[1] + 8 + 17);
	HomeList_compute(5, &o, &g); // clamped to 2
	assert(g.strip_lines == 2);
}

// The mockup's tag on its 16 px line: 2 and 8 px padding, 10 px before the name.
static void tag(void) {
	HomeListTag t = HomeList_tag(16, 50);
	assert(t.pad_y == 2 && t.pad_x == 8 && t.gap == 10);
	assert(t.w == 66 && t.h == 20);
	t = HomeList_tag(24, 70); // 1.5 x
	assert(t.pad_y == 3 && t.pad_x == 12 && t.gap == 15 && t.w == 94 && t.h == 30);
}

// Minimal's fade: the selection 1; below 0.6, 0.4, 0.28, then 0.2 on; above 0.3, 0.22, then 0.2; linear between.
static int near(float a, float b) {
	return a - b < 0.0001f && b - a < 0.0001f;
}
static void minimal_fade(void) {
	assert(near(HomeMin_alpha(0), 1.0f));
	assert(near(HomeMin_alpha(1), 0.8f) && near(HomeMin_alpha(2), 0.6f) && near(HomeMin_alpha(3), 0.5f));
	assert(near(HomeMin_alpha(4), 0.42f) && near(HomeMin_alpha(9), 0.42f));
	assert(near(HomeMin_alpha(-1), 0.6f) && near(HomeMin_alpha(-2), 0.48f) && near(HomeMin_alpha(-9), 0.4f));
	assert(near(HomeMin_alpha(0.5f), 0.9f) && near(HomeMin_alpha(-0.5f), 0.8f));
}

static void minimal_glide(void) {
	HomeMinGlide g;
	HomeMin_glideStart(&g, 1, 2.0f, 1000);
	// a stalled frame (100 ms) moves at most 0.05 s worth, at the speed reached
	float v = MIN_GLIDE_V0 + (MIN_GLIDE_MAX - MIN_GLIDE_V0) * 100.0f / MIN_GLIDE_RAMP_MS;
	assert(near(HomeMin_glideStep(&g, 1100, 10), 2.0f + v * 0.05f));
	HomeMin_glideStart(&g, 1, 2.0f, 1000);
	HomeMin_glideStep(&g, 1016, 10);
	assert(g.v < MIN_GLIDE_MAX && g.pos > 2.0f);
	for (unsigned t = 1032; t < 1000 + MIN_GLIDE_RAMP_MS + 100; t += 16)
		HomeMin_glideStep(&g, t, 100);
	assert(near(g.v, MIN_GLIDE_MAX));
	// clamped at the ends
	HomeMin_glideStart(&g, -1, 0.2f, 0);
	HomeMin_glideStep(&g, 50, 10);
	assert(near(g.pos, 0.0f));
	HomeMin_glideStart(&g, 1, 8.9f, 0);
	HomeMin_glideStep(&g, 50, 10);
	assert(near(g.pos, 9.0f));
	// let go: the next whole name ahead, never back
	unsigned ms;
	g = (HomeMinGlide){1, 6.2f, 20.0f, 0, 0};
	assert(HomeMin_glideSettle(&g, 10, &ms) == 7 && g.dir == 0);
	assert(ms == 120); // 3 x 0.8 / 20 s
	g = (HomeMinGlide){-1, 6.58f, 12.0f, 0, 0};
	assert(HomeMin_glideSettle(&g, 10, &ms) == 6 && ms >= MIN_SETTLE_MIN_MS && ms <= MIN_SETTLE_MAX_MS);
	g = (HomeMinGlide){1, 4.0f, 12.0f, 0, 0};
	assert(HomeMin_glideSettle(&g, 10, &ms) == 4 && ms == MIN_SETTLE_MIN_MS);
	g = (HomeMinGlide){1, 9.0f, 24.0f, 0, 0};
	assert(HomeMin_glideSettle(&g, 10, &ms) == 9);
	assert(near(HomeMin_easeOut(0), 0.0f) && near(HomeMin_easeOut(1), 1.0f) && HomeMin_easeOut(0.5f) > 0.5f);
}

int main(void) {
	row_order();
	clamp();
	geometry();
	tag();
	minimal_fade();
	minimal_glide();
	printf("test_home_list_layout: all passed\n");
	return 0;
}
