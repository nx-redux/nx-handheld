// Home B2's geometry and D-pad (docs/home-b2.md): the worked numbers of the mockup on the Brick (1024 x 768) and the
// Smart Pro S (1920 x 1080 Brick px), then the moves.
#include "../../nextui/home_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define BAR 84.0f

static int near(float a, float b) {
	return fabsf(a - b) < 0.01f;
}

static int rectIs(HomeRect r, float x, float y, float w, float h) {
	return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
}

static void brick_strip_and_tools(void) {
	HomeLayout l;
	// two strip lines, 3 tools, 2 pins: the top section down to the bottom whatever the pins (187..650, 463), its
	// squares four rows of round(373 / 4) = 93 (30.3 apart) at x 880, Continue 799 wide
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &l);
	assert(!l.wide && l.k == 2 && near(l.strip_x, 54) && near(l.strip_base[0], 121) && near(l.strip_base[1], 157));
	assert(near(l.top_y, 187) && near(l.top_h, 463) && near(l.square, 93) && near(l.glyph, 54));
	assert(l.ntop == 4 && l.top[0].kind == HOME_TILE_CONTINUE && rectIs(l.top[0].r, 51, 187, 799, 463));
	assert(l.top[1].kind == HOME_TILE_TOOL && rectIs(l.top[1].r, 880, 187, 93, 93));
	assert(near(l.top[2].r.y, 187 + 93 + 91.0f / 3) && near(l.top[3].r.y, 187 + 2 * (93 + 91.0f / 3)));
	// the pins below it, past the screen's end: two 446 wide at x 51 and 527, y 684 (the hint bar's top, 34 under 650), 249 tall: two rows
	// (34 apart) from 118 (34 under the tab row) to 650
	assert(l.npins == 2 && rectIs(l.pins[0].r, 51, 684, 446, 249) && rectIs(l.pins[1].r, 527, 684, 446, 249));
	assert(near(l.page_h, 684 + 249 + 34 + BAR));
	// one strip line: baseline 124, top 155..650 (495), squares 101 at x 872, Continue 791
	HomeLayout_compute(1024, 768, BAR, 1, 2, 3, &l);
	assert(near(l.strip_base[0], 124) && near(l.top_h, 495) && near(l.square, 101));
	assert(rectIs(l.top[0].r, 51, 155, 791, 495) && rectIs(l.top[1].r, 872, 155, 101, 101));
	// no tools: Continue the full 922
	HomeLayout_compute(1024, 768, BAR, 2, 2, 0, &l);
	assert(l.ntop == 1 && rectIs(l.top[0].r, 51, 187, 922, 463) && l.square == 0);
	// 1 tool: from the top; 4 tools: one column of 4; 5 tools: a second column, the first full, Continue 676
	HomeLayout_compute(1024, 768, BAR, 2, 2, 1, &l);
	assert(l.ntop == 2 && rectIs(l.top[1].r, 880, 187, 93, 93));
	HomeLayout_compute(1024, 768, BAR, 2, 2, 4, &l);
	assert(l.ntop == 5 && rectIs(l.top[4].r, 880, 557, 93, 93));
	HomeLayout_compute(1024, 768, BAR, 2, 2, 5, &l);
	assert(l.ntop == 6 && rectIs(l.top[0].r, 51, 187, 676, 463) && rectIs(l.top[1].r, 757, 187, 93, 93));
	assert(l.top[5].kind == HOME_TILE_TOOL && l.top[5].ref == 4 && rectIs(l.top[5].r, 880, 187, 93, 93));
	// no strip (fresh): the top from 118 (HOME_BOTTOM under the bar, as the bottom over it); no pins: down to 650
	HomeLayout_compute(1024, 768, BAR, 0, 0, 0, &l);
	assert(near(l.top_y, 118) && near(l.top_h, 650 - 118) && l.npins == 0);
	// more pins: rows of two from 684, the page grows
	HomeLayout_compute(1024, 768, BAR, 2, 5, 0, &l);
	assert(l.npins == 5 && rectIs(l.pins[2].r, 51, 967, 446, 249) && rectIs(l.pins[4].r, 51, 1250, 446, 249));
	assert(near(l.page_h, 1250 + 249 + 34 + BAR));
}

