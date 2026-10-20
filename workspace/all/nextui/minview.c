// The Minimal style over a root tab or a game list (minview.h): the names stacked down the body, the selection one
// pitch under its top, solid in the accent, the others outlined and fading with their distance. A list can hold
// thousands of names, so only those near the selection are composed (a few dozen kept, the farthest dropped first),
// and a name's line count, which places every name after it, is measured once and remembered.

#include "minview.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_accent.h"
#include "ui_contextmenu.h"
#include "ui_ease.h"
#include "ui_fade.h"
#include "ui_menubar.h"
#include "ui_message.h"

#include "gamelist.h"
#include "home.h"
#include "home_list_layout.h" // HomeMin_alpha
#include "imgloader.h"		  // screen
#include "list_window.h"
#include "menutabs.h"
#include "minimal_draw.h"
#include "rowview.h" // RowView_beginSprites (the frame drew GPU sprites)
#include "view_common.h"

#define SLIDE_MS 300 // a one-name move's slide, UI_easeStandard (as Home's Minimal)
#define SLOTS 32	 // names composed at once (a screen shows a dozen or so)
// A list opening (A on a folder: the name becomes the next list's title) and closing (B: the title drops back into
// its row): after the others fade out (MIN_LEAVE_MS), the name flies between its row and the title bar over MORPH_MS
// while the new list's names rise into place, each RISE_STAGGER_MS after the one above (or, back, fade in).
#define MORPH_MS 280
#define RISE_DELAY_MS 80
#define RISE_MS 220
#define RISE_STAGGER_MS 30
#define RISE_DP 24.0f

typedef struct {
	int index; // the entry's (-1: free)
	SDL_Surface* solid;
	SDL_Surface* outline;
} Slot;
static Slot slots[SLOTS];
static int* line_counts = NULL; // per entry: its wrapped line count (0: not measured yet)
static int line_cap = 0;

static MinMetrics m;
static bool have_m = false;
static unsigned seen_top = 0; // the Directory serial (0: none)
static unsigned seen_gen = 0;
static int seen_n = -1, seen_w = 0, seen_scale = 0;
static Uint32 seen_accent = 0;

static float pos_from = 0, pos_to = 0; // the shown position: the selection's index, tweened between
static Tween slide_tw;
static unsigned slide_ms = SLIDE_MS; // the slide's length: SLIDE_MS, or the glide's settle (glideRelease)
static bool slide_decel = false;	 // the slide eases out (the glide's settle) rather than UI_easeStandard
static HomeMinGlide glide;			 // a held UP/DOWN's glide (as Home's Minimal)
static int last_sel = -1;
static bool sprites = false; // this frame draws the names as GPU sprites (not under a context menu)
static MinOpen open_band;
static MinFade bottom_fade;

static bool leaving = false; // A on a folder: the other names fading out before it opens
static Uint32 leave_start = 0;
typedef struct {
	bool active;
	bool back; // B: from the title down into the row
	Uint32 start;
	SDL_Rect row;	   // the name's lines in the list (forward: where it was; back: where it lands, set on its frame)
	SDL_Rect title;	   // the title's text in the bar
	SDL_Surface* name; // the name's lines, solid in the accent, as the list draws them
	SDL_Surface* text; // the title as the bar draws it
} Morph;
static Morph morph;
static bool morph_pending = false;	 // the leave ran; the morph starts if the list that opens is a Minimal one
static SDL_Surface* black_px = NULL; // one black pixel the GPU stretches over the bar's title while the name flies

static float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static int scaleKey(void) {
	return (int)lroundf(FIXED_SCALE * 100.0f);
}

static float currentPos(void) {
	if (glide.dir)
		return glide.pos;
	float t = tweenProgress(&slide_tw, slide_ms);
	return pos_from + (pos_to - pos_from) * (slide_decel ? HomeMin_easeOut(t) : UI_easeStandard(t));
}

static int nameWidth(SDL_Surface* dst) {
	return dst->w - 2 * NX_DP(NX_MENU_GUTTER_DP);
}

static void slotsClear(void) {
	for (int i = 0; i < SLOTS; i++) {
		GFX_freeSurfaceAndTexture(slots[i].solid);
		GFX_freeSurfaceAndTexture(slots[i].outline);
		slots[i] = (Slot){.index = -1};
	}
}

