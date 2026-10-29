// The slice of SDL2 that fake_pico8 uses, as fake_sdl.c (built as libSDL2-2.0.so.0) gives it.
#ifndef FAKE_SDL_H
#define FAKE_SDL_H

#include <stdint.h>

typedef struct {
	int freq;
	uint16_t format;
	uint8_t channels;
	uint8_t silence;
	uint16_t samples;
	uint16_t padding;
	uint32_t size;
	void (*callback)(void* userdata, uint8_t* stream, int len);
	void* userdata;
} SDL_AudioSpec;

typedef union {
	uint32_t type;
	uint8_t padding[56];
} SDL_Event;

#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define AUDIO_S16LSB 0x8010
#define SDL_CONTROLLER_BUTTON_A 0
#define SDL_CONTROLLER_BUTTON_B 1
#define SDL_CONTROLLER_BUTTON_START 6
#define SDL_CONTROLLER_BUTTON_DPAD_UP 11

int SDL_Init(uint32_t flags);
void* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags);
void* SDL_CreateRenderer(void* window, int index, uint32_t flags);
void* SDL_CreateTexture(void* renderer, uint32_t format, int access, int w, int h);
int SDL_UpdateTexture(void* texture, const void* rect, const void* pixels, int pitch);
int SDL_RenderClear(void* renderer);
int SDL_RenderCopy(void* renderer, void* texture, const void* src, const void* dst);
void SDL_RenderPresent(void* renderer);
int SDL_PollEvent(SDL_Event* event);
void SDL_Delay(uint32_t ms);
uint32_t SDL_GetTicks(void);
int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
int SDL_NumJoysticks(void);
int SDL_IsGameController(int index);
void* SDL_GameControllerOpen(int index);
uint8_t SDL_GameControllerGetButton(void* pad, int button);

#endif
