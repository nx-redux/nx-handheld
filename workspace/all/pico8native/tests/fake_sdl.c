// A stand-in libSDL2-2.0.so.0 for test_shim: what the shim forwards to. Calls the shim must
// never let through (it answers them itself) abort, so the test sees them as a crash.
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "fake_sdl.h"

static char window, renderer, texture;
static int key_sent;

int SDL_Init(uint32_t flags) {
	(void)flags;
	return 0;
}

void* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags) {
	(void)title, (void)x, (void)y, (void)w, (void)h, (void)flags;
	return &window;
}

void* SDL_CreateRenderer(void* win, int index, uint32_t flags) {
	(void)win, (void)index, (void)flags;
	return &renderer;
}

void* SDL_CreateTexture(void* ren, uint32_t format, int access, int w, int h) {
	(void)ren, (void)format, (void)access, (void)w, (void)h;
	return &texture;
}

int SDL_UpdateTexture(void* tex, const void* rect, const void* pixels, int pitch) {
	(void)tex, (void)rect, (void)pixels, (void)pitch;
	return 0;
}

int SDL_RenderClear(void* ren) {
	(void)ren;
	return 0;
}

int SDL_RenderCopy(void* ren, void* tex, const void* src, const void* dst) {
	(void)ren, (void)tex, (void)src, (void)dst;
	return 0;
}

void SDL_RenderPresent(void* ren) {
	(void)ren;
}

// One real key press, which must not reach the game
int SDL_PollEvent(SDL_Event* event) {
	if (key_sent)
		return 0;
	key_sent = 1;
	if (event)
		event->type = SDL_KEYDOWN;
	return 1;
}

// Only PICO-8's other threads may really sleep
void SDL_Delay(uint32_t ms) {
	if (syscall(SYS_gettid) == getpid())
		abort();
	usleep(ms * 1000);
}

uint32_t SDL_GetTicks(void) {
	abort();
}

int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
	(void)desired, (void)obtained;
	abort();
}

void SDL_PauseAudio(int pause_on) {
	(void)pause_on;
	abort();
}

void SDL_LockAudio(void) {
	abort();
}

void SDL_UnlockAudio(void) {
	abort();
}

int SDL_NumJoysticks(void) {
	return 0;
}

int SDL_IsGameController(int index) {
	(void)index;
	return 0;
}

void* SDL_GameControllerOpen(int index) {
	(void)index;
	return NULL;
}

uint8_t SDL_GameControllerGetButton(void* pad, int button) {
	(void)pad, (void)button;
	abort();
}
