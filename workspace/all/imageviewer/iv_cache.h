#ifndef IV_CACHE_H
#define IV_CACHE_H

// Prefetch cache of the viewer: decoded next/prev neighbors, kept in
// IV_CACHE_SLOTS slots with least-recently-used eviction and a total-pixel
// budget (IV_CACHE_PIXEL_BUDGET). Only prefetched images ever enter it.

#include <stdbool.h>
#include "sdl.h"

#define IV_CACHE_SLOTS 3
// Two largest prefetchable neighbors
#define IV_CACHE_PIXEL_BUDGET (2L * IV_PREFETCH_MAX_PIXELS)

// True when `path` is cached.
bool IvCache_contains(const char* path);

// Removes and returns a hit; ownership of the surface moves to the caller.
bool IvCache_take(const char* path, SDL_Surface** out_surf, int* out_ow, int* out_oh);

// Inserts with LRU eviction: least-recently-used entries go until there is a
// free slot and the pixel budget holds; `surf` is freed instead of cached
// when it alone is over the budget. The cache owns `surf` either way.
void IvCache_put(const char* path, SDL_Surface* surf, int ow, int oh);

// Frees every cached surface.
void IvCache_clear(void);

#endif
