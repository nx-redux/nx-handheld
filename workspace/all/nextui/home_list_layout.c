#include "home_list_layout.h"

int HomeList_count(int ngames, int ntools) {
	return 1 + (ngames > 0 ? ngames : 0) + (ntools > 0 ? ntools : 0);
}

HomeListItem HomeList_item(int i, int ngames, int ntools) {
	if (ngames < 0)
		ngames = 0;
	if (ntools < 0)
		ntools = 0;
	i = HomeList_clampSel(i, ngames, ntools);
	if (i == 0)
		return (HomeListItem){HOMELIST_CONTINUE, 0};
	if (i <= ngames)
		return (HomeListItem){HOMELIST_GAME, i - 1};
	return (HomeListItem){HOMELIST_TOOL, i - 1 - ngames};
}

int HomeList_clampSel(int sel, int ngames, int ntools) {
	int n = HomeList_count(ngames, ntools);
	if (sel >= n)
		sel = n - 1;
	return sel < 0 ? 0 : sel;
}

void HomeList_compute(int strip_lines, const HomeListOpts* o, HomeListGeom* out) {
	int lines = strip_lines < 0 ? 0 : strip_lines > 2 ? 2
													  : strip_lines;
	out->strip_lines = lines;
	out->strip_base[0] = out->strip_base[1] = 0;
	if (lines == 0) {
		out->list_top = o->list_top_plain;
		return;
	}
	out->strip_base[0] = o->bar + o->strip_gap + o->asc;
	out->strip_base[1] = out->strip_base[0] + o->line_step;
	out->list_top = out->strip_base[lines - 1] + o->desc + o->list_gap;
}

static const float MIN_BELOW[] = {1.0f, 0.5f, 0.4f, 0.28f, 0.2f};
static const float MIN_ABOVE[] = {1.0f, 0.3f, 0.22f, 0.2f};

float HomeMin_alpha(float distance) {
	const float* steps = distance < 0 ? MIN_ABOVE : MIN_BELOW;
	int last = distance < 0 ? (int)(sizeof(MIN_ABOVE) / sizeof(MIN_ABOVE[0])) - 1
							: (int)(sizeof(MIN_BELOW) / sizeof(MIN_BELOW[0])) - 1;
	float d = distance < 0 ? -distance : distance;
	if (d >= last)
		return steps[last];
	int i = (int)d;
	return steps[i] + (steps[i + 1] - steps[i]) * (d - i);
}

// 2 / 16 and 8 / 16 of the tag's line, its gap 10 / 16 of it (the mockup's px on its 16 px line)
HomeListTag HomeList_tag(int th, int text_w) {
	HomeListTag t;
	t.pad_y = (th * 2 + 8) / 16;
	t.pad_x = (th * 8 + 8) / 16;
	t.gap = (th * 10 + 8) / 16;
	t.w = text_w + 2 * t.pad_x;
	t.h = th + 2 * t.pad_y;
	return t;
}

void HomeMin_glideStart(HomeMinGlide* g, int dir, float pos, unsigned now) {
	g->dir = dir;
	g->pos = pos;
	g->v = MIN_GLIDE_V0;
	g->start = g->last = now;
}

float HomeMin_glideStep(HomeMinGlide* g, unsigned now, int n) {
	if (!g->dir)
		return g->pos;
	float dt = (now - g->last) / 1000.0f;
	if (dt > 0.05f) // a stalled frame doesn't jump
		dt = 0.05f;
	g->last = now;
	float k = (now - g->start) / (float)MIN_GLIDE_RAMP_MS;
	g->v = MIN_GLIDE_V0 + (MIN_GLIDE_MAX - MIN_GLIDE_V0) * (k > 1.0f ? 1.0f : k);
	float p = g->pos + g->dir * g->v * dt;
	g->pos = p < 0 ? 0 : p > n - 1 ? n - 1
								   : p;
	return g->pos;
}

int HomeMin_glideSettle(HomeMinGlide* g, int n, unsigned* ms) {
	int whole = (int)g->pos; // pos >= 0
	int to = g->dir > 0 && g->pos - whole > 0.001f ? whole + 1 : g->dir < 0 && g->pos - whole > 0.999f ? whole + 1
																									   : whole;
	if (to > n - 1)
		to = n - 1;
	if (to < 0)
		to = 0;
	float d = to - g->pos;
	if (d < 0)
		d = -d;
	float t = g->v > 0 ? 3.0f * d / g->v * 1000.0f : 0;
	*ms = t < MIN_SETTLE_MIN_MS ? MIN_SETTLE_MIN_MS : t > MIN_SETTLE_MAX_MS ? MIN_SETTLE_MAX_MS
																			: (unsigned)t;
	g->dir = 0;
	return to;
}

float HomeMin_easeOut(float t) {
	float u = 1.0f - t;
	return 1.0f - u * u * u;
}
