// imgloader.c's thumbnail cache bookkeeping, SDL-free so it can be host-tested (common/tests/test_thumbcache_model.c).

#include "thumbcache_model.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

void ThumbCache_key(const char* path, bool fit, char* out, size_t size) {
	snprintf(out, size, "%s%s", fit && path[0] ? "fit:" : "", path);
}

int ThumbCache_find(const ThumbCacheEntry* cache, int n, const char* key) {
	for (int i = 0; i < n; i++)
		if (cache[i].occupied && strcmp(cache[i].path, key) == 0)
			return i;
	return -1;
}

void* ThumbCache_insert(ThumbCacheEntry* cache, int n, int* counter, const char* key, void* surface) {
	// Check if already cached (update in place)
	int target = ThumbCache_find(cache, n, key);
	if (target >= 0) {
		void* old = cache[target].surface;
		cache[target].surface = surface;
		cache[target].lru_counter = ++*counter;
		return old;
	}

	// Find empty slot
	for (int i = 0; i < n; i++) {
		if (!cache[i].occupied) {
			target = i;
			break;
		}
	}

	// No empty slot - evict LRU
	void* old = NULL;
	if (target < 0) {
		int min_lru = INT_MAX;
		for (int i = 0; i < n; i++) {
			if (cache[i].lru_counter < min_lru) {
				min_lru = cache[i].lru_counter;
				target = i;
			}
		}
		old = cache[target].surface;
	}

	strncpy(cache[target].path, key, sizeof(cache[target].path) - 1);
	cache[target].path[sizeof(cache[target].path) - 1] = '\0';
	cache[target].surface = surface;
	cache[target].lru_counter = ++*counter;
	cache[target].occupied = true;
	return old;
}
