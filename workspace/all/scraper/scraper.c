#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_confirmdialog.h"
#include "ui_downloadprogress.h"
#include "ui_emptystate.h"
#include "ui_loadingoverlay.h"
#include "ui_menubar.h"
#include "ui_splash.h"
#include "ui_quitrequest.h"
#include "ui_list.h"
#include "ui_listview.h"
#include "utils.h"

#include "scraper_api.h"
#include "scraper_names.h"
#include "scraper_systems.h"
#include "scraper_core.h"
#include "scraper_fetch.h"
#include "ui_keyboard.h"
#include "display_helper.h"
#include "scraper_scan.h"
#include "scraper_optimize.h"
#include "scraper_prefs.h"
#include "scrape_queue.h"

// ============================================
// Constants
// ============================================

#define MAX_SYSTEMS 128


// ============================================
// User Credentials
// ============================================

static char cred_username[64] = "";
static char cred_password[128] = "";

static void loadCredentials(void) {
	cred_username[0] = '\0';
	cred_password[0] = '\0';
	getFile(creds_user_path(), cred_username, sizeof(cred_username));
	getFile(creds_pass_path(), cred_password, sizeof(cred_password));
	// Trim newlines
	char* nl;
	if ((nl = strchr(cred_username, '\n')))
		*nl = '\0';
	if ((nl = strchr(cred_password, '\n')))
		*nl = '\0';
	ScraperAPI_setUserCredentials(cred_username, cred_password);
}

static void saveCredentials(void) {
	mkdir_p(creds_dir());
	putFile(creds_user_path(), cred_username);
	putFile(creds_pass_path(), cred_password);
	ScraperAPI_setUserCredentials(cred_username, cred_password);
}

// ============================================
// Data Structures
// ============================================

typedef struct {
	char name[256];	   // Display name (e.g. "Game Boy Advance")
	char tag[64];	   // Tag extracted from folder (e.g. "GBA")
	char path[512];	   // Full path to ROM directory
	int system_id;	   // ScreenScraper system ID
	int rom_count;	   // Total ROMs
	int scraped_count; // ROMs with existing artwork
	bool supported;	   // tag known to ScreenScraper (system_id >= 0)
} SystemEntry;

typedef struct {
	char filename[256];	 // ROM filename (basename; folder games: the .cue/.m3u)
	char path[512];		 // Full path to ROM file
	char label[256];	 // Display name, relative to the system folder
	char art_png[512];	 // .media/<name>.png (nextui ROM_mediaArtPath): the base the variant paths derive from
	bool has_artwork;	 // scraped per ScraperPrefs_isComplete: a screenshot or box art, plus the mix when it is on
	bool has_screenshot; // Whether the .media/screenshot/ variant exists
	bool has_boxart;	 // Whether the .media/boxart/ variant exists
	bool has_mix;		 // Whether the .media/mix/ mix exists
} ROMEntry;

typedef enum {
	SCREEN_MAIN_MENU,
	SCREEN_SYSTEMS,
	SCREEN_ROMS,
	SCREEN_PROGRESS,
	SCREEN_SETTINGS,
} ScreenState;

// ============================================
// Scrape Queue (scrape_queue.c; every access holds queue_mutex)
// ============================================

static ScrapeQueue scrape_queue;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t scraper_thread;
static volatile bool scraper_running = false;
static volatile bool scraper_thread_started = false;
static volatile bool queue_dirty = false;

// ============================================
// Global State
// ============================================

static SDL_Surface* screen;
static ScreenState current_screen = SCREEN_MAIN_MENU;

static SystemEntry systems[MAX_SYSTEMS];
static int system_count = 0;
static ListView systems_view;
static char systems_badge_buf[64];
static char systems_label_buf[340]; // "<name> (<tag>)" for the current row

// grows as a system is scanned (no fixed cap: big arcade sets have thousands)
static ROMEntry* roms = NULL;
static int roms_cap = 0;
static int rom_count = 0;
static int rom_selected = 0;
static int rom_scroll = 0;

static int progress_selected = 0;
static int progress_scroll = 0;

static int settings_selected = 0;
static int settings_scroll = 0;

// Settings screen user info cache
static ScraperUserInfo cached_user_info = {0};
static bool user_info_fetched = false;

// Download preferences (Settings rows); also decide when a game counts as scraped.
static ScraperPrefs scraper_prefs;
static bool settings_mix_on_entry = false; // Generate mix when Settings opened

// ============================================
// System Scanner
// ============================================

typedef struct {
	int roms;
	int scraped;
} GameCounts;

static bool count_game_cb(const ScanGame* g, void* ud) {
	GameCounts* c = ud;
	c->roms++;
	// each picture as a .png, or a .jpg/.jpeg of the same name (the companion app's art)
	char variant[512];
	Scraper_variantPath(g->art_png, "screenshot", variant, sizeof(variant));
	bool has_screenshot = ROM_artExists(variant, sizeof(variant));
	Scraper_variantPath(g->art_png, "boxart", variant, sizeof(variant));
	bool has_boxart = ROM_artExists(variant, sizeof(variant));
	Scraper_variantPath(g->art_png, "mix", variant, sizeof(variant));
	bool has_mix = ROM_artExists(variant, sizeof(variant));
	if (ScraperPrefs_isComplete(&scraper_prefs, has_screenshot, has_boxart, has_mix))
		c->scraped++;
	return true;
}

// Count games the way nextui lists them (any non-hidden file, folder games
// as one, nested folders included) and how many are already scraped (ScraperPrefs_isComplete).
static GameCounts countGames(const char* dirpath) {
	GameCounts c = {0, 0};
	Scan_walk(dirpath, count_game_cb, &c);
	return c;
}

// Folders that hold standalone games rather than one console's ROMs: there is
// no ScreenScraper system to query, so they are left out of the Library rather
// than listed as unsupported. nextui's "Fetch Artwork" excludes the same tags.
static bool isLibraryExcludedTag(const char* tag) {
	static const char* excluded[] = {"PORTS", "CUSTOM", "EXTRAS", NULL};
	for (int i = 0; excluded[i]; i++)
		if (strcasecmp(tag, excluded[i]) == 0)
			return true;
	return false;
}

static int systemCompare(const void* a, const void* b) {
	return strcasecmp(((const SystemEntry*)a)->name, ((const SystemEntry*)b)->name);
}

