#ifndef __CONFIRM_WRAP_H__
#define __CONFIRM_WRAP_H__

// The confirm dialog's subtitle word-wrap (ui_confirmdialog.c), header-only with a measure callback so it can be
// unit-tested on the host without SDL: see tests/test_confirm_wrap.c.

#include <stdio.h>
#include <string.h>

// Width in px of utf8 as it would be drawn.
typedef int (*ConfirmWrap_measureFn)(void* ctx, const char* utf8);

// Greedy word-wrap `text` (spaces separate words; the first 511 bytes are used) to `max_w` into up to max_lines
// 256-byte lines. A word wider than max_w gets a line of its own; words past the last line are dropped. Returns the
// line count (0 for no words).
static inline int ConfirmWrap_lines(const char* text, int max_w, ConfirmWrap_measureFn measure, void* ctx,
									char (*lines)[256], int max_lines) {
	char work[512];
	snprintf(work, sizeof(work), "%s", text);

	int n = 0;
	lines[0][0] = '\0';
	char* save = NULL;
	for (char* tok = strtok_r(work, " ", &save); tok && n < max_lines; tok = strtok_r(NULL, " ", &save)) {
		char cand[256];
		snprintf(cand, sizeof(cand), "%s%s%s", lines[n], lines[n][0] ? " " : "", tok);
		if (measure(ctx, cand) <= max_w || !lines[n][0]) {
			snprintf(lines[n], sizeof(lines[n]), "%s", cand);
		} else if (n + 1 < max_lines) {
			n++;
			snprintf(lines[n], sizeof(lines[n]), "%s", tok);
		} else {
			break; // out of lines; drop the rest
		}
	}
	return lines[0][0] ? n + 1 : 0;
}

#endif
