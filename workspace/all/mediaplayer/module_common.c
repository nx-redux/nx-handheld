#include <signal.h>
#include <string.h>
#include <msettings.h>
#include "api.h"
#include "audio_manager.h"
#include "module_common.h"
#include "indicator_autohide.h"
#include "ui_menubar.h"
#include "ui_quitrequest.h"
#include "ui_contextmenu.h"

static bool autosleep_disabled = false;

void ModuleCommon_init(void) {
	autosleep_disabled = false;
	IndicatorAutoHide_reset();
}

static volatile sig_atomic_t quit_requested = 0;

void ModuleCommon_requestQuit(void) {
	quit_requested = 1;
}

bool ModuleCommon_quitRequested(void) {
	return quit_requested != 0;
}

GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, IndicatorType* show_setting,
												 const ContextMenuItem* menu_items, int menu_count) {
	GlobalInputResult result = {false, false, false, 0};

	// SIGTERM/SIGINT: leave through the module's normal quit path
	if (quit_requested) {
		result.should_quit = true;
		result.input_consumed = true;
		return result;
	}

	// Quit on MENU + SELECT via the shared confirm dialog — the same combo the
	// pak tools and in-game minarch use. UI_handleQuitRequest runs its own
	// blocking A/B dialog and only fires on the chord, so it's a no-op most
	// frames.
	{
		bool want_quit = false, dlg_dirty = false;
		UI_handleQuitRequest(screen, &want_quit, &dlg_dirty, "Quit Media Player?", NULL);
		if (want_quit) {
			result.should_quit = true;
			result.input_consumed = true;
			return result;
		}
		if (dlg_dirty) {
			result.dirty = true;
			result.input_consumed = true;
			return result;
		}
	}

	// Context menu (opened on a MENU tap below): consumes all input while open.
	if (ContextMenu_isOpen()) {
		// Keep the MENU-tap state machine ticking so it doesn't fire on stale
		// state after close.
		PAD_tappedMenu(SDL_GetTicks());

		ContextMenuResult cmr = ContextMenu_handleInput();
		if (cmr.action == CONTEXTMENU_SELECTED) {
			if (cmr.id == CTX_ID_QUIT) {
				result.should_quit = true;
			} else {
				result.context_id = cmr.id;
			}
			result.dirty = true;
		} else if (cmr.action == CONTEXTMENU_CANCEL) {
			result.dirty = true;
		} else if (PAD_justRepeated(BTN_UP) || PAD_justRepeated(BTN_DOWN)) {
			// Selection moved: redraw the overlay (main surface is untouched)
			ContextMenu_render(screen);
			PLAT_GPU_Flip();
		}
		result.input_consumed = true;
		return result;
	}

	// MENU tap opens the current page's context menu. A long MENU press is the
	// OSD (handled globally by keymon); PAD_tappedMenu only fires on a short
	// release, so the two don't collide.
	if (PAD_tappedMenu(SDL_GetTicks())) {
		if (menu_count > 0) {
			ContextMenu_open((ContextMenuItem*)menu_items, menu_count);
			// Clear the marquee layer so it doesn't float above the menu's dim
			// backdrop; it re-renders after close.
			GFX_clearLayers(LAYER_SCROLLTEXT);
			ContextMenu_render(screen);
			PLAT_GPU_Flip();
		}
		result.input_consumed = true;
		result.dirty = true;
		return result;
	}

	// Handle power management
	{
		bool dirty_before = result.dirty ? true : false;
		bool dirty_tmp = dirty_before;
		PWR_update(&dirty_tmp, show_setting, NULL, NULL);

		if (dirty_tmp && !dirty_before) {
			result.dirty = true;
		}
	}

	return result;
}

void ModuleCommon_setAutosleepDisabled(bool disabled) {
	if (disabled && !autosleep_disabled) {
		PWR_disableAutosleep();
		autosleep_disabled = true;
	} else if (!disabled && autosleep_disabled) {
		PWR_enableAutosleep();
		autosleep_disabled = false;
	}
}

void ModuleCommon_quit(void) {
	// Ensure autosleep is re-enabled
	if (autosleep_disabled) {
		PWR_enableAutosleep();
		autosleep_disabled = false;
	}

	// Clear GPU scroll text layer
	GFX_clearLayers(LAYER_SCROLLTEXT);
}

void ModuleCommon_PWR_update(bool* dirty, IndicatorType* show_setting) {
	// Poll for audio device changes (keeps AudioManager state current)
	AudioMgr_pollEvents();
	IndicatorAutoHide_pwrUpdate(dirty, show_setting);
}
