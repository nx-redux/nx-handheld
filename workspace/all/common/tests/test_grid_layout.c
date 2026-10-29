#include "../../nextui/grid_layout.h"
#include "../../nextui/row_model.h"
#include "../nx_edge.h"

#define E NX_EDGE_DP // the side margins (the tab row's)
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define BAR (28.0f * 42.0f / 30.0f)
static int near(float a, float b) {
	return fabsf(a - b) < 0.1f;
}
static void make(float w, float h, int n, GridLayout* g) {
	GridLayout_compute(w, BAR, h - 2 * BAR, n, g);
}

static void device_vectors(void) {
	GridLayout g;
	// the columns the spec shape fits, the tiles then stretched across to the margins (their height kept)
	make(478, 358, 20, &g); // Brick 3x
	assert(near(g.tile_h, (358 - 2 * BAR - 22) / 2) && g.cols == 3 && near(g.tile_w, (478 - 2 * E - 2 * 14) / 3));
	make(896, 504, 20, &g); // SPS 2x: capped height
	assert(near(g.tile_h, 154) && g.cols == 5 && near(g.tile_w, (896 - 2 * E - 4 * 14) / 5));
	make(717, 538, 20, &g); // Brick 2x
	assert(near(g.tile_h, 154) && g.cols == 4);
	assert(near(g.rows_top, BAR + ((538 - 2 * BAR) - (2 * 154 + 14)) / 2)); // centred
}

// Row-major placement in a centred frame: the rows the body shows (two), scrolling past them a whole row at a time.
static void rows_placement(void) {
	GridLayout g;
	int c, r;
	make(896, 504, 7, &g); // 5 cols: two rows, no scroll
	assert(g.cols == 5 && g.nrows == 2 && !g.sliding && g.rows == 2);
	GridLayout_cell(&g, 4, &c, &r);
	assert(c == 4 && r == 0);
	GridLayout_cell(&g, 5, &c, &r);
	assert(c == 0 && r == 1);
	assert(GridLayout_index(&g, 2, 1) == -1 && GridLayout_index(&g, 1, 1) == 6 && GridLayout_index(&g, 5, 0) == -1);
	assert(near(g.frame_x0, E) && near(GridLayout_columnX(&g, 4) + g.tile_w, 896 - E)); // margin to margin
	assert(near(GridLayout_columnX(&g, 1), E + g.tile_w + 14));
	make(896, 504, 3, &g); // a short single row: its natural width, centred
	assert(near(g.tile_w, 140) && g.frame_x0 > E);
	make(896, 504, 11, &g); // three rows: scrolls
	assert(g.nrows == 3 && g.sliding && g.rows == 2 && GridLayout_rowCount(&g) == 3);
	float step = g.tile_h + g.gap;
	assert(near(GridLayout_rowY(&g, 1, 0), g.rows_top + step) && near(GridLayout_rowY(&g, 2, step), g.rows_top + step));
}

// The scroll: none while the rows all show; else the least that brings the selection's row in, in whole rows,
// clamped to the last row's.
static void scroll_offset(void) {
	GridLayout g;
	make(896, 504, 7, &g);
	assert(GridLayout_offsetFor(&g, 1, 0) == 0);
	make(896, 504, 40, &g); // 8 rows of 5
	float step = g.tile_h + g.gap;
	assert(near(GridLayout_offsetFor(&g, 0, 0), 0) && near(GridLayout_offsetFor(&g, 1, 0), 0));
	assert(near(GridLayout_offsetFor(&g, 2, 0), step));				  // one row down: the next row under the shown two
	assert(near(GridLayout_offsetFor(&g, 2, step), step));			  // shown: kept
	assert(near(GridLayout_offsetFor(&g, 1, step), step));			  // shown (the top one): kept
	assert(near(GridLayout_offsetFor(&g, 0, step), 0));				  // above: up to it
	assert(near(GridLayout_offsetFor(&g, 7, 0), 6 * step));			  // the last row: the last two shown
	assert(near(GridLayout_offsetFor(&g, 9, 0), 6 * step));			  // a stale row clamps to the end
	assert(near(GridLayout_offsetFor(&g, 3, 1.4f * step), 2 * step)); // mid-scroll: back on a row
}

