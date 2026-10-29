// A stand-in libSDL2-2.0.so.0 for test_standalone.sh: the device SDL that Splore runs
// pico8shim.so against in standalone mode. Everything the shim passes through is logged
// to stderr as "sdl: ...", in order with fake_splore's "game: ..." lines.
//
// The pad follows FAKE_PAD, "<present>:<mask>,...": from that many presents on (the
// game's and the dialog's together), the SDL controller buttons held are mask. Events
// for each change come from SDL_PollEvent, so events and button state agree.
// FAKE_QUIT_AT=<present> queues an SDL_QUIT there (power-off's SIGTERM).
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) fprintf(stderr, "sdl: " __VA_ARGS__)

typedef struct {
	int x, y, w, h;
} SDL_Rect;

static char window, renderer, textures[64], pad, joy;
static int next_texture;
static unsigned presents, fills, copies;
static uint32_t evented; // the pad as the events so far told it
static int playing, quit_sent;
static struct timespec start;

static uint32_t pad_now(void) {
	const char* s = getenv("FAKE_PAD");
	uint32_t mask = 0;
	while (s && *s) {
		char* end;
		unsigned at = (unsigned)strtoul(s, &end, 10);
		if (*end != ':')
			break;
		uint32_t m = (uint32_t)strtoul(end + 1, &end, 0);
		if (at <= presents)
			mask = m;
		s = *end == ',' ? end + 1 : end;
	}
	return mask;
}

int SDL_Init(uint32_t flags) {
	clock_gettime(CLOCK_MONOTONIC, &start);
	LOG("init 0x%x\n", flags);
	return 0;
}

int SDL_InitSubSystem(uint32_t flags) {
	LOG("initsubsystem 0x%x\n", flags);
	return 0;
}

void* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags) {
	(void)title, (void)x, (void)y, (void)w, (void)h, (void)flags;
	return &window;
}

void* SDL_CreateRenderer(void* win, int index, uint32_t flags) {
	(void)win, (void)index;
	LOG("renderer flags 0x%x\n", flags);
	return &renderer;
}

int SDL_SetHint(const char* name, const char* value) {
	LOG("hint %s=%s\n", name, value);
	return 1;
}

void* SDL_CreateTexture(void* ren, uint32_t format, int access, int w, int h) {
	(void)ren, (void)format, (void)access;
	LOG("texture %dx%d\n", w, h);
	return next_texture < 64 ? &textures[next_texture++] : NULL;
}

int SDL_UpdateTexture(void* tex, const void* rect, const void* pixels, int pitch) {
	(void)tex, (void)rect, (void)pixels, (void)pitch;
	return 0;
}

void SDL_DestroyTexture(void* tex) {
	(void)tex;
}

int SDL_RenderClear(void* ren) {
	(void)ren;
	return 0;
}

int SDL_RenderCopy(void* ren, void* tex, const void* src, const void* dst) {
	(void)ren, (void)tex, (void)src, (void)dst;
	copies++;
	return 0;
}

int SDL_RenderFillRect(void* ren, const SDL_Rect* rect) {
	(void)ren, (void)rect;
	fills++;
	return 0;
}

void SDL_RenderPresent(void* ren) {
	(void)ren;
	presents++;
	LOG("present %u fills %u copies %u\n", presents, fills, copies);
	fills = copies = 0;
}

int SDL_GetRendererOutputSize(void* ren, int* w, int* h) {
	(void)ren;
	*w = 1024;
	*h = 768;
	return 0;
}

int SDL_RenderReadPixels(void* ren, const SDL_Rect* rect, uint32_t format, void* pixels, int pitch) {
	(void)ren, (void)rect, (void)format;
	memset(pixels, 0x40, (size_t)pitch * 768);
	LOG("read pixels\n");
	return 0;
}

void* SDL_CreateTextureFromSurface(void* ren, void* surface) {
	(void)ren, (void)surface;
	return NULL;
}

void SDL_FreeSurface(void* surface) {
	(void)surface;
}

int SDL_QueryTexture(void* tex, uint32_t* format, int* access, int* w, int* h) {
	(void)tex, (void)format, (void)access, (void)w, (void)h;
	return -1;
}

int SDL_SetTextureBlendMode(void* tex, int mode) {
	(void)tex, (void)mode;
	return 0;
}

