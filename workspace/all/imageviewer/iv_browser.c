#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "iv_browser.h"

// Supported image extensions
static const char* img_exts[] = {
	"png",
	"jpg",
	"jpeg",
	"bmp",
	"gif",
	NULL};

// Check if file is a supported image format
bool ImageBrowser_isImageFile(const char* filename) {
	if (!filename)
		return false;

	const char* dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return false;

	const char* ext = dot + 1;
	for (int i = 0; img_exts[i] != NULL; i++) {
		if (strcasecmp(ext, img_exts[i]) == 0)
			return true;
	}
	return false;
}

// Free browser entries
void ImageBrowser_freeEntries(ImageBrowserContext* ctx) {
	if (ctx->entries) {
		free(ctx->entries);
		ctx->entries = NULL;
	}
	ctx->entry_count = 0;
}

// Load directory contents (image files + directories)
void ImageBrowser_loadDirectory(ImageBrowserContext* ctx, const char* path, const char* root) {
	ImageBrowser_freeEntries(ctx);

	strncpy(ctx->current_path, path, sizeof(ctx->current_path) - 1);
	ctx->current_path[sizeof(ctx->current_path) - 1] = '\0';

	// Create image folder if it doesn't exist and we're at root
	if (strcmp(path, root) == 0) {
		mkdir(path, 0755);
	}

	ctx->entries = DirList_load(path, root, ImageBrowser_isImageFile, &ctx->entry_count);
	if (!ctx->entries)
		fprintf(stderr, "iv_browser: failed to open %s\n", path);
}

// Get display name for file (without extension)
void ImageBrowser_getDisplayName(const char* filename, char* out, int max_len) {
	strncpy(out, filename, max_len - 1);
	out[max_len - 1] = '\0';

	// Remove extension for image files
	char* dot = strrchr(out, '.');
	if (dot && dot != out) {
		*dot = '\0';
	}
}
