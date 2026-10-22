#include "ui_quitrequest.h"
#include "ui_confirmdialog.h"
#include "api.h"
#include "defines.h"

void UI_handleQuitRequest(SDL_Surface* screen, bool* quit, bool* dirty,
						  const char* title, const char* subtitle) {
	// MENU + SELECT requests quit — the same combo minarch uses to exit a game
	// (ma_input.c), reused here to exit an app / pak tool. Edge-triggered on the
	// moment the chord completes so it fires once (not once per held frame) and
	// beats keymon's 500 ms MENU long-press OSD toggle.
	if (!((PAD_isPressed(BTN_MENU) && PAD_justPressed(BTN_SELECT)) ||
		  (PAD_justPressed(BTN_MENU) && PAD_isPressed(BTN_SELECT))))
		return;

	PAD_reset();

	// UI_confirmModal keeps PWR_update running, so auto-sleep and the power
	// button still work while the prompt is up
	if (UI_confirmModal(screen, title, subtitle, NULL, false, true))
		*quit = true;
	*dirty = true;
}
