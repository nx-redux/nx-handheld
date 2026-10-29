#ifndef HOME_LIST_LAYOUT_H
#define HOME_LIST_LAYOUT_H

#include <stdbool.h>

// Home's List layout (Layouts > Home layout: List; the mockup's buildHomeList, nx-showcase.html): the stats strip on
// left-aligned lines under the tab row, then one pill list in the main menu's List style: Continue, the pinned games
// in pin order, the pinned tools. What the rows are and where the strip and the list start; the rows themselves are
// gamelist.c's List. Pure, host-tested (common/tests/test_home_list_layout.c). Units are screen px.

typedef enum { HOMELIST_CONTINUE,
			   HOMELIST_GAME,
			   HOMELIST_TOOL } HomeListKind;

typedef struct {
	HomeListKind kind;
	int ref; // the game's or the tool's index (0 for Continue)
} HomeListItem;

// The rows: Continue, then ngames pinned games (Continue's own excluded by the caller), then ntools tools.
int HomeList_count(int ngames, int ntools);
// Row i (clamped into the list) as its kind and index.
HomeListItem HomeList_item(int i, int ngames, int ntools);
// A selection kept on a row after a rebuild: the last row when the list got shorter, never below 0.
int HomeList_clampSel(int sel, int ngames, int ntools);

// What HomeList_compute takes beyond the strip's line count.
typedef struct {
	int bar;			// the tab row's height (its bottom)
	int strip_gap;		// between the tab row and the strip's first line's top (the mockup's 8 dp)
	int list_gap;		// between the strip's last line's bottom and the first row (the mockup's 12 dp)
	int list_top_plain; // the first row's top with no strip (the main menu List's: the bar + 12 dp)
	int asc, desc;		// the strip font's ascent and descent (px, desc >= 0)
	int line_step;		// between the strip's baselines
} HomeListOpts;

typedef struct {
	int strip_lines;   // 0 (fresh, or Layouts > Top bar hidden), 1 or 2
	int strip_base[2]; // the lines' baselines
	int list_top;	   // the first row's top
} HomeListGeom;

// Top to bottom: the tab row, strip_gap, the strip's lines (each line_step under the last), list_gap, the rows. No
// strip: the rows at list_top_plain, as the main menu's List.
void HomeList_compute(int strip_lines, const HomeListOpts* o, HomeListGeom* out);

// The "Continue" tag before Continue's name (the mockup's .ptag: 12 sp text on a 16 sp line, 2 x 8 dp padding, 10 dp
// before the name), from its text's height th: its padding and the gap to the name, and its whole width for a text
// text_w wide (the room the row's text gives it: the tag, then the gap).
typedef struct {
	int pad_x, pad_y, gap;
	int w, h; // the tag's own size
} HomeListTag;
HomeListTag HomeList_tag(int th, int text_w);

// Home's Minimal (Layouts > Home layout: Minimal; nx-mobile's HomeMinimalScreen) draws the same rows as big names. A
// name's opacity `distance` names from the shown position (negative above it), in steps: below 0.8, 0.6, 0.5, then
// 0.42 on to the screen's edge; above fainter, 0.6, 0.48, then 0.4; the selection 1. Linear between the steps, so it
// stays smooth while the names slide.
float HomeMin_alpha(float distance);

// The Minimal's glide (Home's and the tabs' and game lists'): a held UP/DOWN, from its first repeat, moves the names
// at a speed rising from the repeat's (MIN_GLIDE_V0 names/s) to MIN_GLIDE_MAX over MIN_GLIDE_RAMP_MS; let go, they ease
// out (HomeMin_easeOut) into the next name the glide was heading for, never coasting past it, the ease starting at the
// glide's speed (MIN_SETTLE_MIN_MS to MIN_SETTLE_MAX_MS long).
#define MIN_GLIDE_V0 10.0f // PAD_REPEAT_INTERVAL's rate
#define MIN_GLIDE_MAX 24.0f
#define MIN_GLIDE_RAMP_MS 1200
#define MIN_SETTLE_MIN_MS 60
#define MIN_SETTLE_MAX_MS 300 // a one-name slide's
typedef struct {
	int dir;   // +1 down, -1 up; 0 not gliding
	float pos; // the shown position (a name's index)
	float v;   // names/s
	unsigned start, last;
} HomeMinGlide;
void HomeMin_glideStart(HomeMinGlide* g, int dir, float pos, unsigned now);
// A frame at `now`: on at the speed reached, clamped to the n names. Returns the position.
float HomeMin_glideStep(HomeMinGlide* g, unsigned now, int n);
// Let go: the name it settles on (the next whole one ahead, within the n) and the ease's length (*ms); the glide ends.
int HomeMin_glideSettle(HomeMinGlide* g, int n, unsigned* ms);
// Cubic ease out (its start speed 3 x the distance / its length).
float HomeMin_easeOut(float t);

#endif
