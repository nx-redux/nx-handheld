#ifndef UI_HINTBAR_LAYOUT_H
#define UI_HINTBAR_LAYOUT_H

// The hint bar's pure geometry (SDL-free, host-tested): the one place its icons' centring is defined, for the bar's
// renderer and UI_buttonHintIconTop (ui_buttonhintbar.c) and the info band's ink top (nextui InfoBand_hintInkTop).

// How far below the bar's top edge (bar_h tall) its icon_h tall icons start: centred in the bar.
static inline int UI_hintBarIconOffset(int bar_h, int icon_h) {
	return (bar_h - icon_h) / 2;
}

#include <string.h>

// The hints the bar holds at most.
#define UI_HINTBAR_MAX 4

// A hint's place in the drop order when the bar is over UI_HINTBAR_MAX or wider than the screen: the lowest goes
// first. Shortcuts (START, SELECT), then the menu, then the face actions; A and B never (-1): the screen's own
// action and the way back.
static inline int UI_hintDropRank(const char* button) {
	static const char* order[] = {"START", "SELECT", "MENU", "Y", "X"};
	if (!button || strcmp(button, "A") == 0 || strcmp(button, "B") == 0)
		return -1;
	for (int i = 0; i < (int)(sizeof(order) / sizeof(order[0])); i++)
		if (strcmp(button, order[i]) == 0)
			return i;
	return (int)(sizeof(order) / sizeof(order[0])); // anything else (L1, LEFT/RIGHT, SELECT+START...) last
}

// Which of the n hints to drop next: the lowest rank, the rightmost of a tie; -1 when every one is kept (A, B, or a
// hardware hint: keep[i] true).
static inline int UI_hintDropPick(const char* const* buttons, const int* keep, int n) {
	int pick = -1, best = 0;
	for (int i = 0; i < n; i++) {
		int r = keep && keep[i] ? -1 : UI_hintDropRank(buttons[i]);
		if (r >= 0 && (pick < 0 || r <= best))
			pick = i, best = r;
	}
	return pick;
}

#endif // UI_HINTBAR_LAYOUT_H
