// Stands in for pico8_64 -splore in test_standalone.sh, run under pico8shim.so in
// standalone mode against fake_sdl_standalone.c. Starts SDL as PICO-8 does (every
// subsystem, a vsync renderer, legacy audio, the first controller), then runs frames,
// logging to stderr what it sees: "game: frame <n> ticks <ms>", every pad event and every
// change in the buttons it reads, and "game: quit at frame <n>" when SDL_QUIT comes.
// FAKE_FRAMES frames (default 60), then "game: done".
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fake_sdl.h"

int SDL_InitSubSystem(uint32_t flags);

#define LOG(...) fprintf(stderr, "game: " __VA_ARGS__)

static uint32_t pixels[128 * 128];

int main(void) {
	const char* frames_env = getenv("FAKE_FRAMES");
	int frames = frames_env ? atoi(frames_env) : 60;

	// cart data, written where PICO-8 keeps it: its home's cdata/
	const char* home = getenv("P8NATIVE_HOME");
	if (home) {
		char path[512];
		snprintf(path, sizeof(path), "%s/cdata", home);
		mkdir(path, 0755);
		snprintf(path, sizeof(path), "%s/cdata/splore.p8d.txt", home);
		FILE* f = fopen(path, "w");
		if (!f)
			return 20;
		fputs("saved\n", f);
		fclose(f);
	}

	if (SDL_Init(0xf231) != 0 || SDL_InitSubSystem(0x8000) != 0)
		return 21;
	void* ren = SDL_CreateRenderer(SDL_CreateWindow("pico-8", 0, 0, 1024, 768, 0x1001), -1, 0x4);
	void* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128);
	SDL_AudioSpec want = {.freq = 22050, .format = AUDIO_S16LSB, .channels = 1, .samples = 1024};
	if (SDL_OpenAudio(&want, NULL) != 0)
		return 22;
	SDL_PauseAudio(0);
	void* pad = SDL_NumJoysticks() > 0 && SDL_IsGameController(0) ? SDL_GameControllerOpen(0) : NULL;
	if (!pad)
		return 23;

	uint32_t seen = 0;
	for (int frame = 0; frame < frames; frame++) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				LOG("quit at frame %d\n", frame);
				return 0;
			}
			if (e.type == 0x651 || e.type == 0x652)
				LOG("event %s %d\n", e.type == 0x651 ? "down" : "up", e.padding[12]);
		}
		uint32_t held = 0;
		for (int b = 0; b < 21; b++)
			held |= (uint32_t)SDL_GameControllerGetButton(pad, b) << b;
		for (int b = 0; b < 21; b++)
			if ((held ^ seen) >> b & 1)
				LOG("button %d %s\n", b, held >> b & 1 ? "held" : "let go");
		seen = held;
		LOG("frame %d ticks %u\n", frame, SDL_GetTicks());
		SDL_LockAudio();
		SDL_UnlockAudio();
		SDL_UpdateTexture(tex, NULL, pixels, 128 * 4);
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, NULL, NULL);
		SDL_RenderPresent(ren);
		SDL_Delay(5);
	}
	LOG("done\n");
	return 0;
}
