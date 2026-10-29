// The Grid style (§8): render and D-pad input over the current Directory `top`. Geometry and moves come from
// grid_layout.c (dp, host-tested), the tile look from tiles.c. Everything else (A, B, X, Y, L1/R1, MENU, SELECT,
// START, the F keys) stays on GameList_handleInput's own paths, so a Grid screen opens, resumes, netplays and
// shows the same context menu as a List one.
//
// Drawing (the Brick's A133 draws the UI in software, where SDL's per-pixel-alpha blit costs ~20 ns a pixel): each
// tile is composed once per look (plain, lit) into its own opaque surface, on the Grid's plain black ground with the
// ring's room around it, and cached under its entry's path, size and scale plus a stamp of what it shows. A frame
// hands the visible rows' tiles to the GPU as sprites over the black body (clipped to it), or under a context menu
// copies them onto the screen, lerping lit over plain for the 120 ms crossfade (UI_blitOpaque). The rows scroll
// vertically, a whole row at a time, the selection's row kept in view (grid_layout.c).

#include "gridview.h"

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_text_sizes.h"
#include "ui_ease.h"
#include "ui_accent.h"
#include "ui_font.h"
#include "ui_menubar.h" // UI_pageTitleFont, UI_statusWidth
#include "ui_message.h"
#include "utils.h"

#include "collcount.h"
#include "content.h"
#include "gameinfo.h"
#include "gameinfo_text.h"
#include "gamelist.h"
#include "grid_layout.h"
#include "home.h"
#include "infoband.h"
#include "homeart.h"
#include "imgloader.h" // screen
#include "launcher.h"
#include "list_window.h"
#include "menulogo.h"
#include "menutabs.h"
#include "rowview.h"		// RowView_beginSprites (the frame drew GPU sprites)
#include "rowview_shared.h" // the tile kinds, and view_common.h
#include "ui_contextmenu.h"
#include "artloader.h"
#include "menuart.h" // MenuArt_has
#include "tiles.h"
#include "types.h"
#include "ui_fade.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLIDE_MS 260
#define LIT_MS 120
#define PREFETCH_TWEEN_SHARE 0.6f // a single move's slide and crossfade this far through let prefetch run
#define GRID_ROOT_SHRINK 0.85f	  // the main menu's tiles (computeLayout)

// the slide: the content offset (dp) eases from `off_from` to `off_to`
static float off_from = 0, off_to = 0;
static Tween slide_tw;
// the lit crossfade: `lit_sel` fades in, `lit_prev` fades out
static int lit_sel = -1, lit_prev = -1;
static Tween lit_tw;
// the slide or the crossfade began while the last one ran (a held D-pad's repeats): prefetch builds for the target
static bool slide_retargeted = false, lit_retargeted = false;
// what the state above belongs to: a change of any snaps (no slide in, no crossfade from a stale index)
static unsigned seen_top = 0; // the Directory serial (0 = none)
static unsigned seen_gen = 0;
static int seen_n = -1;
static int seen_screen_w = 0, seen_scale = 0;

// FIXED_SCALE in hundredths: a key that tells 2.25 from 2.5 (an (int) cast truncates both to 2)
static int scaleKey(void) {
	return (int)lroundf(FIXED_SCALE * 100.0f);
}


///////////////////////////////////////
// Timing

static float currentOffset(void) {
	if (!slide_tw.active)
		return off_to;
	return off_from + (off_to - off_from) * UI_easeStandard(tweenProgress(&slide_tw, SLIDE_MS));
}

static float litAmount(int index) {
	float p = tweenProgress(&lit_tw, LIT_MS);
	if (index == lit_sel)
		return p;
	if (index == lit_prev && lit_tw.active)
		return 1.0f - p;
	return 0.0f;
}

///////////////////////////////////////
// Geometry

///////////////////////////////////////
// The Consoles Grid's header


#define HEADER_GAP_DP 8.0f // from the header's last line to the tiles

static bool grid_sprites; // this frame draws GPU sprites (defined with the tile drawing, below)

// The Consoles tab in Grid (Layouts > Top bar shown): the selected console's name in the top bar's row, where a
// page title sits, its "N games" under it; the tiles show the logo alone (TileSpec.no_count).
static bool consoleHeader(void) {
	return stack && stack->count == 1 && GameList_lookTab() == MENU_TAB_CONSOLES && CFG_getPageTitle();
}

// The header's "N games": the page title's size, as the console's name over it.
static TTF_Font* headerCountFont(void) {
	return UI_pageTitleFont();
}

// The header's lines (px): the name's top (centred on the top bar, as a page title), the count's top under it, and the
// body's top under the count (never above the bar's bottom). Without the header: the bar's bottom.
// A game list's Grid (Layouts > Top bar and Extra info shown): the selected game's play time and achievements move
// off its tile into the top bar's rows, two lines at most (kept whether or not the game fills them, so the tiles don't
// move): with achievements, "Today - 1h 27m · 0 of 92" then "Next: …" in place of the page title (the console's
// name); without, the title then the time.
static bool gameHeader(void) {
	return stack && stack->count > 1 && GameList_lookTab() < 0 && CFG_getPageTitle() && CFG_getExtraInfo();
}

// The selected game's segments for the header (time, "n of m", "Next: …"; 0 without any), *ra when it has
// achievements (the header then takes the title's line too; without play time yet, the title leads its first line).
static int headerSegments(int sel, InfoSeg segs[3], bool* ra) {
	*ra = false;
	if (!gameHeader() || !top || sel < 0 || sel >= top->entries->count)
		return 0;
	Entry* e = top->entries->items[sel];
	GameInfo info;
	if (RowView_kindFor(sel, e) != TILE_GAME || !GameInfo_get(e->path, &info) || !(info.has_time || info.has_ra))
		return 0;
	*ra = info.has_ra && info.total > 0;
	return GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0, info.has_time ? info.seconds : -1,
							 info.has_ra ? info.unlocked : 0, info.has_ra ? info.total : 0,
							 info.has_ra ? info.next : NULL, true, segs);
}

bool GridView_headerHidesTitle(void) {
	if (!gameHeader() || !GridView_active() || !top)
		return false;
	InfoSeg segs[3];
	bool ra;
	int n = top->entries->count;
	return headerSegments(n > 0 ? View_selectedIndex(n) : -1, segs, &ra) > 0 && ra;
}

