#include "ui_buttonhintbar.h"
#include "ui_hintbar_layout.h"
#include "ui_draw.h"
#include "api.h"
#include "defines.h"

int UI_renderButtonHintBar(SDL_Surface* dst, char** pairs) {
	return UI_renderButtonHintBarEx(dst, pairs, 204);
}

int UI_renderButtonHintBarEx(SDL_Surface* dst, char** pairs, Uint8 scrim_alpha) {
	IndicatorType show_setting = PWR_getShowSetting();
	char** hw_pairs = show_setting ? GFX_getHardwareHintPairs(show_setting) : NULL;

	struct Hint {
		char* hint;
		char* button;
		int ow;
	};

#define HINT_CANDIDATES 8
	struct Hint hints[HINT_CANDIDATES];
	const char* buttons[HINT_CANDIDATES];
	int keep[HINT_CANDIDATES];
	int count = 0;
	int total_w = 0;

	// Parse hardware hints first (always kept), then caller pairs
	char** groups[] = {hw_pairs, pairs};
	for (int g = 0; g < 2; g++) {
		if (!groups[g])
			continue;
		for (int i = 0; groups[g][i * 2] && count < HINT_CANDIDATES; i++) {
			char* button = groups[g][i * 2];
			char* hint = groups[g][i * 2 + 1];
			if (!hint)
				break;
			int w = GFX_getButtonWidth(hint, button);
			buttons[count] = button;
			keep[count] = g == 0;
			hints[count++] = (struct Hint){hint, button, w};
			total_w += SCALE1(BUTTON_MARGIN) + w;
		}
	}

	if (count == 0)
		return 0;
	total_w += SCALE1(BUTTON_MARGIN);

	// Over UI_HINTBAR_MAX, or wider than the screen: the least needed go first (UI_hintDropRank: START, SELECT,
	// MENU, Y, X, ...), never A, B or a hardware hint
	int room = dst->w - 2 * SCALE1(PADDING);
	while (count > UI_HINTBAR_MAX || total_w > room) {
		int d = UI_hintDropPick(buttons, keep, count);
		if (d < 0)
			break;
		total_w -= SCALE1(BUTTON_MARGIN) + hints[d].ow;
		for (int k = d; k + 1 < count; k++)
			hints[k] = hints[k + 1], buttons[k] = buttons[k + 1], keep[k] = keep[k + 1];
		count--;
	}

	// Full-width semi-transparent black bar (keep in step with UI_buttonHintIconTop): BAR_HEIGHT (the space every screen
	// reserves), the hints in it centred
	int btn_sz = SCALE1(BUTTON_SIZE);
	int bar_h = BAR_HEIGHT;
	int oy = dst->h - bar_h;

	// one cached scrim per alpha (the cache is keyed on size only)
	static SDL_Surface* bars[2] = {NULL, NULL};
	static Uint8 bar_alpha[2] = {0, 0};
	int slot = (bars[0] && bar_alpha[0] != scrim_alpha) ? 1 : 0;
	if (bars[slot] && bar_alpha[slot] != scrim_alpha) { // a third alpha: evict slot 1
		SDL_FreeSurface(bars[slot]);
		bars[slot] = NULL;
	}
	SDL_Surface* button_bar = UI_getScrimAlpha(&bars[slot], dst->w, bar_h, scrim_alpha);
	if (!button_bar)
		return 0;
	bar_alpha[slot] = scrim_alpha;
	SDL_BlitSurface(button_bar, NULL, dst, &(SDL_Rect){0, oy});

	// Render all buttons from the left; the first glyph is pulled left so its
	// circle (inset 16/128 of the glyph) sits on the old text edge
	int by = oy + UI_hintBarIconOffset(bar_h, btn_sz);
	int ox = SCALE1(PADDING) + SCALE1(BUTTON_MARGIN) - (btn_sz * 16 + 64) / 128;
	for (int i = 0; i < count; i++) {
		GFX_blitButton(hints[i].hint, hints[i].button, dst, &(SDL_Rect){ox, by});
		ox += hints[i].ow + SCALE1(BUTTON_MARGIN);
	}

	return total_w;
}

int UI_buttonHintIconTop(int screen_h) {
	int btn_sz = SCALE1(BUTTON_SIZE);
	int bar_h = BAR_HEIGHT;
	return screen_h - bar_h + UI_hintBarIconOffset(bar_h, btn_sz);
}

int UI_buttonHintBarTop(int screen_h) {
	return screen_h - BAR_HEIGHT;
}