static void sps_grid(void) {
	HomeLayout l;
	// 1920 x 1080: the top section down to the bottom (187..962, 775) as with fewer games; 3 tools one column of
	// round(685 / 4) = 171 squares flush right (x 1698); the first two games a column 40% of the 1617 left (646.8)
	// wide left of them, stacked (372.5 each); Continue the rest (940.2)
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 3, &l);
	assert(l.wide && l.k == 4 && near(l.top_y, 187) && near(l.top_h, 775) && near(l.strip_right, 1868));
	assert(l.ntop == 6 && near(l.square, 171) && rectIs(l.top[0].r, 51, 187, 940.2f, 775));
	assert(l.top[1].kind == HOME_TILE_GAME && l.top[1].ref == 0 && rectIs(l.top[1].r, 1021.2f, 187, 646.8f, 372.5f));
	assert(l.top[2].ref == 1 && rectIs(l.top[2].r, 1021.2f, 589.5f, 646.8f, 372.5f));
	assert(l.top[3].kind == HOME_TILE_TOOL && rectIs(l.top[3].r, 1698, 187, 171, 171));
	// the other eight games rows of four 432 wide below it, past the screen's end: from 996 (the hint bar's top), 405
	// tall (two rows, 34 apart, from 118 to 962)
	assert(l.npins == 8 && l.pins[0].ref == 2 && rectIs(l.pins[0].r, 51, 996, 432, 405));
	assert(rectIs(l.pins[3].r, 1437, 996, 432, 405) && rectIs(l.pins[4].r, 51, 1435, 432, 405));
	assert(near(l.page_h, 1435 + 405 + 34 + BAR));
	// 5 tools: a second column (x 1497, the first full), the games column (566.4) and Continue (819.6) left of it
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 5, &l);
	assert(l.ntop == 8 && rectIs(l.top[0].r, 51, 187, 819.6f, 775) && rectIs(l.top[1].r, 900.6f, 187, 566.4f, 372.5f));
	assert(rectIs(l.top[3].r, 1497, 187, 171, 171) && rectIs(l.top[7].r, 1698, 187, 171, 171));
	// no tools: the games column flush right, Continue the rest
	HomeLayout_compute(1920, 1080, BAR, 2, 10, 0, &l);
	assert(l.ntop == 3 && rectIs(l.top[1].r, 1869 - 727.2f, 187, 727.2f, 372.5f) && l.npins == 8);
}

static void top_moves(void) {
	HomeLayout l;
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &l); // Continue, 3 squares; 2 pins
	HomeFocus f = {HOME_SEC_TOP, 0, 0, -1};
	// RIGHT from Continue enters the column at its top
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.top == 2);
	// back LEFT to Continue, then RIGHT returns to the square left
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.top == 0);
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 2);
	// past the right edge: the next tab; UP from the top square: the tab row
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);
	f.top = 1;
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_TABS);
	f.top = 0;
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV);
	// DOWN: the first-row pin under the centre (Continue's centre 457.5 is nearer the left pin's 274 than 750? no: 750)
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.sec == HOME_SEC_PINS && f.pin == 0);
	f = (HomeFocus){HOME_SEC_TOP, 3, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.sec == HOME_SEC_PINS && f.pin == 1);
	// UP from the first row: the top tile last on
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && f.sec == HOME_SEC_TOP && f.top == 3);
	// no pins: DOWN stays
	HomeLayout_compute(1024, 768, BAR, 2, 0, 0, &l);
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY && f.sec == HOME_SEC_TOP);
}