// A list, tab, screen size, scale or accent change: drop what was composed and measured for the last one, and snap to
// the selection.
static bool syncList(SDL_Surface* dst, int n, int lastScreen) {
	SDL_Color a = UI_accent();
	Uint32 accent = (Uint32)a.r << 16 | (Uint32)a.g << 8 | a.b;
	bool changed = top->serial != seen_top || MenuTabs_generation() != seen_gen || n != seen_n || dst->w != seen_w ||
				   scaleKey() != seen_scale || accent != seen_accent || lastScreen != SCREEN_GAMELIST;
	if (!changed)
		return false;
	seen_top = top->serial;
	seen_gen = MenuTabs_generation();
	seen_n = n;
	seen_w = dst->w;
	seen_scale = scaleKey();
	seen_accent = accent;
	slotsClear();
	have_m = MinDraw_metrics(&m);
	if (n > line_cap) {
		int* grown = realloc(line_counts, n * sizeof(int));
		if (!grown)
			return true;
		line_counts = grown;
		line_cap = n;
	}
	if (line_counts)
		memset(line_counts, 0, line_cap * sizeof(int));
	return true;
}

static const char* nameOf(int i) {
	return View_displayName((Entry*)top->entries->items[i]);
}

static int wrapName(int i, int max_w, char lines[][MIN_LINE_MAX]) {
	int c = MinDraw_wrap(m.font, nameOf(i), max_w, MIN_MAX_LINES, lines);
	if (c < 1) {
		lines[0][0] = '\0';
		c = 1;
	}
	return c;
}

// Name i's height: its lines and the gap to the next name.
static int nameH(int i, int max_w) {
	if (i < 0 || i >= line_cap || !line_counts)
		return m.pitch + m.gap;
	if (line_counts[i] == 0) {
		char lines[MIN_MAX_LINES][MIN_LINE_MAX];
		line_counts[i] = wrapName(i, max_w, lines);
	}
	return line_counts[i] * m.pitch + m.gap;
}

// Name i's surfaces, composed into a free slot or the one farthest from sel.
static Slot* slotFor(int i, int sel, int max_w) {
	int far = 0, far_d = -1;
	for (int k = 0; k < SLOTS; k++) {
		if (slots[k].index == i)
			return &slots[k];
		int d = slots[k].index < 0 ? 1 << 30 : abs(slots[k].index - sel);
		if (d > far_d)
			far = k, far_d = d;
	}
	Slot* s = &slots[far];
	GFX_freeSurfaceAndTexture(s->solid);
	GFX_freeSurfaceAndTexture(s->outline);
	char lines[MIN_MAX_LINES][MIN_LINE_MAX];
	int c = wrapName(i, max_w, lines);
	SDL_Color acc = UI_accent();
	acc.a = 255;
	s->index = i;
	s->solid = MinDraw_lines(&m, lines, c, max_w, acc, false);
	s->outline = MinDraw_lines(&m, lines, c, max_w, acc, true);
	return s;
}

// The body's top and bottom: under the tab row (or title), over the hint bar (to the screen's bottom with the hints
// hidden, the names running on under a fade).
static void bodyRange(SDL_Surface* dst, int* top_y, int* bottom_y) {
	*top_y = barPx();
	*bottom_y = CFG_getButtonHints() ? dst->h - barPx() : dst->h;
}

// Name i's top on the screen at the shown position p: the name at p one pitch under the body's top.
static int nameY(int i, float p, int body_top, int max_w) {
	int base = (int)floorf(p);
	float y = body_top + m.pitch - (p - base) * nameH(base, max_w);
	if (i >= base) {
		for (int k = base; k < i; k++)
			y += nameH(k, max_w);
	} else {
		for (int k = i; k < base; k++)
			y -= nameH(k, max_w);
	}
	return (int)lroundf(y);
}