static void scanSystems(void) {
	system_count = 0;
	DIR* dir = opendir(ROMS_PATH);
	if (!dir)
		return;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL && system_count < MAX_SYSTEMS) {
		if (entry->d_name[0] == '.')
			continue;
		if (entry->d_type != DT_DIR)
			continue;

		char tag[64];
		Scraper_extractTag(entry->d_name, tag, sizeof(tag));
		if (tag[0] == '\0' || isLibraryExcludedTag(tag))
			continue;

		// Unknown tags stay in the list, marked unsupported, so a folder that
		// cannot be scraped is visible instead of silently missing.
		int sid = ScraperSystems_getId(tag);

		SystemEntry* sys = &systems[system_count];
		Scraper_extractDisplayName(entry->d_name, sys->name, sizeof(sys->name));
		snprintf(sys->tag, sizeof(sys->tag), "%s", tag);
		snprintf(sys->path, sizeof(sys->path), "%s/%s", ROMS_PATH, entry->d_name);
		sys->system_id = sid;
		sys->supported = sid >= 0;
		GameCounts counts = countGames(sys->path);
		sys->rom_count = counts.roms;
		sys->scraped_count = counts.scraped;

		if (sys->rom_count > 0)
			system_count++;
	}
	closedir(dir);

	qsort(systems, system_count, sizeof(SystemEntry), systemCompare);
}

// ============================================
// ROM Scanner
// ============================================

static int romCompare(const void* a, const void* b) {
	return strcasecmp(((const ROMEntry*)a)->label, ((const ROMEntry*)b)->label);
}

// ---- map.txt display aliases (nextui "Rename Rom") ----------------------
// nextui shows a game's map.txt alias in place of its filename. The alias
// lives in <entry's parent dir>/map.txt keyed by the basename nextui lists:
// the filename (with extension) for a flat game, the folder name for a
// folder game. Scan_walk emits a directory's games together, so a single-slot
// cache reads each map.txt at most a handful of times per scan.

typedef struct {
	char* key;
	char* val;
} MapAlias;

static char map_cache_dir[512] = "";
static MapAlias* map_cache = NULL;
static int map_cache_count = 0;

static void mapCacheFree(void) {
	for (int i = 0; i < map_cache_count; i++) {
		free(map_cache[i].key);
		free(map_cache[i].val);
	}
	free(map_cache);
	map_cache = NULL;
	map_cache_count = 0;
	map_cache_dir[0] = '\0';
}

// Load <dir>/map.txt into the cache (no-op if <dir> is already cached, even
// when it had no map.txt — the empty result is cached too).
static void mapCacheLoad(const char* dir) {
	if (strcmp(dir, map_cache_dir) == 0)
		return;
	mapCacheFree();
	snprintf(map_cache_dir, sizeof(map_cache_dir), "%s", dir);

	char path[600];
	snprintf(path, sizeof(path), "%s/map.txt", dir);
	FILE* f = fopen(path, "r");
	if (!f)
		return;

	int cap = 0;
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), f)) {
		char* nl = strpbrk(line, "\r\n");
		if (nl)
			*nl = '\0';
		char* tab = strchr(line, '\t');
		if (!tab)
			continue;
		*tab = '\0';
		if (line[0] == '\0')
			continue;
		if (map_cache_count == cap) {
			int ncap = cap ? cap * 2 : 64;
			MapAlias* grown = realloc(map_cache, ncap * sizeof(*grown));
			if (!grown)
				break;
			map_cache = grown;
			cap = ncap;
		}
		map_cache[map_cache_count].key = strdup(line);
		map_cache[map_cache_count].val = strdup(tab + 1);
		map_cache_count++;
	}
	fclose(f);
}

static const char* mapAliasFor(const char* dir, const char* key) {
	mapCacheLoad(dir);
	for (int i = 0; i < map_cache_count; i++)
		if (strcmp(map_cache[i].key, key) == 0)
			return map_cache[i].val;
	return NULL;
}

static void refreshRomArt(ROMEntry* rom);

static bool scan_rom_cb(const ScanGame* g, void* ud) {
	(void)ud;
	if (rom_count >= roms_cap) {
		int ncap = roms_cap ? roms_cap * 2 : 512;
		ROMEntry* grown = realloc(roms, (size_t)ncap * sizeof(ROMEntry));
		if (!grown) {
			LOG_error("Scraper: out of memory at %d ROMs, the list is cut short\n", rom_count);
			return false;
		}
		roms = grown;
		roms_cap = ncap;
	}
	ROMEntry* rom = &roms[rom_count++];
	snprintf(rom->filename, sizeof(rom->filename), "%s", g->filename);
	snprintf(rom->path, sizeof(rom->path), "%s", g->path);
	// Show the map.txt alias (nextui "Rename Rom") when the game has one,
	// otherwise the filename-derived label — matching the main game list.
	char map_dir[SCAN_PATH_MAX], map_key[SCAN_NAME_MAX];
	Scraper_mapKeyForGame(g, map_dir, sizeof(map_dir), map_key, sizeof(map_key));
	const char* alias = mapAliasFor(map_dir, map_key);
	snprintf(rom->label, sizeof(rom->label), "%s", (alias && alias[0]) ? alias : g->label);
	snprintf(rom->art_png, sizeof(rom->art_png), "%s", g->art_png);
	refreshRomArt(rom);
	return true;
}

static void refreshRomArt(ROMEntry* rom) {
	// The scraper stores a screenshot and a box art per game, under .media/<variant>/, each only when
	// ScreenScraper had it. A game with either counts as scraped, and with Generate mix on it also needs the
	// .media/mix/ mix (an old root .media/<name>.png mix doesn't count, so a re-scrape upgrades it); a mix alone
	// doesn't count (a re-scrape adds the images the menus use). A .jpg of the same name (the companion app's art)
	// counts like the .png.
	char variant[512];
	Scraper_variantPath(rom->art_png, "screenshot", variant, sizeof(variant));
	rom->has_screenshot = ROM_artExists(variant, sizeof(variant));
	Scraper_variantPath(rom->art_png, "boxart", variant, sizeof(variant));
	rom->has_boxart = ROM_artExists(variant, sizeof(variant));
	Scraper_variantPath(rom->art_png, "mix", variant, sizeof(variant));
	rom->has_mix = ROM_artExists(variant, sizeof(variant));
	rom->has_artwork = ScraperPrefs_isComplete(&scraper_prefs, rom->has_screenshot, rom->has_boxart, rom->has_mix);
}

static void scanROMs(SystemEntry* sys) {
	rom_count = 0;
	rom_selected = 0;
	rom_scroll = 0;
	Scan_walk(sys->path, scan_rom_cb, NULL);
	mapCacheFree(); // release the last directory's map.txt
	qsort(roms, rom_count, sizeof(ROMEntry), romCompare);
}