static void pin_moves(void) {
	HomeLayout l;
	HomeLayout_compute(1920, 1080, BAR, 2, 8, 0, &l); // the column's 2, rows of 4 + 2
	assert(l.npins == 6);
	HomeFocus f = {HOME_SEC_PINS, 0, 3, -1};
	// RIGHT past a row's end: the next tab; LEFT along the row
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT && f.pin == 3);
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.pin == 2);
	// DOWN into the shorter last row: its last pin; DOWN on the last row stays
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.pin == 5);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY);
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_EDGE_NEXT);
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && f.pin == 1);
	f.pin = 0;
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_EDGE_PREV);
	// DOWN from a square (centre 1783.5) lands under it: pin 3 (centre 1653)
	HomeLayout_compute(1920, 1080, BAR, 2, 8, 2, &l);
	f = (HomeFocus){HOME_SEC_TOP, 4, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.pin == 3);
	// RIGHT from Continue enters the games column at its top; LEFT from it returns to Continue
	HomeLayout_compute(1920, 1080, BAR, 2, 8, 5, &l);
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.top == 0);
}

static void from_tabs_and_clamp(void) {
	HomeLayout l;
	HomeLayout_compute(1920, 1080, BAR, 2, 8, 1, &l); // the column's 2, rows 4 + 2, one tool
	HomeFocus f = {HOME_SEC_TOP, 0, 0, -1};
	HomeLayout_fromTabs(&l, &f); // from Continue: the last row's first pin
	assert(f.sec == HOME_SEC_PINS && f.pin == 4);
	f = (HomeFocus){HOME_SEC_TOP, 1, 0, -1};
	HomeLayout_fromTabs(&l, &f); // from another top tile: the last pin
	assert(f.pin == 5);
	HomeLayout_compute(1024, 768, BAR, 2, 0, 0, &l);
	f = (HomeFocus){HOME_SEC_TOP, 0, 0, -1};
	HomeLayout_fromTabs(&l, &f);
	assert(f.sec == HOME_SEC_TOP);
	// clamping after a rebuild with fewer tiles
	HomeLayout_compute(1024, 768, BAR, 2, 1, 1, &l);
	f = HomeLayout_clampFocus(&l, (HomeFocus){HOME_SEC_PINS, 5, 7, 9});
	assert(f.top == 1 && f.pin == 0 && f.prev == -1 && f.sec == HOME_SEC_PINS);
}

static void scroll(void) {
	HomeLayout l;
	HomeLayout_compute(1024, 768, BAR, 2, 5, 0, &l); // rows at 684, 967, 1250, all below the screen's end
	HomeFocus f = {HOME_SEC_PINS, 0, 0, -1};
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, 0), 933 - 650)); // its bottom 933 comes up to 650
	f.pin = 2;
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, 0), 1216 - 650)); // the first row then from 118, 34 under the tab row
	f.pin = 4;
	float s = HomeLayout_scrollFor(&l, f, 768, BAR, 0);
	assert(near(s, l.page_h - 768));
	f = (HomeFocus){HOME_SEC_TOP, 0, 4, -1};
	assert(near(HomeLayout_scrollFor(&l, f, 768, BAR, s), 0));
}

// The strip at Large's size on a Small screen (strip_k 1.5, Home's 3 / 2): its baselines and the top section's start
// under it 1.5x their Brick px offsets from the bar, so they stay the Large px apart once scaled by 2/3; the rest as
// without; strip_k 1 is HomeLayout_compute.
static void strip_keeps_large_size(void) {
	HomeLayout a, b;
	HomeLayout_compute(1024, 768, BAR, 2, 2, 3, &a);
	HomeLayout_computeStrip(1024, 768, BAR, 2, 1.0f, 2, 3, &b);
	assert(near(a.strip_base[0], b.strip_base[0]) && near(a.top_y, b.top_y) && near(a.top_h, b.top_h));
	HomeLayout_computeStrip(1024, 768, BAR, 2, 1.5f, 2, 3, &b);
	assert(near(b.strip_base[0], BAR + 37 * 1.5f) && near(b.strip_base[1], BAR + 73 * 1.5f));
	assert(near(b.top_y, BAR + 103 * 1.5f) && near(b.strip_x, a.strip_x));
	HomeLayout_computeStrip(1024, 768, BAR, 1, 1.5f, 2, 3, &b); // one line
	assert(near(b.strip_base[0], BAR + 40 * 1.5f) && near(b.top_y, BAR + 71 * 1.5f));
	HomeLayout_computeStrip(1024, 768, BAR, 0, 1.5f, 2, 3, &b); // no strip: the top section's start unchanged
	assert(near(b.top_y, BAR + HOME_BOTTOM));
}