static void moves(void) {
	GridLayout g;
	make(896, 504, 12, &g); // 5 + 5 + 2
	int i = 0;
	assert(GridLayout_move(&g, &i, GRID_DIR_LEFT) == GRID_MOVE_EDGE_PREV && i == 0);
	assert(GridLayout_move(&g, &i, GRID_DIR_UP) == GRID_MOVE_STAY && i == 0);
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_MOVED && i == 1);
	i = 4; // the row's last: RIGHT is the edge (never the next row)
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_EDGE_NEXT && i == 4);
	i = 5; // the second row's first: LEFT is the edge
	assert(GridLayout_move(&g, &i, GRID_DIR_LEFT) == GRID_MOVE_EDGE_PREV);
	i = 1;
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_MOVED && i == 6);
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_MOVED && i == 11);
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_STAY && i == 11); // the last row
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_EDGE_NEXT);		 // the short row's last tile
	i = 8;																		 // under col 3 the last row is empty: its last tile
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_MOVED && i == 11);
	assert(GridLayout_move(&g, &i, GRID_DIR_UP) == GRID_MOVE_MOVED && i == 6);
	// no tab to switch to: a row's other end
	assert(GridLayout_wrap(&g, 6, GRID_MOVE_EDGE_NEXT) == 5 && GridLayout_wrap(&g, 6, GRID_MOVE_EDGE_PREV) == 9);
	assert(GridLayout_wrap(&g, 10, GRID_MOVE_EDGE_PREV) == 11 && GridLayout_wrap(&g, 99, GRID_MOVE_EDGE_NEXT) == 10);
}

// The rows touching the body, one more each side, clamped.
static void visible_rows(void) {
	GridLayout g;
	make(896, 504, 400, &g); // 80 rows
	float step = g.tile_h + g.gap;
	int first, last;
	// 40 and 41 shown; the rows peeking into the body's margins above and below them (the shown two are centred),
	// and one more each side
	GridLayout_visibleRows(&g, 40 * step, &first, &last);
	assert(first == 38 && last == 43);
	GridLayout_visibleRows(&g, 0, &first, &last);
	assert(first == 0 && last == 3);
	for (int r = first; r <= last; r++) // each one touches the body, or is the one past it
		assert(GridLayout_rowY(&g, r, 0) < g.body_top + g.body_h + step && GridLayout_rowY(&g, r, 0) + g.tile_h > g.body_top - step);
	make(896, 504, 3, &g); // one row
	GridLayout_visibleRows(&g, 0, &first, &last);
	assert(first == 0 && last == 0);
}

static void empty(void) {
	GridLayout g;
	make(478, 358, 0, &g);
	int i = 0;
	assert(GridLayout_move(&g, &i, GRID_DIR_RIGHT) == GRID_MOVE_EDGE_NEXT); // nothing to move to: edge
	assert(GridLayout_move(&g, &i, GRID_DIR_DOWN) == GRID_MOVE_STAY);
	int first, last;
	GridLayout_visibleRows(&g, 0, &first, &last);
	assert(first == 0 && last == -1);
	assert(GridLayout_wrap(&g, 3, GRID_MOVE_EDGE_NEXT) == 0);
}

// The two screens in dp (px / (FIXED_SCALE × 30/42)): the Brick 1024×768 at 3, the Smart Pro S 1280×720 at 2.
#define BRICK_W (1024.0f / (3 * 30.0f / 42.0f))
#define BRICK_H (768.0f / (3 * 30.0f / 42.0f))
#define SPS_W (1280.0f / (2 * 30.0f / 42.0f))
#define SPS_H (720.0f / (2 * 30.0f / 42.0f))

static float rowsCentre(const GridLayout* g) {
	return g->rows_top + (g->rows * g->tile_h + (g->rows - 1) * g->gap) / 2;
}

