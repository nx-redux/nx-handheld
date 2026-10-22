#ifndef STR_TRIM_H
#define STR_TRIM_H

// Header-only whitespace trimmers shared by the pure parsers (m3u, emu
// shortcuts, emu overlay config, emu-options overrides, minarch cheat
// matching) so they need no utils.c. Tested in common/tests/test_str_trim.c.

#include <ctype.h>
#include <string.h>

// Cut trailing characters found in `set` (e.g. "\r\n") in place; returns the
// new length.
static inline size_t strTrimRight(char* s, const char* set) {
	size_t n = strlen(s);
	while (n > 0 && strchr(set, s[n - 1]))
		s[--n] = '\0';
	return n;
}

// Cut trailing isspace() characters in place and return a pointer past the
// leading ones (into s). NULL stays NULL.
static inline char* strTrimSpace(char* s) {
	if (!s)
		return s;
	while (isspace((unsigned char)*s))
		s++;
	char* end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

// strTrimSpace that keeps the text at s (shifts it left).
static inline void strTrimSpaceInPlace(char* s) {
	char* t = strTrimSpace(s);
	if (t != s)
		memmove(s, t, strlen(t) + 1);
}

#endif // STR_TRIM_H
