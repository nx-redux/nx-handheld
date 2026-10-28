// Stands in for pico8_64 in test_shim: draws, plays and reads the pad through SDL the way
// PICO-8 does, pacing itself with SDL_GetTicks/SDL_Delay at $FAKE_FPS (60 or 30). Each
// frame reports what it saw in the first pixels of its screen:
//   [0] frame number  [1] SDL_GetTicks  [2] pad bits (A, B, Start, Up)  [3] flags below
// With FAKE_STATES=1 it also has what shim/state.c looks for in PICO-8 (pstate, L0_cart,
// num_threads), keeps its state on the heap and in globals as PICO-8 does, and reports:
//   [4] checksum of its heap list  [5] checksum of pstate  [6] checksum of its big block
// Everything a frame depends on is a global or on the heap, never on the stack, so a
// state put back at a present continues exactly.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fake_sdl.h"

#define FLAG_INPUT_LEAKED 1

#define PITCH (128 * 4 + 64)

static uint8_t pixels[128 * PITCH];
static int16_t counter;

// What shim/state.c finds by name; global and kept in the symbol table, as in pico8_64
unsigned char pstate[64 * 1024]; // the machine: no pointers
void* L0_cart;					 // the cart's Lua state: non-NULL once the cart runs
int num_threads;				 // downloads running

// SDL handles in the writable segment: pointers a state keeps from the run it loads into
void *ren, *tex, *pad;
uint32_t flags, frame, due_x3;

struct node {
	struct node* next;
	uint32_t value;
	uint8_t pad[100];
};
static struct node* list; // churned each frame, so the arena moves
static uint8_t* big;	  // a 2 MB block loaded before the cart, as pico8.dat
#define BIG (2u << 20)

static uint32_t mix(uint32_t h, uint32_t v) {
	return (h ^ v) * 16777619u;
}

static uint32_t list_sum(void) {
	uint32_t h = 2166136261u;
	for (struct node* n = list; n; n = n->next)
		h = mix(h, n->value);
	return h;
}

static uint32_t bytes_sum(const uint8_t* p, size_t size) {
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < size; i += 61)
		h = mix(h, p[i]);
	return h;
}

static void states_frame(void) {
	if (frame == 5) { // the cart starts
		L0_cart = malloc(16);
		for (int i = 0; i < 64; i++) {
			struct node* n = calloc(1, sizeof(*n));
			n->value = (uint32_t)i;
			n->next = list;
			list = n;
		}
	}
	if (!list)
		return;
	uint32_t i = 0;
	for (struct node* n = list; n; n = n->next)
		n->value = n->value * 3 + frame + i++;
	pstate[frame % sizeof(pstate)] ^= (uint8_t)(frame * 13 + 1);
	// drop the head, add a node at the tail, now and then a bigger one
	struct node* head = list;
	list = head->next;
	free(head);
	struct node* n = list;
	while (n->next)
		n = n->next;
	n->next = calloc(1, frame % 9 == 0 ? 3000 : sizeof(*n));
	n->next->value = frame;
}

static void audio_cb(void* userdata, uint8_t* stream, int len) {
	(void)userdata;
	int16_t* s = (int16_t*)stream;
	for (int i = 0; i < len / 2; i++)
		s[i] = counter++;
}

static void put(int i, uint32_t v) {
	uint32_t argb = 0xff000000u | (v & 0xffffffu);
	memcpy(pixels + i * 4, &argb, 4);
}

// A thread of PICO-8's own, whose SDL_Delay must really sleep
static void* worker(void* arg) {
	(void)arg;
	for (;;)
		SDL_Delay(50);
	return NULL;
}

int main(void) {
	const char* fps_env = getenv("FAKE_FPS");
	int fps = fps_env ? atoi(fps_env) : 60;
	uint32_t step_x3 = fps == 30 ? 100 : 50; // 1/3 ms per frame

	// cart data, written where PICO-8 keeps it: its home's cdata/
	char path[512];
	snprintf(path, sizeof(path), "%s/cdata", getenv("P8NATIVE_HOME"));
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/cdata/fake.p8d.txt", getenv("P8NATIVE_HOME"));
	FILE* f = fopen(path, "w");
	if (!f)
		return 20;
	fputs("saved\n", f);
	fclose(f);

	const char* states_env = getenv("FAKE_STATES");
	int states = states_env && strcmp(states_env, "1") == 0;
	if (states) {
		big = malloc(BIG);
		for (size_t i = 0; i < BIG; i++)
			big[i] = (uint8_t)(i * 7 + (i >> 12));
	}

	SDL_Init(0);
	ren = SDL_CreateRenderer(SDL_CreateWindow("fake", 0, 0, 512, 512, 0), -1, 0);
	tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
	if (!tex)
		return 21;

	SDL_AudioSpec want = {.freq = 22050, .format = AUDIO_S16LSB, .channels = 1, .samples = 512, .callback = audio_cb};
	if (SDL_OpenAudio(&want, NULL) != 0 || want.size != 1024)
		return 22;
	SDL_PauseAudio(0);

	pthread_t t;
	pthread_create(&t, NULL, worker, NULL);

	if (SDL_NumJoysticks() == 1 && SDL_IsGameController(0))
		pad = SDL_GameControllerOpen(0);
	if (!pad)
		return 23;

	for (;;) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT)
				return 0;
			if (e.type >= 0x300 && e.type < 0x900)
				flags |= FLAG_INPUT_LEAKED;
		}
		while (SDL_GetTicks() * 3 < due_x3)
			SDL_Delay(1);
		due_x3 += step_x3;

		uint32_t bits = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) |
						SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) << 1 |
						SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) << 2 |
						SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) << 3;
		SDL_LockAudio(); // as PICO-8 does around its sound state
		SDL_UnlockAudio();
		for (int y = 0; y < 128; y++)
			for (int x = 0; x < 128; x++)
				memcpy(pixels + y * PITCH + x * 4, &(uint32_t){0xff123456u}, 4);
		put(0, frame++);
		put(1, SDL_GetTicks());
		put(2, bits);
		put(3, flags);
		if (states) {
			states_frame();
			put(4, list_sum());
			put(5, bytes_sum(pstate, sizeof(pstate)));
			put(6, bytes_sum(big, BIG));
		}
		SDL_UpdateTexture(tex, NULL, pixels, PITCH);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
	}
}