static int headerLayout(int* name_y, int* count_y) {
	int bar = barPx();
	TTF_Font* f = UI_pageTitleFont();
	int fh = f ? TTF_FontHeight(f) : 0;
	TTF_Font* fc = headerCountFont();
	int ch = fc ? TTF_FontHeight(fc) : 0;
	int ny = (bar - fh) / 2, cy = ny + fh;
	if (name_y)
		*name_y = ny;
	if (count_y)
		*count_y = cy;									 // the line under the title (the console's count, a game's first info line)
	int lines = consoleHeader() || gameHeader() ? 1 : 0; // under the title, each the title font's height (two in all)
	int bottom = cy + (lines > 0 ? (lines - 1) * fh + ch : 0) + NX_DP(HEADER_GAP_DP);
	return lines > 0 && bottom > bar ? bottom : bar;
}

// The selected game's info under the page title (gameHeader): its segments drawn once into a surface (the trophy and
// separators as on the tiles, InfoBand's), made again only when its text changes.
static void drawGameHeader(SDL_Surface* screen, int sel) {
	if (!gameHeader() || sel < 0 || sel >= top->entries->count)
		return;
	static SDL_Surface* lines_surf = NULL;
	static Uint32 lines_key = 0;
	InfoSeg segs[4];
	bool ra;
	int n = headerSegments(sel, segs, &ra);
	if (n <= 0)
		return;								   // the title alone (nextui.c draws it)
	if (ra && segs[0].kind != INFO_SEG_TIME) { // no play time yet: the console's name leads the first line
		char* name = top->name;
		trimSortingMeta(&name);
		memmove(segs + 1, segs, n * sizeof(InfoSeg));
		segs[0].kind = INFO_SEG_TIME; // in the time's tone, where the time would be ("Game Boy Advance · 0 of 59")
		snprintf(segs[0].text, sizeof(segs[0].text), "%s", name ? name : "");
		n++;
	}
	int name_y, line_y;
	headerLayout(&name_y, &line_y);
	TTF_Font* f = UI_pageTitleFont();
	int fh = f ? TTF_FontHeight(f) : 0;
	int x = NX_DP(NX_MENU_GUTTER_DP), max_w = screen->w - 2 * x;
	// with achievements both lines are the info's, from the title's line (GridView_headerHidesTitle); without, the
	// time alone on the line under the title
	int top_y = ra ? name_y : line_y, nlines = ra ? 2 : 1;
	if (ra) // the title's line is clear of the status icons
		max_w = screen->w - x - UI_statusWidth() - x;
	Uint32 key = 2166136261u;
	key = View_fnv(key, &screen->w, sizeof(screen->w));
	key = View_fnv(key, &fh, sizeof(fh));
	key = View_fnv(key, &max_w, sizeof(max_w));
	for (int i = 0; i < n; i++) {
		key = View_fnv(key, &segs[i].kind, sizeof(segs[i].kind));
		key = View_fnvStr(key, segs[i].text);
	}
	if (!lines_surf || key != lines_key || lines_surf->w != screen->w || lines_surf->h != nlines * fh) {
		GFX_freeSurfaceAndTexture(lines_surf);
		lines_surf =
			fh > 0 ? SDL_CreateRGBSurfaceWithFormat(0, screen->w, nlines * fh, 32, SDL_PIXELFORMAT_ARGB8888) : NULL;
		lines_key = key;
		if (!lines_surf)
			return;
		SDL_FillRect(lines_surf, NULL, 0);
		SDL_SetSurfaceBlendMode(lines_surf, SDL_BLENDMODE_BLEND);
		int row0 = 0; // the segments before Next on the first line, Next on the second
		while (row0 < n && segs[row0].kind != INFO_SEG_NEXT)
			row0++;
		if (row0 > 0)
			InfoBand_drawSegments(lines_surf, segs, row0, x, false, 0, max_w, f);
		if (n > row0 && nlines > 1)
			InfoBand_drawSegments(lines_surf, segs + row0, n - row0, x, false, row0 > 0 ? fh : 0, max_w,
								  UI_pageTitleFont());
	}
	SDL_Rect dst = {0, top_y, lines_surf->w, lines_surf->h};
	if (grid_sprites) // a sprite frame uploads only the bars: the lines reach under the top one
		PLAT_spriteAdd(PLAT_textureForSurface(lines_surf), NULL, &dst, 255, NULL);
	else {
		SDL_Rect clip;
		SDL_GetClipRect(screen, &clip);
		SDL_SetClipRect(screen, NULL);
		SDL_BlitSurface(lines_surf, NULL, screen, &dst);
		SDL_SetClipRect(screen, &clip);
	}
}

// The selection's name and its "N games" (grey; nothing while unknown), from the side margin, clear of the status
// icons.
static void drawHeader(SDL_Surface* screen, int sel) {
	if (!consoleHeader() || sel < 0 || sel >= top->entries->count)
		return;
	Entry* e = top->entries->items[sel];
	int name_y, count_y;
	headerLayout(&name_y, &count_y);
	int x = NX_DP(NX_MENU_GUTTER_DP), max_w = screen->w - x - UI_statusWidth() - NX_DP(NX_MENU_GUTTER_DP);
	SDL_Rect clip;
	SDL_GetClipRect(screen, &clip);
	SDL_SetClipRect(screen, NULL);
	TTF_Font* f = UI_pageTitleFont();
	SDL_Surface* name = f ? GFX_getCachedText(f, (char*)View_displayName(e), COLOR_WHITE) : NULL;
	char count[32];
	GameInfo_gamesLabel(Content_consoleGameCount(e), count, sizeof(count));
	TTF_Font* fc = count[0] ? headerCountFont() : NULL;
	SDL_Surface* c = fc ? GFX_getCachedText(fc, count, COLOR_GRAY) : NULL;
	SDL_Surface* lines[2] = {name, c};
	int ys[2] = {name_y, count_y};
	for (int i = 0; i < 2; i++) {
		if (!lines[i])
			continue;
		SDL_Rect src = {0, 0, lines[i]->w < max_w ? lines[i]->w : max_w, lines[i]->h};
		SDL_Rect dst = {x, ys[i], src.w, src.h};
		if (grid_sprites) // a sprite frame uploads only the bars: the count's line reaches under the top one
			PLAT_spriteAdd(PLAT_textureForSurface(lines[i]), &src, &dst, 255, NULL);
		else
			SDL_BlitSurface(lines[i], &src, screen, &dst);
	}
	SDL_SetClipRect(screen, &clip);
}

