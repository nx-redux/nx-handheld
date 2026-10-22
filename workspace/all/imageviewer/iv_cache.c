// Viewer prefetch cache: 3-slot LRU, bounded by a total-pixel budget (see
// iv_cache.h). Split out of iv_viewer.c so it can be unit-tested on the host:
// see tests/test_iv_cache.c.
#include <stdio.h>
#include <string.h>

#include "iv_cache.h"
#include "iv_defines.h"

typedef struct {
	char path[512];
	SDL_Surface* surf;
	int ow, oh;
	uint32_t lru;
	bool used;
} IvCacheSlot;

static IvCacheSlot vw_cache[IV_CACHE_SLOTS];

bool IvCache_contains(const char* path) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++)
		if (vw_cache[i].used && strcmp(vw_cache[i].path, path) == 0)
			return true;
	return false;
}

bool IvCache_take(const char* path, SDL_Surface** out_surf, int* out_ow, int* out_oh) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (vw_cache[i].used && strcmp(vw_cache[i].path, path) == 0) {
			*out_surf = vw_cache[i].surf;
			*out_ow = vw_cache[i].ow;
			*out_oh = vw_cache[i].oh;
			vw_cache[i].used = false;
			vw_cache[i].surf = NULL;
			return true;
		}
	}
	return false;
}

static long cache_total_pixels(void) {
	long total = 0;
	for (int i = 0; i < IV_CACHE_SLOTS; i++)
		if (vw_cache[i].used)
			total += (long)vw_cache[i].ow * vw_cache[i].oh;
	return total;
}

void IvCache_put(const char* path, SDL_Surface* surf, int ow, int oh) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (vw_cache[i].used && strcmp(vw_cache[i].path, path) == 0) {
			SDL_FreeSurface(vw_cache[i].surf);
			vw_cache[i].surf = surf;
			vw_cache[i].ow = ow;
			vw_cache[i].oh = oh;
			vw_cache[i].lru = SDL_GetTicks();
			return;
		}
	}

	if ((long)ow * oh > IV_CACHE_PIXEL_BUDGET) {
		SDL_FreeSurface(surf);
		return;
	}

	int slot = -1;
	for (;;) {
		int oldest = -1;
		slot = -1;
		for (int i = 0; i < IV_CACHE_SLOTS; i++) {
			if (!vw_cache[i].used)
				slot = i;
			else if (oldest < 0 || vw_cache[i].lru < vw_cache[oldest].lru)
				oldest = i;
		}
		if ((slot >= 0 && cache_total_pixels() + (long)ow * oh <= IV_CACHE_PIXEL_BUDGET) || oldest < 0)
			break;
		SDL_FreeSurface(vw_cache[oldest].surf);
		vw_cache[oldest].surf = NULL;
		vw_cache[oldest].used = false;
	}

	vw_cache[slot].used = true;
	snprintf(vw_cache[slot].path, sizeof(vw_cache[slot].path), "%s", path);
	vw_cache[slot].surf = surf;
	vw_cache[slot].ow = ow;
	vw_cache[slot].oh = oh;
	vw_cache[slot].lru = SDL_GetTicks();
}

void IvCache_clear(void) {
	for (int i = 0; i < IV_CACHE_SLOTS; i++) {
		if (vw_cache[i].used) {
			SDL_FreeSurface(vw_cache[i].surf);
			vw_cache[i].surf = NULL;
			vw_cache[i].used = false;
		}
	}
}
