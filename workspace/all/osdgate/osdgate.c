// libosdgate.so — LD_PRELOAD input gate for standalone SDL2 apps (ffplay, NextCommander).
//
// On devices where trimui_osdd's hardcoded evdev grab misses the physical pad
// (Brick Pro: pad is /dev/input/event3 while trimui_osdd grabs event4), button
// presses inside the OSD panel leak through to standalone SDL2 processes that do
// not go through common/api.c's PAD_poll(). Pressing B to close the OSD then exits
// a playing video in ffplay or navigates up a folder in NextCommander.
//
// Interposes SDL_PollEvent, SDL_PeepEvents (SDL_GETEVENT), and SDL_GetKeyboardState:
// while /tmp/trimui_osd/osdd_show_up exists — plus a 180 ms grace window after it
// disappears so the dismiss button's release/keyup tail is also absorbed — keyboard,
// joystick, and controller input events are dropped while SDL_QUIT, SDL_WINDOWEVENT,
// device hotplug, and SDL_USEREVENT+ (ffplay's video refresh timer) pass through.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_OSD_STATE_PATH "/tmp/trimui_osd/osdd_show_up"
#define POST_CLOSE_GRACE_MS 180
#define SDL2_EVENT_BYTES 56
#define SDL2_GETEVENT 2

typedef int (*poll_fn)(void* event);
typedef int (*peep_fn)(void* events, int numevents, int action, uint32_t minType, uint32_t maxType);
typedef const uint8_t* (*kbstate_fn)(int* numkeys);

static uint64_t monotonic_ms(void) {
	struct timespec ts = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static const char* state_path(void) {
	static const char* cached;
	if (!cached) {
		const char* env = getenv("OSDGATE_STATE_PATH");
		cached = (env && env[0]) ? env : DEFAULT_OSD_STATE_PATH;
	}
	return cached;
}

static int osd_is_blocking(void) {
	static int was_open = 0;
	static uint64_t grace_until_ms = 0;

	if (access(state_path(), F_OK) == 0) {
		was_open = 1;
		return 1;
	}
	if (was_open) {
		was_open = 0;
		grace_until_ms = monotonic_ms() + POST_CLOSE_GRACE_MS;
		return 1;
	}
	if (grace_until_ms != 0) {
		if (monotonic_ms() < grace_until_ms)
			return 1;
		grace_until_ms = 0;
	}
	return 0;
}

static int is_input_event(uint32_t type) {
	// Keyboard (0x300..0x305), mouse (0x400..0x403)
	if (type >= 0x300 && type <= 0x403)
		return 1;
	// Joystick axis/ball/hat/button down/up (0x600..0x604); keep 0x605..0x606 hotplug
	if (type >= 0x600 && type <= 0x604)
		return 1;
	// Controller axis/button down/up (0x650..0x652); keep 0x653+ hotplug/remap
	if (type >= 0x650 && type <= 0x652)
		return 1;
	return 0;
}

int SDL_PollEvent(void* event) {
	static poll_fn real_poll;
	if (!real_poll)
		real_poll = (poll_fn)dlsym(RTLD_NEXT, "SDL_PollEvent");
	if (!real_poll)
		return 0;
	if (!event)
		return real_poll(NULL);

	for (;;) {
		int r = real_poll(event);
		if (r <= 0)
			return r;
		if (!osd_is_blocking())
			return r;
		uint32_t type = 0;
		memcpy(&type, event, sizeof(type));
		if (!is_input_event(type))
			return r;
	}
}

int SDL_PeepEvents(void* events, int numevents, int action, uint32_t minType, uint32_t maxType) {
	static peep_fn real_peep;
	if (!real_peep)
		real_peep = (peep_fn)dlsym(RTLD_NEXT, "SDL_PeepEvents");
	if (!real_peep)
		return 0;

	if (action != SDL2_GETEVENT || !events || numevents <= 0 || !osd_is_blocking())
		return real_peep(events, numevents, action, minType, maxType);

	uint8_t* bytes = (uint8_t*)events;
	for (;;) {
		int n = real_peep(events, numevents, action, minType, maxType);
		if (n <= 0)
			return n;
		int kept = 0;
		for (int i = 0; i < n; i++) {
			uint32_t type = 0;
			memcpy(&type, bytes + (size_t)i * SDL2_EVENT_BYTES, sizeof(type));
			if (!is_input_event(type)) {
				if (kept != i)
					memmove(bytes + (size_t)kept * SDL2_EVENT_BYTES,
							bytes + (size_t)i * SDL2_EVENT_BYTES,
							SDL2_EVENT_BYTES);
				kept++;
			}
		}
		if (kept > 0)
			return kept;
	}
}

const uint8_t* SDL_GetKeyboardState(int* numkeys) {
	static kbstate_fn real_kbstate;
	static const uint8_t zero_keys[512] = {0};
	if (!real_kbstate)
		real_kbstate = (kbstate_fn)dlsym(RTLD_NEXT, "SDL_GetKeyboardState");
	if (!real_kbstate) {
		if (numkeys)
			*numkeys = (int)sizeof(zero_keys);
		return zero_keys;
	}

	int real_n = 0;
	const uint8_t* real_ptr = real_kbstate(&real_n);
	if (!osd_is_blocking()) {
		if (numkeys)
			*numkeys = real_n;
		return real_ptr;
	}
	if (numkeys)
		*numkeys = (real_n > 0 && real_n < (int)sizeof(zero_keys)) ? real_n : (int)sizeof(zero_keys);
	return zero_keys;
}
