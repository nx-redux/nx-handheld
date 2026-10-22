#ifndef __LIST_NAV_H__
#define __LIST_NAV_H__

// Pure row navigation behind ListView (ui_listview.c: selectable-row steps over section headers and the pixel scroll
// window) and ListDialog (selection clamping). Header-only and widget-agnostic (the caller supplies a header-lookup
// callback) so it can be unit-tested on the host without SDL: see tests/test_list_nav.c.

#include <stdbool.h>

// Whether row i is a section header (not selectable).
typedef bool (*ListNav_isHeaderFn)(void* ctx, int i);

// Header rows: a quarter-row breathing gap above (separating the header from the previous group's last row - dropped
// when the header is the list's first row, where there is nothing to separate from), label in a half-row band, and
// a quarter-row gap below.
static inline int ListNav_headerTopGap(int i, int item_h) {
	return i > 0 ? item_h / 4 : 0;
}

static inline int ListNav_rowHeight(bool header, int i, int item_h) {
	return header ? ListNav_headerTopGap(i, item_h) + item_h / 2 + item_h / 4 : item_h;
}

// Step from `from` by dir (+1/-1), skipping headers, wrapping unless no_wrap. Returns `from` when no other
// selectable row exists (guards the all-headers degenerate list against infinite skip loops).
static inline int ListNav_step(int count, bool no_wrap, int from, int dir, ListNav_isHeaderFn is_header, void* ctx) {
	int i = from;
	for (int n = 0; n < count; n++) {
		i += dir;
		if (i < 0) {
			if (no_wrap)
				return from;
			i = count - 1;
		} else if (i >= count) {
			if (no_wrap)
				return from;
			i = 0;
		}
		if (!is_header(ctx, i))
			return i;
	}
	return from;
}

// Nearest selectable row to `target` (clamped into [0, count)), preferring direction `dir`, falling back to the
// other. The clamped target when every row is a header. For LEFT/RIGHT page jumps.
static inline int ListNav_nearest(int count, int target, int dir, ListNav_isHeaderFn is_header, void* ctx) {
	if (target < 0)
		target = 0;
	if (target >= count)
		target = count - 1;
	for (int i = target; i >= 0 && i < count; i += dir)
		if (!is_header(ctx, i))
			return i;
	for (int i = target; i >= 0 && i < count; i -= dir)
		if (!is_header(ctx, i))
			return i;
	return target;
}

// True when every row strictly between a and b is a header: an "adjacent" selection step for glide purposes, even
// though the index moved by 2+.
static inline bool ListNav_adjacent(int a, int b, ListNav_isHeaderFn is_header, void* ctx) {
	if (a < 0 || b < 0 || a == b)
		return false;
	int lo = a < b ? a : b;
	int hi = a > b ? a : b;
	for (int i = lo + 1; i < hi; i++)
		if (!is_header(ctx, i))
			return false;
	return true;
}

// Pixel-based scroll windowing for count > 0 rows and a selection in [0, count): walk row heights from scroll until
// the list_h band is filled; advance scroll until selected is fully visible. Scrolling up brings the section header
// along when it is the row directly above the selection, so the group reads as one unit. Uniform rows degenerate to
// UI_adjustListScroll behavior. Returns the new first row; *visible gets how many rows fit (at most max_visible).
static inline int ListNav_window(int count, int selected, int scroll, int list_h, int item_h, int max_visible,
								 ListNav_isHeaderFn is_header, void* ctx, int* visible) {
	if (scroll < 0)
		scroll = 0;
	if (scroll >= count)
		scroll = count - 1;
	if (selected < scroll)
		scroll = (selected > 0 && is_header(ctx, selected - 1)) ? selected - 1 : selected;

	int first = scroll;
	int n = 0;
	for (;;) {
		int y = 0;
		n = 0;
		bool sel_visible = false;
		for (int i = first; i < count && n < max_visible; i++) {
			int h = ListNav_rowHeight(is_header(ctx, i), i, item_h);
			if (y + h > list_h)
				break;
			if (i == selected)
				sel_visible = true;
			y += h;
			n++;
		}
		if (sel_visible || first >= selected)
			break;
		first++;
	}
	*visible = n;
	return first;
}

// After a list's rows change to `count`: the selection stays on a row (the last when the list shrank past it, 0 when
// empty) and the scroll never starts below the selection.
static inline void ListNav_clampSelection(int count, int* selected, int* scroll) {
	if (*selected >= count)
		*selected = count > 0 ? count - 1 : 0;
	if (*scroll > *selected)
		*scroll = *selected;
}

#endif
