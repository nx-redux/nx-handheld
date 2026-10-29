// Main menu tabs: the current tab is the Directory at stack[0] (see
// docs/superpowers/specs/2026-09-30-main-menu-tabs-design.md). The rules
// live in menutabs_model.c; this file applies them to the stack and draws
// the tab row.

#include "menutabs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "utils.h"

#include "content.h"
#include "gamelist.h"
#include "home.h"
#include "launcher.h"
#include "list_window.h"
#include "menulogo.h"
#include "recents.h"
#include "shortcuts.h"
#include "ui_draw.h"
#include "ui_fade.h"
#include "ui_list.h"
#include "ui_menubar.h"

static MenuTabId tabs[MENU_TAB_COUNT];
static int tab_count = 0;
static MenuTabId current = MENU_TAB_HOME;
static bool simple_mode = false;

// Parked roots (T1-1, menutabs_model.h): L1/R1 parks the outgoing tab's stack[0] here and takes the incoming
// one's, so a tab switch costs no card I/O. Only tabs that are NOT current have a root here (the stack owns
// stack[0]), so every existing free of stack[0] stays correct. A slot's hint keeps its selection after the root
// is dropped. Dropped by MenuTabs_reload (the tabs in its plan's stale_tabs, and the tabs it hid), MenuTabs_setCurrent
// and MenuTabs_dropCached (one).
static MenuTabSlot slots[MENU_TAB_COUNT];
// Content_libraryGen() when stack[0] was built, and when the parked Consoles root was: a Consoles root built
// against an older emulist (a refresh, a delete, a Search rescan) is rebuilt instead of reused.
static unsigned root_gen = 0;
static unsigned consoles_gen = 0;

static void dropSlot(MenuTabId id) {
	Directory* root = MenuTabs_slotDrop(slots, id);
	if (root)
		Directory_free(root);
}

static void dropAllSlots(void) {
	for (int id = 0; id < MENU_TAB_COUNT; id++)
		dropSlot((MenuTabId)id);
}

static bool settingsPakExists(void) {
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/Settings.pak", TOOLS_PATH);
	if (exists(path))
		return true;
	snprintf(path, sizeof(path), "%s/Tools/Settings.pak", PAKS_PATH);
	return exists(path);
}

// What the visible tabs are computed from, kept between reloads (T2-11): a reload re-reads only the inputs its
// mask says may have changed (MenuTabs_reloadPlan), and recomputes the tabs from the rest as last read.
static MenuTabInputs last_in;

// Re-read what `plan` asks for, then recompute the visible tabs. The CFG flags, simple mode, Tools and the
// Settings pak change only outside nextui (Settings, a card edit), so only the full pass (boot, MENU_RELOAD_ALL)
// reads them.
static void refreshInputs(const MenuReloadPlan* plan) {
	if (plan->check_all)
		simple_mode = exists(SIMPLE_MODE_PATH);
	if (plan->validate_pins && Shortcuts_getCount() > 0)
		Shortcuts_validate();
	// No tab lists recents any more, but this is what fills the Game
	// Switcher's list (and applies a pending disc change).
	if (plan->load_recents)
		Recents_load();
	if (plan->check_all) {
		last_in.hide_home = !CFG_getShowHome();
		last_in.show_consoles = CFG_getShowEmulators();
		last_in.show_collections = CFG_getShowCollections();
		last_in.show_tools = CFG_getShowTools();
		last_in.has_tools = hasTools() != 0;
		last_in.simple_mode = simple_mode;
		last_in.has_settings = settingsPakExists();
	}
	if (plan->check_consoles)
		last_in.has_consoles = Content_hasConsoles() != 0;
	if (plan->check_collections)
		last_in.has_collections = hasCollections() != 0;
	tab_count = MenuTabs_visible(&last_in, tabs);
}

void MenuTabs_init(void) {
	MenuReloadPlan full = MenuTabs_reloadPlan(MENU_RELOAD_ALL);
	refreshInputs(&full);
}

int MenuTabs_count(void) {
	return tab_count;
}

MenuTabId MenuTabs_at(int index) {
	if (index < 0 || index >= tab_count)
		return MENU_TAB_HOME;
	return tabs[index];
}

MenuTabId MenuTabs_current(void) {
	return current;
}

static unsigned generation = 0;

