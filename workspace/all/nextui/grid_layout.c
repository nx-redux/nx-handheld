// Grid style geometry (dp floats): tile size, row-major placement in a centred frame of columns, the vertical scroll
// offset (whole rows, the selection's row kept in view) and D-pad moves. Pure, host-tested.

#include "grid_layout.h"
#include "../common/nx_edge.h"
#include <math.h>

#define TILE_W_CAP 140.0f
#define TILE_H_CAP 154.0f
#define GAP 14.0f
#define GUTTER NX_EDGE_DP		   // the side margins: the main menu's shared one (Home's cards, the tab row)
#define RING_ROOM NX_BOTTOM_GAP_DP // the room at the body's top and bottom (nx_edge.h)

static float maxf(float a, float b) {
	return a > b ? a : b;
}
static float minf(float a, float b) {
	return a < b ? a : b;
}

void GridLayout_compute(float screen_w, float body_top, float body_h, int n, GridLayout* out) {
	GridLayout_computeWide(screen_w, body_top, body_h, n, 1.0f, out);
}

void GridLayout_computeWide(float screen_w, float body_top, float body_h, int n, float width_mul, GridLayout* out) {
	GridLayout_computeEx(screen_w, body_top, body_h, n, width_mul, 1.0f, out);
}

void GridLayout_computeEx(float screen_w, float body_top, float body_h, int n, float width_mul, float gap_mul,
						  GridLayout* out) {
	GridLayout_computeSized(screen_w, body_top, body_h, n, width_mul, gap_mul, 1.0f, out);
}

void GridLayout_computeSized(float screen_w, float body_top, float body_h, int n, float width_mul, float gap_mul,
							 float size_mul, GridLayout* out) {
	GridLayout_computeFit(screen_w, body_top, body_h, n, width_mul, gap_mul, size_mul, 0, out);
}

void GridLayout_computeFit(float screen_w, float body_top, float body_h, int n, float width_mul, float gap_mul,
						   float size_mul, int fit_cols, GridLayout* out) {
	GridLayout* g = out;
	g->centred = false;
	g->width_mul = width_mul > 0 ? width_mul : 1.0f;
	g->screen_w = screen_w;
	g->body_top = body_top;
	g->body_h = body_h;
	g->gap = GAP * (gap_mul > 0 ? gap_mul : 1.0f);
	g->gutter = GUTTER;
	g->n = n < 0 ? 0 : n;
	// two rows fill the body (the rows scroll under it, a row at a time)
	g->tile_h = maxf(1.0f, minf(TILE_H_CAP, (body_h - 2 * RING_ROOM - g->gap) / 2) * (size_mul > 0 ? size_mul : 1.0f));
	g->tile_w = g->tile_h * TILE_W_CAP / TILE_H_CAP * g->width_mul;
	if (fit_cols >= 2) { // exactly fit_cols across, the two rows keeping the body's height
		g->tile_w = (screen_w - 2 * GUTTER - (fit_cols - 1) * g->gap) / fit_cols;
		g->centred = true;
	}
	int cols = g->centred ? fit_cols : (int)floorf((screen_w - 2 * GUTTER + g->gap) / (g->tile_w + g->gap));
	// never one column for two or more items: a wide tile (Consoles, Collections) on a tall body would stack them,
	// on one panel and not on another (their bars' height differs in dp). Two across, stretched to the margins below
	// (the height kept, so the rows still fill the body).
	if (cols < 2 && g->n >= 2)
		cols = 2;
	g->cols = cols < 1 ? 1 : cols;
	// the columns span the screen between the margins exactly: the tiles take the width that's left over (their
	// height kept), so every Grid's sides line up with the tab row's
	if (!g->centred && g->n >= g->cols) {
		g->tile_w = (screen_w - 2 * GUTTER - (g->cols - 1) * g->gap) / g->cols;
		g->centred = true;
	}
	g->nrows = (g->n + g->cols - 1) / g->cols;
	g->sliding = g->nrows > 2; // scrolls: more rows than the body shows
	// the rows the body shows: one row of tools sits at the body's centre, two fill it
	g->rows = g->nrows >= 2 ? 2 : 1;
	float block_h = g->rows * g->tile_h + (g->rows - 1) * g->gap;
	g->rows_top = maxf(body_top + RING_ROOM, body_top + (body_h - block_h) / 2);
	g->frame_x0 = (screen_w - (g->cols * g->tile_w + (g->cols - 1) * g->gap)) / 2;
}

void GridLayout_cell(const GridLayout* g, int index, int* col, int* row) {
	*col = index % g->cols;
	*row = index / g->cols;
}

int GridLayout_index(const GridLayout* g, int col, int row) {
	if (col < 0 || row < 0 || col >= g->cols)
		return -1;
	int i = row * g->cols + col;
	return i < g->n ? i : -1;
}

int GridLayout_rowCount(const GridLayout* g) {
	return g->nrows;
}

float GridLayout_offsetFor(const GridLayout* g, int row, float current) {
	if (!g->sliding)
		return 0;
	float step = g->tile_h + g->gap;
	float hi = (g->nrows - g->rows) * step;
	// the least scroll that shows the row: up to it when it's above, down to it when it's below (whole rows)
	float lo_row = current / step, s = current;
	if (row < lo_row)
		s = row * step;
	else if (row > lo_row + g->rows - 1)
		s = (row - g->rows + 1) * step;
	s = roundf(s / step) * step; // on a row, whatever the current offset was
	return minf(hi, maxf(0, s));
}