static void computeLayout(SDL_Surface* screen, int n, GridLayout* g) {
	float pd = pxPerDp();
	float sw = screen->w / pd, sh = screen->h / pd;
	// wider tiles than the spec shape: the Consoles and Collections tabs twice as wide (wide logos 4-8:1, long names),
	// game lists and the Tools tab per device (ui_text_sizes.h GRID_GAMES_W_K, GRID_TOOLS_W_K)
	int tab = GameList_lookTab(); // the root tab, or a hidden tab's list pushed over it (-1: a game list)
	bool root = tab >= 0;
	UIDevice dev = UIScale_deviceIndex(UI_DEVICE_NAME);
	float mul = !root ? TextPx_for(GRID_GAMES_W_K, dev) : (tab == MENU_TAB_TOOLS ? TextPx_for(GRID_TOOLS_W_K, dev) : 2.0f);
	// GRID_GAME_COLS (the 4:3 panels): game lists, Consoles and Collections two across at one tile size, the Tools tab
	// its own width at that same height. The other panels' main menu keeps its shape at GRID_ROOT_SHRINK (its logos,
	// icons and names with it), else the tiles take most of the body
	int panel_cols = (int)TextPx_for(GRID_GAME_COLS, dev);
	int fit_cols = root && tab == MENU_TAB_TOOLS ? 0 : panel_cols;
	float size = root && !panel_cols ? GRID_ROOT_SHRINK : 1.0f;
	float top_dp = headerLayout(NULL, NULL) / pd; // under the Consoles header, else the bar
	// Layouts > Bottom bar hidden: the rows run down to the side margin's distance from the edge (the layout keeps its
	// own NX_BOTTOM_GAP_DP under them: 16 dp in all)
	float bottom_dp = MenuTabs_bottomBar() ? BAR_DP : NX_EDGE_DP - NX_BOTTOM_GAP_DP;
	GridLayout_computeFit(sw, top_dp, sh - top_dp - bottom_dp, n, mul, 1.0f, size, fit_cols, g);
}

///////////////////////////////////////
// Tiles (their kinds: rowview.c's per-list cache, RowView_syncKinds / RowView_kindFor)

// The count-only info (time, "n of m") of a game tile. Requests it when it isn't ready (latest request wins: call
// it for the tile that matters most last). None with Layouts > Extra info hidden: the name alone.
static int gameInfo(Entry* e, InfoSeg segs[3]) {
	GameInfo info;
	if (!CFG_getExtraInfo() || !GameInfo_get(e->path, &info) || !(info.has_time || info.has_ra))
		return 0;
	return GameInfo_segments(time(NULL), info.has_time ? info.last_played : 0, info.has_time ? info.seconds : -1,
							 info.has_ra ? info.unlocked : 0, info.has_ra ? info.total : 0,
							 info.has_ra ? info.next : NULL, false, segs);
}

// "N games" for the main menu's Consoles or Collections tile i ("" while unknown, and on other kinds or lists). A
// collection's count is requested when it isn't known (latest request wins: call it for the selection last).
static void tileCount(int i, char* out, size_t size) {
	out[0] = '\0';
	if (GameList_lookTab() < 0 || i < 0 || i >= top->entries->count)
		return;
	Entry* e = top->entries->items[i];
	TileKind k = RowView_kindFor(i, e);
	if (k == TILE_LOGO)
		GameInfo_gamesLabel(Content_consoleGameCount(e), out, size);
	else if (k == TILE_COLLECTION)
		GameInfo_gamesLabel(CollCount_get(e->path), out, size);
}

// A console or collection tile's count for its plain look (NULL for other kinds). *asked is set when a collection
// queued a count, so the caller can re-ask for the selection last (CollCount's queue keeps the latest request).
static const char* plainCount(int i, char* out, size_t size, bool* asked) {
	if (GameList_lookTab() < 0 || i < 0 || i >= top->entries->count)
		return NULL;
	Entry* e = top->entries->items[i];
	TileKind k = RowView_kindFor(i, e);
	if (k == TILE_LOGO) {
		GameInfo_gamesLabel(Content_consoleGameCount(e), out, size);
		return out;
	}
	if (k != TILE_COLLECTION)
		return NULL;
	int n = CollCount_get(e->path);
	if (n < 0)
		*asked = true;
	GameInfo_gamesLabel(n, out, size);
	return out;
}

///////////////////////////////////////
// The tile cache

#define TILE_CACHE_MAX 112 // a sliding screen's columns (+1 each side) in both rows, the lit looks around the selection,
						   // and (a main-menu tab) every tile's plain and lit look, composed while the grid is still
#define TILE_RING_DP 3.0f  // the room around a tile (the selection ring's, before the colour marked the selection)

typedef struct {
	bool used, lit;
	int w, h, scale; // the tile (px, without the ring room) and scaleKey()
	char path[MAX_PATH];
	Uint32 stamp;	// what the tile shows (tileStamp)
	char count[32]; // the lit look's "N games" when it was composed ("" none or unknown)
	unsigned lru;
	SDL_Surface* surf; // (w + 2 room) × (h + 2 room), opaque
} TileSlot;

static TileSlot tile_cache[TILE_CACHE_MAX];
static unsigned tile_lru = 0;

// Prefetch (GridView_prefetchStep) works for the last frame's grid: its layout and selection, armed by each render and
// disarmed once everything ahead is composed (or the grid stops drawing). It runs between frames, never forcing one.
static struct {
	bool armed;
	GridLayout g;
	int n, sel;
} pf;

// The room around a tile: 3 dp and an anti-aliased edge's px (the tiles keep their spacing).
// GPU sprite mode (the frame's tiles, captions and edge shade drawn by the GPU over a black body, RowView_beginSprites):
// on for every Grid frame but one under a context menu (it draws over the body on the screen, under the sprites)
static bool grid_sprites = false;

static int ringRoom(void) {
	return NX_DPF(TILE_RING_DP) + 1;
}

