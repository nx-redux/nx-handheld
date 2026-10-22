#include "indicator_autohide.h"
#include "ui_menubar.h"

static IndicatorAutoHide autohide;

void IndicatorAutoHide_reset(void) {
	autohide = (IndicatorAutoHide){0};
}

void IndicatorAutoHide_pwrUpdate(bool* dirty, IndicatorType* show_setting) {
	// Checked BEFORE PWR_update. START must be here too or the colour-temp
	// indicator never force-hides after release.
	bool buttons_active = PAD_isPressed(BTN_PLUS) || PAD_isPressed(BTN_MINUS) || PAD_isPressed(BTN_SELECT) ||
						  PAD_isPressed(BTN_START);
	IndicatorAutoHide_before(&autohide, buttons_active, SDL_GetTicks());

	PWR_update(dirty, show_setting, NULL, NULL);

	// Redraw when the menu-bar wifi/bt status changes (the pages only render
	// on input, so without this the wifi icon stays stale until a keypress)
	if (UI_statusBarChanged())
		*dirty = true;

	if (IndicatorAutoHide_after(&autohide, buttons_active, SDL_GetTicks())) {
		*show_setting = INDICATOR_NONE;
		*dirty = true;
	}
}
