#include "ra_helpers.h"
#include <stdlib.h>
#include <string.h>

uint32_t RA_retryDelayMs(int attempt) {
	// Delays: 1s, 2s, 4s, 8s, 8s
	static const uint32_t delays[] = {1000, 2000, 4000, 8000, 8000};
	int idx = (attempt < 5) ? attempt : 4;
	if (idx < 0)
		idx = 0;
	return delays[idx];
}

bool RA_isBackgroundRequest(const char* post_data) {
	return post_data && strstr(post_data, "r=achievementsets") != NULL;
}

bool RA_memRangeFits(size_t mem_size, uint32_t address, uint32_t num_bytes) {
	return address <= mem_size && num_bytes <= mem_size - address;
}

int RA_readMutedIds(FILE* f, uint32_t* ids, int max) {
	int count = 0;
	char line[32];
	while (count < max && fgets(line, sizeof(line), f)) {
		uint32_t id = (uint32_t)strtoul(line, NULL, 10);
		if (id > 0)
			ids[count++] = id;
	}
	return count;
}