static uint8_t colour[4];
static int blend;
int SDL_GetRenderDrawColor(void* ren, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
	(void)ren;
	*r = colour[0], *g = colour[1], *b = colour[2], *a = colour[3];
	return 0;
}
int SDL_SetRenderDrawColor(void* ren, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	(void)ren;
	colour[0] = r, colour[1] = g, colour[2] = b, colour[3] = a;
	return 0;
}
int SDL_GetRenderDrawBlendMode(void* ren, int* mode) {
	(void)ren;
	*mode = blend;
	return 0;
}
int SDL_SetRenderDrawBlendMode(void* ren, int mode) {
	(void)ren;
	blend = mode;
	return 0;
}
void* SDL_GetRenderTarget(void* ren) {
	(void)ren;
	return NULL;
}
int SDL_SetRenderTarget(void* ren, void* tex) {
	(void)ren, (void)tex;
	return 0;
}
void SDL_RenderGetLogicalSize(void* ren, int* w, int* h) {
	(void)ren;
	*w = *h = 0;
}
int SDL_RenderSetLogicalSize(void* ren, int w, int h) {
	(void)ren, (void)w, (void)h;
	return 0;
}
void SDL_RenderGetViewport(void* ren, SDL_Rect* r) {
	(void)ren;
	*r = (SDL_Rect){128, 0, 768, 768}; // PICO-8's square, letterboxed
}
int SDL_RenderSetViewport(void* ren, const SDL_Rect* r) {
	(void)ren;
	if (r)
		LOG("viewport %d,%d %dx%d\n", r->x, r->y, r->w, r->h);
	else
		LOG("viewport whole\n");
	return 0;
}
void SDL_RenderGetScale(void* ren, float* x, float* y) {
	(void)ren;
	*x = *y = 1.0f;
}
int SDL_RenderSetScale(void* ren, float x, float y) {
	(void)ren, (void)x, (void)y;
	return 0;
}
int SDL_RenderIsClipEnabled(void* ren) {
	(void)ren;
	return 0;
}
void SDL_RenderGetClipRect(void* ren, SDL_Rect* r) {
	(void)ren;
	*r = (SDL_Rect){0, 0, 0, 0};
}
int SDL_RenderSetClipRect(void* ren, const SDL_Rect* r) {
	(void)ren, (void)r;
	return 0;
}
const char* SDL_GetError(void) {
	return "fake";
}

// SDL_ControllerButtonEvent: type, timestamp, which, button, state
static void button_event(void* event, uint32_t type, int button) {
	uint8_t ev[56] = {0};
	memcpy(ev, &type, 4);
	ev[12] = (uint8_t)button;
	ev[13] = type == 0x651;
	memcpy(event, ev, sizeof(ev));
}

int SDL_PollEvent(void* event) {
	const char* quit_at = getenv("FAKE_QUIT_AT");
	if (quit_at && !quit_sent && presents >= (unsigned)atoi(quit_at)) {
		if (event) {
			quit_sent = 1;
			uint8_t ev[56] = {0};
			ev[0] = 0x00, ev[1] = 0x01; // SDL_QUIT
			memcpy(event, ev, sizeof(ev));
			LOG("SDL_QUIT queued\n");
		}
		return 1;
	}
	uint32_t now = pad_now(), changed = now ^ evented;
	if (!changed)
		return 0;
	if (!event)
		return 1;
	int b = __builtin_ctz(changed);
	evented ^= 1u << b;
	button_event(event, (now >> b) & 1 ? 0x651 : 0x652, b);
	return 1;
}

void SDL_Delay(uint32_t ms) {
	usleep(ms * 1000);
}

uint32_t SDL_GetTicks(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)((t.tv_sec - start.tv_sec) * 1000 + (t.tv_nsec - start.tv_nsec) / 1000000);
}

int SDL_OpenAudio(void* desired, void* obtained) {
	(void)desired, (void)obtained;
	LOG("open audio\n");
	return 0;
}

void SDL_PauseAudio(int on) {
	playing = !on;
	LOG("pause audio %d\n", on);
}

void SDL_PauseAudioDevice(uint32_t dev, int on) {
	if (dev == 1)
		playing = !on;
	LOG("pause device %u %d\n", dev, on);
}

int SDL_GetAudioDeviceStatus(uint32_t dev) {
	return dev == 1 && playing ? 1 : 2;
}

void SDL_LockAudio(void) {
	LOG("lock audio\n");
}

void SDL_UnlockAudio(void) {}

int SDL_NumJoysticks(void) {
	return 1;
}

int SDL_IsGameController(int index) {
	return index == 0;
}

void* SDL_GameControllerOpen(int index) {
	LOG("controller open %d\n", index);
	return index == 0 ? &pad : NULL;
}

void* SDL_GameControllerGetJoystick(void* p) {
	return p == &pad ? &joy : NULL;
}

// the state as the events so far left it, as SDL's is
uint8_t SDL_GameControllerGetButton(void* p, int button) {
	return p == &pad && button >= 0 && button < 32 ? (evented >> button) & 1 : 0;
}

int SDL_JoystickNumButtons(void* j) {
	return j == &joy ? 11 : 0;
}

// raw button 8 is Menu, as guide (5) in the mapping
uint8_t SDL_JoystickGetButton(void* j, int button) {
	return j == &joy && button == 8 ? (evented >> 5) & 1 : 0;
}