// The Small UI scale (strip_k 1.5): the top section and its squares as at the Large scale, the pin rows below it.
static void small_scale_tools_and_pins(void) {
	HomeLayout l;
	// the Brick at 2x: 1536 x 1152 Brick px, bar 126; strip top 280.5; the top down to the bottom (992, 711.5) with
	// four rows of 155 squares whatever the pins; the pin rows from 1026 (the hint bar's top), 399 tall: two rows (34
	// apart) from 160 (34 under the tab row) to the bottom
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 2, 3, &l);
	assert(near(l.square, 155) && near(l.glyph, 90) && near(l.top_y, 280.5f) && near(l.top_h, 711.5f));
	assert(rectIs(l.top[1].r, 1536 - 51 - 155, 280.5f, 155, 155) && near(l.top[2].r.y, 466));
	assert(near(l.pin_h, 399) && rectIs(l.pins[0].r, 51, 1026, 702, 399) && near(l.page_h, 1585));
	// no tools: the same pin rows
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 4, 0, &l);
	assert(near(l.pin_h, 399) && rectIs(l.pins[2].r, 51, 1026 + 399 + HOME_BOTTOM, 702, 399));
}

// The wide layout with at most 2 pinned games: no pin rows, the top section down to the bottom (962). The tools four
// rows of squares filling it, a column at a time, flush right; the games a column left of them, stacked, six squares
// wide but at most 40% of what the tools leave; Continue the rest.
static void wide_few_games(void) {
	HomeLayout l;
	float d = 171 + 91.0f / 3; // Large: top 187..962 (775); squares round(685 / 4) = 171, the rows 30.33 apart
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 0, &l);
	assert(l.wide && l.npins == 0 && near(l.top_h, 775) && l.ntop == 1 && rectIs(l.top[0].r, 51, 187, 1818, 775));
	assert(near(l.page_h, 1080));
	// 3 tools: one column flush right from the top, Continue up to 30 before it; glyph 58%
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 3, &l);
	assert(near(l.square, 171) && near(l.glyph, 99) && l.ntop == 4);
	assert(rectIs(l.top[0].r, 51, 187, 1617, 775) && rectIs(l.top[1].r, 1698, 187, 171, 171));
	assert(rectIs(l.top[3].r, 1698, 187 + 2 * d, 171, 171));
	// 4 tools fill the column; the 4th at the bottom (962)
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 4, &l);
	assert(l.ntop == 5 && rectIs(l.top[4].r, 1698, 791, 171, 171) && rectIs(l.top[0].r, 51, 187, 1617, 775));
	// 1 game, 6 tools: two columns from 1497 (4 + 2), the game 40% of the 1416 left (566.4) full height, Continue 819.6
	HomeLayout_compute(1920, 1080, BAR, 2, 1, 6, &l);
	assert(l.ntop == 8 && l.npins == 0);
	assert(rectIs(l.top[0].r, 51, 187, 819.6f, 775));
	assert(l.top[1].kind == HOME_TILE_GAME && l.top[1].ref == 0 && rectIs(l.top[1].r, 900.6f, 187, 566.4f, 775));
	assert(l.top[2].kind == HOME_TILE_TOOL && l.top[2].ref == 0 && rectIs(l.top[2].r, 1497, 187, 171, 171));
	assert(rectIs(l.top[5].r, 1497, 791, 171, 171) && rectIs(l.top[6].r, 1698, 187, 171, 171));
	assert(l.top[7].kind == HOME_TILE_TOOL && l.top[7].ref == 5 && rectIs(l.top[7].r, 1698, 187 + d, 171, 171));
	// 2 games, no tools: stacked at the right, 40% of the 1818 (727.2) wide, each (775 − 30) / 2 tall
	HomeLayout_compute(1920, 1080, BAR, 2, 2, 0, &l);
	assert(l.ntop == 3 && rectIs(l.top[0].r, 51, 187, 1060.8f, 775));
	assert(rectIs(l.top[1].r, 1141.8f, 187, 727.2f, 372.5f) && rectIs(l.top[2].r, 1141.8f, 589.5f, 727.2f, 372.5f));
	assert(l.top[2].kind == HOME_TILE_GAME && l.top[2].ref == 1);
	// 2 games, 10 tools: three columns from 1296, the games 486 wide from 780, Continue 699; Continue + 2 + 10 = 13 top
	// tiles
	HomeLayout_compute(1920, 1080, BAR, 2, 2, 10, &l);
	assert(l.ntop == 13 && rectIs(l.top[0].r, 51, 187, 699, 775) && rectIs(l.top[1].r, 780, 187, 486, 372.5f));
	assert(rectIs(l.top[3].r, 1296, 187, 171, 171) && l.top[12].kind == HOME_TILE_TOOL && rectIs(l.top[12].r, 1698, 187 + d, 171, 171));
	// 14 tools: the twelve that fit
	HomeLayout_compute(1920, 1080, BAR, 2, 0, 14, &l);
	assert(l.ntop == 13 && l.top[12].kind == HOME_TILE_TOOL && l.top[12].ref == 11 && rectIs(l.top[12].r, 1698, 791, 171, 171));
	// Small, 9 tools, no games: three columns of 158 from 1335, Continue 1254
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 0, 9, &l);
	assert(l.ntop == 10 && rectIs(l.top[0].r, 51, 238.5f, 1254, 723.5f) && rectIs(l.top[9].r, 1711, 238.5f, 158, 158));
	// Small (strip_k 1.5): top 238.5..962 (723.5); squares round(633.5 / 4) = 158, 30.5 apart down; the game 652 wide
	HomeLayout_computeStrip(1920, 1080, 84, 2, 1.5f, 1, 3, &l);
	assert(near(l.top_h, 723.5f) && near(l.square, 158) && near(l.glyph, 92));
	assert(rectIs(l.top[2].r, 1711, 238.5f, 158, 158) && rectIs(l.top[4].r, 1711, 238.5f + 2 * 188.5f, 158, 158));
	assert(rectIs(l.top[1].r, 1029, 238.5f, 652, 723.5f) && rectIs(l.top[0].r, 51, 238.5f, 948, 723.5f));
	// 3 games: the same top section (the column's two), the third a row below it
	HomeLayout_compute(1920, 1080, BAR, 2, 3, 3, &l);
	assert(l.npins == 1 && l.pins[0].ref == 2 && near(l.top_h, 775) && l.ntop == 6);
}