// Reset artwork: delete every image the scraper downloaded, i.e. the screenshot/, boxart/, boxart2d/, wheel/ and
// mix/ folders under every .media in <ROMS_PATH>, across ALL Roms folders (not just systems the scraper recognises, so
// ports, unknown tags, folder games and orphaned names go too) and nested game folders (Roms/GB/Hacks/.media). The
// root .media/*.png pictures are the user's own art (Ports, hand-made pictures) and the folder backgrounds: kept
// (Scraper_deleteAllArtwork). Returns the number of files deleted.
static int deleteAllArtwork(void) {
	return Scraper_deleteAllArtwork(ROMS_PATH);
}

// ============================================
// Queue Operations
// ============================================

static bool isROMQueued(const char* rom_path) {
	pthread_mutex_lock(&queue_mutex);
	bool queued = ScrapeQueue_find(&scrape_queue, rom_path) >= 0;
	pthread_mutex_unlock(&queue_mutex);
	return queued;
}

static ScrapeStatus getROMQueueStatus(const char* rom_path) {
	pthread_mutex_lock(&queue_mutex);
	int i = ScrapeQueue_find(&scrape_queue, rom_path);
	ScrapeStatus s = i >= 0 ? scrape_queue.items[i].status : SCRAPE_STATUS_IDLE;
	pthread_mutex_unlock(&queue_mutex);
	return s;
}

static void* scraper_thread_func(void* arg);

static void ensureThreadStarted(void) {
	if (!scraper_thread_started) {
		scraper_running = true;
		scraper_thread_started = true;
		pthread_create(&scraper_thread, NULL, scraper_thread_func, NULL);
	}
}

static bool queueAddROM(ROMEntry* rom, SystemEntry* sys, bool force) {
	if (!sys->supported)
		return false;
	if (!force && rom->has_artwork)
		return false;

	ScrapeQueueItem item;
	memset(&item, 0, sizeof(item));
	snprintf(item.filename, sizeof(item.filename), "%s", rom->filename);
	snprintf(item.rom_path, sizeof(item.rom_path), "%s", rom->path);
	snprintf(item.system_path, sizeof(item.system_path), "%s", sys->path);
	snprintf(item.system_name, sizeof(item.system_name), "%s", sys->name);
	snprintf(item.out_png, sizeof(item.out_png), "%s", rom->art_png);
	// Per ROM, not per system: the MD/GPGX tags cover several Sega systems.
	item.system_id = ScraperSystems_getIdForRom(sys->tag, rom->path);

	pthread_mutex_lock(&queue_mutex);
	int added = ScrapeQueue_add(&scrape_queue, &item); // a failed item is retried
	if (added > 0)
		queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
	if (added < 0)
		LOG_error("Scraper: out of memory queueing %s\n", rom->path);
	if (added <= 0)
		return false;

	ensureThreadStarted();
	return true;
}

static int queueAddAllROMs(SystemEntry* sys) {
	int added = 0;
	for (int i = 0; i < rom_count; i++) {
		if (queueAddROM(&roms[i], sys, false))
			added++;
	}
	return added;
}

static int queueAddAllSystems(void) {
	int added = 0;
	// Save/restore current ROM list since scanROMs overwrites globals
	int saved_rom_count = rom_count;
	int saved_rom_selected = rom_selected;
	int saved_rom_scroll = rom_scroll;

	for (int s = 0; s < system_count; s++) {
		if (!systems[s].supported)
			continue;
		scanROMs(&systems[s]);
		for (int r = 0; r < rom_count; r++) {
			if (queueAddROM(&roms[r], &systems[s], false))
				added++;
		}
	}

	// Restore ROM list for current system if we were viewing one
	if (current_screen == SCREEN_ROMS) {
		scanROMs(&systems[systems_view.selected]);
		rom_selected = saved_rom_selected;
		rom_scroll = saved_rom_scroll;
	} else {
		rom_count = saved_rom_count;
		rom_selected = saved_rom_selected;
		rom_scroll = saved_rom_scroll;
	}

	return added;
}

static void queueGetStats(int* done, int* total, int* failed) {
	pthread_mutex_lock(&queue_mutex);
	ScrapeQueue_stats(&scrape_queue, done, total, failed);
	pthread_mutex_unlock(&queue_mutex);
}

// Forget finished work (failed rows and the done count); used after Reset
// artwork, when "scraped" no longer describes what is on the card.
static void queueDropFinished(void) {
	pthread_mutex_lock(&queue_mutex);
	ScrapeQueue_dropFinished(&scrape_queue);
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
}

// True while any queue item is still idle or in progress (i.e. the scraper
// thread has work). Used to refuse a destructive reset mid-scrape. The worker
// marks its item SEARCHING when it claims it and always reports a terminal
// result by rom_path, so this clears once the queue drains.
static bool queueIsBusy(void) {
	pthread_mutex_lock(&queue_mutex);
	bool busy = ScrapeQueue_isBusy(&scrape_queue);
	pthread_mutex_unlock(&queue_mutex);
	return busy;
}

// ============================================
// Background Scraper Thread
// ============================================

// userdata is the worker's own copy of the item; the queue row is found again
// by rom_path, since finished rows leave the list and move the ones behind
static void scrape_status_cb(const char* stage, void* userdata) {
	const ScrapeQueueItem* item = (const ScrapeQueueItem*)userdata;
	ScrapeStatus s = SCRAPE_STATUS_SEARCHING;
	if (strcmp(stage, "downloading") == 0)
		s = SCRAPE_STATUS_DOWNLOADING;
	else if (strcmp(stage, "saving") == 0)
		s = SCRAPE_STATUS_SAVING;
	else if (strcmp(stage, "compositing") == 0)
		s = SCRAPE_STATUS_COMPOSITING;
	pthread_mutex_lock(&queue_mutex);
	ScrapeQueue_setStatus(&scrape_queue, item->rom_path, s);
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
}

// Done rows leave the queue, so the ROM list reads a scraped game's state
// from its art again: re-check the rows the worker finished since last time.
static void refreshFinishedRoms(void) {
	for (;;) {
		pthread_mutex_lock(&queue_mutex);
		char* path = ScrapeQueue_popFinished(&scrape_queue);
		pthread_mutex_unlock(&queue_mutex);
		if (!path)
			break;
		for (int i = 0; i < rom_count; i++) {
			if (strcmp(roms[i].path, path) == 0) {
				refreshRomArt(&roms[i]);
				break;
			}
		}
		free(path);
	}
}

static void scrapeOneQueueItem(const ScrapeQueueItem* item) {
	ScrapeResult r = scrapeOne(item->filename, item->rom_path, item->system_id,
							   item->out_png, scrape_status_cb, (void*)item);

	pthread_mutex_lock(&queue_mutex);
	ScrapeQueue_finish(&scrape_queue, item->rom_path,
					   (r == SCRAPE_RESULT_OK)		   ? SCRAPE_STATUS_DONE
					   : (r == SCRAPE_RESULT_NOTFOUND) ? SCRAPE_STATUS_NOT_FOUND
													   : SCRAPE_STATUS_ERROR);
	queue_dirty = true;
	pthread_mutex_unlock(&queue_mutex);
}

