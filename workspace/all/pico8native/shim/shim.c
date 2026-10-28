/*
 * pico8shim.so: LD_PRELOAD'ed into pico8_64, which the pico8native core runs with SDL's
 * headless video driver and dummy audio driver. It turns PICO-8 into something the core
 * can step one frame at a time over the p8ipc block (P8NATIVE_IPC):
 *
 * - video: the 128x128 screen texture PICO-8 uploads with SDL_UpdateTexture is converted
 *   into ipc->fb; SDL_RenderPresent ends the frame. Nothing is drawn to the window.
 * - pacing: at the end of a frame the shim posts frame_done and waits for the next
 *   frame_req. SDL_GetTicks runs on the time the core grants (clock.h), and PICO-8's
 *   SDL_Delay calls move that clock instead of sleeping.
 * - audio: no device is opened; PICO-8's callback is pulled for each frame's worth of
 *   samples into ipc->audio (audio.h).
 * - input: one virtual controller whose buttons are ipc->buttons (input.h); real input
 *   events never reach PICO-8.
 * - cart saves: file calls under $P8NATIVE_HOME/cdata go to $P8NATIVE_CDATA (paths.h).
 * - save states: P8CMD_SAVE/LOAD snapshot and put back PICO-8's memory while it is
 *   parked at a present (state.h); malloc and friends are state.c's, so PICO-8's own
 *   allocations land in its fixed-address arena.
 *
 * Which SDL calls PICO-8 makes, and wrapping them through dlsym(RTLD_NEXT) without SDL
 * headers, follows josegonzalez/minui-pico-8-pak's shim/sdl-display.c (MIT licence).
 *
 * Everything not marked EXPORT is hidden (-fvisibility=hidden) so nothing here can bind
 * to a name in PICO-8. Logs go to stderr with a "pico8shim:" prefix; the core keeps them.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "../p8ipc.h"
#include "../p8state.h"
#include "audio.h"
#include "clock.h"
#include "frame.h"
#include "input.h"
#include "paths.h"
#include "state.h"

#define EXPORT __attribute__((visibility("default")))
#define LOG(...) fprintf(stderr, "pico8shim: " __VA_ARGS__)

// How long one wait for frame_req lasts before checking the core is still there. Its menu
// stops retro_run for as long as it is open, so a timeout alone does not mean it is gone.
#define WAIT_MS 5000
// SDL_GetTicks calls in a row with no SDL_Delay or present: PICO-8 polling the clock
// without sleeping, which would never see it move. Counts as a 1 ms delay.
#define SPIN_LIMIT 20000
// How long PICO-8 may take to exit on its own after a quit request before the shim ends
// it; the core kills it after 1 s
#define QUIT_MS 800

// From SDL_audio.h, SDL_events.h, SDL_rect.h
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
typedef struct {
	int x, y, w, h;
} SDL_Rect;
#define SDL_EVENT_SIZE 56
#define SDL_QUIT_EVENT 0x100
#define LEGACY_AUDIO_DEVICE 1 /* the id SDL_OpenAudio's device always has */