// D-pad across the wide few-games top: Continue → the games → the tools, UP/DOWN within the game column, DOWN at the
// bottom stays (no rows), UP from the tab row lands in the top section.
static void wide_few_games_moves(void) {
	HomeLayout l;
	HomeLayout_compute(1920, 1080, BAR, 2, 2, 6, &l); // Continue, games 1-2, tools 3-8
	HomeFocus f = {HOME_SEC_TOP, 0, 0, -1};
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_MOVED && f.top == 2);
	assert(HomeLayout_move(&l, &f, HOME_DIR_DOWN) == HOME_MOVE_STAY && f.sec == HOME_SEC_TOP);
	assert(HomeLayout_move(&l, &f, HOME_DIR_RIGHT) == HOME_MOVE_MOVED && l.top[f.top].kind == HOME_TILE_TOOL);
	assert(HomeLayout_move(&l, &f, HOME_DIR_LEFT) == HOME_MOVE_MOVED && f.top == 2);
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_MOVED && f.top == 1);
	assert(HomeLayout_move(&l, &f, HOME_DIR_UP) == HOME_MOVE_TABS);
	HomeLayout_fromTabs(&l, &f);
	assert(f.sec == HOME_SEC_TOP);
}

// The Brick at Home's Small scale (1536 x 1152 Brick px, bar 126): no games, the top down to the bottom (992) with the
// tools four rows of 155 squares, two columns for 8; with games three rows of 119, up to three columns over the pin row.
static void brick_small_tools(void) {
	HomeLayout l;
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 0, 8, &l);
	assert(near(l.top_h, 711.5f) && near(l.square, 155) && near(l.glyph, 90) && l.npins == 0 && l.ntop == 9);
	assert(rectIs(l.top[0].r, 51, 280.5f, 1064, 711.5f) && rectIs(l.top[1].r, 1145, 280.5f, 155, 155));
	assert(rectIs(l.top[8].r, 1330, 280.5f + 3 * 185.5f, 155, 155));
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 0, 0, &l); // nothing pinned: Continue fills it
	assert(l.ntop == 1 && rectIs(l.top[0].r, 51, 280.5f, 1434, 711.5f));
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 2, 9, &l); // pinned games too: the same top section
	assert(near(l.top_h, 711.5f) && l.ntop == 10 && rectIs(l.top[0].r, 51, 280.5f, 879, 711.5f));
	assert(rectIs(l.top[1].r, 960, 280.5f, 155, 155) && rectIs(l.top[9].r, 1330, 280.5f, 155, 155));
	assert(l.npins == 2 && near(l.pins[0].r.y, 1026));
}