static void* scraper_thread_func(void* arg) {
	(void)arg;
	while (scraper_running) {
		// Claim the next IDLE item: a copy, so nothing here points into the
		// queue while the scrape runs unlocked
		ScrapeQueueItem item;
		pthread_mutex_lock(&queue_mutex);
		bool got = ScrapeQueue_takeNext(&scrape_queue, &item);
		if (got)
			queue_dirty = true;
		pthread_mutex_unlock(&queue_mutex);

		if (!got) {
			usleep(200000); // 200ms idle polling
			continue;
		}

		scrapeOneQueueItem(&item);
	}
	return NULL;
}

// ============================================
// Status Text Helpers
// ============================================

static const char* scrapeStatusText(ScrapeStatus status) {
	switch (status) {
	case SCRAPE_STATUS_IDLE:
		return "Queued";
	case SCRAPE_STATUS_SEARCHING:
		return "Searching...";
	case SCRAPE_STATUS_DOWNLOADING:
		return "Downloading...";
	case SCRAPE_STATUS_SAVING:
		return "Saving...";
	case SCRAPE_STATUS_COMPOSITING:
		return "Building mix...";
	case SCRAPE_STATUS_DONE:
		return "Done";
	case SCRAPE_STATUS_NOT_FOUND:
		return "Not Found";
	case SCRAPE_STATUS_ERROR:
		return "Error";
	}
	return "";
}

static const char* romStatusLabel(ROMEntry* rom) {
	if (rom->has_artwork) {
		// Complete only when both stored images are present; otherwise name the missing one, so it is clear
		// what a re-scrape would add.
		if (rom->has_screenshot && rom->has_boxart)
			return "Done";
		if (!rom->has_screenshot)
			return "No screenshot";
		return "No box art";
	}
	if (!isROMQueued(rom->path))
		return NULL;
	// A queued ROM always has one of the ScrapeStatus values that
	// scrapeStatusText() maps to a label, so reuse that mapping.
	return scrapeStatusText(getROMQueueStatus(rom->path));
}

// ============================================
// SCREEN_MAIN_MENU
// ============================================

static ListView main_menu_view;
static const char* main_menu_items[] = {"Library", "Progress", "Optimize images", "Settings"};
#define MAIN_MENU_COUNT ((int)(sizeof(main_menu_items) / sizeof(main_menu_items[0])))
static char progress_badge_buf[32];

static void main_menu_get_row(void* ctx, int i, bool selected,
							  ListViewRow* out) {
	(void)ctx;
	(void)selected;
	out->label = main_menu_items[i];
	if (i == 1) {
		int done, total, failed;
		queueGetStats(&done, &total, &failed);
		if (total > 0) {
			snprintf(progress_badge_buf, sizeof(progress_badge_buf), "%d/%d",
					 done, total);
			out->annotation = progress_badge_buf;
		}
	}
}

static void renderMainMenu(void) {
	GFX_clear(screen);
	ListView* v = &main_menu_view;
	v->title = "Artwork Manager";
	v->font = font.large;
	v->count = MAIN_MENU_COUNT;
	v->get_row = main_menu_get_row;
	v->ctx = NULL;
	v->list_id = (const void*)main_menu_items;
	v->hint_pairs = (char*[]){"B", "EXIT", "A", "OPEN", NULL};
	UI_listViewRender(v, screen);
	GFX_flip(screen);
}

// ============================================
// SCREEN_SYSTEMS (full-mode ListView; in-pill %d/%d badge)
// ============================================

static void systems_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	(void)ctx;
	(void)selected;
	SystemEntry* sys = &systems[i];
	// Show "Game Boy (GB)": the display name (sort prefix already stripped in
	// Scraper_extractDisplayName) with the emulator tag kept, so the console is
	// identifiable without the numeric sort prefix.
	if (sys->tag[0])
		snprintf(systems_label_buf, sizeof(systems_label_buf), "%s (%s)",
				 sys->name, sys->tag);
	else
		snprintf(systems_label_buf, sizeof(systems_label_buf), "%s", sys->name);
	out->label = systems_label_buf;
	if (sys->supported)
		snprintf(systems_badge_buf, sizeof(systems_badge_buf), "%d/%d",
				 sys->scraped_count, sys->rom_count);
	else
		snprintf(systems_badge_buf, sizeof(systems_badge_buf), "Unsupported");
	out->badge = systems_badge_buf;
}

static void renderSystemList(void) {
	GFX_clear(screen);
	// Function-scope arrays: hint_pairs must outlive the UI_listViewRender
	// call below, so a branch-scoped compound literal would be UB (see
	// ui_listview.h).
	char* hints_full[] = {"B", "BACK", "Y", "QUEUE ALL", "A", "OPEN", NULL};
	char* hints_empty[] = {"B", "BACK", NULL};
	ListView* v = &systems_view;
	v->title = "Artwork Manager | Library";
	v->font = font.large;
	v->count = system_count;
	v->get_row = systems_get_row;
	v->ctx = NULL;
	v->list_id = (const void*)systems;
	v->empty_title = "No systems found";
	v->empty_subtitle = "Add ROMs to your SD card";
	if (system_count > 0)
		v->hint_pairs = hints_full;
	else
		v->hint_pairs = hints_empty;
	UI_listViewRender(v, screen);
	GFX_flip(screen);
}

// ============================================
// SCREEN_ROMS (settings-style rows)
// ============================================

static void renderROMList(void) {
	GFX_clear(screen);

	SystemEntry* sys = &systems[systems_view.selected];
	UI_renderMenuBarPage(screen, "Artwork Manager", sys->name);

	if (rom_count == 0) {
		UI_renderEmptyState(screen, "No ROMs found", NULL, NULL);
		UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
		GFX_flip(screen);
		return;
	}

	ListLayout layout = UI_calcListLayout(screen);

	// Kept between frames and grown with the list (no VLA: big lists would
	// overflow the stack)
	static UISettingsItem* items = NULL;
	static int items_cap = 0;
	if (rom_count > items_cap) {
		UISettingsItem* grown = realloc(items, (size_t)rom_count * sizeof(UISettingsItem));
		if (!grown) {
			GFX_flip(screen);
			return;
		}
		items = grown;
		items_cap = rom_count;
	}

	for (int i = 0; i < rom_count; i++) {
		ROMEntry* rom = &roms[i];
		items[i] = (UISettingsItem){
			.label = rom->label,
			.value = romStatusLabel(rom),
			.swatch = -1,
			.cycleable = 0,
			.desc = NULL,
		};
	}

	// ROM rows carry no description, so reserve only one row below the list
	// and show one more game (8 instead of 7 on the Brick).
	UI_renderSettingsPageEx(screen, &layout, items, rom_count, rom_selected, &rom_scroll, NULL, 1);
	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "Y", "QUEUE ALL", "A", "QUEUE", NULL});
	GFX_flip(screen);
}