// Sub-project 8: a still grid centres the rows it uses; a sliding grid keeps both rows as before. The same geometry
// serves the main menu and the game lists (GridView_render computes every Grid through GridLayout_compute).
static void still_centring(void) {
	GridLayout g;
	float brick_mid = BAR + (BRICK_H - 2 * BAR) / 2, sps_mid = BAR + (SPS_H - 2 * BAR) / 2; // 179.2, 252.0
	// Brick: tile 117.3 × 129, 3 columns
	make(BRICK_W, BRICK_H, 3, &g); // one full row (3 tools)
	assert(g.cols == 3 && !g.sliding && g.rows == 1);
	assert(near(g.tile_h, 129) && near(g.rows_top, 114.7f) && near(rowsCentre(&g), brick_mid));
	make(BRICK_W, BRICK_H, 1, &g); // one tile
	assert(g.rows == 1 && near(g.rows_top, 114.7f));
	make(BRICK_W, BRICK_H, 4, &g); // 4 tools: 3 + 1, the two rows centred as a block
	assert(!g.sliding && g.rows == 2 && near(g.rows_top, 43.2f) && near(rowsCentre(&g), brick_mid));
	make(BRICK_W, BRICK_H, 7, &g); // sliding: unchanged (both rows, ring room above)
	assert(g.sliding && g.rows == 2 && near(g.rows_top, BAR + 4) && near(g.rows_top, 43.2f));
	// SPS: tile 140 × 154, 5 columns
	make(SPS_W, SPS_H, 4, &g); // 4 collections: one row at the body's centre
	assert(g.cols == 5 && g.rows == 1 && near(g.rows_top, 175.0f) && near(rowsCentre(&g), sps_mid));
	make(SPS_W, SPS_H, 5, &g);
	assert(g.rows == 1 && near(g.rows_top, 175.0f));
	make(SPS_W, SPS_H, 7, &g); // 5 + 2
	assert(!g.sliding && g.rows == 2 && near(g.rows_top, 91.0f) && near(rowsCentre(&g), sps_mid));
	make(SPS_W, SPS_H, 11, &g); // sliding: as before
	assert(g.sliding && g.rows == 2 && near(g.rows_top, 91.0f));
	make(SPS_W, SPS_H, 40, &g);
	assert(g.sliding && near(g.rows_top, 91.0f));
}

// The selected tile's "N games" and a collection's name, at the Grid tile scale k (tile_w / 140).
static void counts_and_names(void) {
	GridLayout g;
	make(BRICK_W, BRICK_H, 9, &g); // k: the stretched tile's width over the spec's 140, never above 1
	assert(near(GridLayout_tileK(&g) * 140, g.tile_w) || near(GridLayout_tileK(&g), 1.0f));
	make(717, 538, 9, &g); // capped tile: never above 1
	assert(near(GridLayout_tileK(&g), 1.0f));
	// the sizes at a tile scale of 0.838 (a 117 dp tile) and 1
	float kb = 117.2727f / 140, ks = 1.0f;
	// Consoles 13 sp, Collections 14 sp, × k, floored at 10 sp
	assert(fabsf(GridLayout_countSp(GRID_LOGO_COUNT_SP, kb) - 10.89f) < 0.01f);
	assert(fabsf(GridLayout_countSp(GRID_COLL_COUNT_SP, kb) - 11.73f) < 0.01f);
	assert(near(GridLayout_countSp(GRID_LOGO_COUNT_SP, ks), 13) && near(GridLayout_countSp(GRID_COLL_COUNT_SP, ks), 14));
	assert(near(GridLayout_countSp(GRID_LOGO_COUNT_SP, 0.5f), 10)); // a small tile keeps the floor
	// the name: 20 sp × k, its floor max(0.75 × start, 1.25 × count) (Row_collNameSp's rule)
	float sb = GRID_COLL_NAME_SP * kb, ss = GRID_COLL_NAME_SP * ks; // 16.75, 20
	float cb = GridLayout_countSp(GRID_COLL_COUNT_SP, kb), cs = GridLayout_countSp(GRID_COLL_COUNT_SP, ks);
	assert(fabsf(Row_collNameFloor(sb, cb) - 14.66f) < 0.01f && near(Row_collNameFloor(ss, cs), 17.5f));
	assert(near(Row_collNameSp(sb, 150, 200, cb), sb));				  // a word that fits keeps the start
	assert(near(Row_collNameSp(ss, 180, 170, cs), 18.0f));			  // 20 → 19 (171 px) → 18 (162 px)
	assert(fabsf(Row_collNameSp(sb, 400, 200, cb) - 14.66f) < 0.01f); // too wide: the floor, then it wraps
	assert(Row_collNameSp(sb, 400, 200, cb) >= 1.25f * cb - 0.001f);  // the name never reads smaller than its count
	// Consoles: the count's top 6 dp under the logo as drawn, the logo centred in the tile (Brick, dp)
	assert(near(GridLayout_logoCountY(43.2f, 129, 20, 6), 43.2f + 64.5f + 10 + 6));
	assert(GridLayout_logoCountY(0, 129, 40, 6) > GridLayout_logoCountY(0, 129, 20, 6));
}