// Everything Tiles_draw reads for a cached look (the lit caption, with the info, is drawn per frame instead). The lit
// look also shows the accent (the Logo look's outline and content) and the count.
static Uint32 tileStamp(const TileSpec* t, bool lit) {
	Uint32 h = 2166136261u;
	h = View_fnv(h, &t->kind, sizeof(t->kind));
	h = View_fnv(h, &t->scale, sizeof(t->scale));
	h = View_fnvStr(h, t->name);
	h = View_fnvStr(h, t->logo_file);
	h = View_fnvStr(h, t->icon_file);
	h = View_fnv(h, &t->picture, sizeof(t->picture));
	h = View_fnv(h, &t->picture_gen, sizeof(t->picture_gen)); // a re-decoded art may reuse the old pointer
	h = View_fnv(h, &t->no_count, sizeof(t->no_count));
	if (lit) {
		SDL_Color ac = UI_accent();
		Uint8 rgb[3] = {ac.r, ac.g, ac.b};
		h = View_fnv(h, rgb, sizeof(rgb));
	}
	if (lit || t->kind == TILE_COLLECTION || t->kind == TILE_LOGO) // consoles and collections: on the plain look too
		h = View_fnvStr(h, t->count && t->count[0] ? t->count : "");
	return h;
}

static void composeTile(SDL_Surface* s, int w, int h, const TileSpec* t, bool lit) {
	int room = ringRoom();
	SDL_SetClipRect(s, NULL);
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 255)); // the Grid's black ground: the whole surface opaque
	TileSpec look = *t;
	look.no_caption = true;
	look.info = NULL;
	look.ninfo = 0;
	if (!lit && t->kind != TILE_COLLECTION && t->kind != TILE_LOGO)
		look.count = NULL; // a console's and a collection's count is on both looks
	Tiles_draw(s, (SDL_Rect){room, room, w, h}, &look, lit ? 1.0f : 0.0f);
}

// The tile's cached look (path + lit + size + scale), recomposed in place when what it shows changed. NULL when out
// of memory.
static SDL_Surface* cachedTile(const char* path, int w, int h, const TileSpec* t, bool lit) {
	Uint32 stamp = tileStamp(t, lit);
	int room = ringRoom();
	TileSlot* victim = NULL;
	for (int i = 0; i < TILE_CACHE_MAX; i++) {
		TileSlot* c = &tile_cache[i];
		if (!c->used) {
			if (!victim || victim->used)
				victim = c;
			continue;
		}
		if (c->lit == lit && c->w == w && c->h == h && c->scale == scaleKey() && strcmp(c->path, path) == 0) {
			if (c->stamp != stamp) {
				c->stamp = stamp;
				snprintf(c->count, sizeof(c->count), "%s", lit && t->count ? t->count : "");
				composeTile(c->surf, w, h, t, lit);
				PLAT_freeSurfaceTexture(c->surf); // recomposed in place: its texture is stale
			}
			c->lru = ++tile_lru;
			return c->surf;
		}
		if (!victim || (victim->used && c->lru < victim->lru))
			victim = c;
	}
	if (!victim)
		return NULL;
	int sw = w + 2 * room, sh = h + 2 * room;
	if (victim->surf && (victim->surf->w != sw || victim->surf->h != sh)) {
		GFX_freeSurfaceAndTexture(victim->surf);
		victim->surf = NULL;
	}
	PLAT_freeSurfaceTexture(victim->surf); // a reused surface gets a new look: its texture is stale
	if (!victim->surf)
		victim->surf = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!victim->surf) {
		victim->used = false;
		return NULL;
	}
	SDL_Surface* surf = victim->surf;
	victim->used = true;
	victim->lit = lit;
	victim->w = w;
	victim->h = h;
	victim->scale = scaleKey();
	snprintf(victim->path, sizeof(victim->path), "%s", path);
	victim->stamp = stamp;
	snprintf(victim->count, sizeof(victim->count), "%s", lit && t->count ? t->count : "");
	victim->lru = ++tile_lru;
	composeTile(surf, w, h, t, lit);
	return surf;
}

// The count a cached lit look of path at w×h already shows, or NULL (none cached, or composed without a known count).
static const char* litCount(const char* path, int w, int h) {
	for (int i = 0; i < TILE_CACHE_MAX; i++) {
		const TileSlot* c = &tile_cache[i];
		if (c->used && c->lit && c->w == w && c->h == h && c->scale == scaleKey() && c->count[0] &&
			strcmp(c->path, path) == 0)
			return c->count;
	}
	return NULL;
}

static void tileCacheClear(void) {
	for (int i = 0; i < TILE_CACHE_MAX; i++) {
		if (tile_cache[i].surf)
			GFX_freeSurfaceAndTexture(tile_cache[i].surf);
	}
	memset(tile_cache, 0, sizeof(tile_cache));
	tile_lru = 0;
}

// What the cache holds while the Grid isn't drawn (up to TILE_CACHE_MAX looks of ~0.45 MB, and a texture each): set by
// each render, released by tileCacheRelease.
static enum { CACHE_EMPTY,
			  CACHE_SURFACES,
			  CACHE_TEXTURES } cache_held = CACHE_EMPTY;

// The Grid is no longer drawn: its tiles' textures go (re-uploaded from the looks on the next Grid frame); with the
// style itself no longer the Grid (not just Home over it, which comes back to the same tiles), the looks go too.
static void tileCacheRelease(void) {
	if (cache_held == CACHE_EMPTY)
		return;
	if (GameList_currentStyle() != MENU_STYLE_GRID) {
		tileCacheClear();
		cache_held = CACHE_EMPTY;
		return;
	}
	if (cache_held == CACHE_TEXTURES) {
		for (int i = 0; i < TILE_CACHE_MAX; i++)
			PLAT_freeSurfaceTexture(tile_cache[i].surf);
		cache_held = CACHE_SURFACES;
	}
}

static void fillBlack(SDL_Surface* screen, int x, int y, int w, int h) {
	if (w > 0 && h > 0)
		SDL_FillRect(screen, &(SDL_Rect){x, y, w, h}, SDL_MapRGBA(screen->format, 0, 0, 0, 255));
}