// ============================================
// SCREEN_PROGRESS
// ============================================

static void renderProgress(void) {
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Artwork Manager | Progress");

	// Done rows leave the queue on their own (only the done count stays), so
	// the list holds what is waiting, in progress or failed
	pthread_mutex_lock(&queue_mutex);
	int count = scrape_queue.count;
	int done = 0, total = 0, failed = 0;
	ScrapeQueue_stats(&scrape_queue, &done, &total, &failed);

	if (count == 0) {
		pthread_mutex_unlock(&queue_mutex);
		progress_selected = 0;
		progress_scroll = 0;
		char sub[64];
		if (done > 0)
			snprintf(sub, sizeof(sub), "%d scraped", done);
		UI_renderEmptyState(screen, "Queue is empty",
							done > 0 ? sub : "Add ROMs from Library", NULL);
		GFX_flip(screen);
		return;
	}
	// rows vanish as they finish: keep the cursor on the list
	if (progress_selected >= count)
		progress_selected = count - 1;
	if (progress_scroll >= count)
		progress_scroll = count - 1;

	ListLayout layout = UI_calcListLayout(screen);

	// Kept between frames and grown with the queue (no VLA: a big queue
	// would overflow the stack)
	typedef struct {
		char filename[256];
		char sys_name[128];
	} ProgressRow;
	static UISettingsItem* items = NULL;
	static ProgressRow* rows = NULL;
	static int rows_cap = 0;
	if (count > rows_cap) {
		UISettingsItem* grown_items = realloc(items, (size_t)count * sizeof(UISettingsItem));
		if (grown_items)
			items = grown_items;
		ProgressRow* grown_rows = realloc(rows, (size_t)count * sizeof(ProgressRow));
		if (grown_rows)
			rows = grown_rows;
		if (!grown_items || !grown_rows) {
			pthread_mutex_unlock(&queue_mutex);
			GFX_flip(screen);
			return;
		}
		rows_cap = count;
	}

	for (int i = 0; i < count; i++) {
		const ScrapeQueueItem* q = &scrape_queue.items[i];
		char* base = removeExtension(q->filename);
		snprintf(rows[i].filename, sizeof(rows[i].filename), "%s", base ? base : q->filename);
		if (base)
			free(base);

		snprintf(rows[i].sys_name, sizeof(rows[i].sys_name), "%s", q->system_name);

		items[i] = (UISettingsItem){
			.label = rows[i].filename,
			.value = scrapeStatusText(q->status),
			.swatch = -1,
			.cycleable = 0,
			.desc = rows[i].sys_name,
		};
	}
	pthread_mutex_unlock(&queue_mutex);

	char status_msg[128];
	if (failed > 0)
		snprintf(status_msg, sizeof(status_msg), "%d / %d complete, %d failed", done, total, failed);
	else
		snprintf(status_msg, sizeof(status_msg), "%d / %d complete", done, total);

	UI_renderSettingsPage(screen, &layout, items, count,
						  progress_selected, &progress_scroll, status_msg);
	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", NULL});
	GFX_flip(screen);
}

// ============================================
// SCREEN_SETTINGS
// ============================================

// The download preference rows ahead of Reset artwork.
#define PREF_ROWS 4