// A new generation: per-view state resets, and the consoles' own logos (.media/logo.png) are looked for afresh, so
// one copied in shows on the next rescan or tab switch
static void nextGeneration(void) {
	generation++;
	MenuLogo_forget();
}

void MenuTabs_setCurrent(MenuTabId id) {
	// pathToStack just built a fresh stack[0] for id: a parked copy would be a second root for one tab
	dropSlot(id);
	if (id != current)
		nextGeneration(); // another tab now (a deep pinned folder opened from Home lands in Consoles): as a tab switch
	current = id;
	root_gen = Content_libraryGen();
}

void MenuTabs_dropCached(MenuTabId id) {
	if (id != current) // the current tab's root is the stack's: its caller reloads it if it must
		dropSlot(id);
}

void MenuTabs_quit(void) {
	dropAllSlots();
}

bool MenuTabs_isVisible(MenuTabId id) {
	return MenuTabs_indexOf(tabs, tab_count, id) >= 0;
}

bool MenuTabs_pushable(MenuTabId id) {
	if (simple_mode || MenuTabs_isVisible(id))
		return false;
	switch (id) {
	case MENU_TAB_CONSOLES:
		return last_in.has_consoles;
	case MENU_TAB_COLLECTIONS:
		return last_in.has_collections;
	case MENU_TAB_TOOLS:
		return last_in.has_tools;
	default:
		return false;
	}
}

// The hidden tab whose list sits pushed at stack[1] (the root context menu's item), else MENU_TAB_HOME.
static MenuTabId pushedTab(void) {
	if (!stack || stack->count < 2)
		return MENU_TAB_HOME;
	const char* path = ((Directory*)stack->items[1])->path;
	for (int id = MENU_TAB_CONSOLES; id < MENU_TAB_COUNT; id++)
		if (!MenuTabs_isVisible((MenuTabId)id) && exactMatch((char*)path, (char*)MenuTabs_path((MenuTabId)id)))
			return (MenuTabId)id;
	return MENU_TAB_HOME;
}

int MenuTabs_styleCategory(MenuTabId id) {
	switch (id) {
	case MENU_TAB_CONSOLES:
		return MENU_CAT_CONSOLES;
	case MENU_TAB_COLLECTIONS:
		return MENU_CAT_COLLECTIONS;
	case MENU_TAB_TOOLS:
		return MENU_CAT_TOOLS;
	default:
		return -1; // Home has no style setting
	}
}

const char* MenuTabs_path(MenuTabId id) {
	switch (id) {
	case MENU_TAB_CONSOLES:
		return ROMS_PATH;
	case MENU_TAB_COLLECTIONS:
		return COLLECTIONS_PATH;
	case MENU_TAB_TOOLS:
		return TOOLS_PATH;
	default:
		return SDCARD_PATH; // Home
	}
}

static MenuTabPaths tabPaths(void) {
	static char sys_tools[MAX_PATH];
	snprintf(sys_tools, sizeof(sys_tools), "%s/Tools", PAKS_PATH);
	return (MenuTabPaths){ROMS_PATH, COLLECTIONS_PATH, TOOLS_PATH, sys_tools};
}

MenuTabId MenuTabs_forPathVisible(const char* path) {
	MenuTabPaths p = tabPaths();
	MenuTabId id = MenuTabs_forPath(path, &p);
	return MenuTabs_isVisible(id) ? id : MenuTabs_at(0); // Home, unless it is hidden
}

// Build the Directory for a tab with a clamped selection window. `current` must already be id: the row count
// depends on the tab's style.
static Directory* buildRoot(MenuTabId id, int selected, int start, int end) {
	Directory* dir = Directory_new((char*)MenuTabs_path(id), 0);
	root_gen = Content_libraryGen(); // after Directory_new: listing Consoles may have refilled the emulist
	MenuTabs_clampWindow(dir->entries->count, GameList_rowCountAt(true), &selected, &start, &end);
	dir->selected = selected;
	dir->start = start;
	dir->end = end;
	return dir;
}

unsigned MenuTabs_generation(void) {
	return generation;
}

