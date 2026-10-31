#ifndef UI_SCROLL_FADE_H
#define UI_SCROLL_FADE_H

#include <stdbool.h>

// The launcher lists' "more this way" cue in place of scroll arrows (nx-mobile's nxEdgeFade): while rows lie past an
// edge, the content fades out toward it over a band h px tall, as a DstIn mask would. Pure (no SDL), host-tested
// (common/tests/test_scroll_fade.c); the GPU sprites (PLAT_spriteFadeBegin/End) and the screen's rows
// (nextui/scroll_fade.c) both take their alpha from here.

// What a px row dist rows in from the band's outer edge keeps of its alpha, 0..255: sampled at the row's centre,
// t = (dist + 0.5) / h, kept t (linear: Minimal's names) or, strong, t² (ease-in: a quarter at half the band, more of it
// near clear, as the List's rows; a linear fade over rows reads too faint). 255 from h in (or no band), 0 past the edge.
static inline int UI_scrollFadeKeep(int dist, int h, bool strong) {
	if (h <= 0 || dist >= h)
		return 255;
	if (dist < 0)
		return 0;
	float t = (dist + 0.5f) / h;
	float k = strong ? t * t : t;
	return (int)(k * 255.0f + 0.5f);
}

#endif // UI_SCROLL_FADE_H
