#ifndef __UI_TABS_H__
#define __UI_TABS_H__

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "ui_listview.h"

typedef enum {
	MEDIA_TAB_LIBRARY = 0,
	MEDIA_TAB_IPTV = 1,
	MEDIA_TAB_COUNT = 2
} MediaTab;

// True when Settings > Appearance > Layouts selects Minimal for Tools or Game lists.
bool MediaUI_isMinimal(void);

// True when Layouts > Bottom bar (buttonHints) is enabled.
bool MediaUI_tabRowShown(void);

// True while the Minimal list's selection slide tween is still animating.
bool MediaUI_animBusy(void);

// Render the bottom tab bar ("LIBRARY   ONLINE TV" + right-aligned button hints).
void MediaUI_renderTabRow(SDL_Surface* screen, MediaTab active, char** right_hints);

// Unified page renderer: renders Minimal typography when Minimal layout is active,
// or ListView pills otherwise, with the bottom tab bar on root pages.
void MediaUI_renderPage(SDL_Surface* screen, ListView* v,
						const char* full_title, const char* sub_title,
						bool is_root, MediaTab active_tab, char** hints);

// Free cached surfaces on app exit.
void MediaUI_cleanup(void);

#endif