// A game tile with a picture, or one still loading it, as GPU sprites, no composed look (a game list is too long to
// compose ahead): the picture square, grey (its drained copy) with the colour one fading in over it as it lights, the
// black corner mask rounding it on the black body and the edge over that (loading: just the edge), then the caption.
// No ring: the colour marks the selection. The overlays are made once per tile size.
static void drawGameSprites(SDL_Rect r, const TileSpec* t, float lit, const SDL_Rect* clip) {
	static SDL_Surface *mask, *border;
	if (!mask || mask->w != r.w || mask->h != r.h) {
		GFX_freeSurfaceAndTexture(mask);
		mask = Tiles_cornerMask(r.w, r.h);
		GFX_freeSurfaceAndTexture(border);
		border = Tiles_borderOverlay(r.w, r.h);
	}
	if (lit > 1.0f)
		lit = 1.0f;
	Uint8 full = 255, lit_a = (Uint8)(255.0f * lit + 0.5f);
	if (t->picture) { // the square picture, rounded by the mask
		SDL_Surface* grey = lit < 1.0f ? HomeArt_drained(t->picture, t->picture_gen, HOMEART_DRAIN_LEVEL) : NULL;
		if (grey)
			PLAT_spriteAdd(PLAT_textureForSurface(grey), NULL, &r, full, clip);
		if (lit > 0.0f || !grey)
			PLAT_spriteAdd(PLAT_textureForSurface(t->picture), NULL, &r, grey ? lit_a : full, clip);
		if (mask)
			PLAT_spriteAdd(PLAT_textureForSurface(mask), NULL, &r, full, clip);
	}
	if (border) // the edge, on both looks (still loading: on the body's own black)
		PLAT_spriteAdd(PLAT_textureForSurface(border), NULL, &r, Tiles_borderAlpha(), clip);
	SDL_Surface* cap = lit > 0.0f ? Tiles_captionSurface(r, t, lit) : NULL;
	if (cap)
		PLAT_spriteAdd(PLAT_textureForSurface(cap), NULL, &r, lit_a, clip);
}

// One tile at its px rect r: a copy of the cached look, or for a crossfade lit over plain in one pass, then the lit
// caption over it. Returns whether it painted its whole rect with the ring room (opaque); off screen it paints
// nothing.
static bool drawTile(SDL_Surface* screen, const char* path, SDL_Rect r, const TileSpec* t, float lit) {
	int room = ringRoom();
	int x = r.x - room, y = r.y - room, sw = r.w + 2 * room, sh = r.h + 2 * room;
	const SDL_Rect* clip = &screen->clip_rect;
	if (x >= clip->x + clip->w || x + sw <= clip->x || y >= clip->y + clip->h || y + sh <= clip->y)
		return false;
	// a game's picture, or a game tile still loading one (an empty title tile): sprites, no composed look
	if (grid_sprites && ((t->kind == TILE_GAME && t->picture) || (t->kind == TILE_TITLE && !t->name))) {
		drawGameSprites(r, t, lit, clip);
		return true;
	}
	SDL_Surface* plain = lit < 1.0f ? cachedTile(path, r.w, r.h, t, false) : NULL;
	SDL_Surface* lit_s = lit > 0.0f ? cachedTile(path, r.w, r.h, t, true) : NULL;
	if (grid_sprites) { // the GPU: plain, the lit look over it at the crossfade, the caption over that
		Uint8 full = 255, lit_a = (Uint8)(255.0f * (lit > 1.0f ? 1.0f : lit) + 0.5f);
		SDL_Rect where = {x, y, sw, sh};
		if (!plain && !lit_s)
			return false; // out of memory: no look to hand the GPU (the body isn't uploaded, so no slow path either)
		if (plain)
			PLAT_spriteAdd(PLAT_textureForSurface(plain), NULL, &where, full, clip);
		if (lit_s)
			PLAT_spriteAdd(PLAT_textureForSurface(lit_s), NULL, &where, plain ? lit_a : full, clip);
		SDL_Surface* cap = lit > 0.0f ? Tiles_captionSurface(r, t, lit) : NULL;
		if (cap)
			PLAT_spriteAdd(PLAT_textureForSurface(cap), NULL, &r, lit_a, clip);
		return true;
	}
	if ((lit < 1.0f && !plain) || (lit > 0.0f && !lit_s)) {
		// no memory for the cache: the slow path, on its own black
		fillBlack(screen, x, y, sw, sh);
		Tiles_draw(screen, r, t, lit);
		return true;
	}
	if (!lit_s)
		UI_blitOpaque(plain, NULL, 0, 0, sw, sh, screen, x, y, 255);
	else if (!plain)
		UI_blitOpaque(lit_s, NULL, 0, 0, sw, sh, screen, x, y, 255);
	else
		UI_blitOpaque(lit_s, plain, 0, 0, sw, sh, screen, x, y, (int)(lit * 255.0f + 0.5f));
	if (lit > 0.0f)
		Tiles_drawCaption(screen, r, t, lit);
	return true;
}

// What tile i shows (its art requested at tw×th). logo: the caller's buffer for the logo file (MAX_PATH).
static void tileSpec(int i, int tw, int th, float scale, TileSpec* t, char logo[MAX_PATH]) {
	Entry* e = top->entries->items[i];
	TileKind kind = RowView_kindFor(i, e);
	*t = (TileSpec){.kind = kind, .name = View_displayName(e), .scale = scale};
	if (kind == TILE_LOGO) {
		t->logo_file = MenuLogo_fileFor(e->path, logo, MAX_PATH);
		t->no_count = consoleHeader(); // the selection's count is in the header
	} else if (kind == TILE_TOOL) {
		t->icon_file = Tiles_toolIcon(t->name);
		if (!t->icon_file) {
			const char* slash = strrchr(e->path, '/');
			t->icon_file = Tiles_toolIcon(slash ? slash + 1 : e->path);
		}
		if (!t->icon_file)
			t->icon_file = TILE_UNKNOWN_TOOL_ICON;
	} else if (kind == TILE_GAME) {
		SDL_Surface* pic = NULL;
		HomeArtState st = HomeArt_pin(e->path, tw, th, 0, &pic); // valid until the next HomeArt_* call
		unsigned gen = HomeArt_lastGen();						 // read at once: changes after HomeArt_forget (Fetch artwork)
		if (st == HOMEART_READY && pic) {
			t->picture = pic; // its screenshot, or its abstract picture (HomeArt_pin's fallback)
			t->picture_gen = gen;
		} else {
			t->kind = TILE_TITLE; // none at all (HomeArt couldn't make one): a title tile
		}
		if (st == HOMEART_LOADING)
			t->name = NULL; // still loading: the black base only, no title flashing in and out
	}
}

///////////////////////////////////////