// The Consoles tab: tiles twice the spec width, the same height, the text scale (k) still the spec shape's.
static void consoles_wide(void) {
	GridLayout n, w;
	make(BRICK_W, BRICK_H, 9, &n);
	GridLayout_computeWide(BRICK_W, BAR, BRICK_H - 2 * BAR, 9, 2.0f, &w);
	// two wide tiles don't fit across the Brick at 3x: never one column, two across stretched to the margins, the
	// height kept (the rows still fill the body)
	assert(w.cols == 2 && w.sliding && near(w.tile_w, (BRICK_W - 2 * E - 14) / 2) && near(w.tile_h, n.tile_h));
	assert(GridLayout_tileK(&w) < GridLayout_tileK(&n));
	// the Brick Pro at 3x (its bars 2x-sized, so a taller body), two collections: side by side, not stacked
	GridLayout_computeSized(BRICK_W, BAR * 2 / 3, BRICK_H - 2 * BAR * 2 / 3, 2, 2.0f, 2.0f / 3.0f, 0.85f, &w);
	assert(w.cols == 2 && w.rows == 1 && !w.sliding);
	make(SPS_W, SPS_H, 9, &n);
	GridLayout_computeWide(SPS_W, BAR, SPS_H - 2 * BAR, 9, 2.0f, &w);
	assert(near(w.tile_w, (SPS_W - 2 * E - 14) / 2) && near(w.tile_h, n.tile_h) && w.sliding && w.cols == 2);
	assert(near(GridLayout_tileK(&w), 1.0f));
	GridLayout_computeWide(SPS_W, BAR, SPS_H - 2 * BAR, 3, 2.0f, &w); // a still row of three: two columns, two rows
	assert(!w.sliding && w.cols == 2 && w.rows == 2);
}

// A console without a logo draws as a collection tile (§8.4): the name from 20 sp × k, its count at Consoles' 13 sp × k
// (floored at 10 sp) 4 dp under it, so the name's floor follows that smaller count.
static void logoless_console(void) {
	float kb = 117.2727f / 140, ks = 1.0f; // as counts_and_names: a 117 dp tile and a full one
	float cb = GridLayout_countSp(GRID_LOGO_COUNT_SP, kb), cs = GridLayout_countSp(GRID_LOGO_COUNT_SP, ks);
	assert(fabsf(cb - 10.89f) < 0.01f && near(cs, 13.0f));
	float sb = GRID_COLL_NAME_SP * kb, ss = GRID_COLL_NAME_SP * ks;
	// floor = max(0.75 × start, 1.25 × count): Brick 1.25 × 10.89 = 13.61, SPS 1.25 × 13 = 16.25
	assert(fabsf(Row_collNameFloor(sb, cb) - 13.61f) < 0.01f && near(Row_collNameFloor(ss, cs), 16.25f));
	assert(near(GRID_COLL_COUNT_GAP_DP, 4.0f) && GRID_COLL_LINES == 3 && near(GRID_COLL_LINE, 1.15f));
	// a small tile keeps the 10 sp count floor
	assert(near(GridLayout_countSp(GRID_LOGO_COUNT_SP, 0.5f), 10.0f));
}

