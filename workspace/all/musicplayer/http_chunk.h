#ifndef __HTTP_CHUNK_H__
#define __HTTP_CHUNK_H__

// Pure helpers for HTTP chunked transfer decoding (radio_net.c), split out so
// they can be unit-tested on the host: see tests/test_http_chunk.c.

#include <limits.h>
#include <stdlib.h>

// Parses a chunk-size line (hex, optional ";ext"). Returns the size, 0 for the
// last chunk, or -1 when the line is not a hex number or the size is negative
// or larger than INT_MAX (read lengths are int).
static inline long HttpChunk_parseSize(const char* line) {
	char* end = NULL;
	long size = strtol(line, &end, 16);
	if (end == line || size < 0 || size > INT_MAX)
		return -1;
	return size;
}

// Bytes to read next: what is left of the chunk, capped to `room` (free space
// in the destination). Never negative.
static inline int HttpChunk_span(long chunk_size, long chunk_read, long room) {
	long left = chunk_size - chunk_read;
	if (left > room)
		left = room;
	if (left < 0)
		left = 0;
	return (int)left;
}

#endif
