#include <stdio.h>
#include <string.h>

#include "ui_list.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_menubar.h"
#include "ui_iptv.h"
#include "ui_tabs.h"
#include "ui_fonts.h"
#include "ui_listview.h"
#include "ui_toast.h"
#include "iptv.h"
#include "iptv_curated.h"

static ListView iptv_user_channels_view;
static ListView iptv_curated_countries_view;
static ListView iptv_curated_channels_view;

typedef struct {
	const CuratedTVChannel* channels;
	int channel_count;
	const int* sorted_indices;
	int sorted_count;
} CuratedChannelsRowCtx;

static char curated_row_buf[320];

ListView* IPTVUserChannels_view(void) {
	return &iptv_user_channels_view;
}
ListView* IPTVCuratedCountries_view(void) {
	return &iptv_curated_countries_view;
}
ListView* IPTVCuratedChannels_view(void) {
	return &iptv_curated_channels_view;
}

static void iptv_user_get_row(void* ctx, int i, bool selected,
							  ListViewRow* out) {
	const IPTVChannel* channels = ctx;
	out->label = channels[i].name;
	(void)selected;
}

static void iptv_countries_get_row(void* ctx, int i, bool selected,
								   ListViewRow* out) {
	const CuratedTVCountry* countries = ctx;
	out->label = countries[i].name;
	(void)selected;
}

static void iptv_curated_channels_get_row(void* ctx, int i, bool selected,
										  ListViewRow* out) {
	(void)selected;
	const CuratedChannelsRowCtx* rc = ctx;
	if (!rc || i < 0 || i >= rc->sorted_count)
		return;
	int actual = rc->sorted_indices[i];
	if (actual < 0 || actual >= rc->channel_count)
		return;
	const CuratedTVChannel* ch = &rc->channels[actual];
	if (IPTV_userChannelExists(ch->url)) {
		snprintf(curated_row_buf, sizeof(curated_row_buf), "[+] %s", ch->name);
		out->label = curated_row_buf;
	} else {
		out->label = ch->name;
	}
	if (ch->category[0])
		out->annotation = ch->category;
}

void render_iptv_user_channels(SDL_Surface* screen, IndicatorType show_setting,
							   const char* toast_message, uint32_t toast_time) {
	(void)show_setting;
	GFX_clear(screen);

	int channel_count = IPTV_getUserChannelCount();
	const IPTVChannel* channels = IPTV_getUserChannels();

	ListView* v = &iptv_user_channels_view;
	v->title = NULL;
	v->font = font.medium;
	v->count = channel_count;
	v->get_row = iptv_user_get_row;
	v->ctx = (void*)channels;
	v->list_id = (const void*)channels;
	v->empty_title = "No channels saved";
	v->empty_subtitle = "Press A to browse channels";
	static char* empty_btns[] = {"A", "BROWSE", "B", "EXIT", NULL};
	v->empty_btn_pairs = empty_btns;

	static char* hints_play[] = {"B", "EXIT", "A", "PLAY", NULL};
	static char* hints_empty[] = {"B", "EXIT", "A", "BROWSE", NULL};
	char** hints = (channel_count > 0) ? hints_play : hints_empty;

	MediaUI_renderPage(screen, v, "Media Player | Online TV", NULL,
					   true, MEDIA_TAB_IPTV, hints);

	UI_renderToast(screen, toast_message, toast_time);
}

void render_iptv_curated_countries(SDL_Surface* screen, IndicatorType show_setting,
								   const char* toast_message, uint32_t toast_time) {
	(void)show_setting;
	GFX_clear(screen);

	int country_count = IPTV_curated_get_country_count();
	const CuratedTVCountry* countries = IPTV_curated_get_countries();

	ListView* v = &iptv_curated_countries_view;
	v->title = NULL;
	v->font = font.medium;
	v->count = country_count;
	v->get_row = iptv_countries_get_row;
	v->ctx = (void*)countries;
	v->list_id = (const void*)countries;
	v->empty_title = "Couldn't load channel list";
	v->empty_subtitle = "Press MENU to refresh, or check Wi-Fi";
	v->empty_btn_pairs = NULL;

	static char* hints[] = {"B", "BACK", "A", "SELECT", NULL};
	MediaUI_renderPage(screen, v, "Media Player | Browse Channels", "Browse Channels",
					   false, MEDIA_TAB_IPTV, hints);

	UI_renderToast(screen, toast_message, toast_time);
}

void render_iptv_curated_channels(SDL_Surface* screen, IndicatorType show_setting,
								  const char* country_code,
								  const int* sorted_indices, int sorted_count,
								  const char* toast_message, uint32_t toast_time) {
	(void)show_setting;
	GFX_clear(screen);

	const char* country_name = "Channels";
	const CuratedTVCountry* countries = IPTV_curated_get_countries();
	int country_count = IPTV_curated_get_country_count();
	for (int i = 0; i < country_count; i++) {
		if (strcmp(countries[i].code, country_code) == 0) {
			country_name = countries[i].name;
			break;
		}
	}

	char page_title[192];
	UI_pageTitle(page_title, sizeof(page_title), "Media Player", country_name);

	int channel_count = 0;
	const CuratedTVChannel* channels = IPTV_curated_get_channels(country_code, &channel_count);

	CuratedChannelsRowCtx row_ctx = {
		.channels = channels,
		.channel_count = channel_count,
		.sorted_indices = sorted_indices,
		.sorted_count = sorted_count,
	};

	ListView* v = &iptv_curated_channels_view;
	v->title = NULL;
	v->font = font.medium;
	v->count = sorted_count;
	v->get_row = iptv_curated_channels_get_row;
	v->ctx = &row_ctx;
	v->list_id = (const void*)sorted_indices;
	v->empty_title = "No channels found";
	v->empty_subtitle = "Try another country or press MENU to refresh";
	v->empty_btn_pairs = NULL;

	bool selected_exists = false;
	if (sorted_count > 0 && v->selected >= 0 && v->selected < sorted_count) {
		int sel_actual = sorted_indices[v->selected];
		if (sel_actual >= 0 && sel_actual < channel_count)
			selected_exists = IPTV_userChannelExists(channels[sel_actual].url);
	}

	static char* hints_remove[] = {"B", "BACK", "A", "REMOVE", NULL};
	static char* hints_add[] = {"B", "BACK", "A", "ADD", NULL};
	MediaUI_renderPage(screen, v, page_title, country_name,
					   false, MEDIA_TAB_IPTV, selected_exists ? hints_remove : hints_add);

	UI_renderToast(screen, toast_message, toast_time);
}