#define REAL(ret, name, ...)                \
	static ret (*real_##name)(__VA_ARGS__); \
	if (!real_##name)                       \
		*(void**)&real_##name = dlsym(RTLD_NEXT, #name);

static struct p8ipc* ipc;
static pid_t core_pid;
static int owe_done;	  /* a frame_req was taken whose frame_done is still owed */
static int at_present;	  /* parked in SDL_RenderPresent, where a state can be taken or put back */
static uint32_t slice_ms; /* frame_ms of the slice being run */
static int quitting;	  /* P8CMD_QUIT came: PICO-8 runs free until it exits */
static struct timespec quit_at;
static int quit_event_sent;
static struct p8clock clk;
static uint32_t buttons;
static unsigned spin;

static void* screen; /* PICO-8's 128x128 screen texture */
static uint32_t screen_format;

static struct p8audio au;
static pthread_mutex_t audio_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static char fake_pad, fake_joy; /* the virtual controller's handles; never passed to SDL */

static int main_thread(void) {
	return syscall(SYS_gettid) == getpid();
}

// Attaches on the first hook that needs the block, so processes PICO-8 starts (which
// inherit LD_PRELOAD) never touch it
static void attach(void) {
	if (ipc)
		return;
	const char* path = getenv("P8NATIVE_IPC");
	ipc = path ? p8ipc_attach(path) : NULL;
	if (!ipc) {
		LOG("no IPC block at %s, exiting\n", path ? path : "(P8NATIVE_IPC unset)");
		_exit(1);
	}
	core_pid = getppid();
	LOG("attached to %s (core pid %d)\n", path, (int)core_pid);
}

static void fail(const char* what, const char* reason) {
	snprintf(ipc->reason, sizeof(ipc->reason), "%s%s", what, reason);
	LOG("%s%s\n", what, reason);
}

static void fill_audio(void) {
	uint32_t n = audio_frames_for(&au.remainder, au.freq, slice_ms);
	if (n > P8IPC_AUDIO_MAX)
		n = P8IPC_AUDIO_MAX;
	pthread_mutex_lock(&audio_lock);
	audio_pull(&au, ipc->audio, n);
	pthread_mutex_unlock(&audio_lock);
	ipc->audio_frames = n;
	ipc->audio_rate = au.freq;
}

// The core put a p8state_hdr (magic, pico8_64's hash, cart) at the start of state[];
// the snapshot goes after it, and its length and PICO-8's clock into it
static void save_state(void) {
	struct p8state_hdr h;
	const char* why = NULL;
	memcpy(&h, ipc->state, sizeof(h));
	size_t n = state_capture(ipc->state + sizeof(h), P8IPC_STATE_MAX - sizeof(h), &why);
	if (!n) {
		fail("cannot save a state: ", why);
		return;
	}
	h.len = (uint32_t)n;
	// where the clock starts the next frame: the rest of this slice is skipped at the grant
	h.ticks = clk.granted;
	memcpy(ipc->state, &h, sizeof(h));
	ipc->cmd_result = (int32_t)(sizeof(h) + n);
}

// The core checked the header against this pico8_64 and cart; the shim checks the rest.
// PICO-8's clock goes back to where it was when the state was saved.
static void load_state(void) {
	struct p8state_hdr h;
	const char* why = NULL;
	memcpy(&h, ipc->state, sizeof(h));
	if (ipc->state_len > P8IPC_STATE_MAX || ipc->state_len < sizeof(h) || h.len > ipc->state_len - sizeof(h)) {
		fail("cannot load a state: ", "it is cut short");
		return;
	}
	if (!state_load(ipc->state + sizeof(h), h.len, &why)) {
		fail("cannot load a state: ", why);
		return;
	}
	clk.now = clk.granted = h.ticks;
	pthread_mutex_lock(&audio_lock);
	au.fifo_frames = 0; // samples PICO-8 made before the load
	pthread_mutex_unlock(&audio_lock);
	ipc->cmd_result = 1;
}

// Commands come while the shim is parked; each is answered with frame_done
static void run_cmd(uint32_t cmd) {
	ipc->cmd_result = 0;
	switch (cmd) {
	case P8CMD_QUIT:
		LOG("quit requested\n");
		ipc->cmd_result = 1;
		quitting = 1;
		clock_gettime(CLOCK_MONOTONIC, &quit_at);
		break;
	case P8CMD_SAVE:
		if (!at_present)
			ipc->cmd_result = P8CMD_RETRY;
		else
			save_state();
		break;
	case P8CMD_LOAD:
		if (!at_present)
			ipc->cmd_result = P8CMD_RETRY;
		else
			load_state();
		break;
	default:
		fail("", "unknown command");
		break;
	}
	ipc->cmd = P8CMD_NONE;
}

// The end of a slice of the core's time: by a present, or by PICO-8 waiting past it.
// Hands the slice's frame and audio to the core, then waits for the next request.
static void end_slice(int present) {
	attach();
	spin = 0;
	at_present = present;
	if (quitting) {
		// the core is no longer waiting: PICO-8 runs unpaced to its exit (an SDL_QUIT event)
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((now.tv_sec - quit_at.tv_sec) * 1000L + (now.tv_nsec - quit_at.tv_nsec) / 1000000L > QUIT_MS) {
			LOG("PICO-8 did not exit after the quit request, ending it\n");
			_exit(0);
		}
		p8clock_grant(&clk, 16);
		return;
	}
	if (owe_done) {
		fill_audio();
		owe_done = 0;
		sem_post(&ipc->frame_done);
	}
	for (;;) {
		if (p8ipc_wait(&ipc->frame_req, WAIT_MS) != 0) {
			if (getppid() != core_pid) {
				LOG("the core is gone, exiting\n");
				_exit(0);
			}
			continue;
		}
		uint32_t cmd = ipc->cmd;
		if (cmd == P8CMD_NONE)
			break;
		run_cmd(cmd);
		sem_post(&ipc->frame_done);
		if (quitting)
			return;
	}
	owe_done = 1;
	buttons = ipc->buttons;
	slice_ms = ipc->frame_ms;
	p8clock_grant(&clk, slice_ms);
}

/* ---- init ---- */

#define SDL_INIT_SENSOR 0x00008000u

// PICO-8 asks for every subsystem, sensors too, and an SDL built without them refuses the
// lot; nothing here needs them. (As the pak's shim/sdl-nosensor.c does.)
EXPORT int SDL_Init(uint32_t flags) {
	REAL(int, SDL_Init, uint32_t);
	int r = real_SDL_Init ? real_SDL_Init(flags & ~SDL_INIT_SENSOR) : -1;
	LOG("SDL_Init 0x%x → %d\n", flags, r);
	return r;
}

EXPORT int SDL_InitSubSystem(uint32_t flags) {
	REAL(int, SDL_InitSubSystem, uint32_t);
	int r = real_SDL_InitSubSystem ? real_SDL_InitSubSystem(flags & ~SDL_INIT_SENSOR) : -1;
	LOG("SDL_InitSubSystem 0x%x → %d\n", flags, r);
	return r;
}

/* ---- video ---- */

EXPORT void* SDL_CreateTexture(void* renderer, uint32_t format, int access, int w, int h) {
	REAL(void*, SDL_CreateTexture, void*, uint32_t, int, int, int);
	void* t = real_SDL_CreateTexture ? real_SDL_CreateTexture(renderer, format, access, w, h) : NULL;
	LOG("texture %dx%d %s (0x%08x) access %d → %p\n", w, h, frame_format_name(format), format, access, t);
	if (t && w == P8_W && h == P8_H) {
		screen = t;
		screen_format = format;
	}
	return t;
}

EXPORT void SDL_DestroyTexture(void* texture) {
	REAL(void, SDL_DestroyTexture, void*);
	if (texture == screen)
		screen = NULL;
	if (real_SDL_DestroyTexture)
		real_SDL_DestroyTexture(texture);
}

EXPORT void SDL_DestroyRenderer(void* renderer) {
	REAL(void, SDL_DestroyRenderer, void*);
	screen = NULL; // its textures go with it
	if (real_SDL_DestroyRenderer)
		real_SDL_DestroyRenderer(renderer);
}

EXPORT int SDL_UpdateTexture(void* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
	static int logged, refused;
	REAL(int, SDL_UpdateTexture, void*, const SDL_Rect*, const void*, int);
	if (texture && texture == screen && (!rect || (rect->x == 0 && rect->y == 0 && rect->w == P8_W && rect->h == P8_H))) {
		attach();
		if (!logged) {
			logged = 1;
			LOG("screen uploads as %s (0x%08x), pitch %d\n", frame_format_name(screen_format), screen_format, pitch);
		}
		if (frame_convert(pixels, pitch, screen_format, ipc->fb) != 0 && !refused) {
			refused = 1;
			fail("", "screen texture format not supported");
		}
	}
	return real_SDL_UpdateTexture ? real_SDL_UpdateTexture(texture, rect, pixels, pitch) : 0;
}

// Nothing shows what the renderer draws: the frame was taken at upload, so scaling it to
// the window and presenting it would only cost time in every frame the core waits for
EXPORT int SDL_RenderCopy(void* renderer, void* texture, const SDL_Rect* src, const SDL_Rect* dst) {
	(void)renderer, (void)texture, (void)src, (void)dst;
	return 0;
}

EXPORT int SDL_RenderClear(void* renderer) {
	(void)renderer;
	return 0;
}

EXPORT void SDL_RenderPresent(void* renderer) {
	static int logged;
	(void)renderer;
	attach();
	if (!logged) {
		logged = 1;
		LOG("first present%s\n", screen ? "" : ", with no 128x128 screen texture seen");
	}
	ipc->fb_serial++;
	state_frame();
	ipc->state_ready = state_ready();
	end_slice(1);
}

// Logged once each for the spike: a window-surface path would bypass the texture capture
EXPORT int SDL_UpdateWindowSurface(void* window) {
	static int logged;
	REAL(int, SDL_UpdateWindowSurface, void*);
	if (!logged) {
		logged = 1;
		LOG("SDL_UpdateWindowSurface used (frames drawn this way are not captured)\n");
	}
	return real_SDL_UpdateWindowSurface ? real_SDL_UpdateWindowSurface(window) : -1;
}

EXPORT void* SDL_CreateWindow(const char* title, int x, int y, int w, int h, uint32_t flags) {
	REAL(void*, SDL_CreateWindow, const char*, int, int, int, int, uint32_t);
	void* win = real_SDL_CreateWindow ? real_SDL_CreateWindow(title, x, y, w, h, flags) : NULL;
	LOG("window %dx%d flags 0x%x → %s\n", w, h, flags, win ? "ok" : "FAILED");
	return win;
}

#define SDL_RENDERER_PRESENTVSYNC 0x4u

// PICO-8 asks for vsync, which SDL fakes for a renderer without it by sleeping up to a
// display refresh in every present: real time the core's frame would wait out
EXPORT void* SDL_CreateRenderer(void* window, int index, uint32_t flags) {
	REAL(void*, SDL_CreateRenderer, void*, int, uint32_t);
	void* r = real_SDL_CreateRenderer ? real_SDL_CreateRenderer(window, index, flags & ~SDL_RENDERER_PRESENTVSYNC) : NULL;
	LOG("renderer index %d flags 0x%x → %s\n", index, flags, r ? "ok" : "FAILED");
	return r;
}

EXPORT int SDL_SetHint(const char* name, const char* value) {
	REAL(int, SDL_SetHint, const char*, const char*);
	if (name && strcmp(name, "SDL_RENDER_VSYNC") == 0)
		value = "0";
	return real_SDL_SetHint ? real_SDL_SetHint(name, value) : 0;
}

/* ---- clock ---- */

EXPORT uint32_t SDL_GetTicks(void) {
	if (main_thread() && ++spin > SPIN_LIMIT) {
		static int logged;
		if (!logged) {
			logged = 1;
			LOG("PICO-8 polls SDL_GetTicks without waiting; treating that as waiting\n");
		}
		spin = 0;
		if (p8clock_delay(&clk, 1))
			end_slice(0);
	}
	return clk.now;
}

EXPORT void SDL_Delay(uint32_t ms) {
	if (!main_thread()) { // a thread of PICO-8's own: let it sleep for real
		REAL(void, SDL_Delay, uint32_t);
		if (real_SDL_Delay)
			real_SDL_Delay(ms);
		return;
	}
	spin = 0;
	if (ms == 0)
		return;
	if (p8clock_delay(&clk, ms))
		end_slice(0);
}

/* ---- audio ---- */

static int open_audio(const SDL_AudioSpec* want, SDL_AudioSpec* have) {
	LOG("audio %d Hz, format 0x%04x, %d channels, %d samples, callback %p\n", want->freq, want->format,
		want->channels, want->samples, (void*)want->callback);
	if (want->format != P8_AUDIO_S16LSB || want->channels != 2)
		LOG("audio is not S16 stereo; converting\n");
	if (!want->callback) {
		LOG("audio without a callback (SDL_QueueAudio) is not supported\n");
		return -1;
	}
	pthread_mutex_lock(&audio_lock);
	au.callback = want->callback;
	au.userdata = want->userdata;
	au.freq = (uint32_t)want->freq;
	au.format = want->format;
	au.channels = want->channels;
	au.samples = want->samples;
	au.paused = 1; // SDL opens devices paused
	au.fifo_frames = 0;
	au.remainder = 0;
	pthread_mutex_unlock(&audio_lock);
	if (have) {
		*have = *want;
		have->silence = want->format == P8_AUDIO_U8 ? 0x80 : 0;
		have->size = (uint32_t)want->samples * want->channels * ((want->format & 0xff) / 8);
	}
	return 0;
}

EXPORT int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained) {
	if (!desired)
		return -1;
	// with no obtained, SDL fills in desired's computed fields instead
	return open_audio(desired, obtained ? obtained : desired);
}

EXPORT uint32_t SDL_OpenAudioDevice(const char* device, int iscapture, const SDL_AudioSpec* desired,
									SDL_AudioSpec* obtained, int allowed_changes) {
	(void)device;
	(void)allowed_changes;
	if (iscapture || !desired)
		return 0;
	return open_audio(desired, obtained) == 0 ? LEGACY_AUDIO_DEVICE + 1 : 0;
}

EXPORT void SDL_PauseAudio(int pause_on) {
	pthread_mutex_lock(&audio_lock);
	au.paused = pause_on;
	pthread_mutex_unlock(&audio_lock);
}

EXPORT void SDL_PauseAudioDevice(uint32_t dev, int pause_on) {
	(void)dev;
	SDL_PauseAudio(pause_on);
}

// What PICO-8 locks against is the callback, which now runs on its main thread from end_slice
EXPORT void SDL_LockAudio(void) {
	pthread_mutex_lock(&audio_lock);
}

EXPORT void SDL_UnlockAudio(void) {
	pthread_mutex_unlock(&audio_lock);
}

EXPORT void SDL_LockAudioDevice(uint32_t dev) {
	(void)dev;
	pthread_mutex_lock(&audio_lock);
}

EXPORT void SDL_UnlockAudioDevice(uint32_t dev) {
	(void)dev;
	pthread_mutex_unlock(&audio_lock);
}

EXPORT void SDL_CloseAudio(void) {
	pthread_mutex_lock(&audio_lock);
	au.callback = NULL;
	pthread_mutex_unlock(&audio_lock);
}

/* ---- input: one virtual controller, found by PICO-8 when it counts joysticks at start ---- */

static int is_input_event(uint32_t type) {
	// keyboard, text, mouse, joystick, controller, touch, gesture (0x300..0x8ff)
	return type >= 0x300 && type < 0x900;
}

EXPORT int SDL_PollEvent(void* event) {
	REAL(int, SDL_PollEvent, void*);
	if (quitting && !quit_event_sent) {
		if (event) {
			uint32_t ev[SDL_EVENT_SIZE / 4] = {0};
			ev[0] = SDL_QUIT_EVENT;
			ev[1] = clk.now;
			memcpy(event, ev, sizeof(ev));
			quit_event_sent = 1;
		}
		return 1;
	}
	if (!real_SDL_PollEvent)
		return 0;
	uint8_t scratch[SDL_EVENT_SIZE];
	for (;;) {
		int got = real_SDL_PollEvent(scratch);
		if (!got)
			return 0;
		uint32_t type;
		memcpy(&type, scratch, 4);
		if (is_input_event(type))
			continue;
		if (event)
			memcpy(event, scratch, sizeof(scratch)); // a peek drops it: only window/quit events get here
		return 1;
	}
}

EXPORT int SDL_NumJoysticks(void) {
	return 1;
}

EXPORT int SDL_IsGameController(int index) {
	return index == 0;
}

EXPORT const char* SDL_GameControllerNameForIndex(int index) {
	return index == 0 ? "NX Redux" : NULL;
}

EXPORT const char* SDL_JoystickNameForIndex(int index) {
	return index == 0 ? "NX Redux" : NULL;
}

EXPORT void* SDL_GameControllerOpen(int index) {
	LOG("controller %d opened\n", index);
	return index == 0 ? &fake_pad : NULL;
}

EXPORT void SDL_GameControllerClose(void* pad) {
	(void)pad;
}

EXPORT int SDL_GameControllerGetAttached(void* pad) {
	return pad == &fake_pad;
}

EXPORT void* SDL_GameControllerGetJoystick(void* pad) {
	return pad == &fake_pad ? &fake_joy : NULL;
}

EXPORT char* SDL_GameControllerMapping(void* pad) {
	(void)pad;
	return NULL; // the caller would SDL_free it; nothing to free
}

EXPORT uint8_t SDL_GameControllerGetButton(void* pad, int button) {
	return pad == &fake_pad ? (uint8_t)input_pad_button(buttons, button) : 0;
}

EXPORT int16_t SDL_GameControllerGetAxis(void* pad, int axis) {
	(void)pad;
	(void)axis;
	return 0;
}

EXPORT void* SDL_JoystickOpen(int index) {
	LOG("joystick %d opened\n", index);
	return index == 0 ? &fake_joy : NULL;
}

EXPORT void SDL_JoystickClose(void* joy) {
	(void)joy;
}

EXPORT int32_t SDL_JoystickInstanceID(void* joy) {
	return joy == &fake_joy ? 0 : -1;
}

// The raw-joystick view has no buttons or axes: PICO-8 reads it as a controller
EXPORT int SDL_JoystickNumButtons(void* joy) {
	(void)joy;
	return 0;
}

EXPORT int SDL_JoystickNumAxes(void* joy) {
	(void)joy;
	return 0;
}

EXPORT int SDL_JoystickNumBalls(void* joy) {
	(void)joy;
	return 0;
}

EXPORT uint8_t SDL_JoystickGetButton(void* joy, int button) {
	(void)joy;
	(void)button;
	return 0;
}

EXPORT int16_t SDL_JoystickGetAxis(void* joy, int axis) {
	(void)joy;
	(void)axis;
	return 0;
}

EXPORT int SDL_GetModState(void) {
	return 0;
}

/* ---- cart save redirect ---- */

#define REDIRECT(p)         \
	char redirect_buf[512]; \
	p = paths_redirect(p, redirect_buf, sizeof(redirect_buf))

static int real_mkdir_fn(const char* path, mode_t mode) {
	REAL(int, mkdir, const char*, mode_t);
	return real_mkdir ? real_mkdir(path, mode) : -1;
}

__attribute__((constructor)) static void shim_init(void) {
	paths_mkdir = real_mkdir_fn; // creating $P8NATIVE_CDATA skips the hook below
}

EXPORT FILE* fopen(const char* path, const char* mode) {
	REAL(FILE*, fopen, const char*, const char*);
	REDIRECT(path);
	return real_fopen(path, mode);
}

EXPORT FILE* fopen64(const char* path, const char* mode) {
	REAL(FILE*, fopen64, const char*, const char*);
	REDIRECT(path);
	return real_fopen64(path, mode);
}

static mode_t open_mode(int flags, va_list ap) {
#ifdef O_TMPFILE
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE)
#else
	if (flags & O_CREAT)
#endif
		return (mode_t)va_arg(ap, int);
	return 0;
}

