#ifndef HOME_LAYOUT_H
#define HOME_LAYOUT_H

#include <stdbool.h>

#include "../common/nx_edge.h"

// Home B2 (docs/home-b2.md): the stats strip under the tabs, the top section (Continue and the tool squares) and the
// pinned games in rows below. Pure, host-tested.
//
// Units are Brick px (the mockup's): a screen at FIXED_SCALE s draws s / 3 px per Brick px, so the Brick (3x) is
// 1024 x 768 of them and the Smart Pro S (2x) 1920 x 1080. The page is laid out for its size: a screen under 1.6:1
// takes the Brick's arrangement, a wider one the Smart Pro S's 8-column grid.

#define HOME_MAX_PINS 16
#define HOME_MAX_TOP 15		  // Continue, 2 games (the wide layout's game column), 12 tool squares
#define HOME_WIDE_COL_GAMES 2 // the wide layout puts up to this many games in the top section, beside Continue
#define HOME_TOOL_COLS 3	  // the tool squares' columns at most (more tools than fit are left off, see shortcuts.h)

// Home's units per dp: 1.5 a px at the Brick's 3.0 (2.14 px a dp), 2 at the Smart Pro S's 2.25 (1.61): 45/14 on both
#define HOME_UNITS_PER_DP (45.0f / 14.0f)
// the content's left and right margin: the shared NX_EDGE_DP (16 dp) in whole units (51), the same px as 16 dp on every
// screen (Brick 34, Brick Pro 29, Smart Pro S 26)
#define HOME_EDGE ((float)(int)(NX_EDGE_DP * HOME_UNITS_PER_DP + 0.5f))
#define HOME_GAP 30.0f	  // between tiles
#define HOME_PIN_H 155.0f // a pin row's height
// the top section's bottom over the tab row: the shared NX_BOTTOM_GAP_DP (4 dp) in whole units (13), where the Grid's
// last row ends
#define HOME_PAGE_BOTTOM ((float)(int)(NX_BOTTOM_GAP_DP * HOME_UNITS_PER_DP + 0.5f))
#define HOME_BOTTOM 34.0f // the page's margin at either bar (the last row to the hint bar, a scrolled-to row to the tabs)
#define HOME_RADIUS 24.0f // a tile's corners
#define HOME_RING 6.0f	  // a lit game's ring, outside the tile
#define HOME_EDGE_W 2.0f  // a tile's edge, white 14%

typedef struct {
	float x, y, w, h;
} HomeRect; // page coordinates (y = 0 at the screen top, unscrolled)

typedef enum { HOME_TILE_CONTINUE,			  // Continue, or Pick a game
			   HOME_TILE_GAME,				  // a pinned game, in the rows or the wide game column (ref: its index)
			   HOME_TILE_TOOL } HomeTileKind; // a pinned tool's square (ref: its index in the tools)
typedef struct {
	HomeTileKind kind;
	int ref;
	HomeRect r;
} HomeTile;

typedef struct {
	bool wide;		 // the Smart Pro S arrangement
	int strip_lines; // 0 (fresh), 1 or 2
	float strip_x, strip_right, strip_base[2];
	float top_y, top_h;
	float square, glyph; // the tool squares' side and glyph (0: no tools)
	int ntop;
	HomeTile top[HOME_MAX_TOP]; // Continue first, then the column's games, then the squares in reading order
	int k;						// pins per row
	float pin_h;				// a pin row's height: two rows to the page scrolled to them (no rows: HOME_PIN_H)
	int npins;
	HomeTile pins[HOME_MAX_PINS]; // the rows' games, row by row
	float page_h;
} HomeLayout;

