// Artwork Manager download preferences: key=0|1 lines in
// creds_dir()/download.txt. Kept in its own TU so host tests can link it
// without the scraper's API/network stack.
#include "scraper_prefs.h"
#include "scraper_core.h" // creds_dir
#include "utils.h"		  // mkdir_p, writeFileAtomic

#include <stdio.h>
#include <string.h>

void ScraperPrefs_loadFrom(const char* path, ScraperPrefs* out) {
	out->box2d = out->wheel = out->mix = false;

	FILE* f = fopen(path, "r");
	if (!f)
		return;

	char line[128];
	while (fgets(line, sizeof(line), f)) {
		char key[32];
		int val;
		if (sscanf(line, "%31[^=]=%d", key, &val) != 2)
			continue;
		bool on = val == 1;
		if (strcmp(key, "box2d") == 0)
			out->box2d = on;
		else if (strcmp(key, "wheel") == 0)
			out->wheel = on;
		else if (strcmp(key, "mix") == 0)
			out->mix = on;
	}
	fclose(f);
}

// Replaced whole (tmp + rename), never truncated in place: the scrape worker reads the file for each game while
// the Download options screen saves it, and a read of a half-written file turned every option off for that game.
bool ScraperPrefs_saveTo(const char* path, const ScraperPrefs* p) {
	char buf[64];
	int len = snprintf(buf, sizeof(buf), "box2d=%d\nwheel=%d\nmix=%d\n", p->box2d, p->wheel, p->mix);
	return len > 0 && (size_t)len < sizeof(buf) && writeFileAtomic(path, buf, (size_t)len);
}

bool ScraperPrefs_isComplete(const ScraperPrefs* p, bool has_screenshot, bool has_boxart, bool has_mix) {
	return (has_screenshot || has_boxart) && (!p->mix || has_mix);
}

static void prefs_path(char* out, size_t out_size) {
	snprintf(out, out_size, "%s/download.txt", creds_dir());
}

void ScraperPrefs_load(ScraperPrefs* out) {
	char path[MAX_PATH];
	prefs_path(path, sizeof(path));
	ScraperPrefs_loadFrom(path, out);
}

bool ScraperPrefs_save(const ScraperPrefs* p) {
	mkdir_p(creds_dir());
	char path[MAX_PATH];
	prefs_path(path, sizeof(path));
	return ScraperPrefs_saveTo(path, p);
}