// Empty the stack (it stays allocated): the lists pushed over the old root are freed (they belong to it), and the
// root is parked when it is the current tab's plain root, else freed.
static void parkStack(void) {
	while (stack->count > 1)
		DirectoryArray_pop(stack);
	Directory* old = stack->count ? Array_pop(stack) : NULL;
	if (old && exactMatch(old->path, MenuTabs_path(current))) {
		// not a PLATFORM-merged root (pathToStack): those are rebuilt, never parked
		Directory* displaced = MenuTabs_slotPark(slots, current, old, old->selected, old->start, old->end);
		if (displaced)
			Directory_free(displaced);
		if (current == MENU_TAB_CONSOLES)
			consoles_gen = root_gen;
	} else if (old)
		Directory_free(old);
	top = NULL;
}

void MenuTabs_releaseStack(void) {
	if (stack)
		parkStack();
}

// stack[0] for `id`, replacing the whole stack. The outgoing root is parked (when it is the current tab's plain root)
// and id's parked root reused, so a switch reads nothing from the card; generation still moves, so per-view state
// resets as for a fresh root.
void MenuTabs_openRoot(MenuTabId id) {
	nextGeneration();
	if (!stack)
		stack = Array_new();
	parkStack();

	current = id; // before anything reads the row count
	Directory* dir = MenuTabs_slotTake(slots, id);
	if (dir && id == MENU_TAB_CONSOLES && consoles_gen != Content_libraryGen()) {
		Directory_free(dir);
		dir = NULL;
	}
	if (dir) {
		// the rows depend on the layout and scale, which may have changed while it was parked
		MenuTabs_clampWindow(dir->entries->count, GameList_rowCountAt(true), &dir->selected, &dir->start, &dir->end);
		if (id == MENU_TAB_CONSOLES)
			root_gen = consoles_gen;
	} else {
		const MenuTabSlot* hint = &slots[id];
		dir = hint->hint ? buildRoot(id, hint->selected, hint->start, hint->end) : buildRoot(id, 0, 0, 0);
	}
	Array_push(stack, dir);
	top = dir;
}

bool MenuTabs_step(int delta) {
	if (tab_count < 2 || stack->count != 1)
		return false;
	int i = MenuTabs_indexOf(tabs, tab_count, current);
	MenuTabs_openRoot(tabs[MenuTabs_wrap(tab_count, i < 0 ? 0 : i, delta)]);
	return true;
}

void MenuTabs_reload(int keep_selected, unsigned what) {
	MenuReloadPlan plan = MenuTabs_reloadPlan(what);
	nextGeneration(); // per-view state resets as for a fresh root, whether or not stack[0] is rebuilt
	// only a tab the change can show in has an out-of-date parked root; it is rebuilt on its next visit (from its
	// hint). The others stay parked: they equal a rebuild (and a Consoles root parked before an emulist refill is
	// still caught by MenuTabs_openRoot's consoles_gen check).
	for (int id = 0; id < MENU_TAB_COUNT; id++) {
		if (plan.stale_tabs & (1u << id))
			dropSlot((MenuTabId)id);
	}
	refreshInputs(&plan);
	// a tab the change hid (Settings toggles, a last ROM or collection gone) holds its parked root until it shows
	// again: dropped now, its hint kept. The current tab has none parked (its root is the stack's).
	for (int id = 0; id < MENU_TAB_COUNT; id++) {
		if (!MenuTabs_isVisible((MenuTabId)id))
			dropSlot((MenuTabId)id);
	}
	MenuTabId next = tabs[MenuTabs_resolve(tabs, tab_count, current)];
	MenuTabId old_tab = current;
	Directory* old = stack->items[0];
	// the current tab's root, when the change can't show in it, is what a rebuild would give: kept, but
	// re-windowed exactly as a rebuild is (ListWindow_reload ignores the old window)
	bool keep_root = next == old_tab && !(plan.stale_tabs & (1u << old_tab)) &&
					 exactMatch(old->path, MenuTabs_path(old_tab)) &&
					 (old_tab != MENU_TAB_CONSOLES || root_gen == Content_libraryGen());
	if (keep_root) {
		old->selected =
			ListWindow_reload(old->entries->count, GameList_rowCountAt(true), keep_selected, &old->start, &old->end);
	} else {
		dropSlot(next); // a parked copy would be a second root for this tab (its hint stays)
		current = next; // before anything reads the row count (it depends on the tab's style)
		Directory* fresh;
		if (next == old_tab) {
			fresh = buildRoot(next, 0, 0, 0);
			// buildRoot's window from the top, re-windowed so the kept selection shows (as reloadDirectoryAt)
			fresh->selected = ListWindow_reload(fresh->entries->count, GameList_rowCountAt(true), keep_selected,
												&fresh->start, &fresh->end);
		} else {
			// the neighbour reopens where it was left (its hint's row and window), as a tab switch would
			const MenuTabSlot* hint = &slots[next];
			fresh = hint->hint ? buildRoot(next, hint->selected, hint->start, hint->end) : buildRoot(next, 0, 0, 0);
		}
		if (next != old_tab) {
			// the current tab vanished (e.g. its last ROM was deleted from inside Consoles › GBA): a list pushed
			// over it belongs to that tab, so it can't stay over another tab's root -- B would land on the wrong
			// tab, and the caller's reloadDirectoryAt would rebuild it there. Back to the new root.
			while (stack->count > 1)
				DirectoryArray_pop(stack);
		}
		Directory_free(old);
		stack->items[0] = fresh;
		if (stack->count == 1)
			top = fresh;
	}
	Home_reset(); // the pins or what Continue shows may have changed
}