// W x H: the screen (Brick px); bar: the tab row's and the hint bar's height. ngames: the pinned games (Continue's own
// excluded), ntools: the pinned tools.
void HomeLayout_compute(float W, float H, float bar, int strip_lines, int ngames, int ntools, HomeLayout* out);
// The same with the stats strip's offsets (its baselines and the top section's start under it) strip_k times their
// size: Home passes the UI scale over its own (1.5), so the strip is drawn at the UI scale.
// The top section always runs down to the hint bar: the tools four rows of squares filling it (a column per 4 tools;
// HomeLayoutOpts.tool_rows), on a wide screen (1.6:1, the Smart Pro S) the first HOME_WIDE_COL_GAMES games a column
// left of them (stacked, top tiles; six squares wide, at most 40% of what the tools leave), and Continue the rest. The
// other pinned games are rows below it, past the screen's end (the page scrolls to them, two rows to a page): two a
// row under 1.6:1 (the Brick, the Brick Pro), four on a wide screen (HomeLayoutOpts.wide_pin_cols).
void HomeLayout_computeStrip(float W, float H, float bar, int strip_lines, float strip_k, int ngames, int ntools,
							 HomeLayout* out);
// The same with the stats text text_k times its default size: the strip's baselines and the top section's start move
// with it (strip_k still sizes the rest).
// What HomeLayout_computeOpts takes beyond the screen: strip_k and text_k as HomeLayout_computeStripText's (0, or a
// NULL opts, is 1). tool_rows: the top section's tool squares per column (0: 4). wide_pin_cols: the wide layout's pins
// a row (0: 4).
// strip_base0: the strip's first baseline (0: under the tab row, at the bar); with the tab row in the bottom bar the
// strip moves up into the top bar's row, the top section HOME_BOTTOM-ish (30 × the strip's scale) under its last line.
typedef struct {
	float strip_k, text_k;
	int tool_rows, wide_pin_cols;
	float strip_base0;
} HomeLayoutOpts;
void HomeLayout_computeOpts(float W, float H, float bar, int strip_lines, const HomeLayoutOpts* opts, int ngames,
							int ntools, HomeLayout* out);
void HomeLayout_computeStripText(float W, float H, float bar, int strip_lines, float strip_k, float text_k, int ngames, int ntools,
								 HomeLayout* out);

typedef enum { HOME_SEC_TOP,
			   HOME_SEC_PINS } HomeSection;
// The focus: its section and, per section, the tile it is (or was last) on; prev = the top tile last left (ties go back
// to it), -1 none.
typedef struct {
	HomeSection sec;
	int top, pin, prev;
} HomeFocus;
typedef enum { HOME_DIR_UP,
			   HOME_DIR_DOWN,
			   HOME_DIR_LEFT,
			   HOME_DIR_RIGHT } HomeDir;
typedef enum { HOME_MOVE_STAY,
			   HOME_MOVE_MOVED,
			   HOME_MOVE_EDGE_PREV, // past the left edge: the caller switches tab (a fresh press only)
			   HOME_MOVE_EDGE_NEXT } HomeMoveResult;

// The D-pad on Home (docs/home-b2.md, D-pad on Home): in the top section a press goes to the nearest tile on that side
// that overlaps the current one across the move, ties to the tile last left, else the top-left one; DOWN goes to the
// first-row pin under the tile's centre. In the pins LEFT/RIGHT run along a row, UP/DOWN keep the column (a shorter
// last row takes its last pin), UP from the first row returns to the top tile last on. DOWN off the bottom of the page
// stays.
HomeMoveResult HomeLayout_move(const HomeLayout* l, HomeFocus* f, HomeDir dir);
// A focus kept in range after a rebuild (fewer tiles: the last one; no pins: the top section).
HomeFocus HomeLayout_clampFocus(const HomeLayout* l, HomeFocus f);
// The focused tile's rect.
HomeRect HomeLayout_focusRect(const HomeLayout* l, HomeFocus f);
// The page scroll (Brick px) keeping the focused pin's ring under the tab row and the pin HOME_BOTTOM above the hint
// bar, moving as little as possible from `current`; 0 in the top section; clamped to [0, page_h − H].
float HomeLayout_scrollFor(const HomeLayout* l, HomeFocus f, float H, float bar, float current);

// A card caption's baselines above its bottom (px) when its info line is sub_px instead of sub_px0: the info line's
// offset and the gap up to the title both grow with it, so the lines keep their proportions.
void HomeLayout_captionBaselines(float sub_up, float title_up, float sub_px0, float sub_px, float* sub_out,
								 float* title_out);

#endif
