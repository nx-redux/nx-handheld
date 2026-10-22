// Art-path helpers and the Reset artwork delete (per .media, and across Roms), split into their own TU so host tests
// can link them without the ScreenScraper API/network stack that scraper_core.c pulls in.
#include "scraper_core.h"
#include "scraper_scan.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

void Scraper_variantPath(const char* out_png, const char* variant,
						 char* out, size_t out_size) {
	if (!variant || !variant[0]) {
		snprintf(out, out_size, "%s", out_png);
		return;
	}

	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", out_png);
	char* slash = strrchr(dir, '/');
	if (!slash) {
		snprintf(out, out_size, "%s/%s", variant, out_png);
		return;
	}
	*slash = '\0';
	snprintf(out, out_size, "%s/%s/%s", dir, variant, slash + 1);
}

// Delete every PNG directly inside one variant folder. Returns the count deleted.
static int deletePngsIn(const char* dir_path) {
	int deleted = 0;
	DIR* dir = opendir(dir_path);
	if (!dir)
		return 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		size_t n = strlen(entry->d_name);
		if (entry->d_name[0] == '.' || n <= 4 || strcasecmp(entry->d_name + n - 4, ".png") != 0)
			continue;
		char png_path[MAX_PATH + 256];
		snprintf(png_path, sizeof(png_path), "%s/%s", dir_path, entry->d_name);
		if (remove(png_path) == 0)
			deleted++;
	}
	closedir(dir);
	return deleted;
}

int Scraper_deleteMediaArtwork(const char* media_path) {
	static const char* variants[] = {"screenshot", "boxart", "boxart2d", "wheel", "mix"};
	int deleted = 0;
	for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
		char variant_path[MAX_PATH];
		snprintf(variant_path, sizeof(variant_path), "%s/%s", media_path, variants[v]);
		deleted += deletePngsIn(variant_path);
		rmdir(variant_path); // no-op unless it is now empty
	}
	return deleted;
}

static void deleteInMediaDir(const char* media, void* userdata) {
	*(int*)userdata += Scraper_deleteMediaArtwork(media);
}

int Scraper_deleteAllArtwork(const char* roms_root) {
	int deleted = 0;
	Scan_eachMediaDir(roms_root, deleteInMediaDir, &deleted);
	return deleted;
}
