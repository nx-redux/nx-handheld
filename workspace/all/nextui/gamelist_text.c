// gamelist.c's string rules, SDL-free so they can be host-tested (common/tests/test_gamelist_text.c).

#include "gamelist_text.h"

#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "utils.h"

void GameListText_copyRow(char* dst, size_t size, const char* src) {
	if (size == 0)
		return;
	size_t n = strlen(src);
	if (n >= size) {
		n = size - 1;
		while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
			n--; // src[n] continues the character before it: leave that character out
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

void GameListText_collectionStem(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	snprintf(out, size, "%s", slash ? slash + 1 : path);
	size_t n = strlen(out);
	if (n > 4 && suffixMatch(".txt", out))
		out[n - 4] = '\0';
}

void GameListText_fitTitle(const char* name, char* out, size_t out_size, int max_w, GameListText_measureFn measure,
						   void* ctx) {
	size_t name_len = strlen(name);
	GameListText_copyRow(out, out_size, name);
	if (name_len < out_size && measure(ctx, out) <= max_w)
		return;
	size_t len = name_len;
	while (len > 0) {
		len--;
		while (len > 0 && ((unsigned char)name[len] & 0xC0) == 0x80)
			len--; // don't split a UTF-8 sequence
		if (len + 3 >= out_size)
			continue; // the cut and its "..." must fit whole
		snprintf(out, out_size, "%.*s...", (int)len, name);
		if (measure(ctx, out) <= max_w)
			return;
	}
	GameListText_copyRow(out, out_size, name);
}

bool GameListText_folderGameParent(const char* path, const char* roms_path, char* parent_out, size_t size) {
	if (!suffixMatch(".cue", path) && !suffixMatch(".m3u", path))
		return false;
	if (!prefixMatch((char*)roms_path, path))
		return false;
	char work[MAX_PATH];
	strncpy(work, path, sizeof(work) - 1);
	work[sizeof(work) - 1] = '\0';
	char* slash = strrchr(work, '/');
	if (!slash)
		return false;
	char base[MAX_PATH];
	strncpy(base, slash + 1, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	char* dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
	*slash = '\0'; // work = parent dir
	char* parent_name = strrchr(work, '/');
	if (!parent_name || !exactMatch(parent_name + 1, base))
		return false;
	// never treat a console dir as the "game folder" — a stray playlist named after the console (eg. DC/DC.m3u)
	// must not delete the whole library
	*parent_name = '\0';
	bool console_dir = exactMatch(work, roms_path);
	*parent_name = '/';
	if (console_dir)
		return false;
	snprintf(parent_out, size, "%s", work);
	return true;
}
