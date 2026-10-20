#include "minimal_draw.h"

#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "menutabs.h"
#include "ui_accent.h"
#include "ui_ease.h"
#include "ui_fade.h"
#include "ui_font.h"
#include "ui_list.h"

static float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static SDL_Color opaque(SDL_Color c) {
	c.a = 255;
	return c;
}

bool MinDraw_metrics(MinMetrics* m) {
	int px = NX_DP(MIN_NAME_DP);
	m->font = UIFont_getPx(px, true);
	if (!m->font)
		return false;
	m->font_h = TTF_FontHeight(m->font);
	m->pitch = px + NX_DP(2);
	m->outline = 1; // a hairline at every scale: NX_DP(1)'s 2 px read heavy on the 2.x-scale handhelds
	m->gap = NX_DP(MIN_ITEM_GAP_DP);
	return true;
}

///////////////////////////////////////
// Text

int MinDraw_textW(TTF_Font* f, const char* text) {
	int w = 0;
	if (f && text && text[0])
		GFX_measureText(f, text, &w, NULL);
	return w;
}

void MinDraw_ellipsize(TTF_Font* f, const char* in, char* out, int max_w) {
	char src[MIN_LINE_MAX];
	snprintf(src, sizeof(src), "%s", in ? in : "");
	out[0] = '\0';
	if (!f || max_w <= 0 || !src[0])
		return;
	if (MinDraw_textW(f, src) <= max_w) {
		snprintf(out, MIN_LINE_MAX, "%s", src);
		return;
	}
	char cut[MIN_LINE_MAX + 8];
	int w = GFX_truncateText(f, src, cut, max_w, 0);
	if (w <= max_w && strcmp(cut, "...") != 0)
		snprintf(out, MIN_LINE_MAX, "%s", cut);
}

int MinDraw_wrap(TTF_Font* f, const char* text, int max_w, int max_lines, char lines[][MIN_LINE_MAX]) {
	char work[MIN_LINE_MAX];
	snprintf(work, sizeof(work), "%s", text ? text : "");
	int n = 0;
	const char* p = work;
	while (*p == ' ')
		p++;
	while (*p && n < max_lines) {
		if (n == max_lines - 1) { // the rest, cut to fit
			MinDraw_ellipsize(f, p, lines[n], max_w);
			if (lines[n][0])
				n++;
			break;
		}
		// take words while they fit; a single word too wide is cut on its own line
		char line[MIN_LINE_MAX] = "";
		const char* q = p;
		const char* taken_end = p;
		while (*q) {
			const char* word_end = strchr(q, ' ');
			if (!word_end)
				word_end = q + strlen(q);
			char cand[MIN_LINE_MAX];
			snprintf(cand, sizeof(cand), "%.*s", (int)(word_end - p), p);
			if (line[0] && MinDraw_textW(f, cand) > max_w)
				break;
			snprintf(line, sizeof(line), "%s", cand);
			taken_end = word_end;
			q = word_end;
			while (*q == ' ')
				q++;
		}
		MinDraw_ellipsize(f, line, lines[n], max_w);
		if (lines[n][0])
			n++;
		p = taken_end;
		while (*p == ' ')
			p++;
	}
	return n;
}

// Text with its top-left at (x, y). Rendered fresh, not through GFX_getCachedText: the names are composed once into
// their own surfaces, and they would only evict the lists' text from that shared cache.
static void drawText(SDL_Surface* dst, TTF_Font* f, const char* text, SDL_Color c, int x, int y) {
	if (!f || !text || !text[0])
		return;
	SDL_Surface* surf = GFX_renderText(f, text, c);
	if (!surf)
		return;
	SDL_BlitSurface(surf, NULL, dst, &(SDL_Rect){x, y});
	SDL_FreeSurface(surf);
}

SDL_Surface* MinDraw_lines(const MinMetrics* m, char lines[][MIN_LINE_MAX], int n, int w, SDL_Color c, bool outlined) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w + 2 * m->outline, n * m->pitch + m->font_h + 2 * m->outline,
													32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	SDL_FillRect(s, NULL, (Uint32)c.r << 16 | (Uint32)c.g << 8 | c.b);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	if (outlined)
		TTF_SetFontOutline(m->font, m->outline);
	int in = outlined ? 0 : m->outline;
	for (int l = 0; l < n; l++)
		drawText(s, m->font, lines[l], c, in, in + l * m->pitch);
	if (outlined)
		TTF_SetFontOutline(m->font, 0);
	return s;
}

///////////////////////////////////////
// Drawing

