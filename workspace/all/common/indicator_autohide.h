#ifndef INDICATOR_AUTOHIDE_H
#define INDICATOR_AUTOHIDE_H

#include <stdbool.h>
#include <stdint.h>

// The volume/brightness/colour-temp indicator stays up for
// INDICATOR_VISIBLE_AFTER_RELEASE_MS after its buttons are released, then is
// forced hidden for INDICATOR_FORCE_HIDE_MS (PWR_update would otherwise keep it
// on screen in the apps that only redraw on input).
#define INDICATOR_VISIBLE_AFTER_RELEASE_MS 800
#define INDICATOR_FORCE_HIDE_MS 500

typedef struct {
	bool buttons_were_active;
	uint32_t release_time; // 0 = not counting
} IndicatorAutoHide;

// Pure model (host-tested). Call _before with the buttons' state ahead of
// PWR_update, then _after; _after returns true while the indicator must be
// forced hidden (and the screen redrawn).
static inline void IndicatorAutoHide_before(IndicatorAutoHide* s, bool buttons_active, uint32_t now) {
	if (s->buttons_were_active && !buttons_active)
		s->release_time = now; // buttons just released: start the timer
}

static inline bool IndicatorAutoHide_after(IndicatorAutoHide* s, bool buttons_active, uint32_t now) {
	bool hide = false;
	if (s->release_time > 0) {
		uint32_t elapsed = now - s->release_time;
		if (elapsed >= INDICATOR_VISIBLE_AFTER_RELEASE_MS) {
			hide = true;
			if (elapsed >= INDICATOR_VISIBLE_AFTER_RELEASE_MS + INDICATOR_FORCE_HIDE_MS)
				s->release_time = 0;
		}
	}
	s->buttons_were_active = buttons_active;
	return hide;
}

#ifndef INDICATOR_AUTOHIDE_MODEL_ONLY
#include "api.h"

// Forget any pending release (module init).
void IndicatorAutoHide_reset(void);

// PWR_update for the Media/Music Player pages: also redraws when the menu-bar
// Wi-Fi/BT status changes, and force-hides the indicator after its buttons
// (SELECT = brightness, START = colour temp, PLUS/MINUS = volume) are released.
void IndicatorAutoHide_pwrUpdate(bool* dirty, IndicatorType* show_setting);
#endif

#endif // INDICATOR_AUTOHIDE_H
