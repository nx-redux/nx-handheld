#ifndef __SHADER_STRIP_H__
#define __SHADER_STRIP_H__

// Pure "#pragma parameter" filter behind load_shader_from_file and the
// parameter parser behind extractPragmaParameters (generic_video.c), split
// out so they can be unit-tested on the host: see tests/test_shader_strip.c.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADER_PRAGMA_PREFIX "#pragma parameter"

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
		if (n > 0 && strncmp(p, SHADER_PRAGMA_PREFIX, 17) != 0) {
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

// Copies the next line of *pos into line (size >= 1; at most size - 1 bytes:
// the rest of a longer line is skipped, never read as a line of its own) and
// moves *pos past the line's '\n'.
static inline void ShaderStrip_readLine(const char** pos, char* line, size_t size) {
	const char* p = *pos;
	size_t i = 0;
	while (*p && *p != '\n') {
		if (i < size - 1)
			line[i++] = *p;
		p++;
	}
	line[i] = '\0';
	if (*p == '\n')
		p++;
	*pos = p;
}

// A `#pragma parameter NAME "Label" default min max step` line: 1 when it
// parses (name and label are 128-byte buffers, vals gets default, min, max,
// step), 0 when the line is no parameter, -1 when it is one that doesn't parse.
static inline int ShaderStrip_parsePragma(const char* line, char* name, char* label, float vals[4]) {
	size_t plen = strlen(SHADER_PRAGMA_PREFIX);
	if (strncmp(line, SHADER_PRAGMA_PREFIX, plen) != 0)
		return 0;
	const char* start = line + plen;
	while (*start == ' ')
		start++;
	return sscanf(start, "%127s \"%127[^\"]\" %f %f %f %f", name, label, &vals[0], &vals[1], &vals[2], &vals[3]) == 6
			   ? 1
			   : -1;
}

#endif