// A collection tile's text block (px): name lines + 4 dp + the reserved count line, centred in the tile.
static void collection_text(void) {
	// Brick: tile 276 px, name 43 px (16.75 sp) step 49, gap 9, count 30 px (11.73 sp), pad 25 px
	assert(GridLayout_collMaxLines(276 - 2 * 25, 49, 9, 30) == 3);
	GridCollText t = GridLayout_collText(276, 2, 49, 9, 30);
	assert(t.name_y == 69 && t.name_h == 98 && t.count_y == 176 && t.block_h == 137);
	t = GridLayout_collText(276, 1, 49, 9, 30);
	assert(t.name_y == 94 && t.count_y == 152);
	// SPS: tile 220 px, name 34 px (20 sp) step 39, gap 6, count 24 px (14 sp), pad 20 px
	assert(GridLayout_collMaxLines(220 - 2 * 20, 39, 6, 24) == 3);
	t = GridLayout_collText(220, 3, 39, 6, 24);
	assert(t.name_y == 36 && t.name_h == 117 && t.count_y == 159 && t.block_h == 147);
	t = GridLayout_collText(220, 5, 39, 6, 24); // clamps to 3 lines
	assert(t.name_h == 117);
	t = GridLayout_collText(220, 1, 39, 6, 24);
	assert(t.name_y == 75 && t.count_y == 120);
	// the reserved line keeps the group inside the tile; a cramped tile still shows one line
	assert(t.name_y >= 0 && t.count_y + 24 <= 220);
	assert(GridLayout_collMaxLines(40, 39, 6, 24) == 1);
}

// A game list's Grid centres in the body under the game-list header (GameList listTopAt(false): the strip
// SCALE1(BUTTON_SIZE + 2 × BUTTON_MARGIN) = 28 logical px, in dp at each device scale), not only under BAR/BAR: the
// rows' centre follows body_top + body_h / 2 for any body, symmetric or not.
static void game_list_centring(void) {
	GridLayout g;
	const int scales[2] = {3, 2};
	const float sw[2] = {BRICK_W, SPS_W}, sh[2] = {BRICK_H, SPS_H};
	for (int i = 0; i < 2; i++) {
		float px_per_dp = scales[i] * 30.0f / 42.0f;
		float header = (16 + 2 * 6) * scales[i] / px_per_dp; // the game-list header strip, dp
		assert(near(header, BAR));
		float body_h = sh[i] - header - BAR; // header above, hint bar below
		make(sw[i], sh[i], 2, &g);
		GridLayout h;
		GridLayout_compute(sw[i], header, body_h, 2, &h); // one still row of two games
		assert(h.rows == 1 && near(rowsCentre(&h), header + body_h / 2) && near(h.rows_top, g.rows_top));
		GridLayout_compute(sw[i], header, body_h, h.cols + 1, &h); // two still rows
		assert(h.rows == 2 && near(rowsCentre(&h), header + body_h / 2));
		// an asymmetric body (header 39.2 dp on top, 60 dp off the bottom): still centred on its own middle
		float abody = sh[i] - header - 60;
		GridLayout_compute(sw[i], header, abody, 2, &h);
		assert(h.rows == 1 && near(rowsCentre(&h), header + abody / 2));
		assert(h.rows_top >= header + 4 - 0.01f); // the ring room under the header holds
	}
}

// The selected Consoles tile's "N games" stays inside the tile, clear of the 1.5 dp outline, at both scales: under a
// logo (the logo box's tallest fit) and under a logo-less name (4 lines unless the count needs the room) (px).
static void count_fits_tile(void) {
	struct {
		int tile_h, pad, word_h, gap, count_h, edge, inset_y;
	} dev[2] = {
		// Brick 3x: tile 129 dp = 276 px, k 0.838: pad 14 dp×k = 25, 17 sp×k = 37 px font (height 50), gap 6 dp = 13,
		// count 10.89 sp = 28 px font (height 38), outline 1.5 dp = 3, logo inset 30 dp×k = 54
		{276, 25, 50, 13, 38, 3, 54},
		// SPS 2x: tile 154 dp = 220 px, k 1: pad 20, 17 sp = 29 px font (height 39), gap 9, 13 sp = 22 px (height 30),
		// outline 2, inset 43
		{220, 20, 39, 9, 30, 2, 43},
	};
	for (int i = 0; i < 2; i++) {
		int th = dev[i].tile_h, lh = dev[i].word_h, gap = dev[i].gap, ch = dev[i].count_h, edge = dev[i].edge;
		// the logo case: the tallest logo the box allows, centred; the count 6 dp under it
		int box_h = th - 2 * dev[i].inset_y;
		int cy = (int)floorf(GridLayout_logoCountY(0, (float)th, (float)box_h, (float)gap) + 0.5f);
		assert(cy + ch <= th - edge);
		// the no-logo case: plain (no count) keeps 4 lines; with the count the lines drop until it fits
		assert(GridLayout_wordMaxLines(th, dev[i].pad, lh, gap, 0, edge, 4) == 4);
		int n = GridLayout_wordMaxLines(th, dev[i].pad, lh, gap, ch, edge, 4);
		assert(n == 3);
		int top = GridLayout_wordTop(th, n, lh);
		int count_top = top + n * lh + gap;
		assert(top >= edge && count_top + ch <= th - edge);
		// before the cap: 4 lines put the count past the tile's edge
		int top4 = GridLayout_wordTop(th, 4, lh);
		assert(top4 + 4 * lh + gap + ch > th);
		// a short name (1 line) is never capped below 1 and fits
		assert(GridLayout_wordMaxLines(th, dev[i].pad, lh, gap, ch, edge, 1) == 1);
	}
	// a cramped tile still shows one line
	assert(GridLayout_wordMaxLines(60, 10, 50, 13, 38, 3, 4) == 1);
}

