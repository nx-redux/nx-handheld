#ifndef GRID_LAYOUT_H
#define GRID_LAYOUT_H

#include <stdbool.h>

// grid_layout.h — Grid geometry (dp), still/sliding placement, slide offset, moves. SDL-free.
typedef struct {
	float screen_w, body_top, body_h; // body = between the top bar and the bottom bar
	float tile_w, tile_h, gap, gutter;
	float width_mul; // tile_w over the spec shape's (Consoles and Collections 2, game lists 1.5); text scales by the spec
	int cols, n;
	int nrows;		// ceil(n / cols): the rows, row-major
	bool sliding;	// more rows than the body shows: the rows scroll up and down, a whole row at a time
	int rows;		// the rows the body shows: 1 (n <= cols) or 2
	float rows_top; // y of the top shown row (offset 0): the shown rows centred in the body as a block
	float frame_x0; // x of column 0: the columns centred across
	bool centred;	// GridLayout_computeFit's columns (exactly fit_cols across)
} GridLayout;

// The tiles fill the body two rows high (row-major, the columns centred across); with more rows than that the rows
// scroll vertically (nx-mobile's Grid: rows fill the band, snap to a row, columns follow the width).
void GridLayout_compute(float screen_w, float body_top, float body_h, int n, GridLayout* out);
// The same with tiles width_mul times the spec shape's width (height unchanged).
void GridLayout_computeWide(float screen_w, float body_top, float body_h, int n, float width_mul, GridLayout* out);
// The same with the gap between tiles gap_mul times the spec's 14 dp.
void GridLayout_computeEx(float screen_w, float body_top, float body_h, int n, float width_mul, float gap_mul,
						  GridLayout* out);
// The same with tiles size_mul times the size they would take (both sides, and so their content: GridLayout_tileK).
void GridLayout_computeSized(float screen_w, float body_top, float body_h, int n, float width_mul, float gap_mul,
							 float size_mul, GridLayout* out);
// The same with the tiles' width set (their height kept) so exactly fit_cols columns span the screen between the
// gutters (0: as many as the shape fits).
void GridLayout_computeFit(float screen_w, float body_top, float body_h, int n, float width_mul, float gap_mul,
						   float size_mul, int fit_cols, GridLayout* out);
void GridLayout_cell(const GridLayout* g, int index, int* col, int* row); // row-major
int GridLayout_index(const GridLayout* g, int col, int row);			  // -1 when empty
int GridLayout_rowCount(const GridLayout* g);
// The scroll (dp, whole rows) that shows `row`, moved from `current` as little as it takes; 0 when the rows all show.
float GridLayout_offsetFor(const GridLayout* g, int row, float current);
// x (dp, screen) of a column's left edge.
float GridLayout_columnX(const GridLayout* g, int col);
// y (dp, screen) of a row's top for a given scroll.
float GridLayout_rowY(const GridLayout* g, int row, float offset);
// The rows touching the body +1 each side, clamped; loop `for (r = first; r <= last; r++)`. Empty (0, -1) when none.
void GridLayout_visibleRows(const GridLayout* g, float offset, int* first, int* last);
typedef enum { GRID_DIR_UP,
			   GRID_DIR_DOWN,
			   GRID_DIR_LEFT,
			   GRID_DIR_RIGHT } GridDir;
typedef enum { GRID_MOVE_STAY,
			   GRID_MOVE_MOVED,
			   GRID_MOVE_EDGE_PREV,
			   GRID_MOVE_EDGE_NEXT } GridMove;
// UP/DOWN a row in the column (a shorter last row: its last tile; past the first or last row: STAY); LEFT/RIGHT along
// the row, past its first or last tile the edge (GRID_MOVE_EDGE_PREV / _NEXT: the main menu switches tab).
GridMove GridLayout_move(const GridLayout* g, int* index, GridDir dir);
// Past a row's end with no tab to switch to: the other end of the same row. 0 with no tiles.
int GridLayout_wrap(const GridLayout* g, int index, GridMove edge);
// The main-menu tiles' selected look (sub-project 8, "Logo"): the tile stays dark with a 1.5 dp outline at 70% of the
// accent, its logo / tool icon / name in the accent; no fill. Game tiles keep their 3 dp accent ring.
#define GRID_SEL_OUTLINE_DP 1.5f
// "N games" on the selected tile, in the accent: Consoles 13 sp, 6 dp under the logo as drawn; Collections 14 sp, 4 dp
// under the name. Sizes × the tile scale (GridLayout_countSp); the gaps are unscaled (as the Carousel's).
#define GRID_LOGO_COUNT_SP 13.0f
#define GRID_LOGO_COUNT_GAP_DP 6.0f
#define GRID_COLL_COUNT_SP 14.0f
#define GRID_COLL_COUNT_GAP_DP 4.0f
#define GRID_COUNT_MIN_SP 10.0f // never below the caption floor
// A collection's name: full white, 20 sp × the tile scale, shrinking (whole sp) to max(0.75 × start, 1.25 × count)
// until its longest word fits (Row_collNameSp's rule), then wrapping to at most 3 lines with "…"; line height 1.15.
#define GRID_COLL_NAME_SP 20.0f
#define GRID_COLL_LINE 1.15f
#define GRID_COLL_LINES 3
// The tile scale k: tile_w / 140 (the reference tile), never above 1. Insets and text scale by it.
float GridLayout_tileK(const GridLayout* g);
// A count's size (sp) at tile scale k: max(10, spec_sp × k).
float GridLayout_countSp(float spec_sp, float tile_k);
// Consoles: the count line's top, gap under what's drawn (drawn_h) centred in the tile at tile_y..tile_y + tile_h
// (any unit, the logo stays centred).
float GridLayout_logoCountY(float tile_y, float tile_h, float drawn_h, float gap);
// A console tile's logo (logo_h tall) and its "N games" line (count_h; 0: none) centred as one block, gap apart: the
// logo's top and the count's top. GridLayout_logoBoxH: the logo's box height (box_h, the tile less its insets) less
// the count's line, so the tallest logo keeps the block inside the box.
void GridLayout_logoBlock(float tile_y, float tile_h, float logo_h, float gap, float count_h, float* logo_y,
						  float* count_y);
int GridLayout_logoBoxH(int box_h, int gap, int count_h);
// Collections: a name of `lines` lines (clamped to 1..GRID_COLL_LINES) at line_h each, then gap, then the count line's
// reserved count_h, centred in a tile_h tile as one group. The line is reserved whether or not it shows, so the name
// sits at the same y plain and selected (px; tops from the tile's top).
typedef struct {
	int name_y, name_h, count_y, block_h;
} GridCollText;
GridCollText GridLayout_collText(int tile_h, int lines, int line_h, int gap, int count_h);
// The name lines that fit avail_h px over the reserved gap + count line: 1..GRID_COLL_LINES.
int GridLayout_collMaxLines(int avail_h, int line_h, int gap, int count_h);
// A console without a logo (px): its name, centred in the tile at line_h a line, takes the lines that fit inside the
// pads (1..max_lines); with a count under it (count_h > 0) also only as many as leave room for gap + the count line
// above the tile's bottom edge (edge: the selected outline's width), so the count never leaves the tile.
int GridLayout_wordMaxLines(int tile_h, int pad, int line_h, int gap, int count_h, int edge, int max_lines);
// The name's top in the tile for `lines` lines (px from the tile's top); the count's top is that + lines × line_h + gap.
int GridLayout_wordTop(int tile_h, int lines, int line_h);

#endif
