// Stands in for pico8_64 in test_shim: draws, plays and reads the pad through SDL the way
// PICO-8 does, pacing itself with SDL_GetTicks/SDL_Delay at $FAKE_FPS (60 or 30). Each
// frame reports what it saw in the first pixels of its screen:
//   [0] frame number  [1] SDL_GetTicks  [2] pad bits (A, B, Start, Up)  [3] flags below
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

	SDL_Init(0);
	void* ren = SDL_CreateRenderer(SDL_CreateWindow("fake", 0, 0, 512, 512, 0), -1, 0);
	void* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
	if (!tex)
		return 21;

	SDL_AudioSpec want = {.freq = 22050, .format = AUDIO_S16LSB, .channels = 1, .samples = 512, .callback = audio_cb};
	if (SDL_OpenAudio(&want, NULL) != 0 || want.size != 1024)
		return 22;
	SDL_PauseAudio(0);

	pthread_t t;
	pthread_create(&t, NULL, worker, NULL);

	void* pad = NULL;
	uint32_t flags = 0, frame = 0, due_x3 = 0;
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
		SDL_UpdateTexture(tex, NULL, pixels, PITCH);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
	}
}