// The console logos of the tiles within LOGO_WARM_RADIUS of the selection, decoded on the art loader's thread
// (nearest first) at the box their tiles fit them in, once per selection, list or size: a tile composed as it slides in
// then finds its logo decoded instead of decoding it in that frame.
#define LOGO_WARM_RADIUS 12
static void warmLogos(int n, int sel, int tw, int th, float scale) {
	static unsigned seen_serial, seen_gen;
	static int seen_sel = -1, seen_n, seen_tw, seen_th;
	if (top->serial == seen_serial && MenuTabs_generation() == seen_gen && sel == seen_sel && n == seen_n &&
		tw == seen_tw && th == seen_th)
		return;
	seen_serial = top->serial, seen_gen = MenuTabs_generation(), seen_sel = sel, seen_n = n, seen_tw = tw;
	seen_th = th;
	int bw, bh;
	if (consoleHeader()) // as drawLogo decodes them: the whole box without a count line, else less it
		Tiles_logoBox(tw, th, scale, &bw, &bh);
	else
		Tiles_gridLogoBox(tw, th, scale, &bw, &bh);
	if (bw <= 0 || bh <= 0)
		return;
	for (int d = 0; d <= LOGO_WARM_RADIUS; d++) {
		for (int side = -1; side <= 1; side += 2) {
			int i = sel + side * d;
			if (i < 0 || i >= n || (d == 0 && side > 0))
				continue;
			Entry* e = top->entries->items[i];
			if (RowView_kindFor(i, e) != TILE_LOGO)
				continue;
			char logo[MAX_PATH];
			if (!MenuLogo_fileFor(e->path, logo, sizeof(logo)))
				continue;
			if (!MenuArt_has(logo, bw, bh)) // cached already: drawLogo's MenuArt_get would never take the loader's copy
				ArtLoader_request(logo, bw, bh, d);
		}
	}
}

// A scrolling grid's edges: the rows peeking into the body's margins above and below the shown two (they're centred)
// fade out toward the bars, black at the body's edge (nx-mobile's Grid). Only on a side with more rows that way.
static void edgeFades(SDL_Surface* screen, const GridLayout* g, float off, int bar, int body_h) {
	if (!g->sliding)
		return;
	int top_h = NX_DPF(g->rows_top) - bar;
	int bottom_y = NX_DPF(g->rows_top + g->rows * g->tile_h + (g->rows - 1) * g->gap);
	int bottom_h = bar + body_h - bottom_y;
	float hi = (g->nrows - g->rows) * (g->tile_h + g->gap);
	struct {
		bool on;
		int y, h;
		bool dark_top;
	} edges[2] = {{off > 0.5f, bar, top_h, true}, {off < hi - 0.5f, bottom_y, bottom_h, false}};
	for (int i = 0; i < 2; i++) {
		if (!edges[i].on || edges[i].h <= 0)
			continue;
		SDL_Surface* f = UI_easedFadeSurface(screen->w, edges[i].h, 1.0f, 2.0f, edges[i].dark_top);
		if (!f)
			continue;
		if (grid_sprites)
			PLAT_spriteAdd(PLAT_textureForSurface(f), NULL, &(SDL_Rect){0, edges[i].y, screen->w, edges[i].h}, 255,
						   &screen->clip_rect);
		else
			UI_blitFade(f, NULL, screen, 0, edges[i].y);
	}
}

bool GridView_active(void) {
	return GameList_currentStyle() == MENU_STYLE_GRID && !Home_active();
}

// A list, tab, screen size or scale change: start over with the grid snapped to the selection.
static bool syncList(SDL_Surface* screen, int n, int lastScreen) {
	// the kinds are shared with the Carousel and Backdrop, which may have keyed them to another list since: re-key them
	// on every frame (a no-op while they're this list's), not only when the Grid's own key changes
	RowView_syncKinds(n);
	bool changed = top->serial != seen_top || MenuTabs_generation() != seen_gen || n != seen_n ||
				   screen->w != seen_screen_w || scaleKey() != seen_scale || lastScreen != SCREEN_GAMELIST;
	if (!changed)
		return false;
	seen_top = top->serial;
	seen_gen = MenuTabs_generation();
	seen_n = n;
	seen_screen_w = screen->w;
	seen_scale = scaleKey();
	return true;
}