// MENU_TAB_PATH: the tab key, plus a second line "launch" when the game was
// launched from Home itself (Continue or a pin), so boot returns to Home
// even when the ROM's own tab is another one.
static bool home_launch = false;

void MenuTabs_markHomeLaunch(void) {
	home_launch = true;
}

void MenuTabs_clearHomeLaunch(void) {
	home_launch = false;
}

// MENU_TAB_PATH as read once at boot (initialTab, savedHomeLaunch and savedPush all ask): saveState writes it only
// right before nextui exits for a launch or a restart, and drops this copy when it does, so it can't go stale.
static struct {
	bool read;
	char key[64];
	char* second; // into key, or NULL
} saved_state;

void MenuTabs_saveState(void) {
	saved_state.read = false;
	if (home_launch && current == MENU_TAB_HOME)
		putFile(MENU_TAB_PATH, "home\nlaunch\n");
	else if (pushedTab() != MENU_TAB_HOME) {
		// a hidden tab's list pushed over a tab by the context menu: the tab it was pushed over plus the pushed
		// tab's key, so boot reopens that tab and loadLast re-pushes the list
		char state[64];
		snprintf(state, sizeof(state), "%s\n%s\n", MenuTabs_key(current), MenuTabs_key(pushedTab()));
		putFile(MENU_TAB_PATH, state);
	} else
		putFile(MENU_TAB_PATH, (char*)MenuTabs_key(current));
	home_launch = false;
}

// MENU_TAB_PATH split into the tab key and its optional second line (both into the cached copy).
static void readSavedState(const char** key, const char** second) {
	if (!saved_state.read) {
		saved_state.read = true;
		saved_state.key[0] = '\0';
		saved_state.second = NULL;
		if (exists(MENU_TAB_PATH))
			getFile(MENU_TAB_PATH, saved_state.key, sizeof(saved_state.key));
		char* nl = strchr(saved_state.key, '\n');
		if (nl) {
			*nl++ = '\0';
			trimTrailingNewlines(nl);
			saved_state.second = nl;
		}
		trimTrailingNewlines(saved_state.key);
	}
	*key = saved_state.key;
	*second = saved_state.second;
}

// The pushed tab a second line names (Consoles, Collections or Tools), else MENU_TAB_HOME.
static MenuTabId pushedFromLine(const char* second) {
	MenuTabId id = MENU_TAB_HOME;
	if (!second || !MenuTabs_parseKey(second, &id))
		return MENU_TAB_HOME;
	return id;
}

MenuTabId MenuTabs_savedPush(void) {
	const char *key, *second;
	readSavedState(&key, &second);
	return pushedFromLine(second);
}

bool MenuTabs_savedHomeLaunch(void) {
	const char *key, *second;
	readSavedState(&key, &second);
	MenuTabId saved = MENU_TAB_HOME;
	return MenuTabs_parseKey(key, &saved) && saved == MENU_TAB_HOME && second && strcmp(second, "launch") == 0;
}

// A saved path belongs to the Home tab when it, or a folder it sits in
// below the card root, is pinned (a ROM launched from a pinned game folder).
static bool homeOwnsPath(const char* path) {
	size_t root_len = strlen(SDCARD_PATH);
	if (!MenuTabs_underPath(path, SDCARD_PATH))
		return false;
	char p[MAX_PATH];
	snprintf(p, sizeof(p), "%s", path);
	while (strlen(p) > root_len) {
		if (Shortcuts_exists(p + root_len))
			return true;
		char* slash = strrchr(p, '/');
		if (!slash)
			break;
		*slash = '\0';
	}
	return false;
}