static float easeOut(float t) {
	t = clamp01(t);
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// What a name keeps of its alpha (and how far below its place it is) in the transitions: the others fading out on A,
// the new list's names rising in after a list opened (k: its place from the top of the screen), the parent's fading
// back in around the name dropping into its row.
static float transitionAlpha(int i, int sel, int k, int* rise) {
	*rise = 0;
	float a = i == sel ? 1.0f : MinOpen_othersAlpha(&open_band);
	if (leaving && i != sel)
		a *= 1.0f - clamp01((SDL_GetTicks() - leave_start) / (float)MIN_LEAVE_MS);
	if (morph.active) {
		float t = (float)(SDL_GetTicks() - morph.start);
		if (morph.back) {
			if (i == sel) // the flying name lands here
				return 0.0f;
			a *= clamp01((t - RISE_DELAY_MS) / (float)RISE_MS);
		} else {
			float e = easeOut((t - RISE_DELAY_MS - k * RISE_STAGGER_MS) / (float)RISE_MS);
			*rise = (int)lroundf((1.0f - e) * NX_DP(RISE_DP));
			a *= e;
		}
	}
	return a;
}

static void drawName(SDL_Surface* dst, int i, int y, float p, int sel, int k, int max_w, const SDL_Rect* clip) {
	int rise;
	float ta = transitionAlpha(i, sel, k, &rise);
	if (ta <= 0.0f)
		return;
	y += rise;
	Slot* s = slotFor(i, sel, max_w);
	float d = i - p;
	float a = HomeMin_alpha(d) * ta;
	float fill = clamp01(1.0f - fabsf(d));
	int x = NX_DP(NX_MENU_GUTTER_DP) - m.outline;
	MinDraw_blit(s->outline, (SDL_Rect){x, y - m.outline, 0, 0}, a * (1.0f - fill) * MIN_OUTLINE_ALPHA, clip, 255,
				 sprites, dst);
	MinDraw_blit(s->solid, (SDL_Rect){x, y - m.outline, 0, 0}, a * fill, clip, 255, sprites, dst);
}

static void morphFree(void) {
	GFX_freeSurfaceAndTexture(morph.name);
	GFX_freeSurfaceAndTexture(morph.text);
	morph = (Morph){0};
}

// The name between its row and the title: its lines scaled down to the title's height as they fly, crossfading into
// the title as the bar draws it; forward, the bar's own title stays covered until the name has landed there.
static void drawMorph(SDL_Surface* dst) {
	if (!morph.active)
		return;
	float t = clamp01((SDL_GetTicks() - morph.start) / (float)MORPH_MS);
	float k = UI_easeStandard(morph.back ? 1.0f - t : t); // 0: at the row, 1: at the title
	SDL_Rect whole = {0, 0, dst->w, dst->h};
	if (!morph.back && t < 1.0f) {
		if (!black_px) {
			black_px = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888);
			if (black_px)
				SDL_FillRect(black_px, NULL, 0xFF000000u);
		}
		int pad = NX_DP(4);
		SDL_Rect cover = {morph.title.x - pad, 0, morph.title.w + 2 * pad, barPx()};
		MinDraw_blit(black_px, cover, 1.0f, &whole, 255, sprites, dst);
	}
	float s = m.pitch > 0 ? 1.0f + ((float)morph.title.h / m.pitch - 1.0f) * k : 1.0f;
	float x = morph.row.x + (morph.title.x - morph.row.x) * k;
	float y = morph.row.y + (morph.title.y - morph.row.y) * k;
	if (morph.name)
		MinDraw_blit(morph.name,
					 (SDL_Rect){(int)lroundf(x - m.outline * s), (int)lroundf(y - m.outline * s),
								(int)lroundf(morph.name->w * s), (int)lroundf(morph.name->h * s)},
					 1.0f - k, &whole, 255, sprites, dst);
	if (morph.text)
		MinDraw_blit(morph.text, (SDL_Rect){(int)lroundf(x), (int)lroundf(y), 0, 0}, k, &whole, 255, sprites, dst);
}

// The title the bar draws over this list (nextui's listTitle, at the game lists' gutter), and its rect.
static SDL_Surface* titleSurface(SDL_Rect* r) {
	TTF_Font* f = UI_pageTitleFont();
	if (!f || !top || !top->name)
		return NULL;
	char* name = top->name;
	trimSortingMeta(&name);
	char fitted[MIN_LINE_MAX];
	MinDraw_ellipsize(f, name, fitted, screen ? screen->w * 8 / 10 : 0);
	SDL_Surface* s = fitted[0] ? GFX_renderText(f, fitted, COLOR_GRAY) : NULL;
	if (!s)
		return NULL;
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	*r = (SDL_Rect){NX_DP(NX_MENU_GUTTER_DP), (UI_menuBarHeight() - s->h) / 2, s->w, s->h};
	return s;
}