void GridView_render(SDL_Surface* screen, int lastScreen) {
	pf.armed = false;
	if (!screen || !top)
		return;
	int bar = headerLayout(NULL, NULL); // the body's top: the bar's bottom, or under the Consoles header
	int body_h = screen->h - bar - (MenuTabs_bottomBar() ? barPx() : 0);
	// plain black under the tiles (and under the hint bar's scrim): no backdrop on a Grid screen. The tiles are opaque
	// (ring room included), so only what's between them is filled, below.
	int n = top->entries->count;
	bool snap = syncList(screen, n, lastScreen);
	if (n <= 0 || body_h <= 0) {
		fillBlack(screen, 0, bar, screen->w, screen->h - bar);
		lit_sel = lit_prev = -1;
		lit_tw.active = slide_tw.active = false;
		off_from = off_to = 0;
		UI_renderCenteredMessage(screen, GameList_emptyText());
		return;
	}

	GridLayout g;
	computeLayout(screen, n, &g);
	int sel = View_selectedIndex(n);
	int sel_col, sel_row;
	GridLayout_cell(&g, sel, &sel_col, &sel_row);
	(void)sel_col;

	// the scroll toward the least that shows the selected row (whole rows)
	float target = GridLayout_offsetFor(&g, sel_row, off_to);
	if (snap || !animationsOn()) {
		off_from = off_to = target;
		slide_tw.active = false;
	} else if (target != off_to) {
		slide_retargeted = slide_tw.active && tweenProgress(&slide_tw, SLIDE_MS) < 1.0f;
		off_from = currentOffset();
		off_to = target;
		tweenStart(&slide_tw);
	}
	// the lit crossfade from the previous selection
	if (snap || lit_sel < 0) {
		lit_sel = sel;
		lit_prev = -1;
		lit_tw.active = false;
	} else if (sel != lit_sel) {
		lit_retargeted = lit_tw.active && tweenProgress(&lit_tw, LIT_MS) < 1.0f;
		lit_prev = lit_sel;
		lit_sel = sel;
		tweenStart(&lit_tw);
	}
	float off = currentOffset();

	int tw = NX_DPF(g.tile_w), th = NX_DPF(g.tile_h);
	float scale = GridLayout_tileK(&g);
	warmLogos(n, sel, tw, th, scale);

	// the lit tiles' info first, the selection's last: GameInfo's queue keeps the latest request
	InfoSeg prev_segs[3], sel_segs[3];
	int prev_n = 0, sel_n = 0;
	if (lit_tw.active && lit_prev >= 0 && lit_prev < n) {
		Entry* e = top->entries->items[lit_prev];
		if (RowView_kindFor(lit_prev, e) == TILE_GAME)
			prev_n = gameHeader() ? 0 : gameInfo(e, prev_segs);
	}
	{
		Entry* e = top->entries->items[sel];
		if (RowView_kindFor(sel, e) == TILE_GAME)
			sel_n = gameInfo(e, sel_segs);
		if (gameHeader())
			sel_n = 0; // asked all the same (the header's lookup reads it): its info is in the header, not on the tile
	}
	// the main menu's "N games" on the lit tiles, the selection's last (CollCount's queue keeps the latest request)
	char prev_count[32] = "", sel_count[32];
	// whenever there is a previous selection, not only while it fades: it keeps that tile's plain look keyed on its
	// count (an empty one here re-composed the look, and again once the count came back)
	if (lit_prev >= 0 && lit_prev < n)
		tileCount(lit_prev, prev_count, sizeof(prev_count));
	tileCount(sel, sel_count, sizeof(sel_count));

	grid_sprites = !ContextMenu_isOpen();
	cache_held = CACHE_TEXTURES;		// the looks drawn below (and prefetched after) may get textures
	RowView_beginSprites(grid_sprites); // marks the frame as a sprite frame (the host uploads only the bars)
	if (!grid_sprites)					// the software way (under a context menu): the body black, the tiles over it
		fillBlack(screen, 0, bar, screen->w, screen->h - bar);
	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &(SDL_Rect){0, bar, screen->w, body_h});

	int first, last;
	GridLayout_visibleRows(&g, off, &first, &last);
	bool asked = false; // a plain collection tile queued its count: the selection asks again, last
	for (int row = first; row <= last; row++) {
		int y = NX_DPF(GridLayout_rowY(&g, row, off));
		for (int col = 0; col < g.cols; col++) {
			int i = GridLayout_index(&g, col, row);
			if (i < 0)
				continue;
			char logo[MAX_PATH];
			TileSpec t;
			tileSpec(i, tw, th, scale, &t, logo);
			if (i == sel) {
				t.info = sel_segs;
				t.ninfo = sel_n;
				t.count = sel_count;
			} else if (i == lit_prev) {
				t.info = prev_segs;
				t.ninfo = prev_n;
				t.count = prev_count;
			}
			char plain[32];
			if (i != sel && i != lit_prev)
				t.count = plainCount(i, plain, sizeof(plain), &asked);
			drawTile(screen, ((Entry*)top->entries->items[i])->path,
					 (SDL_Rect){NX_DPF(GridLayout_columnX(&g, col)), y, tw, th}, &t, litAmount(i));
		}
	}
	if (asked)
		tileCount(sel, sel_count, sizeof(sel_count)); // the selection's count stays the latest request
	edgeFades(screen, &g, off, bar, body_h);
	drawHeader(screen, sel);
	drawGameHeader(screen, sel);
	RowView_beginSprites(false);
	grid_sprites = false;

	SDL_SetClipRect(screen, &prev_clip);

	// what the next move needs is composed between frames (GridView_prefetchStep), for this layout and selection
	pf.armed = true;
	pf.g = g;
	pf.n = n;
	pf.sel = sel;
}

static bool pastDeadline(Uint32 deadline) {
	return (Sint32)(SDL_GetTicks() - deadline) >= 0; // wrap-safe
}

// Compose ahead what the next move needs, so it starts on cached tiles: the rows just off the body above and below at
// the scroll's target (plain) and the selection's neighbours (lit; the caption isn't part of the cached look). Until
// done or the deadline; true when it stopped with more.
#define MAIN_MENU_PRECOMPOSE 48 // a main-menu tab this long or shorter has all its plain tiles composed ahead

static bool prefetchTiles(const GridLayout* g, int n, int sel, Uint32 deadline) {
	int tw = NX_DPF(g->tile_w), th = NX_DPF(g->tile_h);
	float scale = GridLayout_tileK(g);
	bool more = false, asked = false, counted = false;
	int first, last;
	GridLayout_visibleRows(g, off_to, &first, &last);
	int body_top = NX_DPF(g->body_top), body_bottom = NX_DPF(g->body_top + g->body_h);
	for (int row = first; row <= last && !more; row++) {
		int y = NX_DPF(GridLayout_rowY(g, row, off_to));
		if (y + th > body_top && y < body_bottom)
			continue; // in the body at the target: drawn (cached) as the scroll brings it in
		for (int col = 0; col < g->cols; col++) {
			int i = GridLayout_index(g, col, row);
			if (i < 0)
				continue;
			if (pastDeadline(deadline)) {
				more = true;
				break;
			}
			char logo[MAX_PATH], plain[32];
			TileSpec t;
			tileSpec(i, tw, th, scale, &t, logo);
			t.count = plainCount(i, plain, sizeof(plain), &asked);
			if ((t.kind == TILE_GAME && t.picture) || (t.kind == TILE_TITLE && !t.name)) {
				// drawn from its picture, or loading one (drawGameSprites): no look to compose
				if (t.picture)
					PLAT_textureForSurface(t.picture);
				continue;
			}
			SDL_Surface* s = cachedTile(((Entry*)top->entries->items[i])->path, tw, th, &t, false);
			if (s) // its texture too, between frames (the Grid draws GPU sprites)
				PLAT_textureForSurface(s);
		}
	}
	int around[4] = {sel - 1, sel + 1, sel - g->cols, sel + g->cols};
	for (int k = 0; k < 4 && !more; k++) {
		int i = around[k];
		if (i < 0 || i >= n)
			continue;
		if (pastDeadline(deadline)) {
			more = true;
			break;
		}
		char logo[MAX_PATH], count[32];
		TileSpec t;
		tileSpec(i, tw, th, scale, &t, logo);
		// the lit look carries its count: the move then starts cached. A neighbour whose lit look already shows a
		// known count reuses it (no CollCount stat each pass); the selection always asks afresh. The ±1 neighbours (the
		// next move's selection, which the render asks afresh too) ask each pass: a count that changed since their look
		// was composed is recomposed here, not in the move's first frame (a still-unknown one keeps the known count).
		const char* path = ((Entry*)top->entries->items[i])->path;
		const char* known = litCount(path, tw, th);
		if (known && k < 2) {
			tileCount(i, count, sizeof(count));
			counted = true;
			if (!count[0])
				snprintf(count, sizeof(count), "%s", known);
		} else if (known) {
			snprintf(count, sizeof(count), "%s", known);
		} else {
			tileCount(i, count, sizeof(count));
			counted = true;
		}
		t.count = count;
		if ((t.kind == TILE_GAME && t.picture) || (t.kind == TILE_TITLE && !t.name)) {
			if (t.picture)
				PLAT_textureForSurface(t.picture);
			continue;
		}
		SDL_Surface* s = cachedTile(path, tw, th, &t, true);
		if (s)
			PLAT_textureForSurface(s);
	}
	// A main-menu tab (a short list): every tile's plain look, then its lit one (with its count), nearest first, while
	// the grid is still, so a move only ever reuses composed tiles (a compose costs about a frame). A game list is too
	// long for that: its neighbours only.
	if (!more && stack->count == 1 && n <= MAIN_MENU_PRECOMPOSE && !slide_tw.active && !lit_tw.active) {
		for (int lit = 0; lit <= 1 && !more; lit++) {
			for (int d = 0; d < n && !more; d++) {
				for (int side = -1; side <= 1 && !more; side += 2) {
					int i = sel + side * d;
					if (i < 0 || i >= n || (d == 0 && side > 0)) // the selection too: its plain look is what it fades to
						continue;
					if (pastDeadline(deadline)) {
						more = true;
						break;
					}
					char logo[MAX_PATH], count[32];
					TileSpec t;
					tileSpec(i, tw, th, scale, &t, logo);
					const char* path = ((Entry*)top->entries->items[i])->path;
					if (lit) {
						const char* known = litCount(path, tw, th);
						if (known)
							snprintf(count, sizeof(count), "%s", known);
						else {
							tileCount(i, count, sizeof(count));
							counted = true;
						}
						t.count = count;
					} else {
						t.count = plainCount(i, count, sizeof(count), &asked);
					}
					SDL_Surface* s = cachedTile(path, tw, th, &t, lit);
					if (s)
						PLAT_textureForSurface(s);
				}
			}
		}
	}
	if (counted || asked) {
		char sel_count[32];
		tileCount(sel, sel_count, sizeof(sel_count)); // the selection's count stays the latest request
	}
	return more;
}