void MinDraw_blit(SDL_Surface* s, SDL_Rect r, float alpha, const SDL_Rect* clip, Uint8 shade, bool sprite,
				  SDL_Surface* dst) {
	int a = (int)(alpha * MenuTabs_contentAlpha() * 255.0f + 0.5f);
	if (!s || a <= 0)
		return;
	if (a > 255)
		a = 255;
	if (r.w <= 0 || r.h <= 0)
		r.w = s->w, r.h = s->h;
	SDL_Texture* t = sprite ? PLAT_textureForSurface(s) : NULL;
	if (t) {
		PLAT_spriteAddShaded(t, NULL, &r, (Uint8)a, shade, clip);
		return;
	}
	SDL_SetSurfaceAlphaMod(s, (Uint8)a);
	SDL_SetSurfaceColorMod(s, shade, shade, shade);
	if (r.w == s->w && r.h == s->h)
		SDL_BlitSurface(s, NULL, dst, &r);
	else
		SDL_BlitScaled(s, NULL, dst, &r);
	SDL_SetSurfaceColorMod(s, 255, 255, 255);
}

SDL_Surface* MinFade_get(MinFade* f, bool top, int w, int h) {
	if (h <= 0)
		return NULL;
	if (f->s && f->w == w && f->h == h)
		return f->s;
	MinFade_free(f);
	// its own copy of the eased fade (UI_easedFadeSurface's is shared), for its GPU texture
	SDL_Surface* src = UI_easedFadeSurface(w, h, 0.9f, 3.5f, top);
	SDL_Surface* s = src ? SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
	if (!s)
		return NULL;
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(src, NULL, s, NULL);
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	f->s = s;
	f->w = w;
	f->h = h;
	return s;
}

void MinFade_free(MinFade* f) {
	GFX_freeSurfaceAndTexture(f->s);
	*f = (MinFade){0};
}

///////////////////////////////////////
// The open band

bool MinOpen_start(MinOpen* o, const MinMetrics* m, char lines[][MIN_LINE_MAX], int n, SDL_Rect from) {
	if (!CFG_getMenuAnimations())
		return false;
	GFX_freeSurfaceAndTexture(o->name);
	o->name = MinDraw_lines(m, lines, n > 0 ? n : 1, from.w, opaque(UI_accent()), false);
	o->from = from;
	o->start = SDL_GetTicks();
	o->active = true;
	return true;
}

bool MinOpen_done(const MinOpen* o) {
	return o->active && SDL_GetTicks() - o->start >= MIN_LEAVE_MS + MIN_OPEN_GROW_MS;
}

float MinOpen_othersAlpha(const MinOpen* o) {
	if (!o->active)
		return 1.0f;
	return 1.0f - clamp01((SDL_GetTicks() - o->start) / (float)MIN_LEAVE_MS);
}

static SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
	return (SDL_Color){(Uint8)(a.r + (b.r - a.r) * t + 0.5f), (Uint8)(a.g + (b.g - a.g) * t + 0.5f),
					   (Uint8)(a.b + (b.b - a.b) * t + 0.5f), 255};
}

void MinOpen_draw(MinOpen* o, const MinMetrics* m, bool sprite, SDL_Surface* dst) {
	if (!o->active)
		return;
	Uint32 t = SDL_GetTicks() - o->start;
	if (t < MIN_LEAVE_MS) // the other names fade out first (the list's own draw)
		return;
	float k = clamp01((t - MIN_LEAVE_MS) / (float)MIN_OPEN_GROW_MS);
	float g = UI_cubicBezier(0.65f, 0.0f, 0.35f, 1.0f, k);
	float dark = clamp01((k - 0.4f) / 0.6f); // black once the band has (nearly) filled the screen
	SDL_Color band = mix((SDL_Color){0, 0, 0, 255}, opaque(UI_accent()), MIN_BAND_TINT);
	Uint32 rgb = (Uint32)band.r << 16 | (Uint32)band.g << 8 | band.b;
	if (!o->band || o->band_rgb != rgb) { // one opaque pixel in the band's colour, stretched (the dark: a shade)
		GFX_freeSurfaceAndTexture(o->band);
		o->band = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
		if (o->band)
			SDL_FillRect(o->band, NULL, 0xFF000000u | rgb);
		o->band_rgb = rgb;
	}
	int pad = NX_DP(6);
	float top0 = o->from.y - pad, bottom0 = o->from.y + o->from.h + pad;
	float top = top0 * (1.0f - g), bottom = bottom0 + (dst->h - bottom0) * g;
	SDL_Rect whole = {0, 0, dst->w, dst->h};
	Uint8 shade = (Uint8)((1.0f - dark) * 255.0f + 0.5f);
	MinDraw_blit(o->band, (SDL_Rect){0, (int)top, dst->w, (int)(bottom - top + 0.5f)}, 1.0f, &whole, shade, sprite, dst);
	if (o->name) // where it was, going dark with the band
		MinDraw_blit(o->name, (SDL_Rect){o->from.x - m->outline, o->from.y - m->outline, 0, 0}, 1.0f, &whole, shade,
					 sprite, dst);
}

void MinOpen_free(MinOpen* o) {
	GFX_freeSurfaceAndTexture(o->name);
	GFX_freeSurfaceAndTexture(o->band);
	*o = (MinOpen){0};
}
