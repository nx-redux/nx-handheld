// UI_scrollFadeKeep (common/ui/ui_scroll_fade.h): the List's and Minimal's edge fades.
#include "../ui/ui_scroll_fade.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
	// no band, or from the band's inner end in: the row as it is
	assert(UI_scrollFadeKeep(0, 0, true) == 255 && UI_scrollFadeKeep(5, -3, false) == 255);
	assert(UI_scrollFadeKeep(45, 45, true) == 255 && UI_scrollFadeKeep(400, 45, false) == 255);
	// past the outer edge: nothing
	assert(UI_scrollFadeKeep(-1, 45, true) == 0);
	// rising from the edge, never past 255, strong never above linear
	for (int h = 1; h <= 128; h++) {
		int last_s = -1, last_l = -1;
		for (int d = 0; d < h; d++) {
			int ks = UI_scrollFadeKeep(d, h, true), kl = UI_scrollFadeKeep(d, h, false);
			assert(ks >= last_s && kl >= last_l && ks <= kl && kl <= 255 && ks >= 0);
			last_s = ks, last_l = kl;
		}
	}
	// sampled at the row centres: the first of 10 rows keeps 5% (linear) or 0.25% (strong, rounds to 1); half way
	// (row 4.5 of 10 rows, centre 0.5) a half and a quarter
	assert(UI_scrollFadeKeep(0, 10, false) == 13 && UI_scrollFadeKeep(0, 10, true) == 1);
	assert(UI_scrollFadeKeep(1, 4, false) == 96 && UI_scrollFadeKeep(1, 4, true) == 36); // t = 0.375
	assert(UI_scrollFadeKeep(3, 4, true) == 195);										 // t = 0.875
	printf("test_scroll_fade: ok\n");
	return 0;
}
