#ifndef SCROLL_FADE_H
#define SCROLL_FADE_H

// The launcher lists' scroll fade in place of scroll arrows (nx-mobile's nxEdgeFade, ui_scroll_fade.h): while rows lie
// past an edge of a List's or Minimal's body, the rows fade out toward that edge into whatever is under them (the
// background, the game's art, Minimal's black). Begin before the list draws its rows, End after them, with the edges
// that have more past them. Sprite frames fade the sprites added in between (PLAT_spriteFadeBegin/End); a frame drawn
// into the screen (under a context menu) fades what landed in the bands' rows of the screen the same way.

#include "sdl.h"
#include <stdbool.h>

typedef struct {
	int top_y, top_h;		// the top band: rows top_y .. top_y + top_h, fading toward top_y (0 tall: none)
	int bottom_y, bottom_h; // the bottom band: rows bottom_y - bottom_h .. bottom_y, fading toward bottom_y
	bool strong;			// ease-in (the List's rows) rather than linear (Minimal's names)
	bool sprites;			// the rows are GPU sprites; else they are drawn into dst
} ScrollFade;

// Not sprites: the bands' rows of dst are set aside and cleared, so the rows land on clear pixels (what was there, a
// header's fade or a black body, goes back under them at End).
void ScrollFade_begin(SDL_Surface* dst, const ScrollFade* f);
// The rows fade toward the top edge (top) and the bottom edge (bottom) where asked; a band not asked is put back as
// the rows were drawn.
void ScrollFade_end(SDL_Surface* dst, bool top, bool bottom);
void ScrollFade_quit(void);

#endif