// The selected name's lines as the list draws them, and their rect on the screen.
static SDL_Surface* rowSurface(SDL_Rect* r) {
	int n = top ? top->entries->count : 0;
	if (!have_m || !screen || n <= 0)
		return NULL;
	int sel = View_selectedIndex(n), body_top, body_bottom, max_w = nameWidth(screen);
	bodyRange(screen, &body_top, &body_bottom);
	char lines[MIN_MAX_LINES][MIN_LINE_MAX];
	int c = wrapName(sel, max_w, lines);
	SDL_Color acc = UI_accent();
	acc.a = 255;
	*r = (SDL_Rect){NX_DP(NX_MENU_GUTTER_DP), nameY(sel, currentPos(), body_top, max_w), max_w, c * m.pitch};
	return MinDraw_lines(&m, lines, c, max_w, acc, false);
}

bool MinView_afterOpen(bool opened_list) {
	bool pending = morph_pending;
	morph_pending = false;
	if (!pending || !opened_list || !MinView_active() || !animationsOn()) {
		morphFree();
		return false;
	}
	GFX_freeSurfaceAndTexture(morph.text);
	morph.text = titleSurface(&morph.title);
	morph.back = false;
	morph.start = SDL_GetTicks();
	morph.active = true;
	return true;
}

void MinView_beforeBack(void) {
	morphFree();
	if (!MinView_active() || !animationsOn() || !have_m)
		return;
	morph.text = titleSurface(&morph.title); // the title the bar draws now: where the name flies down from
}

bool MinView_afterBack(void) {
	if (!morph.text || !MinView_active() || !screen) {
		morphFree();
		return false;
	}
	// the parent's names (its metrics and line counts) are this frame's: built now, before its first draw
	syncList(screen, top->entries->count, SCREEN_GAMELIST);
	last_sel = -1;
	pos_from = pos_to = (float)View_selectedIndex(top->entries->count);
	slide_tw.active = false;
	morph.name = rowSurface(&morph.row);
	if (!morph.name) {
		morphFree();
		return false;
	}
	morph.back = true;
	morph.start = SDL_GetTicks();
	morph.active = true;
	return true;
}

bool MinView_active(void) {
	return GameList_currentStyle() == MENU_STYLE_MINIMAL && !Home_active();
}

