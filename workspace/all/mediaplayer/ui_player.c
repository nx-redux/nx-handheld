#include <stdio.h>
#include <string.h>

#include "vp_defines.h"
#include "api.h"
#include "ui_buttonhintbar.h"
#include "ui_emptystate.h"
#include "ui_menubar.h"
#include "ui_player.h"
#include "ui_tabs.h"
#include "ui_icons.h"
#include "ui_list.h"
#include "ui_listview.h"
#include "video_browser.h"
#include "positions.h"

// Full-mode ListView for the file browser (the widget owns selection,
// scroll, glide and marquee; module_player drives input through the
// accessor below).
static ListView video_browser_view;
static char browser_row_buf[256];

ListView* VideoBrowser_view(void) {
	return &video_browser_view;
}

// Build the display name shown for a browser entry (matches the row loop).
static void browser_entry_display(const VideoFileEntry* entry, char* out, size_t out_sz) {
	if (entry->is_dir) {
		if (Icons_isLoaded()) {
			strncpy(out, entry->name, out_sz - 1);
			out[out_sz - 1] = '\0';
		} else {
			snprintf(out, out_sz, "[%s]", entry->name);
		}
	} else {
		VideoBrowser_getDisplayName(entry->name, out, out_sz);
	}
}

// Row provider for the browser ListView: label via browser_entry_display,
// icon variant follows the widget-supplied `selected` flag (the
// glide-tracking row_sel), preserving the pill-follows-icon behavior.
static void browser_get_row(void* ctx, int i, bool selected, ListViewRow* out) {
	VideoBrowserContext* b = ctx;
	VideoFileEntry* entry = &b->entries[i];
	browser_entry_display(entry, browser_row_buf, sizeof(browser_row_buf));
	out->label = browser_row_buf;
	if (Icons_isLoaded()) {
		if (entry->is_dir)
			out->icon = Icons_getFolder(selected);
		else
			out->icon = Icons_getForFormat(VideoBrowser_detectFormat(entry->name), selected);
	}
}

// Render the video file browser
void render_video_browser(SDL_Surface* screen, IndicatorType show_setting,
						  VideoBrowserContext* ctx) {
	(void)show_setting;
	GFX_clear(screen);

	bool is_root = (strcmp(ctx->current_path, VIDEO_ROOT) == 0);
	const char* header_title = "Media Player | Library";
	const char* sub_title = NULL;
	char folder_title[300];
	if (!is_root) {
		const char* slash = strrchr(ctx->current_path, '/');
		if (slash && slash[1] != '\0') {
			sub_title = slash + 1;
			header_title = UI_pageTitle(folder_title, sizeof(folder_title), "Videos", sub_title);
		}
	}

	ListView* v = &video_browser_view;
	v->title = NULL;
	v->font = font.medium;
	v->count = ctx->entry_count;
	v->get_row = browser_get_row;
	v->ctx = ctx;
	v->list_id = (const void*)ctx->entries;
	v->empty_title = "No videos found";
	v->empty_subtitle = "Add videos to /Videos on your SD card";
	static char* empty_root_btns[] = {"L1", "ONLINE TV", "B", "EXIT", NULL};
	v->empty_btn_pairs = is_root ? empty_root_btns : NULL;

	static char* hints_root_empty[] = {"B", "EXIT", NULL};
	static char* hints_root_open[] = {"B", "EXIT", "A", "OPEN", NULL};
	static char* hints_root_resume[] = {"B", "EXIT", "A", "RESUME", NULL};
	static char* hints_sub_open[] = {"B", "BACK", "A", "OPEN", NULL};
	static char* hints_sub_resume[] = {"B", "BACK", "A", "RESUME", NULL};

	VideoFileEntry* sel =
		(ctx->entry_count > 0 && v->selected >= 0 && v->selected < ctx->entry_count)
			? &ctx->entries[v->selected]
			: NULL;
	bool sel_can_resume = sel && !sel->is_dir && Positions_get(sel->path) > 0;

	char** hints;
	if (is_root) {
		if (ctx->entry_count <= 0)
			hints = hints_root_empty;
		else
			hints = sel_can_resume ? hints_root_resume : hints_root_open;
	} else {
		hints = sel_can_resume ? hints_sub_resume : hints_sub_open;
	}

	MediaUI_renderPage(screen, v, header_title, sub_title, is_root, MEDIA_TAB_LIBRARY, hints);
}