bool GridView_prefetchStep(Uint32 deadline) {
	if (!pf.armed)
		return false;
	// the grid the layout was worked out for is gone (a new list, tab, size or scale not drawn yet, or a moved
	// selection): nothing until the next render re-arms it
	if (!GridView_active() || !top || !screen || top->serial != seen_top || MenuTabs_generation() != seen_gen ||
		top->entries->count != pf.n || screen->w != seen_screen_w || scaleKey() != seen_scale ||
		View_selectedIndex(pf.n) != pf.sel) {
		pf.armed = false;
		return false;
	}
	// not through the first part of a single move's slide or crossfade (a compose would stall their frames); past
	// PREFETCH_TWEEN_SHARE, or on a held D-pad (its repeats retarget both before they get that far), the target's
	// tiles are composed so the next moves land on cached ones
	if ((slide_tw.active && !slide_retargeted && tweenProgress(&slide_tw, SLIDE_MS) < PREFETCH_TWEEN_SHARE) ||
		(lit_tw.active && !lit_retargeted && tweenProgress(&lit_tw, LIT_MS) < PREFETCH_TWEEN_SHARE))
		return true;
	if (prefetchTiles(&pf.g, pf.n, pf.sel, deadline))
		return true;
	pf.armed = false;
	return false;
}

// Select `index` and keep the List window consistent for a later switch back to List: the selection at the top, clamped.
static void selectTile(int index, int n) {
	top->selected = index;
	ListWindow_selectAtTop(n, GameList_rowCount(), index, &top->start, &top->end);
}

bool GridView_handleInput(unsigned long now, bool* dirty, bool* switched_tab) {
	(void)now;
	if (switched_tab)
		*switched_tab = false;
	int btn;
	GridDir dir;
	if (PAD_justRepeated(BTN_UP)) {
		btn = BTN_UP;
		dir = GRID_DIR_UP;
	} else if (PAD_justRepeated(BTN_DOWN)) {
		btn = BTN_DOWN;
		dir = GRID_DIR_DOWN;
	} else if (PAD_justRepeated(BTN_LEFT)) {
		btn = BTN_LEFT;
		dir = GRID_DIR_LEFT;
	} else if (PAD_justRepeated(BTN_RIGHT)) {
		btn = BTN_RIGHT;
		dir = GRID_DIR_RIGHT;
	} else {
		return false;
	}

	int n = top->entries->count;
	GridLayout g;
	computeLayout(screen, n, &g);
	int index = n > 0 ? View_selectedIndex(n) : 0;
	GridMove m = GridLayout_move(&g, &index, dir);
	bool edge = m == GRID_MOVE_EDGE_PREV || m == GRID_MOVE_EDGE_NEXT;
	if (m == GRID_MOVE_MOVED) {
		selectTile(index, n);
		*dirty = true;
	} else if (edge && stack->count > 1 && GameList_lookTab() < 0) {
		// a game list: past a row's end, on to the previous or next game (the row above or below); a held key too,
		// stopping at the list's ends
		int t = index + (m == GRID_MOVE_EDGE_PREV ? -1 : 1);
		if (t >= 0 && t < n) {
			selectTile(t, n);
			*dirty = true;
		}
	} else if (edge && PAD_justPressed(btn) && (stack->count > 1 || MenuTabs_count() <= 1)) {
		// no tab to switch to (a hidden tab's list pushed over a tab, or the only tab): a fresh press wraps to the
		// row's other end; a held key stops
		int t = GridLayout_wrap(&g, index, m);
		if (t != index) {
			selectTile(t, n);
			*dirty = true;
		}
	} else if (edge && PAD_justPressed(btn)) {
		// the main menu: past a row's end, a fresh press switches tab; a held key stops
		unsigned gen = MenuTabs_generation();
		GameList_switchTab(m == GRID_MOVE_EDGE_PREV ? -1 : 1, dirty);
		if (switched_tab)
			*switched_tab = MenuTabs_generation() != gen;
	}
	return true;
}

bool GridView_animating(void) {
	if (!GridView_active()) {
		pf.armed = false;
		slide_tw.active = lit_tw.active = false;
		off_from = off_to;
		tileCacheRelease();
		return false;
	}
	bool a = tweenTick(&slide_tw, SLIDE_MS);
	bool b = tweenTick(&lit_tw, LIT_MS);
	if (a && !slide_tw.active)
		off_from = off_to;
	return a || b;
}

void GridView_quit(void) {
	pf.armed = false;
	cache_held = CACHE_EMPTY;
	tileCacheClear(); // the tile kinds are rowview.c's: RowView_quit frees them
}
