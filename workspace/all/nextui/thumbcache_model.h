#ifndef THUMBCACHE_MODEL_H
#define THUMBCACHE_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include "defines.h"

// The thumbnail loader's small LRU cache of decoded pictures (imgloader.c), SDL-free so it can be host-tested
// (common/tests/test_thumbcache_model.c). The surfaces are opaque here: whoever gets one back frees it.
//
// Keyed by path and compose mode (ThumbCache_key): the screenshot's faded background and the fitted box (List art =
// Mix, 2D box art or Wheel) are different surfaces of what may be the same file (a Port's root picture is both its
// screenshot and its legacy mix), so a Layouts > List art switch never reuses the other mode's surface.

#define THUMB_CACHE_SIZE 8
// "fit:" + a path startLoadThumb accepts (shorter than MAX_PATH, the task's buffer): every key fits whole
#define THUMB_KEY_MAX (MAX_PATH + 8)

typedef struct {
	char path[THUMB_KEY_MAX]; // ThumbCache_key
	void* surface;			  // NULL: a remembered miss (no file, or it failed to decode), not retried until invalidated
	int lru_counter;
	bool occupied;
} ThumbCacheEntry;

// The cache key for `path`: the path itself for the background, "fit:" + path for the fitted box.
void ThumbCache_key(const char* path, bool fit, char* out, size_t size);

// The slot holding key, or -1.
int ThumbCache_find(const ThumbCacheEntry* cache, int n, const char* key);

// Takes over the caller's reference to `surface` (NULL caches a miss) under key: in place when key is cached, else
// in a free slot, else in the least recently used one. *counter is the cache's LRU clock. Returns the surface the
// slot held (the replaced or evicted one, for the caller to free), or NULL.
void* ThumbCache_insert(ThumbCacheEntry* cache, int n, int* counter, const char* key, void* surface);

#endif
