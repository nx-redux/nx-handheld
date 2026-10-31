#ifndef INFOBAND_H
#define INFOBAND_H

#include "gameinfo_text.h"
#include "infoband_layout.h"
#include "sdl.h"
#include <stdbool.h>

// The band for the current List screen: one line of segments, left-aligned at layout->text_x (the rows' text start;
// 0 = UI_listTextX). No scroll arrows: the List's rows fade at an edge with more past it (scroll_fade.h).
// `layout` is the caller's list geometry (the one its rows use), so the band and the rows always agree.
// In two steps, so a caller can keep the band on its layer while nothing changed: the block built (or kept) for these
// contents (one cached ARGB block, rebuilt only when text/size change), returning a number that changes with every
// rebuild; then that block drawn onto `layer`.
unsigned InfoBand_prepare(const InfoBandLayout* layout, const InfoSeg* segs, int nsegs);
void InfoBand_draw(int layer);
// The segment painter (the band, the Game Switcher, Home): one line of segments in `font` (font.small on List
// screens, 14 sp; Home's tiles use 11 sp) with the dark shadow, separators and the trophy (12 dp at font.small,
// scaled with the line height otherwise), fitted to max_w (the last segment is cut, then dropped). Its right edge
// at x (align_right) or its left edge at x; y is the top of a TTF_FontHeight(font) line. Returns the drawn width
// (0 = nothing).
int InfoBand_drawSegments(SDL_Surface* dst, const InfoSeg* segs, int n, int x, bool align_right, int y, int max_w,
						  TTF_Font* font);
// The same, with the dark shadow only when `shadow` (the Carousel-Vertical's side caption has none, §7).
int InfoBand_drawSegmentsEx(SDL_Surface* dst, const InfoSeg* segs, int n, int x, bool align_right, int y, int max_w,
							TTF_Font* font, bool shadow);
// The width InfoBand_drawSegments would draw for the same arguments (0 = nothing): to centre a line.
int InfoBand_segmentsWidth(const InfoSeg* segs, int n, int max_w, TTF_Font* font);
// One separator's width and the trophy's with its gap, in `font`: the row as drawn, for caption_fit.h's measure.
int InfoBand_separatorWidth(TTF_Font* font);
int InfoBand_trophyWidth(TTF_Font* font);
// A rendered text surface with its dark shadow, the glyphs rendered once for both: s blitted black at shadow_a
// SCALE1(1) right and down (none at 0), then as it is (its own colour and alpha mods) at (x, y). s's mods are left as
// they were (it may be a shared cached surface); the caller keeps ownership.
void InfoBand_blitShadowed(SDL_Surface* s, SDL_Surface* dst, int x, int y, Uint8 shadow_a);
void InfoBand_quit(void); // free the cached block
#endif