// The stats text's own factor moves the strip's baselines and the top section's start (its text grew); the top section
// then fills what is left.
static void strip_text_factor(void) {
	HomeLayout a, b;
	HomeLayout_computeStrip(1536, 1152, 126, 2, 1.5f, 2, 3, &a);
	HomeLayout_computeStripText(1536, 1152, 126, 2, 1.5f, 1.0f, 2, 3, &b);
	assert(near(a.strip_base[0], b.strip_base[0]) && near(a.strip_base[1], b.strip_base[1]) && near(a.top_y, b.top_y));
	assert(near(a.glyph, b.glyph) && near(a.square, b.square) && a.ntop == b.ntop);
	float tk = 30.0f / 27.0f;
	HomeLayout_computeStripText(1536, 1152, 126, 2, 1.5f, tk, 2, 3, &b);
	assert(near(b.strip_base[0], 126 + 37 * 1.5f * tk) && near(b.strip_base[1], 126 + 73 * 1.5f * tk));
	assert(near(b.top_y, 126 + 103 * 1.5f * tk));
	assert(near(b.top[0].r.y, b.top_y) && b.top_h < a.top_h); // Continue starts under the taller strip
	HomeLayout_computeStripText(1536, 1152, 126, 1, 1.5f, tk, 2, 3, &b);
	assert(near(b.strip_base[0], 126 + 40 * 1.5f * tk) && near(b.top_y, 126 + 71 * 1.5f * tk));
}

// The Continue caption's baselines above the card's bottom grow with its info line: the Brick's 21 px time 21 px up and
// the title 28 px over it, at 33 px 33 up and the title 44 over it (the stats lines' gap).
static void caption_baselines(void) {
	float sub, title;
	HomeLayout_captionBaselines(21, 49, 21, 21, &sub, &title);
	assert(near(sub, 21) && near(title, 49));
	HomeLayout_captionBaselines(21, 49, 21, 33, &sub, &title);
	assert(near(sub, 33) && near(title, 33 + 28 * 33.0f / 21));
}

// The 4:3 pins (below the top section): two rows, HOME_BOTTOM apart, fill the page scrolled to the second, from the
// ring's rest under the tab row to the bottom; the next row starts under the hint bar.
static void pins_fit_two_rows(void) {
	HomeLayout a;
	HomeLayoutOpts o = {1.5f, 33.0f / 27.0f};
	HomeLayout_computeOpts(1536, 1152, 126, 2, &o, 6, 3, &a);
	assert(near(a.pins[2].r.y, a.pins[0].r.y + a.pin_h + HOME_BOTTOM));
	float s = HomeLayout_scrollFor(&a, (HomeFocus){HOME_SEC_PINS, 0, 2, -1}, 1152, 126, 0);
	assert(near(a.pins[0].r.y - s, 126 + HOME_BOTTOM)); // as far from the tab row as the last from the hint bar
	assert(near(a.pins[2].r.y + a.pin_h - s, 1152 - 126 - HOME_BOTTOM));
	assert(a.pins[4].r.y - s >= 1152 - 126); // the third row under the hint bar
}