static void renderSettings(void) {
	GFX_clear(screen);

	UI_renderMenuBar(screen, "Artwork Manager | Settings");

	ListLayout layout = UI_calcListLayout(screen);

	bool logged_in = cred_username[0] && cred_password[0];

	const char* user_display = cred_username[0] ? cred_username : "Not set";
	const char* pass_display = cred_password[0] ? "********" : "Not set";

	// The download preference rows come first, then reset; the account rows
	// below it only appear once logged in.
	UISettingsItem pref_items[PREF_ROWS] = {
		{.label = "3D box art", .value = "Always", .swatch = -1, .cycleable = 0, .desc = "Always downloaded"},
		{.label = "2D box art", .value = scraper_prefs.box2d ? "On" : "Off", .swatch = -1, .cycleable = 1, .desc = "Also download the flat 2D box art"},
		{.label = "Wheel", .value = ScraperPrefs_wantWheel(&scraper_prefs) ? "On" : "Off", .swatch = -1, .cycleable = !scraper_prefs.mix, .desc = scraper_prefs.mix ? "Needed by Generate mix" : "Also download the game's logo (wheel)"},
		{.label = "Generate mix", .value = scraper_prefs.mix ? "On" : "Off", .swatch = -1, .cycleable = 1, .desc = "Build a screenshot + box art + wheel picture on the device for the List layout. Scraping takes longer and uses more storage"},
	};
	UISettingsItem reset_item = {.label = "Reset artwork", .value = NULL, .swatch = -1, .cycleable = 0, .desc = "Delete every image the scraper downloaded (your own .media pictures are kept)"};
	UISettingsItem user_item = {.label = "Username", .value = user_display, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper username"};
	UISettingsItem pass_item = {.label = "Password", .value = pass_display, .swatch = -1, .cycleable = 0, .desc = "ScreenScraper password"};

	if (logged_in) {
		char quota_str[32] = "—";
		char max_str[32] = "—";
		if (user_info_fetched && cached_user_info.valid) {
			snprintf(quota_str, sizeof(quota_str), "%d", cached_user_info.requests_today);
			snprintf(max_str, sizeof(max_str), "%d", cached_user_info.max_requests_per_day);
		}

		UISettingsItem items[] = {
			pref_items[0],
			pref_items[1],
			pref_items[2],
			pref_items[3],
			reset_item,
			user_item,
			pass_item,
			{.label = "Requests Today", .value = quota_str, .swatch = -1, .cycleable = 0, .desc = "API requests used today"},
			{.label = "Max Requests", .value = max_str, .swatch = -1, .cycleable = 0, .desc = "Daily request limit"},
			{.label = "Logout", .value = NULL, .swatch = -1, .cycleable = 0, .desc = "Clear saved credentials"},
		};

		int count = sizeof(items) / sizeof(items[0]);
		UI_renderSettingsPage(screen, &layout, items, count,
							  settings_selected, &settings_scroll, NULL);
	} else {
		UISettingsItem items[] = {
			pref_items[0],
			pref_items[1],
			pref_items[2],
			pref_items[3],
			reset_item,
			user_item,
			pass_item,
		};

		int count = sizeof(items) / sizeof(items[0]);
		UI_renderSettingsPage(screen, &layout, items, count,
							  settings_selected, &settings_scroll, NULL);
	}

	UI_renderButtonHintBar(screen, (char*[]){"B", "BACK", "A", "SELECT", NULL});
	GFX_flip(screen);
}

// ============================================
// Action Helpers
// ============================================

// Fetch and cache ScreenScraper account info when online.
static void fetchUserInfoIfOnline(void) {
	if (ScraperAPI_isOnline()) {
		UI_renderLoadingOverlay(screen, "Loading", "Fetching account info...");
		GFX_flip(screen);
		cached_user_info = ScraperAPI_fetchUserInfo();
		user_info_fetched = true;
	}
}

// Prompt for a credential field via the on-screen keyboard, persist it,
// and refresh account info if both credentials are now set.
static void editCredentialField(const char* prompt, char* field, size_t field_size) {
	UIKeyboard_init();
	char* input = UIKeyboard_open(prompt);
	PAD_poll();
	PAD_reset();
	if (input) {
		snprintf(field, field_size, "%s", input);
		free(input);
		saveCredentials();
		user_info_fetched = false;
	}
	// Auto-fetch user info if both credentials are now set
	if (cred_username[0] && cred_password[0] && !user_info_fetched) {
		fetchUserInfoIfOnline();
	}
}

// Show the "no network" overlay for a fixed dwell.
static void showNoNetworkOverlay(void) {
	UI_renderLoadingOverlay(screen, "No Network", "Connect to WiFi first");
	GFX_flip(screen);
	SDL_Delay(1500);
}

// Report how many ROMs were queued (or that nothing needed queuing).
static void reportQueued(int added) {
	if (added > 0) {
		char msg[64];
		snprintf(msg, sizeof(msg), "Queued %d ROMs", added);
		UI_renderLoadingOverlay(screen, "Queued", msg);
	} else {
		UI_renderLoadingOverlay(screen, "Nothing to queue",
								"All ROMs already queued or scraped");
	}
	GFX_flip(screen);
	SDL_Delay(1000);
}

// Confirm-and-delete all downloaded artwork. Refuses while the scrape queue is
// still working; on confirm, deletes the .media variant folders' PNGs (the root
// .media pictures are the user's own and stay), drops stale "Done" rows,
// and refreshes the cached system/ROM counts so the UI reflects the reset.
static void resetArtworkFlow(void) {
	if (queueIsBusy()) {
		UI_renderLoadingOverlay(screen, "Scrape in progress",
								"Wait for the queue to finish");
		GFX_flip(screen);
		SDL_Delay(1500);
		return;
	}

	if (!UI_confirmModal(screen, "Reset artwork?",
						 "Deletes downloaded images only",
						 &app_quit, false, true))
		return;

	int deleted = deleteAllArtwork();
	queueDropFinished(); // the done count and failures describe art that is gone
	scanSystems();		 // refresh scraped_count badges
	// Reset the loaded ROM list's has_artwork flags too, if one is in memory.
	if (rom_count > 0 && systems_view.selected < system_count)
		scanROMs(&systems[systems_view.selected]);

	char msg[64];
	snprintf(msg, sizeof(msg), "Deleted %d files", deleted);
	UI_renderLoadingOverlay(screen, "Reset complete", msg);
	GFX_flip(screen);
	SDL_Delay(1000);
}

// ---- Optimize images ------------------------------------------------------
// Rewrites the downloaded screenshots, box art and 2D box art already on the card as
// 256-colour PNGs (scraper_optimize.c; the root .media pictures are left alone)
// on a worker thread, one file at a time, while this loop draws the progress
// page; B asks the worker to stop after the file in hand (each file is
// replaced by a tmp + rename, so a stop never leaves one half-written).

typedef struct {
	OptimizeList list;
	volatile int index; // file the worker is on
	volatile bool cancel, done;
	int optimized, already, no_gain, failed;
	long long saved;
} OptimizeJob;

static OptimizeJob optimize_job;

static void* optimize_thread_func(void* arg) {
	OptimizeJob* job = arg;
	for (int i = 0; i < job->list.count && !job->cancel; i++) {
		job->index = i;
		long long saved = 0;
		switch (Optimize_file(job->list.paths[i], &saved)) {
		case OPTIMIZE_DONE:
			job->optimized++;
			job->saved += saved;
			break;
		case OPTIMIZE_ALREADY:
			job->already++;
			break;
		case OPTIMIZE_NO_GAIN:
			job->no_gain++;
			break;
		case OPTIMIZE_FAILED:
			job->failed++;
			break;
		}
	}
	job->done = true;
	return NULL;
}

// The system a path belongs to, as the Library names it: the Roms folder name
// with its tag and sort prefix dropped ("1) Game Boy (GB)" -> "Game Boy").
static void optimizeSystemName(const char* path, char* out, int out_size) {
	out[0] = '\0';
	size_t root_len = strlen(ROMS_PATH);
	if (strncmp(path, ROMS_PATH "/", root_len + 1) != 0)
		return;
	const char* folder = path + root_len + 1;
	const char* slash = strchr(folder, '/');
	char dirname[256];
	snprintf(dirname, sizeof(dirname), "%.*s", slash ? (int)(slash - folder) : (int)strlen(folder), folder);
	Scraper_extractDisplayName(dirname, out, out_size);
}

static void renderOptimizeProgress(void) {
	OptimizeJob* job = &optimize_job;
	int total = job->list.count;
	int i = job->index;
	char system[256] = "";
	if (i >= 0 && i < total)
		optimizeSystemName(job->list.paths[i], system, sizeof(system));
	char status[300], detail[64];
	if (job->cancel)
		snprintf(status, sizeof(status), "Stopping...");
	else if (system[0])
		snprintf(status, sizeof(status), "Optimizing %s", system);
	else
		snprintf(status, sizeof(status), "Optimizing images");
	snprintf(detail, sizeof(detail), "%d of %d", i + 1, total);

	GFX_clear(screen);
	UI_renderMenuBar(screen, "Artwork Manager | Optimize images");
	UI_renderDownloadProgress(screen, &(UIDownloadProgress){
										  .status = status,
										  .detail = detail,
										  .progress = total > 0 ? i * 100 / total : 0,
										  .show_bar = true,
									  });
	if (!job->cancel)
		UI_renderButtonHintBar(screen, (char*[]){"B", "CANCEL", NULL});
	GFX_flip(screen);
}

// Confirm, then convert every art PNG under Roms. Refuses while the scrape
// queue is working (the scraper thread writes art into the same folders).
static void optimizeImagesFlow(void) {
	if (queueIsBusy()) {
		UI_renderLoadingOverlay(screen, "Scrape in progress",
								"Wait for the queue to finish");
		GFX_flip(screen);
		SDL_Delay(1500);
		return;
	}

	if (!UI_confirmModal(screen, "Optimize images?",
						 "Shrinks the art files on the card to 256 colours. They look the same "
						 "on screen and take far less space. This can't be undone.",
						 &app_quit, false, true))
		return;

	UI_renderLoadingOverlay(screen, "Optimize images", "Finding art files...");
	GFX_flip(screen);

	OptimizeJob* job = &optimize_job;
	memset(job, 0, sizeof(*job));
	if (!Optimize_collect(ROMS_PATH, &job->list)) {
		UI_renderLoadingOverlay(screen, "Optimize images", "Out of memory");
		GFX_flip(screen);
		SDL_Delay(1500);
		return;
	}
	if (job->list.count == 0) {
		UI_renderLoadingOverlay(screen, "Nothing to optimize", "No art files found");
		GFX_flip(screen);
		SDL_Delay(1500);
		return;
	}

	pthread_t thread;
	if (pthread_create(&thread, NULL, optimize_thread_func, job) != 0) {
		Optimize_freeList(&job->list);
		UI_renderLoadingOverlay(screen, "Optimize images", "Could not start");
		GFX_flip(screen);
		SDL_Delay(1500);
		return;
	}

	// a long job: don't let the device doze off while it runs
	PWR_disableAutosleep();
	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;
	while (!job->done) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (PAD_justPressed(BTN_B) || app_quit)
			job->cancel = true;
		renderOptimizeProgress();
	}
	pthread_join(thread, NULL);
	PWR_enableAutosleep();
	bool cancelled = job->cancel;
	Optimize_freeList(&job->list);
	PAD_poll();
	PAD_reset();
	if (app_quit)
		return;

	char summary[256];
	int skipped = job->already + job->no_gain;
	int n = snprintf(summary, sizeof(summary), "Optimized %d %s, saved %.1f MB. Skipped %d (already optimized or no smaller).",
					 job->optimized, job->optimized == 1 ? "file" : "files",
					 job->saved / (1024.0 * 1024.0), skipped);
	if (job->failed > 0 && n > 0 && n < (int)sizeof(summary))
		snprintf(summary + n, sizeof(summary) - n, " %d could not be read.", job->failed);
	UI_confirmModalHints(screen, cancelled ? "Optimize stopped" : "Optimize complete", summary,
						 (char*[]){"A", "OK", NULL}, &app_quit, false, true);
}

