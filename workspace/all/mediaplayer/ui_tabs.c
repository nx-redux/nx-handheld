#include "ui_tabs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "defines.h"
#include "ui_accent.h"
#include "ui_buttonhintbar.h"
#include "ui_draw.h"
#include "ui_ease.h"
#include "ui_emptystate.h"
#include "ui_fade.h"
#include "ui_font.h"
#include "ui_list.h"
#include "ui_menubar.h"
#include "../nextui/minimal_draw.h"

#define TAB_DIM_ALPHA 97
#define TAB_SLIDE_MS 300
#define MIN_SLOTS 24

static const char* const tab_labels[MEDIA_TAB_COUNT] = {
	"LIBRARY",
	"ONLINE TV",
};

static float clamp01f(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Same distance-based opacity curve as nextui's HomeMin_alpha (home_list_layout.c).
static float min_distance_alpha(float d) {
	if (d >= 0.0f) {
		if (d <= 1.0f)
			return 1.0f + (0.80f - 1.0f) * d;
		if (d <= 2.0f)
			return 0.80f + (0.60f - 0.80f) * (d - 1.0f);
		if (d <= 3.0f)
			return 0.60f + (0.50f - 0.60f) * (d - 2.0f);
		if (d <= 4.0f)
			return 0.50f + (0.42f - 0.50f) * (d - 3.0f);
		return 0.42f;
	}
	float a = -d;
	if (a <= 1.0f)
		return 1.0f + (0.60f - 1.0f) * a;
	if (a <= 2.0f)
		return 0.60f + (0.48f - 0.60f) * (a - 1.0f);
	if (a <= 3.0f)
		return 0.48f + (0.40f - 0.48f) * (a - 2.0f);
	return 0.40f;
}

bool MediaUI_isMinimal(void) {
	return CFG_getGameListStyle() == MENU_STYLE_MINIMAL ||
		   CFG_getMenuStyle(MENU_CAT_TOOLS) == MENU_STYLE_MINIMAL;
}

bool MediaUI_tabRowShown(void) {
	return CFG_getButtonHints();
}

static int glyph_top(TTF_Font* f, Uint16 ch, int fallback) {
	int minx, maxx, miny, maxy, adv;
	return (f && TTF_GlyphMetrics(f, ch, &minx, &maxx, &miny, &maxy, &adv) == 0 && maxy > 0)
			   ? maxy
			   : fallback;
}

void MediaUI_renderTabRow(SDL_Surface* screen, MediaTab active, char** right_hints) {
	if (!MediaUI_tabRowShown())
		return;

	int bar_h = BAR_HEIGHT;
	int row_y = screen->h - bar_h;
	static SDL_Surface* scrim = NULL;
	SDL_Surface* sc = UI_getScrimAlpha(&scrim, screen->w, bar_h, 204);
	if (sc)
		UI_blitFade(sc, NULL, screen, 0, row_y);

	int left = MediaUI_isMinimal() ? NX_DP(NX_MENU_GUTTER_DP) : UI_listTextX();
	int right_edge = screen->w - left;
	int gap = NX_DP(20);

	TTF_Font* tf = UI_pageTitleFont();
	if (!tf)
		tf = font.medium;

	int x = left;
	for (int i = 0; i < MEDIA_TAB_COUNT; i++) {
		SDL_Surface* text = GFX_getCachedText(tf, tab_labels[i], COLOR_WHITE);
		if (!text)
			continue;
		SDL_Rect dst = {x, row_y + (bar_h - text->h) / 2, 0, 0};
		if ((MediaTab)i == active) {
			SDL_BlitSurface(text, NULL, screen, &dst);
		} else {
			SDL_SetSurfaceAlphaMod(text, TAB_DIM_ALPHA);
			SDL_BlitSurface(text, NULL, screen, &dst);
			SDL_SetSurfaceAlphaMod(text, 255);
		}
		x += text->w + (i + 1 < MEDIA_TAB_COUNT ? gap : 0);
	}

	if (!right_hints)
		return;

	int glyph_h = glyph_top(font.tiny, 'H', TTF_FontAscent(font.tiny) * 7 / 10);
	int pair_count = 0;
	while (right_hints[pair_count * 2] && right_hints[pair_count * 2 + 1] && pair_count < 4)
		pair_count++;

	int hw[4] = {0};
	int total_hw = 0;
	int hint_gap = SCALE1(BUTTON_MARGIN * 2);
	for (int i = 0; i < pair_count; i++) {
		hw[i] = GFX_blitButtonScaled(right_hints[i * 2 + 1], right_hints[i * 2], NULL, 0, 0, glyph_h);
		total_hw += hw[i] + (i > 0 ? hint_gap : 0);
	}

	// Only draw hints that fit cleanly to the right of the tab labels
	while (pair_count > 0 && x + gap + total_hw > right_edge) {
		pair_count--;
		total_hw = 0;
		for (int i = 0; i < pair_count; i++)
			total_hw += hw[i] + (i > 0 ? hint_gap : 0);
	}

	int hx = right_edge - total_hw;
	int cy = row_y + bar_h / 2;
	for (int i = 0; i < pair_count; i++) {
		GFX_blitButtonScaled(right_hints[i * 2 + 1], right_hints[i * 2], screen, hx, cy, glyph_h);
		hx += hw[i] + hint_gap;
	}
}

// ---- Minimal list state & surface cache ----

typedef struct {
	int index;
	uint32_t label_hash;
	SDL_Surface* solid;
	SDL_Surface* outline;
} MinSlot;

static MinSlot min_slots[MIN_SLOTS];
static int* min_line_counts = NULL;
static int min_line_cap = 0;
static const void* min_seen_list_id = NULL;
static int min_seen_count = -1;
static int min_seen_w = 0;
static int min_seen_scale = 0;
static Uint32 min_seen_accent = 0;

static float min_pos_from = 0.0f;
static float min_pos_to = 0.0f;
static int min_last_sel = -1;
static Uint32 min_slide_start = 0;
static bool min_slide_active = false;
static MinFade min_top_fade;
static MinFade min_bottom_fade;

static uint32_t hash_str(const char* s) {
	uint32_t h = 2166136261u;
	if (!s)
		return h;
	while (*s) {
		h ^= (uint8_t)(*s++);
		h *= 16777619u;
	}
	return h;
}

static void clear_min_slots(void) {
	for (int i = 0; i < MIN_SLOTS; i++) {
		if (min_slots[i].solid)
			SDL_FreeSurface(min_slots[i].solid);
		if (min_slots[i].outline)
			SDL_FreeSurface(min_slots[i].outline);
		min_slots[i] = (MinSlot){.index = -1, .label_hash = 0, .solid = NULL, .outline = NULL};
	}
}

void MediaUI_cleanup(void) {
	clear_min_slots();
	free(min_line_counts);
	min_line_counts = NULL;
	min_line_cap = 0;
	MinFade_free(&min_top_fade);
	MinFade_free(&min_bottom_fade);
	min_seen_list_id = NULL;
	min_seen_count = -1;
}

static float slide_progress(void) {
	if (!min_slide_active)
		return 1.0f;
	Uint32 elapsed = SDL_GetTicks() - min_slide_start;
	if (elapsed >= TAB_SLIDE_MS) {
		min_slide_active = false;
		return 1.0f;
	}
	return (float)elapsed / (float)TAB_SLIDE_MS;
}

static float current_min_pos(void) {
	if (!CFG_getMenuAnimations())
		return min_pos_to;
	float t = slide_progress();
	return min_pos_from + (min_pos_to - min_pos_from) * UI_easeStandard(t);
}

bool MediaUI_animBusy(void) {
	if (!MediaUI_isMinimal() || !CFG_getMenuAnimations())
		return false;
	return slide_progress() < 1.0f && fabsf(min_pos_to - min_pos_from) > 0.001f;
}

static void row_label_for(ListView* v, int i, char* out, size_t out_sz) {
	ListViewRow row;
	memset(&row, 0, sizeof(row));
	if (v->get_row)
		v->get_row(v->ctx, i, i == v->selected, &row);
	snprintf(out, out_sz, "%s", row.label ? row.label : "");
}

static int row_height_for(ListView* v, const MinMetrics* m, int i, int max_w) {
	if (i < 0 || i >= v->count || !min_line_counts || i >= min_line_cap)
		return m->pitch + m->gap;
	if (min_line_counts[i] == 0) {
		char label[MIN_LINE_MAX];
		char lines[MIN_MAX_LINES][MIN_LINE_MAX];
		row_label_for(v, i, label, sizeof(label));
		int c = MinDraw_wrap(m->font, label, max_w, MIN_MAX_LINES, lines);
		min_line_counts[i] = c > 0 ? c : 1;
	}
	return min_line_counts[i] * m->pitch + m->gap;
}

static int row_y_at(ListView* v, const MinMetrics* m, int i, float p, int body_top, int max_w) {
	int base = (int)floorf(p);
	if (base < 0)
		base = 0;
	if (base >= v->count)
		base = v->count - 1;
	float y = (float)(body_top + m->pitch) - (p - (float)base) * (float)row_height_for(v, m, base, max_w);
	if (i >= base) {
		for (int k = base; k < i; k++)
			y += (float)row_height_for(v, m, k, max_w);
	} else {
		for (int k = i; k < base; k++)
			y -= (float)row_height_for(v, m, k, max_w);
	}
	return (int)lroundf(y);
}

static MinSlot* slot_for(ListView* v, const MinMetrics* m, int i, int sel, int max_w) {
	char label[MIN_LINE_MAX];
	row_label_for(v, i, label, sizeof(label));
	uint32_t lh = hash_str(label);

	int far = 0, far_d = -1;
	for (int k = 0; k < MIN_SLOTS; k++) {
		if (min_slots[k].index == i && min_slots[k].label_hash == lh && min_slots[k].solid && min_slots[k].outline)
			return &min_slots[k];
		int d = min_slots[k].index < 0 ? (1 << 30) : abs(min_slots[k].index - sel);
		if (d > far_d) {
			far = k;
			far_d = d;
		}
	}

	MinSlot* s = &min_slots[far];
	if (s->solid)
		SDL_FreeSurface(s->solid);
	if (s->outline)
		SDL_FreeSurface(s->outline);

	char lines[MIN_MAX_LINES][MIN_LINE_MAX];
	int c = MinDraw_wrap(m->font, label, max_w, MIN_MAX_LINES, lines);
	if (c < 1) {
		lines[0][0] = '\0';
		c = 1;
	}
	if (min_line_counts && i >= 0 && i < min_line_cap)
		min_line_counts[i] = c;

	SDL_Color acc = UI_accent();
	acc.a = 255;
	s->index = i;
	s->label_hash = lh;
	s->solid = MinDraw_lines(m, lines, c, max_w, acc, false);
	s->outline = MinDraw_lines(m, lines, c, max_w, acc, true);
	return s;
}

static void render_minimal_page(SDL_Surface* screen, ListView* v,
								const char* full_title, const char* sub_title,
								bool is_root, MediaTab active_tab, char** hints) {
	v->no_lr_paging = is_root;

	const char* bar_title = CFG_getPageTitle() ? full_title : (sub_title ? sub_title : "");
	UI_renderMenuBarAt(screen, bar_title, NULL, NX_DP(NX_MENU_GUTTER_DP), true, false);

	if (v->count <= 0) {
		if (v->empty_btn_pairs)
			UI_renderEmptyStateButtons(screen, v->empty_title ? v->empty_title : "",
									   v->empty_subtitle, v->empty_btn_pairs);
		else
			UI_renderEmptyState(screen, v->empty_title ? v->empty_title : "",
								v->empty_subtitle, NULL);
		if (is_root)
			MediaUI_renderTabRow(screen, active_tab, hints);
		else if (hints)
			UI_renderButtonHintBarAt(screen, hints, 204, NX_DP(NX_MENU_GUTTER_DP));
		return;
	}

	if (v->selected < 0)
		v->selected = 0;
	if (v->selected >= v->count)
		v->selected = v->count - 1;

	SDL_Color a = UI_accent();
	Uint32 accent = ((Uint32)a.r << 16) | ((Uint32)a.g << 8) | (Uint32)a.b;
	int scale_key = (int)lroundf(FIXED_SCALE * 100.0f);
	bool reset = (v->list_id != min_seen_list_id || v->count != min_seen_count ||
				  screen->w != min_seen_w || scale_key != min_seen_scale || accent != min_seen_accent);
	if (reset) {
		min_seen_list_id = v->list_id;
		min_seen_count = v->count;
		min_seen_w = screen->w;
		min_seen_scale = scale_key;
		min_seen_accent = accent;
		clear_min_slots();
		if (v->count > min_line_cap) {
			int* grown = realloc(min_line_counts, (size_t)v->count * sizeof(int));
			if (grown) {
				min_line_counts = grown;
				min_line_cap = v->count;
			}
		}
		if (min_line_counts && min_line_cap > 0)
			memset(min_line_counts, 0, (size_t)min_line_cap * sizeof(int));
		min_pos_from = (float)v->selected;
		min_pos_to = (float)v->selected;
		min_last_sel = v->selected;
		min_slide_active = false;
	} else if (v->selected != min_last_sel) {
		min_pos_from = current_min_pos();
		min_pos_to = (float)v->selected;
		min_last_sel = v->selected;
		min_slide_start = SDL_GetTicks();
		min_slide_active = true;
	}

	MinMetrics m;
	if (!MinDraw_metrics(&m)) {
		UI_listViewRender(v, screen);
		return;
	}

	int body_top = BAR_HEIGHT;
	int body_bottom = CFG_getButtonHints() ? (screen->h - BAR_HEIGHT) : screen->h;
	int max_w = screen->w - 2 * NX_DP(NX_MENU_GUTTER_DP);
	v->last_visible = (body_bottom - body_top) / (m.pitch + m.gap);
	if (v->last_visible < 1)
		v->last_visible = 1;

	float p = current_min_pos();
	SDL_Rect prev_clip;
	SDL_GetClipRect(screen, &prev_clip);
	SDL_Rect clip = {0, body_top, screen->w, body_bottom - body_top};
	SDL_SetClipRect(screen, &clip);

	bool more_above = false;
	bool more_below = false;
	int x = NX_DP(NX_MENU_GUTTER_DP) - m.outline;

	for (int i = 0; i < v->count; i++) {
		int y = row_y_at(v, &m, i, p, body_top, max_w);
		int rh = row_height_for(v, &m, i, max_w);
		if (y < body_top)
			more_above = true;
		if (y + rh - m.gap > body_bottom)
			more_below = true;
		if (y + rh < body_top - m.pitch || y > body_bottom + m.pitch)
			continue;

		MinSlot* s = slot_for(v, &m, i, v->selected, max_w);
		float d = (float)i - p;
		float alpha = min_distance_alpha(d);
		float fill = clamp01f(1.0f - fabsf(d));
		MinDraw_blit(s->outline, (SDL_Rect){x, y - m.outline, 0, 0},
					 alpha * (1.0f - fill) * MIN_OUTLINE_ALPHA, &clip, 255, false, screen);
		MinDraw_blit(s->solid, (SDL_Rect){x, y - m.outline, 0, 0},
					 alpha * fill, &clip, 255, false, screen);
	}

	int fade_h = NX_DP(MIN_EDGE_FADE_DP);
	if (more_above) {
		SDL_Surface* tf = MinFade_get(&min_top_fade, true, screen->w, fade_h);
		if (tf)
			SDL_BlitSurface(tf, NULL, screen, &(SDL_Rect){0, body_top, 0, 0});
	}
	if (more_below) {
		SDL_Surface* bf = MinFade_get(&min_bottom_fade, false, screen->w, fade_h);
		if (bf)
			SDL_BlitSurface(bf, NULL, screen, &(SDL_Rect){0, body_bottom - fade_h, 0, 0});
	}

	SDL_SetClipRect(screen, &prev_clip);

	if (is_root)
		MediaUI_renderTabRow(screen, active_tab, hints);
	else if (hints)
		UI_renderButtonHintBarAt(screen, hints, 204, NX_DP(NX_MENU_GUTTER_DP));
}

void MediaUI_renderPage(SDL_Surface* screen, ListView* v,
						const char* full_title, const char* sub_title,
						bool is_root, MediaTab active_tab, char** hints) {
	if (MediaUI_isMinimal()) {
		render_minimal_page(screen, v, full_title, sub_title, is_root, active_tab, hints);
		return;
	}

	v->no_lr_paging = false;
	const char* bar_title = (CFG_getPageTitle() || !is_root) ? full_title : "";
	UI_renderMenuBar(screen, bar_title);

	v->title = NULL;
	v->hint_pairs = (is_root && MediaUI_tabRowShown()) ? NULL : hints;
	UI_listViewRender(v, screen);

	if (is_root)
		MediaUI_renderTabRow(screen, active_tab, hints);
}