// The tools per column (the top section's squares): 5 (the Brick Pro's, the Smart Pro S's) makes each column of
// squares 5 tall, smaller, filling the same height; 0 is the Brick's 4.
static void tool_rows_option(void) {
	HomeLayout a, b;
	HomeLayoutOpts o = {1.5f, 1.0f, 0, 0};
	HomeLayout_computeOpts(1536, 1152, 126, 2, &o, 2, 5, &a);
	o.tool_rows = 5;
	HomeLayout_computeOpts(1536, 1152, 126, 2, &o, 2, 5, &b);
	assert(b.square < a.square);
	int col0 = 0; // tiles sharing the first tool's x: one column
	float x0 = -1;
	for (int i = 0; i < b.ntop; i++)
		if (b.top[i].kind == HOME_TILE_TOOL) {
			if (x0 < 0)
				x0 = b.top[i].r.x;
			col0 += b.top[i].r.x == x0;
		}
	assert(col0 == 5); // 5 tools in one column, not 4 + 1
	float last_bottom = 0;
	for (int i = 0; i < b.ntop; i++)
		if (b.top[i].kind == HOME_TILE_TOOL && b.top[i].r.y + b.top[i].r.h > last_bottom)
			last_bottom = b.top[i].r.y + b.top[i].r.h;
	assert(near(last_bottom, b.top_y + b.top_h)); // the column fills the section
}

// The wide layout's pins: wide_pin_cols a row (0: 4), each (the row less the gaps) / cols wide; the first two games in
// the top section's column whatever the count, the tools tool_rows a column (the Smart Pro S at Home's scale).
static void wide_pins_option(void) {
	HomeLayout a, b;
	HomeLayoutOpts o = {1.5f, 1.0f, 5, 0};
	HomeLayout_computeOpts(2560, 1440, 140, 2, &o, 6, 6, &a);
	assert(a.wide && a.k == 4 && near(a.pins[0].r.w, (2560 - 2 * HOME_EDGE - 3 * HOME_GAP) / 4));
	assert(a.npins == 4 && a.pins[0].ref == 2 && near(a.pins[0].r.y, 1440 - 140));
	o.wide_pin_cols = 3;
	HomeLayout_computeOpts(2560, 1440, 140, 2, &o, 9, 6, &b);
	assert(b.k == 3 && near(b.pins[0].r.w, (2560 - 2 * HOME_EDGE - 2 * HOME_GAP) / 3));
	assert(near(b.pins[2].r.y, b.pins[0].r.y) && b.pins[3].r.y > b.pins[0].r.y); // 3 a row
	// two rows to the page scrolled to them: from the ring's rest under the tab row to the bottom
	assert(near(b.pin_h, (1440 - 140 - HOME_BOTTOM - (140 + HOME_BOTTOM) - HOME_BOTTOM) / 2));
	assert(near(b.pins[3].r.y, b.pins[0].r.y + b.pin_h + HOME_BOTTOM));
	// the top section to the bottom as with two games: the column's games, 5 tools in the first column
	HomeLayout c;
	HomeLayout_computeOpts(2560, 1440, 140, 2, &o, 2, 6, &c);
	assert(near(b.top_h, c.top_h) && near(b.square, c.square) && b.ntop == c.ntop && c.npins == 0);
	for (int i = 0; i < b.ntop; i++)
		assert(b.top[i].kind == c.top[i].kind && rectIs(b.top[i].r, c.top[i].r.x, c.top[i].r.y, c.top[i].r.w, c.top[i].r.h));
	int col0 = 0;
	for (int i = 0; i < b.ntop; i++)
		col0 += b.top[i].kind == HOME_TILE_TOOL && near(b.top[i].r.x, b.top[b.ntop - 1].r.x - b.square - HOME_GAP);
	assert(col0 == 5 && near(b.top[0].r.h, b.top_h) && b.top[0].kind == HOME_TILE_CONTINUE);
}

int main(void) {
	wide_pins_option();
	tool_rows_option();
	strip_text_factor();
	caption_baselines();
	pins_fit_two_rows();
	brick_small_tools();
	wide_few_games();
	wide_few_games_moves();
	brick_strip_and_tools();
	strip_keeps_large_size();
	small_scale_tools_and_pins();
	sps_grid();
	top_moves();
	pin_moves();
	from_tabs_and_clamp();
	scroll();
	printf("test_home_layout: all passed\n");
	return 0;
}
