#ifndef MINVIEW_H
#define MINVIEW_H

#include "sdl.h"
#include <stdbool.h>

// The Minimal style (minimal_draw.h) over the current Directory `top`: a root tab (Consoles, Collections, Tools) or a
// game list whose Layouts row is Minimal. Just the names, no art. GameList_render/GameList_handleInput delegate here;
// the tab row, the title and the hint bar stay theirs, and so do LEFT/RIGHT (a tab switch at the root, a page in a
// game list) and L1/R1 (a tab, or a letter).

bool MinView_active(void); // GameList_currentStyle() == MENU_STYLE_MINIMAL && !Home_active()
// The body: plain black and the names around the selection (no hints).
void MinView_render(SDL_Surface* screen, int lastScreen);

typedef enum {
	MINVIEW_PASS,	 // not this view's: the list's own handlers take it
	MINVIEW_HANDLED, // a D-pad move, or A starting the open band (sets *dirty)
	MINVIEW_OPEN,	 // the band has gone dark (or A with Menu animations off): open the selection as the list's A does
} MinViewInput;
// UP/DOWN one name (a fresh press at either end wraps to the other), A (the band first). While the band runs it
// swallows every button.
MinViewInput MinView_handleInput(unsigned long now, bool* dirty);
// After MINVIEW_OPEN on a folder opened (opened_list: a list, not a game it launched): true when the name now flies
// into the new list's title (it is a Minimal one too), so the caller drops the slide.
bool MinView_afterOpen(bool opened_list);
// B in a Minimal list: call before closing it (keeps its title), then MinView_afterBack once closed: true when the
// title now drops back into its row (the parent is a Minimal one too), so the caller drops the slide.
void MinView_beforeBack(void);
bool MinView_afterBack(void);
// The slide or the open band is running (one settled frame after the slide).
bool MinView_animating(void);
// Free the cached names.
void MinView_quit(void);

#endif // MINVIEW_H