// ============================================
// Main
// ============================================

// scraper.elf --scan: print the Library exactly as the GUI would list it
// (system, tag, support, art/total) and every game it would queue. Headless,
// no video; for bug reports and device tests.
static int run_headless_scan(void) {
	ScraperPrefs_load(&scraper_prefs);
	scanSystems();
	for (int i = 0; i < system_count; i++) {
		SystemEntry* sys = &systems[i];
		printf("system\t%s\t(%s)\t%s\t%d/%d\n", sys->name, sys->tag,
			   sys->supported ? "supported" : "unsupported",
			   sys->scraped_count, sys->rom_count);
		if (!sys->supported)
			continue;
		scanROMs(sys);
		for (int r = 0; r < rom_count; r++) {
			// Trailing column reports variant completeness (issue 4): the same
			// wording the game list shows, or "-" when nothing is scraped yet.
			const char* completeness = romStatusLabel(&roms[r]);
			printf("game\t%s\t%s\t%s\t%s\t%s\n", sys->tag, roms[r].label,
				   roms[r].has_artwork ? "art" : "none", roms[r].path,
				   completeness ? completeness : "-");
		}
	}
	return 0;
}

int main(int argc, char* argv[]) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fetch") == 0)
			return run_headless_fetch(argc, argv);
		if (strcmp(argv[i], "--scan") == 0)
			return run_headless_scan();
	}

	screen = GFX_init(MODE_MAIN);
	UI_showSplashScreen(screen, "Artwork Manager");

	InitSettings();
	PAD_init();
	PWR_init();
	setup_signal_handlers();
	ScraperAPI_init();
	loadCredentials();
	ScraperPrefs_load(&scraper_prefs);

	mkdir_p(TMP_DIR);
	scanSystems();

	// Cold-start the main-menu ListView; selection persists across screen
	// changes thereafter (static view), matching the old module-static index.
	UI_listViewReset(&main_menu_view, MAIN_MENU_COUNT, main_menu_items);

	bool dirty = true;
	IndicatorType show_setting = INDICATOR_NONE;

	while (!app_quit) {
		GFX_startFrame();
		PAD_poll();
		// MENU + SELECT exits (same combo as in-game and the other pak tools).
		bool req_quit = false;
		UI_handleQuitRequest(screen, &req_quit, &dirty, "Exit Artwork Manager?", NULL);
		if (req_quit)
			app_quit = true;

		// Check if background thread made progress
		if (queue_dirty) {
			queue_dirty = false;
			dirty = true;
			refreshFinishedRoms();
		}

		switch (current_screen) {
		case SCREEN_MAIN_MENU: {
			ListViewAction act = UI_listViewHandleInput(&main_menu_view);
			if (act.type == LISTVIEW_BACK) {
				app_quit = true;
				break;
			}
			if (act.type == LISTVIEW_ACTIVATED) {
				switch (act.index) {
				case 0: // Library
					current_screen = SCREEN_SYSTEMS;
					UI_listViewReset(&systems_view, system_count, systems);
					dirty = true;
					break;
				case 1: // Progress
					current_screen = SCREEN_PROGRESS;
					progress_selected = 0;
					progress_scroll = 0;
					dirty = true;
					break;
				case 2: // Optimize images
					optimizeImagesFlow();
					dirty = true;
					break;
				case 3: // Settings
					current_screen = SCREEN_SETTINGS;
					settings_selected = 0;
					settings_scroll = 0;
					settings_mix_on_entry = scraper_prefs.mix;
					// Fetch user info if credentials are set
					if (ScraperAPI_hasUserCredentials() && !user_info_fetched) {
						fetchUserInfoIfOnline();
					}
					dirty = true;
					break;
				}
				break;
			}

			break;
		}
		case SCREEN_SYSTEMS: {
			ListViewAction act = UI_listViewHandleInput(&systems_view);
			if (act.type == LISTVIEW_BACK) {
				// Long system names marquee on LAYER_SCROLLTEXT; the band
				// persists across screens unless cleared on the way out.
				GFX_clearLayers(LAYER_SCROLLTEXT);
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}
			if (act.type == LISTVIEW_ACTIVATED) {
				GFX_clearLayers(LAYER_SCROLLTEXT);
				SystemEntry* sys = &systems[act.index];
				if (!sys->supported) {
					char msg[128];
					snprintf(msg, sizeof(msg), "ScreenScraper has no system for the (%s) tag", sys->tag);
					UI_renderLoadingOverlay(screen, "Unsupported system", msg);
					GFX_flip(screen);
					SDL_Delay(1500);
					dirty = true;
					break;
				}
				scanROMs(sys);
				current_screen = SCREEN_ROMS;
				dirty = true;
				break;
			}
			if (act.type == LISTVIEW_BUTTON && act.btn == BTN_Y &&
				system_count > 0 && act.index >= 0) {
				if (!ScraperAPI_isOnline()) {
					showNoNetworkOverlay();
					dirty = true;
					break;
				}
				reportQueued(queueAddAllSystems());
				dirty = true;
				break;
			}

			break;
		}
		case SCREEN_ROMS: {
			if (PAD_justPressed(BTN_B)) {
				// Refresh system counts before going back
				systems[systems_view.selected].scraped_count =
					countGames(systems[systems_view.selected].path).scraped;
				current_screen = SCREEN_SYSTEMS;
				dirty = true;
				break;
			}

			if (PAD_navigateMenu(&rom_selected, rom_count))
				dirty = true;

			if (PAD_justPressed(BTN_A) && rom_count > 0) {
				if (!ScraperAPI_isOnline()) {
					showNoNetworkOverlay();
					dirty = true;
					break;
				}
				ROMEntry* rom = &roms[rom_selected];
				if (isROMQueued(rom->path)) {
					UI_renderLoadingOverlay(screen, "Already queued",
											"ROM is already in the queue");
					GFX_flip(screen);
					SDL_Delay(800);
				} else {
					queueAddROM(rom, &systems[systems_view.selected], true);
					UI_renderLoadingOverlay(screen, "Queued",
											rom->filename);
					GFX_flip(screen);
					SDL_Delay(500);
				}
				dirty = true;
				break;
			}

			if (PAD_justPressed(BTN_Y) && rom_count > 0) {
				if (!ScraperAPI_isOnline()) {
					showNoNetworkOverlay();
					dirty = true;
					break;
				}
				reportQueued(queueAddAllROMs(&systems[systems_view.selected]));
				dirty = true;
				break;
			}

			break;
		}
		case SCREEN_PROGRESS: {
			if (PAD_justPressed(BTN_B)) {
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}

			pthread_mutex_lock(&queue_mutex);
			int count = scrape_queue.count;
			pthread_mutex_unlock(&queue_mutex);
			if (count > 0 && PAD_navigateMenu(&progress_selected, count))
				dirty = true;

			break;
		}
		case SCREEN_SETTINGS: {
			bool logged_in = cred_username[0] && cred_password[0];
			int settings_count = PREF_ROWS + (logged_in ? 6 : 3);

			if (PAD_justPressed(BTN_B)) {
				// Generate mix changes which games count as scraped:
				// refresh the counts the same way resetArtworkFlow does.
				if (scraper_prefs.mix != settings_mix_on_entry) {
					scanSystems();
					if (rom_count > 0 && systems_view.selected < system_count)
						scanROMs(&systems[systems_view.selected]);
				}
				current_screen = SCREEN_MAIN_MENU;
				dirty = true;
				break;
			}

			if (PAD_navigateMenu(&settings_selected, settings_count))
				dirty = true;

			// Rows 1-3 toggle on left, right or A; Wheel is locked on while
			// Generate mix needs it.
			if (settings_selected >= 1 && settings_selected < PREF_ROWS &&
				(PAD_justPressed(BTN_LEFT) || PAD_justPressed(BTN_RIGHT) ||
				 PAD_justPressed(BTN_A))) {
				if (settings_selected == 2 && scraper_prefs.mix)
					break;
				if (settings_selected == 1)
					scraper_prefs.box2d = !scraper_prefs.box2d;
				else if (settings_selected == 2)
					scraper_prefs.wheel = !scraper_prefs.wheel;
				else
					scraper_prefs.mix = !scraper_prefs.mix;
				ScraperPrefs_save(&scraper_prefs);
				dirty = true;
				break;
			}

			if (PAD_justPressed(BTN_A)) {
				switch (settings_selected) {
				case PREF_ROWS + 0: { // Reset artwork
					resetArtworkFlow();
					dirty = true;
					break;
				}
				case PREF_ROWS + 1: { // Username
					editCredentialField("ScreenScraper Username",
										cred_username, sizeof(cred_username));
					dirty = true;
					break;
				}
				case PREF_ROWS + 2: { // Password
					editCredentialField("ScreenScraper Password",
										cred_password, sizeof(cred_password));
					dirty = true;
					break;
				}
				case PREF_ROWS + 5: { // Logout (only reachable when logged in)
					cred_username[0] = '\0';
					cred_password[0] = '\0';
					saveCredentials();
					user_info_fetched = false;
					cached_user_info = (ScraperUserInfo){0};
					// land on Reset artwork (the Logout row is gone); the renderer keeps it in view
					settings_selected = PREF_ROWS;
					settings_scroll = 0;
					dirty = true;
					break;
				}
				}
				break;
			}

			break;
		}
		}

		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (UI_statusBarChanged())
			dirty = true;
		// Selection-pill glides advance frame-by-frame, so keep redrawing
		// while the active screen's ListView pill is still travelling.
		if ((current_screen == SCREEN_MAIN_MENU &&
			 UI_listViewBusy(&main_menu_view)) ||
			(current_screen == SCREEN_SYSTEMS &&
			 UI_listViewBusy(&systems_view)))
			dirty = true;

		if (dirty) {
			switch (current_screen) {
			case SCREEN_MAIN_MENU:
				renderMainMenu();
				break;
			case SCREEN_SYSTEMS:
				renderSystemList();
				break;
			case SCREEN_ROMS:
				renderROMList();
				break;
			case SCREEN_PROGRESS:
				renderProgress();
				break;
			case SCREEN_SETTINGS:
				renderSettings();
				break;
			}
			dirty = false;
		} else {
			if (current_screen == SCREEN_MAIN_MENU)
				UI_listViewTickIdle(&main_menu_view);
			else if (current_screen == SCREEN_SYSTEMS)
				UI_listViewTickIdle(&systems_view);
			GFX_sync();
		}
	}

	// Cleanup: stop background thread
	if (scraper_thread_started) {
		scraper_running = false;
		pthread_join(scraper_thread, NULL);
	}
	ScrapeQueue_free(&scrape_queue);
	free(roms);

	// Remove temp directory
	system("rm -rf " TMP_DIR);

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();

	return EXIT_SUCCESS;
}
