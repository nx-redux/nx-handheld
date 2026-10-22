#include "ma_state_read.h"
#include <stdlib.h>
#include <string.h>

// The size of a RASTATE file's MEM block (header "RASTATE<ver>MEM <le32>"), 0 for raw core data.
size_t State_memSize(const uint8_t* header) {
	if (memcmp(header, "RASTATE", 7) != 0 || memcmp(header + 8, "MEM ", 4) != 0)
		return 0;
	return (size_t)header[12] | (size_t)header[13] << 8 | (size_t)header[14] << 16 | (size_t)header[15] << 24;
}

// Reads the state data: [want] bytes when known, else to the end. The buffer is at least
// [min_size] (the core's serialize_size, zero-filled past the data) but grows past it: a
// core's live size can be smaller than a state it wrote later in a game (flycast includes
// its render lists, ~27 MB after boot and ~36 MB mid-game), and unserialize rejects data
// cut to the smaller size. *out_size is what to pass to unserialize: max(data, min_size).
void* State_readData(StateReadFn rd, void* stream, size_t min_size, size_t want, size_t* out_size) {
	size_t cap = want > min_size ? want : min_size;
	if (cap > STATE_READ_MAX)
		return NULL;
	uint8_t* buf = calloc(1, cap);
	if (!buf)
		return NULL;
	size_t len = 0;
	while (!want || len < want) {
		if (len == cap) {
			size_t grown = cap * 2;
			if (grown > STATE_READ_MAX) {
				free(buf);
				return NULL;
			}
			uint8_t* next = realloc(buf, grown);
			if (!next) {
				free(buf);
				return NULL;
			}
			memset(next + cap, 0, grown - cap);
			buf = next;
			cap = grown;
		}
		size_t ask = cap - len;
		if (want && ask > want - len)
			ask = want - len;
		int64_t n = rd(stream, buf + len, ask);
		if (n < 0) {
			free(buf);
			return NULL;
		}
		if (n == 0)
			break;
		len += (size_t)n;
	}
	*out_size = len > min_size ? len : min_size;
	return buf;
}
