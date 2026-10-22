// Host test for folder_tag.h (Roms folder "(TAG)" parsing).
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../folder_tag.h"

static const char* tag(const char* name, size_t size, int flags) {
	static char out[64];
	strcpy(out, "junk");
	bool ok = folderTag(name, out, size, flags);
	assert(ok == (out[0] != '\0'));
	return out;
}

int main(void) {
	assert(strcmp(tag("Game Boy (GB)", 16, 0), "GB") == 0);
	assert(strcmp(tag("1) Game Boy Advance (GBA)", 16, 0), "GBA") == 0); // sort prefix's ')' isn't a tag
	assert(strcmp(tag("Arcade (FBN) (MAME)", 16, 0), "MAME") == 0);		 // the last group
	assert(strcmp(tag("No Tag", 16, 0), "") == 0);
	assert(strcmp(tag("Empty ()", 16, 0), "") == 0);
	assert(strcmp(tag("Unbalanced (GB", 16, 0), "") == 0);
	assert(strcmp(tag(NULL, 16, 0), "") == 0);

	// Too long: rejected by default, cut with FOLDER_TAG_TRUNCATE.
	assert(strcmp(tag("X (ABCDEFGHIJKLMNOP)", 16, 0), "") == 0);
	assert(strcmp(tag("X (ABCDEFGHIJKLMNO)", 16, 0), "ABCDEFGHIJKLMNO") == 0); // 15 fits
	assert(strcmp(tag("X (ABCDEFGHIJKLMNOP)", 4, FOLDER_TAG_TRUNCATE), "ABC") == 0);

	// First ')' after the '(' vs the name's last ')'.
	assert(strcmp(tag("X (GB) extra)", 16, 0), "GB") == 0);
	assert(strcmp(tag("X (GB) extra)", 16, FOLDER_TAG_LAST_CLOSE), "GB) extra") == 0);
	assert(strcmp(tag("X (GB", 16, FOLDER_TAG_LAST_CLOSE), "") == 0);
	assert(strcmp(tag("A) B (", 16, FOLDER_TAG_LAST_CLOSE), "") == 0); // ')' before the '('

	char none[1] = {'x'};
	assert(!folderTag("X (GB)", none, 0, 0) && none[0] == 'x'); // zero-sized: untouched
	assert(!folderTag("X (GB)", none, 1, 0) && none[0] == '\0');

	printf("test_folder_tag: OK\n");
	return 0;
}
