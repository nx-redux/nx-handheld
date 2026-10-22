// Host unit test for ui/list_nav.h (ListView's header-skipping steps and scroll window, ListDialog's selection
// clamp). Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include "../ui/list_nav.h"

// rows: header, a, b, header, c
static const bool GROUPED[] = {true, false, false, true, false};
static const bool ALL_HEADERS[] = {true, true, true};
static const bool NONE[64] = {false};

static bool is_header(void* ctx, int i) {
	return ((const bool*)ctx)[i];
}

static void test_steps(void) {
	void* g = (void*)GROUPED;
	assert(ListNav_step(5, false, 1, -1, is_header, g) == 4); // up from the first row wraps past the header
	assert(ListNav_step(5, true, 1, -1, is_header, g) == 1);  // no_wrap stays
	assert(ListNav_step(5, false, 2, +1, is_header, g) == 4); // skips the middle header
	assert(ListNav_step(5, false, 4, +1, is_header, g) == 1); // wraps past the leading header
	assert(ListNav_step(5, true, 4, +1, is_header, g) == 4);
	assert(ListNav_step(3, false, 1, +1, is_header, (void*)ALL_HEADERS) == 1); // all headers: no infinite loop

	assert(ListNav_nearest(5, 3, +1, is_header, g) == 4);
	assert(ListNav_nearest(5, 3, -1, is_header, g) == 2);
	assert(ListNav_nearest(5, 10, +1, is_header, g) == 4); // clamped
	assert(ListNav_nearest(5, -5, -1, is_header, g) == 1); // clamped, then the other way
	assert(ListNav_nearest(3, 1, +1, is_header, (void*)ALL_HEADERS) == 1);

	assert(ListNav_adjacent(2, 4, is_header, g)); // only a header between
	assert(ListNav_adjacent(4, 2, is_header, g));
	assert(ListNav_adjacent(1, 2, is_header, g));
	assert(!ListNav_adjacent(1, 4, is_header, g)); // row 2 between
	assert(!ListNav_adjacent(1, 1, is_header, g));
	assert(!ListNav_adjacent(-1, 2, is_header, g));
}

static void test_window(void) {
	void* g = (void*)GROUPED;
	// item_h 40: a header is 30 px as the first row (no gap above), else 40; rows are 40
	assert(ListNav_headerTopGap(0, 40) == 0 && ListNav_headerTopGap(3, 40) == 10);
	assert(ListNav_rowHeight(true, 0, 40) == 30 && ListNav_rowHeight(true, 3, 40) == 40);
	assert(ListNav_rowHeight(false, 0, 40) == 40);
	int visible = -1;
	// selection at the bottom: the window advances until it is fully in the 100 px band
	assert(ListNav_window(5, 4, 0, 100, 40, 32, is_header, g, &visible) == 3 && visible == 2);
	// already visible: the window stays
	assert(ListNav_window(5, 1, 0, 100, 40, 32, is_header, g, &visible) == 0 && visible == 2);
	// the window never fills backwards: a selection at the scroll row keeps it
	assert(ListNav_window(5, 4, 4, 100, 40, 32, is_header, g, &visible) == 4 && visible == 1);
	// scrolling up onto a group's first row brings its header along
	assert(ListNav_window(5, 1, 3, 100, 40, 32, is_header, g, &visible) == 0 && visible == 2);
	// ...but not a row that isn't a header
	assert(ListNav_window(5, 2, 4, 100, 40, 32, is_header, g, &visible) == 2 && visible == 2);
	// an out-of-range scroll is clamped
	assert(ListNav_window(5, 4, 99, 100, 40, 32, is_header, g, &visible) == 4);
	assert(ListNav_window(5, 0, -3, 100, 40, 32, is_header, g, &visible) == 0);
	// at most max_visible rows
	assert(ListNav_window(64, 0, 0, 1000, 10, 32, is_header, (void*)NONE, &visible) == 0 && visible == 32);
	assert(ListNav_window(64, 63, 0, 1000, 10, 32, is_header, (void*)NONE, &visible) == 32 && visible == 32);
	// a band shorter than one row: the window still lands on the selection, showing nothing
	assert(ListNav_window(5, 2, 0, 20, 40, 32, is_header, g, &visible) == 2 && visible == 0);
}

static void test_clamp(void) {
	int sel = 5, scroll = 4;
	ListNav_clampSelection(3, &sel, &scroll); // the list shrank below the selection
	assert(sel == 2 && scroll == 2);
	sel = 3, scroll = 1;
	ListNav_clampSelection(0, &sel, &scroll); // emptied
	assert(sel == 0 && scroll == 0);
	sel = 2, scroll = 1;
	ListNav_clampSelection(5, &sel, &scroll); // still in range: unchanged
	assert(sel == 2 && scroll == 1);
}

int main(void) {
	test_steps();
	test_window();
	test_clamp();
	printf("test_list_nav: OK\n");
	return 0;
}
