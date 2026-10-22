#ifndef __SHADER_STRIP_H__
#define __SHADER_STRIP_H__

// Pure "#pragma parameter" filter behind load_shader_from_file
// (generic_video.c), split out so it can be unit-tested on the host: see
// tests/test_shader_strip.c.

#include <stdlib.h>
#include <string.h>

// Returns a malloc'd copy of source without its "#pragma parameter" lines and
// with every kept line ending in '\n' (empty lines dropped, as strtok did), or
// NULL on OOM. A last line without a newline gains one, hence the +2.
static inline char* ShaderStrip_parameters(const char* source) {
	size_t len = strlen(source);
	char* out = malloc(len + 2);
	if (!out)
		return NULL;
	char* w = out;
	const char* p = source;
	while (*p) {
		const char* nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		if (n > 0 && strncmp(p, "#pragma parameter", 17) != 0) {
			memcpy(w, p, n);
			w += n;
			*w++ = '\n';
		}
		p += n;
		if (*p == '\n')
			p++;
	}
	*w = '\0';
	return out;
}

#endif