// Whether the saved tab is where last_path was saved from. A launch from
// Search or the Game Switcher saves the ROM under whatever tab was showing;
// that tab doesn't list it, so boot should open the ROM's own tab instead.
static bool savedOwnsPath(MenuTabId saved, const char* last_path, MenuTabId path_tab) {
	switch (saved) {
	case MENU_TAB_HOME:
		return homeOwnsPath(last_path);
	default:
		return path_tab == saved;
	}
}

MenuTabId MenuTabs_initialTab(const char* last_path) {
	const char *key, *second;
	readSavedState(&key, &second);
	MenuTabId saved = MENU_TAB_HOME;
	bool saved_valid = MenuTabs_parseKey(key, &saved);
	bool home_launched = saved_valid && saved == MENU_TAB_HOME && second && strcmp(second, "launch") == 0;
	bool has_path = last_path && last_path[0];
	MenuTabId path_tab = MENU_TAB_HOME;
	bool owns = false;
	if (has_path) {
		MenuTabPaths p = tabPaths();
		path_tab = MenuTabs_forPath(last_path, &p);
		// a launch from a hidden tab's list pushed over the saved tab: boot reopens that tab (loadLast re-pushes the list)
		MenuTabId pushed = pushedFromLine(second);
		bool list_push = pushed != MENU_TAB_HOME && path_tab == pushed && !MenuTabs_isVisible(pushed);
		owns = home_launched || list_push || (saved_valid && savedOwnsPath(saved, last_path, path_tab));
	}
	return MenuTabs_pickInitial(tabs, tab_count, saved_valid, saved, has_path, path_tab, owns);
}

// ---- Tab row ----
// Drawn in the bottom bar at the root (nx-mobile's tabs at the bottom), over the hint bar's scrim: the labels,
// left-aligned on the title's edge, the current one white and the others dimmed (no underline), and on the right
// SELECT RECENT and START SEARCH (each by its toggle; only the first where both don't fit); no other hints at the root.
// When the labels don't fit they scroll (current tab kept 16 dp clear of the edges), with a 16 dp fade on each side
// that has more to scroll.

// The tab row keeps the page title's size: its font is the page title's (UI_pageTitleFont) and its measures dp
#define TAB_LABEL_GAP NX_DP(20) // pixels between labels
#define TAB_EDGE NX_DP(16)		// scroll margin + edge fade width
#define TAB_DIM_ALPHA 97		// 38%: labels of the other tabs

// A tab's label as the row shows it: in capitals (as the hints beside it), made once per tab.
static const char* rowLabel(MenuTabId id) {
	static char labels[MENU_TAB_COUNT][32];
	if ((int)id < 0 || (int)id >= MENU_TAB_COUNT)
		return MenuTabs_label(id);
	char* out = labels[id];
	if (!out[0]) {
		const char* in = MenuTabs_label(id);
		size_t i = 0;
		for (; in && in[i] && i + 1 < sizeof(labels[id]); i++)
			out[i] = (char)toupper((unsigned char)in[i]);
		out[i] = '\0';
	}
	return out;
}

// The label strip, cached so an idle frame only blits it.
static struct {
	SDL_Surface* surf;
	TTF_Font* font;
	int off, cur, count, w, h;
	MenuTabId ids[MENU_TAB_COUNT];
	float scale;
} strip;

// The last layoutLabels result, so a dirty frame doesn't re-measure the labels: keyed on the font, the visible tabs
// (each id's label is a fixed string) and FIXED_SCALE.
static struct {
	TTF_Font* font;
	int count, width;
	float scale;
	MenuTabId ids[MENU_TAB_COUNT];
	int xs[MENU_TAB_COUNT], ws[MENU_TAB_COUNT];
} label_layout;