EXPORT int open(const char* path, int flags, ...) {
	REAL(int, open, const char*, int, ...);
	va_list ap;
	va_start(ap, flags);
	mode_t mode = open_mode(flags, ap);
	va_end(ap);
	REDIRECT(path);
	return real_open(path, flags, mode);
}

EXPORT int open64(const char* path, int flags, ...) {
	REAL(int, open64, const char*, int, ...);
	va_list ap;
	va_start(ap, flags);
	mode_t mode = open_mode(flags, ap);
	va_end(ap);
	REDIRECT(path);
	return real_open64(path, flags, mode);
}

EXPORT int mkdir(const char* path, mode_t mode) {
	REDIRECT(path);
	return real_mkdir_fn(path, mode);
}

EXPORT DIR* opendir(const char* path) {
	REAL(DIR*, opendir, const char*);
	REDIRECT(path);
	return real_opendir(path);
}

// glibc 2.28 (the devices') has stat() call these; newer glibc has none, so they are only exported
EXPORT int __xstat(int ver, const char* path, struct stat* st) {
	REAL(int, __xstat, int, const char*, struct stat*);
	REDIRECT(path);
	if (!real___xstat) {
		errno = ENOSYS;
		return -1;
	}
	return real___xstat(ver, path, st);
}

EXPORT int __xstat64(int ver, const char* path, struct stat64* st) {
	REAL(int, __xstat64, int, const char*, struct stat64*);
	REDIRECT(path);
	if (!real___xstat64) {
		errno = ENOSYS;
		return -1;
	}
	return real___xstat64(ver, path, st);
}

EXPORT int remove(const char* path) {
	REAL(int, remove, const char*);
	REDIRECT(path);
	return real_remove(path);
}

EXPORT int rename(const char* from, const char* to) {
	REAL(int, rename, const char*, const char*);
	char from_buf[512], to_buf[512];
	from = paths_redirect(from, from_buf, sizeof(from_buf));
	to = paths_redirect(to, to_buf, sizeof(to_buf));
	return real_rename(from, to);
}
