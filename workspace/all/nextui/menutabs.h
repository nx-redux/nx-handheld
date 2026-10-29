#ifndef MENUTABS_H
#define MENUTABS_H

#include "menutabs_model.h"
#include "sdl.h"
#include "types.h"
#include <stdbool.h>

void MenuTabs_init(void); // compute the visible tabs from settings + content
int MenuTabs_count(void);
MenuTabId MenuTabs_at(int index);
MenuTabId MenuTabs_current(void);
// Only assigns the current tab (pathToStack builds stack[0] itself); drops id's parked root, and moves the
// generation when the tab changes.
void MenuTabs_setCurrent(MenuTabId id);
// openDirectory's stack rebuild (pathToStack) replaces the whole stack: empty it first, freeing the lists over the
// root and parking the root (as a tab switch does) when it is the current tab's plain root. `top` is NULL after.
void MenuTabs_releaseStack(void);
// Something changed what a tab that isn't current lists, without a MenuTabs_reload (a new collection from Add to
// Collection): its parked root is dropped and rebuilt on its next visit. No-op for the current tab.
void MenuTabs_dropCached(MenuTabId id);
// Menu_quit: free the parked roots.
void MenuTabs_quit(void);
bool MenuTabs_isVisible(MenuTabId id);
// The root context menu offers `id` (Consoles, Collections or Tools) as a list pushed over the current tab: the tab is
// hidden, has something to list, and simple mode is off.
bool MenuTabs_pushable(MenuTabId id);
const char* MenuTabs_path(MenuTabId id);			 // the stack[0] path for a tab
MenuTabId MenuTabs_forPathVisible(const char* path); // MenuTabs_forPath, then Home
// The tab's main menu style category (MENU_CAT_* in config.h), or -1 for Home.
int MenuTabs_styleCategory(MenuTabId id);
// Make `id` current with its root at stack[0], replacing the whole stack and setting `top`. The outgoing root is
// parked and id's parked root reused (re-windowed for today's row count); with none parked a fresh Directory is
// built on the tab's remembered selection.
void MenuTabs_openRoot(MenuTabId id);
// L1/R1 at the root: park the current tab's root, move by delta (wrapping), open it.
// Returns true when the tab changed.
bool MenuTabs_step(int delta);
// Something changed what the tabs hold (pin, unpin, delete, rename, refresh); `what` is a mask of MENU_RELOAD_*
// bits naming what may have changed (MenuTabs_reloadPlan). Drops the parked roots of the tabs it can show in,
// re-reads only those inputs, recomputes the visible tabs and rebuilds stack[0] when the change can show in the
// current tab (else keeps it, re-windowed as a rebuild would be); if the current tab vanished, opens the resolved
// neighbour at its root on its remembered row (the lists pushed over the old tab are popped). keep_selected clamps.
// The parked roots of tabs no longer visible are dropped (their hints stay).
void MenuTabs_reload(int keep_selected, unsigned what);
// Bumped whenever MenuTabs_openRoot/MenuTabs_reload replace stack[0] (a tab switch or reload), and when
// MenuTabs_setCurrent changes the tab (openDirectory's stack rebuild landing in another tab). Readers use it
// to tell a tab change from a push/pop; per-list caches key on Directory.serial (which also changes on a
// push, pop or openDirectory stack rebuild) alongside it.
unsigned MenuTabs_generation(void);
// The next MenuTabs_saveState records that the launch came from Home itself (Continue or a pin):
// boot then reopens Home even when the ROM belongs to another tab. Call only right before Entry_open:
// Entry_open clears the mark when it returns, whether or not anything launched.
void MenuTabs_markHomeLaunch(void);
// Drop a pending mark (Entry_open calls this last, so a mark never outlives its own open).
void MenuTabs_clearHomeLaunch(void);
// saveLast() hook: write the current tab key to MENU_TAB_PATH ("home\nlaunch\n" after
// MenuTabs_markHomeLaunch while on Home; "<tab>\n<pushed>\n" from a hidden tab's list (consoles, collections
// or tools) pushed over <tab>).
// Clears the mark.
void MenuTabs_saveState(void);
// MENU_TAB_PATH carries a launch from Home itself (the "launch" line): loadLast then only reselects Home's row.
bool MenuTabs_savedHomeLaunch(void);
// MENU_TAB_PATH's pushed-list line: the launch came from that hidden tab's list pushed over the saved tab (its
// id), else MENU_TAB_HOME.
MenuTabId MenuTabs_savedPush(void);
// The tab to open at boot: MENU_TAB_PATH if visible, else the tab of last_path, else Home.
MenuTabId MenuTabs_initialTab(const char* last_path);
// The tab row, in the bottom bar at the root: the labels (the current one lit, the others dimmed) and the RECENT and
// SEARCH hints. It takes no focus: the tabs switch by L1/R1, and by LEFT/RIGHT where the layout passes them on (the
// List root, Minimal, a vertical stack, the edges of Home and the Carousel).
void MenuTabs_renderRow(SDL_Surface* screen, int ow);
// The root shows the tab row (more than one tab, Layouts > Bottom bar on): it holds the bottom bar, so the root's
// content keeps clear of it and draws no hint bar of its own.
bool MenuTabs_rowShown(void);
// The bottom bar is taken (Layouts > Bottom bar): by the tab row at the root, the hint bar elsewhere. Content that runs
// to the bottom edge without a bar stops above it.
bool MenuTabs_bottomBar(void);

#endif