// Label strip layout for one font: label x/width per visible tab (strip x 0
// = the first label's left edge); returns the strip's width.
static int layoutLabels(TTF_Font* f, int xs[MENU_TAB_COUNT], int ws[MENU_TAB_COUNT]) {
	if (label_layout.font != f || label_layout.count != tab_count || label_layout.scale != FIXED_SCALE ||
		memcmp(label_layout.ids, tabs, sizeof(MenuTabId) * tab_count) != 0) {
		int x = 0;
		for (int i = 0; i < tab_count; i++) {
			int w = 0;
			TTF_SizeUTF8(f, rowLabel(tabs[i]), &w, NULL);
			label_layout.xs[i] = x;
			label_layout.ws[i] = w;
			x += w + (i + 1 < tab_count ? TAB_LABEL_GAP : 0);
		}
		label_layout.font = f;
		label_layout.count = tab_count;
		label_layout.scale = FIXED_SCALE;
		label_layout.width = x;
		memcpy(label_layout.ids, tabs, sizeof(MenuTabId) * tab_count);
	}
	memcpy(xs, label_layout.xs, sizeof(int) * tab_count);
	memcpy(ws, label_layout.ws, sizeof(int) * tab_count);
	return label_layout.width;
}

// Multiply the alpha of columns [x0, x0 + n) by a linear ramp (rising: 0 -> 1 left to right, else 1 -> 0).
static void fadeColumns(SDL_Surface* s, int x0, int n, bool rising) {
	if (n <= 0 || SDL_LockSurface(s) != 0)
		return;
	for (int i = 0; i < n; i++) {
		int x = x0 + i;
		if (x < 0 || x >= s->w)
			continue;
		int k = rising ? i : n - 1 - i; // 0 at the outer edge
		Uint32 f = (Uint32)(k * 256 / n);
		for (int y = 0; y < s->h; y++) {
			Uint32* px = (Uint32*)((Uint8*)s->pixels + y * s->pitch + x * 4);
			Uint32 a = (*px >> 24) & 0xff;
			*px = (*px & 0x00ffffff) | (((a * f) >> 8) << 24);
		}
	}
	SDL_UnlockSurface(s);
}

static bool stripMatches(TTF_Font* f, int off, int cur, int w, int h) {
	if (!strip.surf || strip.font != f || strip.off != off || strip.cur != cur || strip.count != tab_count ||
		strip.w != w || strip.h != h || strip.scale != FIXED_SCALE)
		return false;
	return memcmp(strip.ids, tabs, sizeof(MenuTabId) * tab_count) == 0;
}

// The labels as one ARGB surface (band_w x bar_h), edge-faded on a side with more to scroll.
static SDL_Surface* labelStrip(TTF_Font* f, const int* xs, int labels_w, int off, int cur, int band_w, int bar_h,
							   int band_h) {
	if (stripMatches(f, off, cur, band_w, bar_h))
		return strip.surf;
	if (strip.surf)
		SDL_FreeSurface(strip.surf);
	strip.surf = NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, band_w, bar_h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s)
		return NULL;
	// Transparent *white*: the labels alpha-blend onto it and are white masks, so anti-aliased edges keep their white
	// instead of darkening towards a 0x00000000 fill (a dark fringe once blended again).
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 255, 255, 255, 0));
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_BLEND);
	for (int i = 0; i < tab_count; i++) {
		SDL_Surface* text = GFX_getCachedText(f, rowLabel(tabs[i]), COLOR_WHITE);
		if (!text)
			continue;
		SDL_Rect dst = {xs[i] - off, (band_h - text->h) / 2};
		if (i == cur) {
			SDL_BlitSurface(text, NULL, s, &dst);
		} else {
			// The text cache is shared: dim for this blit only.
			SDL_SetSurfaceAlphaMod(text, TAB_DIM_ALPHA);
			SDL_BlitSurface(text, NULL, s, &dst);
			SDL_SetSurfaceAlphaMod(text, 255);
		}
	}
	int edge = TAB_EDGE < band_w / 2 ? TAB_EDGE : band_w / 2;
	if (off > 0)
		fadeColumns(s, 0, edge, true);
	if (off < labels_w - band_w)
		fadeColumns(s, band_w - edge, edge, false);

	strip.surf = s;
	strip.font = f;
	strip.off = off;
	strip.cur = cur;
	strip.count = tab_count;
	strip.w = band_w;
	strip.h = bar_h;
	strip.scale = FIXED_SCALE;
	memcpy(strip.ids, tabs, sizeof(MenuTabId) * tab_count);
	return s;
}

// A glyph's height above the baseline (px) in f: its maxy, or `fallback` when the font can't tell.
static int glyphTop(TTF_Font* f, Uint16 ch, int fallback) {
	int minx, maxx, miny, maxy, adv;
	return f && TTF_GlyphMetrics(f, ch, &minx, &maxx, &miny, &maxy, &adv) == 0 && maxy > 0 ? maxy : fallback;
}