void MinView_render(SDL_Surface* dst, int lastScreen) {
	if (!dst || !top)
		return;
	int n = top->entries->count;
	bool snap = syncList(dst, n, lastScreen);
	int body_top, body_bottom;
	bodyRange(dst, &body_top, &body_bottom);
	SDL_FillRect(dst, &(SDL_Rect){0, body_top, dst->w, body_bottom - body_top}, SDL_MapRGBA(dst->format, 0, 0, 0, 255));
	if (n <= 0 || !have_m || body_bottom <= body_top) {
		last_sel = -1;
		slide_tw.active = false;
		if (n <= 0)
			UI_renderCenteredMessage(dst, "Empty folder");
		return;
	}

	// a one-name move slides; anything farther (a wrap, a page, a letter, a new list) snaps
	// (a held glide moves the names itself, glideStep)
	int sel = View_selectedIndex(n);
	if (snap)
		glide.dir = 0;
	if (glide.dir)
		; // glideStep placed them
	else if (snap || last_sel < 0 || abs(sel - last_sel) != 1 || !animationsOn()) {
		pos_from = pos_to = (float)sel;
		slide_tw.active = false;
	} else if (sel != last_sel) {
		pos_from = currentPos();
		pos_to = (float)sel;
		slide_ms = SLIDE_MS;
		slide_decel = false;
		tweenStart(&slide_tw);
	}
	last_sel = sel;
	float p = currentPos();

	int max_w = nameWidth(dst);
	SDL_Rect clip = {0, body_top, dst->w, body_bottom - body_top};
	sprites = !ContextMenu_isOpen();
	RowView_beginSprites(sprites); // marks the frame as a sprite frame (the host uploads only the bars)
	SDL_Rect prev_clip;
	SDL_GetClipRect(dst, &prev_clip);
	SDL_SetClipRect(dst, &clip);

	// from the shown position up while a name still reaches the body, then down while one starts inside it
	int base = (int)floorf(p);
	if (base < 0)
		base = 0;
	if (base > n - 1)
		base = n - 1;
	int y = nameY(base, p, body_top, max_w);
	int above = 0; // the names drawn above base: the top one rises first
	for (int i = base, yi = y; i >= 0 && yi + nameH(i, max_w) > body_top; i--) {
		above++;
		if (i > 0)
			yi -= nameH(i - 1, max_w);
	}
	for (int i = base, yi = y, k = above - 1; i >= 0 && k >= 0; i--, k--) {
		drawName(dst, i, yi, p, sel, k, max_w, &clip);
		if (i > 0)
			yi -= nameH(i - 1, max_w);
	}
	for (int i = base + 1, yi = y + nameH(base, max_w), k = above; i < n && yi < body_bottom; i++, k++) {
		drawName(dst, i, yi, p, sel, k, max_w, &clip);
		yi += nameH(i, max_w);
	}
	if (morph.active && morph.back) // where the name lands: its row on this frame
		morph.row.y = nameY(sel, p, body_top, max_w);
	if (!CFG_getButtonHints()) { // the names fade out to the screen's bottom edge
		int h = barPx() + NX_DP(24);
		SDL_Surface* f = MinFade_get(&bottom_fade, false, dst->w, h);
		if (sprites)
			MinDraw_blit(f, (SDL_Rect){0, dst->h - h, 0, 0}, 1.0f, &clip, 255, true, dst);
		else if (f)
			UI_blitFade(f, NULL, dst, 0, dst->h - h);
	}
	SDL_SetClipRect(dst, &prev_clip);
	drawMorph(dst);
	MinOpen_draw(&open_band, &m, sprites, dst); // over everything, the bars included
	RowView_beginSprites(false);
	sprites = false;
}

static void selectName(int i, int n) {
	top->selected = i;
	ListWindow_selectAtTop(n, GameList_rowCount(), i, &top->start, &top->end); // a later switch to List finds it
}

// A on the selection: a folder's name gets ready to become the next list's title (the others fade out first); a
// game's or a tool's band starts on its rows. False with Menu animations off (it opens straight away).
static bool openStart(int n) {
	if (!have_m || !screen || !animationsOn())
		return false;
	int sel = View_selectedIndex(n);
	if (((Entry*)top->entries->items[sel])->type == ENTRY_DIR) {
		morphFree();
		morph.name = rowSurface(&morph.row);
		leaving = true;
		leave_start = SDL_GetTicks();
		return true;
	}
	int body_top, body_bottom;
	bodyRange(screen, &body_top, &body_bottom);
	int max_w = nameWidth(screen);
	char lines[MIN_MAX_LINES][MIN_LINE_MAX];
	int c = wrapName(sel, max_w, lines);
	SDL_Rect from = {NX_DP(NX_MENU_GUTTER_DP), nameY(sel, currentPos(), body_top, max_w), max_w, c * m.pitch};
	return MinOpen_start(&open_band, &m, lines, c, from);
}

// Let go: the names ease out into the next name the glide was heading for, from its speed (as Home's Minimal).
static void glideRelease(int n) {
	pos_from = glide.pos;
	int to = HomeMin_glideSettle(&glide, n, &slide_ms);
	pos_to = (float)to;
	slide_decel = true;
	tweenStart(&slide_tw);
	if (!slide_tw.active || pos_from == pos_to)
		slide_tw.active = false, pos_from = pos_to;
	if (to != View_selectedIndex(n))
		selectName(to, n);
	last_sel = to; // the render doesn't slide again to it
}

