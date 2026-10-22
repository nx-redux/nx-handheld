#ifndef __IPTV_TEXT_H__
#define __IPTV_TEXT_H__

// Pure text helpers of the Online TV code (module_iptv.c, iptv_curated.c),
// split out so they can be unit-tested on the host: see
// tests/test_iptv_text.c.

// Copy `name` into `out`, dropping any "[...]" tag segments (e.g. the
// "[Geo-blocked]" / "[Not 24/7]" status tags iptv-org puts in channel names)
// and collapsing the whitespace they leave behind (leading, repeated and
// trailing spaces). Used only for the saved channel name; the browser list
// keeps the tags as a heads-up.
static inline void IPTV_stripNameTags(const char* name, char* out, int out_sz) {
	if (out_sz <= 0)
		return;
	int j = 0, depth = 0;
	for (int i = 0; name[i] && j < out_sz - 1; i++) {
		char c = name[i];
		if (c == '[') {
			depth++;
			continue;
		}
		if (c == ']') {
			if (depth > 0)
				depth--;
			continue;
		}
		if (depth > 0)
			continue;
		if (c == ' ' && (j == 0 || out[j - 1] == ' '))
			continue; // collapse runs of spaces left by removed tags
		out[j++] = c;
	}
	while (j > 0 && out[j - 1] == ' ')
		j--; // trim trailing space
	out[j] = '\0';
}

// Lowercase a country code into dst for the URL/cache filename, keeping only
// [a-z0-9]. The code is third-party data (iptv-org countries.json) and flows
// into the fetch URL and a cache file path, so stripping other characters
// prevents shell-metacharacter injection and path traversal (e.g. "../").
static inline void IPTV_lcCode(const char* code, char* dst, int dst_sz) {
	if (dst_sz <= 0)
		return;
	int j = 0;
	for (int i = 0; code[i] && j < dst_sz - 1; i++) {
		char ch = code[i];
		if (ch >= 'A' && ch <= 'Z')
			ch = (char)(ch - 'A' + 'a');
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
			dst[j++] = ch;
	}
	dst[j] = '\0';
}

#endif