// The logo and its "N games" centred as one block: equal room above the logo and below the count; no count, the logo
// alone centred. The logo's box gives up the count's line, so the tallest logo still leaves the block in the box.
static void logo_block_centred(void) {
	float ly, cy;
	GridLayout_logoBlock(10, 230, 120, 9, 30, &ly, &cy);
	assert(near(ly - 10, (10 + 230) - (cy + 30)) && near(cy, ly + 120 + 9));
	GridLayout_logoBlock(10, 230, 120, 9, 0, &ly, &cy);
	assert(near(ly, 10 + (230 - 120) / 2.0f));
	assert(GridLayout_logoBoxH(144, 9, 30) == 144 - 39 && GridLayout_logoBoxH(144, 9, 0) == 144);
	// the tallest logo the reduced box allows: the block fits the box (inset 43 each side of a 230 tile)
	GridLayout_logoBlock(0, 230, (float)GridLayout_logoBoxH(230 - 86, 9, 30), 9, 30, &ly, &cy);
	assert(ly >= 43 - 0.01f && cy + 30 <= 230 - 43 + 0.01f);
}

// Game lists on the 4:3 panels (GRID_GAME_COLS 2): two columns across, the tiles widened at their height, flush with
// the margins, the rows scrolling; without fit_cols the shape's columns, stretched to the margins too (dp).
static void fit_two_columns(void) {
	GridLayout a, f;
	float body = 358 - 2 * BAR; // Brick 3x
	GridLayout_computeSized(478, BAR, body, 40, 1.5f, 1.0f, 1.0f, &a);
	GridLayout_computeFit(478, BAR, body, 40, 1.5f, 1.0f, 1.0f, 2, &f);
	assert(a.cols == 2 && near(a.tile_w, (478 - 2 * E - 14) / 2.0f)); // the shape's 2 across, stretched to the margins
	assert(f.centred && f.cols == 2 && near(f.tile_h, a.tile_h) && near(f.tile_w, (478 - 2 * E - 14) / 2.0f));
	assert(f.sliding && f.rows == 2 && f.nrows == 20);
	assert(near(GridLayout_columnX(&f, 0), E) && near(GridLayout_columnX(&f, 1) + f.tile_w, 478 - E)); // flush
	// Consoles' shape (2x wide) narrowed to the same two across, at the same height as a game list's
	GridLayout c;
	GridLayout_computeFit(478, BAR, body, 40, 2.0f, 1.0f, 1.0f, 2, &c);
	assert(c.centred && c.cols == 2 && near(c.tile_w, f.tile_w) && near(c.tile_h, f.tile_h));
	// none asked: the shape's columns, stretched the same way
	GridLayout_computeFit(896, BAR, 504 - 2 * BAR, 40, 1.5f, 1.0f, 1.0f, 0, &f);
	GridLayout_computeSized(896, BAR, 504 - 2 * BAR, 40, 1.5f, 1.0f, 1.0f, &a);
	assert(f.cols == a.cols && near(f.tile_w, a.tile_w) && near(GridLayout_columnX(&f, 0), E));
}

int main(void) {
	fit_two_columns();
	logo_block_centred();
	device_vectors();
	still_centring();
	counts_and_names();
	logoless_console();
	consoles_wide();
	collection_text();
	game_list_centring();
	count_fits_tile();
	rows_placement();
	scroll_offset();
	moves();
	visible_rows();
	empty();
	printf("test_grid_layout: ok\n");
	return 0;
}
