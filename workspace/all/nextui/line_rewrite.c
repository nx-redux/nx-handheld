#include "line_rewrite.h"
#include <stdlib.h>
#include <string.h>

char* LineRewrite_filter(const char* data, size_t len, const char* key, bool map_key,
						 const char* replacement, bool append_if_absent, size_t* out_len, bool* changed) {
	size_t key_len = strlen(key);
	size_t rep_len = replacement ? strlen(replacement) : 0;
	// never longer than the input plus one replacement per line, a newline
	// before an appended line, and the appended line
	size_t lines = 1;
	for (size_t i = 0; i < len; i++)
		if (data[i] == '\n')
			lines++;
	char* out = malloc(len + (lines + 1) * rep_len + 2);
	if (!out)
		return NULL;
	size_t n = 0;
	bool matched = false;
	size_t pos = 0;
	while (pos < len) {
		const char* line = data + pos;
		const char* nl = memchr(line, '\n', len - pos);
		size_t raw_len = nl ? (size_t)(nl - line) + 1 : len - pos;
		size_t cmp_len = nl ? (size_t)(nl - line) : raw_len;
		if (cmp_len > 0 && line[cmp_len - 1] == '\r')
			cmp_len--; // windows line ending
		bool is_match;
		if (map_key) {
			const char* tab = memchr(line, '\t', cmp_len);
			is_match = tab && (size_t)(tab - line) == key_len && memcmp(line, key, key_len) == 0;
		} else
			is_match = cmp_len == key_len && memcmp(line, key, key_len) == 0;
		if (is_match) {
			matched = true;
			if (replacement) {
				memcpy(out + n, replacement, rep_len);
				n += rep_len;
			}
		} else {
			memcpy(out + n, line, raw_len);
			n += raw_len;
		}
		pos += raw_len;
	}
	if (!matched && append_if_absent && replacement) {
		if (n > 0 && out[n - 1] != '\n')
			out[n++] = '\n';
		memcpy(out + n, replacement, rep_len);
		n += rep_len;
		matched = true;
	}
	out[n] = '\0';
	*out_len = n;
	*changed = matched;
	return out;
}