// A frame of a held glide: on at its speed, the selection the name nearest; let go (or the other way pressed), it
// settles. True while it glides.
static bool glideStep(int n, bool* dirty) {
	if (!glide.dir)
		return false;
	*dirty = true;
	if (n <= 0) {
		glide.dir = 0;
		return false;
	}
	if (!PAD_isPressed(glide.dir > 0 ? BTN_DOWN : BTN_UP) || PAD_isPressed(glide.dir > 0 ? BTN_UP : BTN_DOWN)) {
		glideRelease(n);
		return false;
	}
	int sel = (int)lroundf(HomeMin_glideStep(&glide, SDL_GetTicks(), n));
	if (sel != View_selectedIndex(n))
		selectName(sel, n);
	last_sel = sel;
	return true;
}

MinViewInput MinView_handleInput(unsigned long now, bool* dirty) {
	(void)now;
	if (open_band.active) { // the band: the item opens once it has gone dark; nothing else meanwhile
		*dirty = true;
		if (!MinOpen_done(&open_band))
			return MINVIEW_HANDLED;
		open_band.active = false;
		return MINVIEW_OPEN;
	}
	if (leaving) { // a folder: it opens once the others have faded out (MinView_afterOpen takes it from there)
		*dirty = true;
		if (SDL_GetTicks() - leave_start < MIN_LEAVE_MS)
			return MINVIEW_HANDLED;
		leaving = false;
		morph_pending = true;
		return MINVIEW_OPEN;
	}
	int n = top ? top->entries->count : 0;
	if (glideStep(n, dirty) && PAD_justPressed(BTN_A)) // A mid-glide: on the name it settles on
		glideRelease(n);
	if (n <= 0)
		return MINVIEW_PASS;
	if (PAD_justPressed(BTN_A)) {
		*dirty = true;
		return openStart(n) ? MINVIEW_HANDLED : MINVIEW_OPEN;
	}
	int btn = PAD_justRepeated(BTN_UP) ? BTN_UP : PAD_justRepeated(BTN_DOWN) ? BTN_DOWN
																			 : 0;
	if (!btn)
		return MINVIEW_PASS;
	if (!PAD_justPressed(btn) && animationsOn()) { // held: glide (from the first repeat on)
		int dir = btn == BTN_UP ? -1 : 1;
		if (glide.dir != dir) {
			HomeMin_glideStart(&glide, dir, currentPos(), SDL_GetTicks());
			slide_tw.active = false;
		}
		*dirty = true;
		return MINVIEW_HANDLED;
	}
	int sel = View_selectedIndex(n), to = sel + (btn == BTN_UP ? -1 : 1);
	if (to < 0 || to >= n) // a fresh press wraps to the other end; a held one stops
		to = PAD_justPressed(btn) && n > 1 ? (to < 0 ? n - 1 : 0) : sel;
	if (to != sel) {
		selectName(to, n);
		*dirty = true;
	}
	return MINVIEW_HANDLED;
}

void MinView_focusBottom(void) {
	int n = top ? top->entries->count : 0;
	if (n > 0)
		selectName(n - 1, n);
}

bool MinView_animating(void) {
	if (!MinView_active()) {
		slide_tw.active = false;
		glide.dir = 0;
		open_band.active = false;
		leaving = false;
		if (morph.active)
			morphFree();
		return false;
	}
	bool a = tweenTick(&slide_tw, slide_ms);
	if (a && !slide_tw.active)
		pos_from = pos_to;
	bool transition = false;
	if (morph.active) {
		Uint32 t = SDL_GetTicks() - morph.start, end = MORPH_MS;
		if (!morph.back && RISE_DELAY_MS + RISE_MS + 12 * RISE_STAGGER_MS > end)
			end = RISE_DELAY_MS + RISE_MS + 12 * RISE_STAGGER_MS; // the last names on screen still rising
		if (morph.back && RISE_DELAY_MS + RISE_MS > end)
			end = RISE_DELAY_MS + RISE_MS;
		if (t >= end)
			morphFree();
		transition = true; // one settled frame after
	}
	return a || open_band.active || leaving || transition || glide.dir;
}

void MinView_quit(void) {
	slotsClear();
	free(line_counts);
	line_counts = NULL;
	line_cap = 0;
	seen_top = 0;
	MinOpen_free(&open_band);
	MinFade_free(&bottom_fade);
	morphFree();
	GFX_freeSurfaceAndTexture(black_px);
	black_px = NULL;
}