bool MenuTabs_rowShown(void) {
	return tab_count > 1 && CFG_getButtonHints(); // Layouts > Bottom bar (buttonhints): the tabs and the hints
}

bool MenuTabs_bottomBar(void) {
	return CFG_getButtonHints(); // the tab row at the root, the hint bar elsewhere: both shown by Layouts > Bottom bar
}

void MenuTabs_renderRow(SDL_Surface* screen, int ow) {
	(void)ow; // the status group stays in the top bar
	// one tab left (the others hidden or empty): no row, a lone label has nothing to switch to; Layouts > Bottom bar
	// hidden: none either (L1/R1 still switch tabs)
	if (!MenuTabs_rowShown())
		return;

	// the row fills the bottom bar, over the hint bar's scrim
	int bar_h = BAR_HEIGHT;
	int row_h = bar_h;
	int row_y = screen->h - bar_h;
	int band_h = row_h;
	static SDL_Surface* scrim = NULL;
	SDL_Surface* sc = UI_getScrimAlpha(&scrim, screen->w, bar_h, 204);
	if (sc)
		UI_blitFade(sc, NULL, screen, 0, row_y);
	int margin = Home_edgePx(); // NX_EDGE_DP, as Home rounds it: in line with the Continue card's left, the tools' right
	int left = margin;

	int xs[MENU_TAB_COUNT], ws[MENU_TAB_COUNT];
	// the page title's font; labels that don't fit scroll (never a smaller font)
	TTF_Font* f = UI_pageTitleFont();
	if (!f)
		f = font.medium;
	int labels_w = layoutLabels(f, xs, ws);

	// the hints on the right, each as its toggle allows: SELECT RECENT and START SEARCH while the labels still fit
	// beside both (the Smart Pro S), else the first alone (the Bricks)
	const char* hb[2] = {NULL, NULL};
	const char* hl[2] = {NULL, NULL};
	int nh = 0;
	if (CFG_getShowRecentHint())
		hb[nh] = "SELECT", hl[nh++] = "RECENT";
	if (CFG_getShowSearchHint())
		hb[nh] = "START", hl[nh++] = "SEARCH";
	// the glyphs as tall as the labels' capitals (their visible text), not the hint bar's buttons
	int glyph_h = glyphTop(font.tiny, 'H', TTF_FontAscent(font.tiny) * 7 / 10);
	int hw[2] = {0, 0}, hints_w = 0;
	for (int i = 0; i < nh; i++) {
		hw[i] = GFX_blitButtonScaled((char*)hl[i], (char*)hb[i], NULL, 0, 0, glyph_h);
		hints_w += hw[i] + SCALE1(BUTTON_MARGIN);
	}
	if (nh == 2 && screen->w - margin - hints_w - TAB_LABEL_GAP - left < labels_w) {
		hints_w -= hw[1] + SCALE1(BUTTON_MARGIN); // no room: the first alone
		nh = 1;
	}
	int right_limit = screen->w - margin - (nh ? hints_w + TAB_LABEL_GAP : 0);
	int band_w = right_limit - left;
	if (nh) {
		int hx = screen->w - margin - hints_w + SCALE1(BUTTON_MARGIN);
		for (int i = 0; i < nh; i++) {
			GFX_blitButtonScaled((char*)hl[i], (char*)hb[i], screen, hx, row_y + bar_h / 2, glyph_h);
			hx += hw[i] + SCALE1(BUTTON_MARGIN);
		}
	}
	if (band_w <= 0)
		return;

	int cur = MenuTabs_indexOf(tabs, tab_count, current);
	if (cur < 0)
		cur = 0;
	int cur_x = xs[cur], cur_w = ws[cur];

	// The labels scroll inside the band, the current one kept 16 dp from its edges.
	int band_x = left;
	int off = MenuTabs_scrollOffset(labels_w, band_w, cur_x, cur_w, TAB_EDGE);

	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_SetClipRect(screen, &(SDL_Rect){band_x, row_y, band_w, bar_h});

	SDL_Surface* labels = labelStrip(f, xs, labels_w, off, cur, band_w, row_h, band_h);
	if (labels)
		SDL_BlitSurface(labels, NULL, screen, &(SDL_Rect){band_x, row_y});

	SDL_SetClipRect(screen, &prev_clip);
}
