// Host-compiled unit test for the viewer prefetch cache (iv_cache.c): LRU
// eviction, the pixel budget, replacing an entry, take/contains/clear. SDL is
// stubbed: SDL_GetTicks is a test-controlled clock and SDL_FreeSurface counts
// frees, so no SDL library is linked (only its headers are needed).
// Build & run (from workspace/all/imageviewer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -DUSE_SDL2 -I. -I../common -I../../tg5040/platform \
//      -DPLATFORM='"tg5040"' $(sdl2-config --cflags) -I$(sdl2-config --prefix)/include \
//      iv_cache.c tests/test_iv_cache.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../iv_cache.h"
#include "../iv_defines.h"

static Uint32 now;
static int freed;

Uint32 SDL_GetTicks(void) {
	return now;
}

void SDL_FreeSurface(SDL_Surface* s) {
	if (s) {
		freed++;
		free(s);
	}
}

static SDL_Surface* surf(void) {
	SDL_Surface* s = calloc(1, sizeof(SDL_Surface));
	assert(s);
	return s;
}

static void put(const char* path, int ow, int oh) {
	now += 10;
	IvCache_put(path, surf(), ow, oh);
}

static void test_lru_eviction(void) {
	freed = 0;
	put("/a", 100, 100);
	put("/b", 100, 100);
	put("/c", 100, 100);
	assert(IvCache_contains("/a") && IvCache_contains("/b") && IvCache_contains("/c"));

	// Re-putting /a refreshes it (and frees the old surface)
	put("/a", 100, 100);
	assert(freed == 1);

	// A fourth image evicts the least recently used: /b
	put("/d", 100, 100);
	assert(freed == 2);
	assert(!IvCache_contains("/b"));
	assert(IvCache_contains("/a") && IvCache_contains("/c") && IvCache_contains("/d"));

	IvCache_clear();
	assert(freed == 5);
	assert(!IvCache_contains("/a") && !IvCache_contains("/d"));
}

static void test_budget(void) {
	freed = 0;
	const int side = 4000; // 16 MP: the largest prefetch, two fit the budget
	put("/big1", side, side);
	put("/small", 10, 10);
	put("/big2", side, side); // over budget with /big1: the oldest (/big1) goes
	assert(freed == 1);
	assert(!IvCache_contains("/big1"));
	assert(IvCache_contains("/small") && IvCache_contains("/big2"));

	// Over the whole budget alone: freed at once, nothing evicted
	put("/huge", 9000, 9000);
	assert(freed == 2);
	assert(!IvCache_contains("/huge"));
	assert(IvCache_contains("/small") && IvCache_contains("/big2"));
	IvCache_clear();
	assert(freed == 4);
}

static void test_take(void) {
	freed = 0;
	put("/x", 30, 20);
	SDL_Surface* got = NULL;
	int ow = 0, oh = 0;
	assert(IvCache_take("/x", &got, &ow, &oh));
	assert(got && ow == 30 && oh == 20);
	assert(!IvCache_contains("/x"));
	assert(!IvCache_take("/x", &got, &ow, &oh));
	IvCache_clear();
	assert(freed == 0); // taken surfaces belong to the caller
	SDL_FreeSurface(got);
}

int main(void) {
	test_lru_eviction();
	test_budget();
	test_take();
	printf("test_iv_cache: all assertions passed\n");
	return 0;
}