float GridLayout_columnX(const GridLayout* g, int col) {
	return g->frame_x0 + col * (g->tile_w + g->gap);
}

float GridLayout_rowY(const GridLayout* g, int row, float offset) {
	return g->rows_top + row * (g->tile_h + g->gap) - offset;
}

void GridLayout_visibleRows(const GridLayout* g, float offset, int* first, int* last) {
	int r = g->nrows;
	float step = g->tile_h + g->gap;
	if (r <= 0 || step <= 0) {
		*first = 0;
		*last = -1;
		return;
	}
	// the rows touching the body, one more each side (slid in during a scroll, composed ahead)
	int f = (int)floorf((g->body_top - g->rows_top + offset - g->tile_h) / step) + 1;
	int l = (int)floorf((g->body_top + g->body_h - g->rows_top + offset) / step);
	f -= 1, l += 1;
	*first = f < 0 ? 0 : f;
	*last = l > r - 1 ? r - 1 : l;
	if (*first > *last) {
		*first = 0;
		*last = -1;
	}
}

GridMove GridLayout_move(const GridLayout* g, int* index, GridDir dir) {
	int n = g->n;
	if (n <= 0) {
		if (dir == GRID_DIR_LEFT)
			return GRID_MOVE_EDGE_PREV;
		if (dir == GRID_DIR_RIGHT)
			return GRID_MOVE_EDGE_NEXT;
		return GRID_MOVE_STAY;
	}
	int i = *index;
	if (i < 0)
		i = 0;
	if (i >= n)
		i = n - 1;
	int col, row;
	GridLayout_cell(g, i, &col, &row);

	if (dir == GRID_DIR_UP || dir == GRID_DIR_DOWN) {
		int tr = row + (dir == GRID_DIR_UP ? -1 : 1);
		if (tr < 0 || tr >= g->nrows)
			return GRID_MOVE_STAY;
		int t = GridLayout_index(g, col, tr);
		if (t < 0) // a shorter last row: its last tile
			t = n - 1;
		if (t == i)
			return GRID_MOVE_STAY;
		*index = t;
		return GRID_MOVE_MOVED;
	}

	// LEFT/RIGHT along the row; past its first or last tile, the edge (the caller switches tab, or steps on)
	int tc = col + (dir == GRID_DIR_LEFT ? -1 : 1);
	int t = GridLayout_index(g, tc, row);
	if (t < 0)
		return dir == GRID_DIR_LEFT ? GRID_MOVE_EDGE_PREV : GRID_MOVE_EDGE_NEXT;
	*index = t;
	return GRID_MOVE_MOVED;
}

int GridLayout_wrap(const GridLayout* g, int index, GridMove edge) {
	int n = g->n;
	if (n <= 0)
		return 0;
	if (index < 0)
		index = 0;
	if (index >= n)
		index = n - 1;
	int col, row;
	GridLayout_cell(g, index, &col, &row);
	if (edge == GRID_MOVE_EDGE_NEXT)
		return row * g->cols; // the row's first tile
	int last = row * g->cols + g->cols - 1;
	return last < n ? last : n - 1; // the row's last tile
}

float GridLayout_tileK(const GridLayout* g) {
	return minf(1.0f, g->tile_w / g->width_mul / TILE_W_CAP); // the spec shape's width: a wide tile's text doesn't grow
}

float GridLayout_countSp(float spec_sp, float tile_k) {
	return maxf(GRID_COUNT_MIN_SP, spec_sp * tile_k);
}

float GridLayout_logoCountY(float tile_y, float tile_h, float drawn_h, float gap) {
	return tile_y + tile_h / 2 + drawn_h / 2 + gap;
}

void GridLayout_logoBlock(float tile_y, float tile_h, float logo_h, float gap, float count_h, float* logo_y,
						  float* count_y) {
	float block_h = logo_h + (count_h > 0 ? gap + count_h : 0);
	*logo_y = tile_y + (tile_h - block_h) / 2;
	*count_y = *logo_y + logo_h + gap;
}

int GridLayout_logoBoxH(int box_h, int gap, int count_h) {
	return count_h > 0 ? box_h - gap - count_h : box_h;
}

GridCollText GridLayout_collText(int tile_h, int lines, int line_h, int gap, int count_h) {
	lines = lines < 1 ? 1 : (lines > GRID_COLL_LINES ? GRID_COLL_LINES : lines);
	GridCollText t;
	t.name_h = lines * line_h;
	t.block_h = t.name_h + gap + count_h;
	t.name_y = (tile_h - t.block_h) / 2;
	t.count_y = t.name_y + t.name_h + gap;
	return t;
}

int GridLayout_wordMaxLines(int tile_h, int pad, int line_h, int gap, int count_h, int edge, int max_lines) {
	if (line_h <= 0 || max_lines < 1)
		return 1;
	int n = (tile_h - 2 * pad) / line_h;
	if (n > max_lines)
		n = max_lines;
	// centred: half the name, the gap and the count line below the tile's middle, clear of the edge
	while (count_h > 0 && n > 1 && n * line_h + 2 * (gap + count_h + edge) > tile_h)
		n--;
	return n < 1 ? 1 : n;
}

int GridLayout_wordTop(int tile_h, int lines, int line_h) {
	return (tile_h - lines * line_h) / 2;
}

int GridLayout_collMaxLines(int avail_h, int line_h, int gap, int count_h) {
	int n = line_h > 0 ? (avail_h - gap - count_h) / line_h : 1;
	return n < 1 ? 1 : (n > GRID_COLL_LINES ? GRID_COLL_LINES : n);
}
