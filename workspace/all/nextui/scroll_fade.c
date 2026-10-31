// The List's and Minimal's scroll fade (scroll_fade.h). Sprite frames hand it to the compositor; a screen frame (the
// rows blended into the screen, under a context menu) does it on the bands' rows alone: before the rows, the rows
// under each band are copied aside and cleared, so the rows land on clear pixels (premultiplied: a BLEND onto
// (0,0,0,0) leaves colour times alpha, and the alpha); after them, each band row's pixels are scaled by what the fade
// keeps of that row and put back over the copy (premultiplied over: the result a direct blend would give, faded).
// Two bands of half a row (or 24 dp) across the screen: a few hundred KB touched, only on such frames.

#include "scroll_fade.h"

#include "api.h"
#include "ui_scroll_fade.h"

#include <stdlib.h>
#include <string.h>

static ScrollFade cur;
static bool begun = false;
static Uint32* saved[2] = {NULL, NULL}; // the band rows as they were before the rows (0 top, 1 bottom)
static size_t saved_cap[2] = {0, 0};
static SDL_Rect band[2]; // the rows saved, inside dst

// Band i's rows inside dst (h 0: none).
static SDL_Rect bandRect(SDL_Surface* dst, int i) {
	int y = i == 0 ? cur.top_y : cur.bottom_y - cur.bottom_h, h = i == 0 ? cur.top_h : cur.bottom_h;
	if (y < 0)
		h += y, y = 0;
	if (y + h > dst->h)
		h = dst->h - y;
	return (SDL_Rect){0, y, dst->w, h > 0 ? h : 0};
}

void ScrollFade_begin(SDL_Surface* dst, const ScrollFade* f) {
	begun = false;
	if (!dst || !f)
		return;
	cur = *f;
	begun = true;
	if (cur.sprites) {
		PLAT_spriteFadeBegin();
		return;
	}
	if (dst->format->BytesPerPixel != 4 || (SDL_MUSTLOCK(dst) && SDL_LockSurface(dst) != 0)) {
		begun = false; // no fade on a surface it can't read
		return;
	}
	for (int i = 0; i < 2; i++) {
		band[i] = bandRect(dst, i);
		if (i == 1 && band[1].y < band[0].y + band[0].h) { // a body under two bands: the top one keeps the overlap
			int cut = band[0].y + band[0].h - band[1].y;
			band[1].y += cut, band[1].h = band[1].h > cut ? band[1].h - cut : 0;
		}
		size_t need = (size_t)band[i].w * band[i].h;
		if (need > saved_cap[i]) {
			Uint32* grown = realloc(saved[i], need * sizeof(Uint32));
			if (!grown) {
				band[i].h = 0;
				continue;
			}
			saved[i] = grown;
			saved_cap[i] = need;
		}
		for (int y = 0; y < band[i].h; y++) {
			Uint32* row = (Uint32*)((Uint8*)dst->pixels + (band[i].y + y) * dst->pitch);
			memcpy(saved[i] + (size_t)y * band[i].w, row, band[i].w * sizeof(Uint32));
			memset(row, 0, band[i].w * sizeof(Uint32));
		}
	}
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

// x * k / 255, rounded (k 0..255)
static inline Uint32 mul255(Uint32 x, Uint32 k) {
	return (x * k + 127) / 255;
}

// One band row: the rows' pixels scaled by keep, over the copy. All four channels scale alike (premultiplied), so the
// channel order doesn't matter beyond finding the alpha (ashift).
static void compositeRow(Uint32* row, const Uint32* under, int w, int keep, int ashift) {
	for (int x = 0; x < w; x++) {
		Uint32 d = row[x];
		if (!d) { // nothing drawn here: what was there
			row[x] = under[x];
			continue;
		}
		if (keep < 255)
			d = mul255(d >> 24, keep) << 24 | mul255(d >> 16 & 0xFF, keep) << 16 | mul255(d >> 8 & 0xFF, keep) << 8 |
				mul255(d & 0xFF, keep);
		Uint32 inv = 255 - (d >> ashift & 0xFF), s = under[x];
		if (inv == 0 || !s) {
			row[x] = d;
			continue;
		}
		Uint32 out = 0;
		for (int c = 0; c < 32; c += 8) {
			Uint32 v = (d >> c & 0xFF) + mul255(s >> c & 0xFF, inv);
			out |= (v > 255 ? 255 : v) << c;
		}
		row[x] = out;
	}
}

void ScrollFade_end(SDL_Surface* dst, bool top, bool bottom) {
	if (!begun || !dst)
		return;
	begun = false;
	if (cur.sprites) {
		PLAT_spriteFadeEnd(cur.top_y, top ? cur.top_h : 0, cur.bottom_y, bottom ? cur.bottom_h : 0, cur.strong);
		return;
	}
	if (SDL_MUSTLOCK(dst) && SDL_LockSurface(dst) != 0)
		return;
	int ashift = dst->format->Ashift;
	for (int i = 0; i < 2; i++) {
		bool on = i == 0 ? top : bottom;
		int h = i == 0 ? cur.top_h : cur.bottom_h;
		int edge = i == 0 ? cur.top_y : cur.bottom_y - 1; // the band's outermost row
		for (int y = 0; y < band[i].h; y++) {
			int sy = band[i].y + y;
			int keep = on ? UI_scrollFadeKeep(i == 0 ? sy - edge : edge - sy, h, cur.strong) : 255;
			compositeRow((Uint32*)((Uint8*)dst->pixels + sy * dst->pitch), saved[i] + (size_t)y * band[i].w,
						 band[i].w, keep, ashift);
		}
	}
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

void ScrollFade_quit(void) {
	for (int i = 0; i < 2; i++) {
		free(saved[i]);
		saved[i] = NULL;
		saved_cap[i] = 0;
	}
	begun = false;
}
