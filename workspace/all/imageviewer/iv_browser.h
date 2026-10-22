#ifndef IV_BROWSER_H
#define IV_BROWSER_H
#include <stdbool.h>
#include "dir_list.h"

typedef DirListEntry ImageEntry; // size_bytes: 0 for dirs; from stat during loadDirectory

typedef struct {
	char current_path[512];
	ImageEntry* entries;
	int entry_count;
} ImageBrowserContext;

bool ImageBrowser_isImageFile(const char* filename); // .png .jpg .jpeg .bmp .gif, case-insensitive
void ImageBrowser_loadDirectory(ImageBrowserContext* ctx, const char* path, const char* root);
void ImageBrowser_freeEntries(ImageBrowserContext* ctx);
void ImageBrowser_getDisplayName(const char* filename, char* out, int max_len); // strips extension
#endif
