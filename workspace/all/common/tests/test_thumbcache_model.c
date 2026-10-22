// Host test for nextui/thumbcache_model.c: the thumbnail cache's keys, in-place update and LRU eviction. Built with
// AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../nextui/thumbcache_model.h"

static int pics[32]; // stand-in surfaces

int main(void) {
	char bg[THUMB_KEY_MAX], fit[THUMB_KEY_MAX];
	ThumbCache_key("/sd/a.png", false, bg, sizeof(bg));
	ThumbCache_key("/sd/a.png", true, fit, sizeof(fit));
	assert(strcmp(bg, "/sd/a.png") == 0 && strcmp(fit, "fit:/sd/a.png") == 0); // the two modes never share
	ThumbCache_key("", true, fit, sizeof(fit));
	assert(strcmp(fit, "") == 0);

	static ThumbCacheEntry cache[THUMB_CACHE_SIZE];
	int clock = 0;
	char key[THUMB_KEY_MAX];
	for (int i = 0; i < THUMB_CACHE_SIZE; i++) { // fills the free slots: nothing to free
		snprintf(key, sizeof(key), "/sd/%d.png", i);
		assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, key, &pics[i]) == NULL);
	}
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "/sd/3.png") == 3);
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "fit:/sd/3.png") == -1);

	// in place: the old surface comes back to be freed, the slot is the newest
	assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, "/sd/0.png", &pics[20]) == &pics[0]);
	assert(cache[0].surface == &pics[20] && cache[0].lru_counter == clock);
	// a remembered miss (NULL) is cached like a picture
	assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, "/sd/1.png", NULL) == &pics[1]);
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "/sd/1.png") == 1 && cache[1].surface == NULL);

	// full: the least recently used slot goes (2: 0 and 1 were refreshed above)
	assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, "/sd/new.png", &pics[21]) == &pics[2]);
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "/sd/2.png") == -1);
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "/sd/new.png") == 2);
	// a lookup's bump (as startLoadThumb does) protects a slot
	cache[3].lru_counter = ++clock;
	assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, "/sd/newer.png", &pics[22]) == &pics[4]);
	// evicting a remembered miss hands back nothing to free
	for (int i = 0; i < THUMB_CACHE_SIZE; i++)
		if (i != 1)
			cache[i].lru_counter = clock + 10;
	assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, "/sd/x.png", &pics[23]) == NULL);
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "/sd/1.png") == -1);

	// a freed slot (invalidated) is reused before any eviction
	cache[5].occupied = false;
	cache[5].surface = NULL;
	assert(ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, "/sd/y.png", &pics[24]) == NULL);
	assert(ThumbCache_find(cache, THUMB_CACHE_SIZE, "/sd/y.png") == 5);

	// an over-long key is cut to the slot, never past it
	char longkey[THUMB_KEY_MAX + 50];
	memset(longkey, 'k', sizeof(longkey) - 1);
	longkey[sizeof(longkey) - 1] = '\0';
	ThumbCache_insert(cache, THUMB_CACHE_SIZE, &clock, longkey, &pics[25]);
	int slot = -1;
	for (int i = 0; i < THUMB_CACHE_SIZE; i++)
		if (cache[i].surface == &pics[25])
			slot = i;
	assert(slot >= 0 && strlen(cache[slot].path) == THUMB_KEY_MAX - 1);
	printf("test_thumbcache_model: OK\n");
	return 0;
}
