// Host test for workspace/all/osdgate/osdgate.c: verifies SDL_PollEvent,
// SDL_PeepEvents (SDL_GETEVENT), SDL_GetKeyboardState, pass-through of
// SDL_QUIT / FF_REFRESH_EVENT (SDL_USEREVENT), and the post-close release
// grace window.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SDL2_EVENT_BYTES 56
#define SDL2_GETEVENT 2
#define SDL_QUIT_T 0x100u
#define SDL_KEYDOWN_T 0x300u
#define SDL_JOYBUTTONDOWN_T 0x603u
#define SDL_JOYBUTTONUP_T 0x604u
#define SDL_JOYDEVICEADDED_T 0x605u
#define SDL_USEREVENT_T 0x8000u

typedef struct {
	uint32_t type;
	uint8_t pad[SDL2_EVENT_BYTES - sizeof(uint32_t)];
} FakeEvent;

// Exported by the fake SDL2 shared library compiled in test-osdgate.sh
extern void fake_sdl_push(uint32_t type);
extern void fake_sdl_set_key(int scancode, uint8_t pressed);
extern int SDL_PollEvent(void* event);
extern int SDL_PeepEvents(void* events, int numevents, int action, uint32_t minType, uint32_t maxType);
extern const uint8_t* SDL_GetKeyboardState(int* numkeys);

static void touch_file(const char* path) {
	FILE* f = fopen(path, "w");
	assert(f);
	fclose(f);
}

int main(void) {
	const char* flag = getenv("OSDGATE_STATE_PATH");
	assert(flag && flag[0]);
	unlink(flag);

	// 1. OSD closed: input events pass through normally
	fake_sdl_push(SDL_JOYBUTTONDOWN_T);
	FakeEvent ev = {0};
	assert(SDL_PollEvent(&ev) == 1 && ev.type == SDL_JOYBUTTONDOWN_T);
	fake_sdl_set_key(40, 1);
	int nk = 0;
	assert(SDL_GetKeyboardState(&nk)[40] == 1);

	// 2. OSD open: SDL_PollEvent drops key/joy input, preserves SDL_QUIT & hotplug
	touch_file(flag);
	fake_sdl_push(SDL_KEYDOWN_T);
	fake_sdl_push(SDL_JOYBUTTONDOWN_T);
	fake_sdl_push(SDL_JOYDEVICEADDED_T);
	fake_sdl_push(SDL_QUIT_T);
	assert(SDL_PollEvent(&ev) == 1 && ev.type == SDL_JOYDEVICEADDED_T);
	assert(SDL_PollEvent(&ev) == 1 && ev.type == SDL_QUIT_T);
	assert(SDL_PollEvent(&ev) == 0);
	assert(SDL_GetKeyboardState(&nk)[40] == 0);

	// 3. OSD open: SDL_PeepEvents(SDL_GETEVENT) drops input, keeps FF_REFRESH_EVENT
	fake_sdl_push(SDL_KEYDOWN_T);
	fake_sdl_push(SDL_USEREVENT_T);
	FakeEvent batch[2];
	memset(batch, 0, sizeof(batch));
	int got = SDL_PeepEvents(batch, 2, SDL2_GETEVENT, 0, 0xFFFFu);
	assert(got == 1 && batch[0].type == SDL_USEREVENT_T);

	// 4. OSD closes: button-release tail inside the 180 ms grace window is absorbed
	unlink(flag);
	fake_sdl_push(SDL_JOYBUTTONUP_T);
	assert(SDL_PollEvent(&ev) == 0);

	// 5. After grace window expires, normal input resumes
	usleep(210 * 1000);
	fake_sdl_push(SDL_JOYBUTTONDOWN_T);
	assert(SDL_PollEvent(&ev) == 1 && ev.type == SDL_JOYBUTTONDOWN_T);
	assert(SDL_GetKeyboardState(&nk)[40] == 1);

	printf("test_osdgate: all tests passed\n");
	return 0;
}
