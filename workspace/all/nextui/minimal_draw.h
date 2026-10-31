#ifndef MINIMAL_DRAW_H
#define MINIMAL_DRAW_H

// The Minimal's look (nx-mobile's HomeMinimalScreen), shared by Home's (home.c, Layouts > Home layout: Minimal) and the
// tabs' and game lists' (minview.c, their Layouts rows: Minimal): big names wrapped at their words, the selected one
// solid in the accent and the others its outline fading with their distance (HomeMin_alpha), drawn as GPU sprites; A
// grows the selected name's band over the screen before the item opens.

#include "sdl.h"
#include <stdbool.h>

#define MIN_NAME_DP 38.0f	   // the names' size (nx-mobile's 38 dp)
#define MIN_MAX_LINES 3		   // a long name wraps at its words onto at most this many lines
#define MIN_ITEM_GAP_DP 8.0f   // between two names (a name's own lines stay a pitch apart)
#define MIN_OUTLINE_ALPHA 0.6f // an unselected name's outline: this share of the accent
#define MIN_LEAVE_MS 120	   // A: the other names fade out first (a launch, or a list opening),
#define MIN_OPEN_GROW_MS 350   // then a launch's band grows from the selected name over the screen as it goes black
#define MIN_BAND_TINT 0.28f	   // the band: this share of the accent over black (a White accent would flash)
#define MIN_LINE_MAX 256	   // a wrapped line's buffer
// The names fade out over this at the body's top and bottom edges while more lie past them (nx-mobile's
// MINIMAL_EDGE_MASK, linear; scroll_fade.h): the Minimal's scroll cue, as the List's rows fade.
#define MIN_EDGE_FADE_DP 24.0f

// The names' font and spacing at the current scale.
typedef struct {
	TTF_Font* font;
	int font_h;	 // the font's height
	int pitch;	 // a line's step (the font's size and 2 dp)
	int outline; // the outline's width: a 1 px hairline at every scale
	int gap;	 // between two names
} MinMetrics;

// False when the font isn't there.
bool MinDraw_metrics(MinMetrics* m);

int MinDraw_textW(TTF_Font* f, const char* text);
// `in` cut to max_w with "..." (GFX_truncateText's, as the game lists' cut text) (out: MIN_LINE_MAX). Empty when
// nothing fits.
void MinDraw_ellipsize(TTF_Font* f, const char* in, char* out, int max_w);
// Greedy word wrap into at most max_lines lines; the last line takes the rest, ellipsized. Returns the count.
int MinDraw_wrap(TTF_Font* f, const char* text, int max_w, int max_lines, char lines[][MIN_LINE_MAX]);

// Lines drawn into a surface sized for them (straight alpha: its ground is c at alpha 0, so the blended edges keep c),
// each a pitch under the last; outlined with the font's outline set to m->outline (its glyphs then sit that far up and
// left, so the solid's are drawn that far in to match). w: the lines' room.
SDL_Surface* MinDraw_lines(const MinMetrics* m, char lines[][MIN_LINE_MAX], int n, int w, SDL_Color c, bool outlined);

// s into r at alpha and grey level shade: a GPU sprite clipped to clip
// when sprite (the CPU blending a dozen big names a frame held the Brick near 10 fps), else blended into dst (clipped
// by its clip rect). r's w, h: the size to draw at (0: s's own).
void MinDraw_blit(SDL_Surface* s, SDL_Rect r, float alpha, const SDL_Rect* clip, Uint8 shade, bool sprite,
				  SDL_Surface* dst);

// A fade to black at the screen's top (top) or bottom edge, clear at its inner end, w x h; made again only when its
// size changes.
typedef struct {
	SDL_Surface* s;
	int w, h;
} MinFade;
SDL_Surface* MinFade_get(MinFade* f, bool top, int w, int h);
void MinFade_free(MinFade* f);

// The launch: A on a game or a tool fades the other names out (MinOpen_othersAlpha, for the list's own draw), then
// a dark tint of the accent grows from the name's rows over the screen while it goes black, the name (in the
// accent, as it was drawn) staying where it was and fading with it; then the item opens.
typedef struct {
	bool active;
	Uint32 start;
	SDL_Rect from;	   // the name's rows when A was pressed
	SDL_Surface* name; // its lines in the accent
	SDL_Surface* band; // the band's colour (one pixel the GPU stretches),
	Uint32 band_rgb;   // and the colour it was made for
} MinOpen;
// Starts the band on the name's lines, from its rows at `from`; false with Menu animations off (the item opens
// straight away).
bool MinOpen_start(MinOpen* o, const MinMetrics* m, char lines[][MIN_LINE_MAX], int n, SDL_Rect from);
// The band has gone dark: time to open the item.
bool MinOpen_done(const MinOpen* o);
// What the names other than the selected one keep of their alpha: fading out over MIN_LEAVE_MS while the band runs,
// else 1.
float MinOpen_othersAlpha(const MinOpen* o);
void MinOpen_draw(MinOpen* o, const MinMetrics* m, bool sprite, SDL_Surface* dst);
void MinOpen_free(MinOpen* o);

#endif // MINIMAL_DRAW_H
