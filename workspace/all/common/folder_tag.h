#ifndef FOLDER_TAG_H
#define FOLDER_TAG_H

// The "(TAG)" of a Roms folder name, shared by nextui (menu logos), ratools
// and the scraper. Header-only and dependency-free so those pure modules (and
// their host tests) don't have to link utils.c. Tested in
// common/tests/test_folder_tag.c.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum {
	FOLDER_TAG_TRUNCATE = 1,   // cut a tag that doesn't fit out_size (default: reject it)
	FOLDER_TAG_LAST_CLOSE = 2, // end at the name's last ')' (default: the first ')' after the '(')
};

// "Game Boy (GB)" -> "GB": the text inside the name's last '(' and its ')'.
// Returns false, with out = "", when there's no '(', no ')' after it, an empty
// "()", or (without FOLDER_TAG_TRUNCATE) a tag of out_size bytes or more.
static inline bool folderTag(const char* name, char* out, size_t out_size, int flags) {
	if (!out_size)
		return false;
	out[0] = '\0';
	const char* open = name ? strrchr(name, '(') : NULL;
	if (!open)
		return false;
	const char* close = (flags & FOLDER_TAG_LAST_CLOSE) ? strrchr(open, ')') : strchr(open, ')');
	if (!close || close <= open + 1)
		return false;
	size_t len = (size_t)(close - open - 1);
	if (len >= out_size) {
		if (!(flags & FOLDER_TAG_TRUNCATE))
			return false;
		len = out_size - 1;
	}
	memcpy(out, open + 1, len);
	out[len] = '\0';
	return true;
}

#endif // FOLDER_TAG_H
