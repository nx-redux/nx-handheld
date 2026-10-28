// pico8native_libretro.so: a libretro core that runs the user's official PICO-8
// (pico8_64 from Bios/P8 or Bios/PICO) as a child process with pico8shim.so
// preloaded, and drives it one frame per retro_run over the shared-memory block
// in p8ipc.h. The shim does the SDL side (video, audio, input, clock); this file
// starts and ends the child and moves frames, samples and buttons.
//
// The child is a direct fork+execve of the frontend: the shim's parked wait gives
// up only when its parent is gone (getppid), and PR_SET_PDEATHSIG kills it if the
// frontend dies without unloading. ASLR is off in the child so the save-state
// arena (Task 7) maps at a fixed address.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "libretro.h"
#include "core_args.h"
#include "p8ipc.h"

#define CORE_NAME "PICO-8 (native)"
#define CORE_VERSION "1.0"
#define DEFAULT_RATE 22050
#define FIRST_FRAME_TIMEOUT_MS 10000 // pico8_64 boots in ~0.6 s; a cold SD card can be slower
#define FRAME_TIMEOUT_MS 3000
#define QUIT_TIMEOUT_MS 1000
#define WAIT_SLICE_MS 250 // frame waits check that the child is still alive this often
#define ENV_MAX 512
#define P8_PATH 1024 // paths the core builds; env strings add a name to one

extern char** environ;

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static struct {
	struct p8ipc* ipc;
	char ipc_path[64];
	pid_t child;
	int dead;		  // the child failed or exited; retro_run only waits for the frontend to stop
	unsigned frame;	  // retro_run count, for the 17/17/16 ms pattern
	unsigned rate;	  // sample rate reported to the frontend
	int rate_refused; // the frontend would not take a new rate; logged once
	int splore;
} p8 = {.child = -1, .rate = DEFAULT_RATE};

static void core_log(enum retro_log_level level, const char* fmt, ...) {
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (log_cb)
		log_cb(level, "pico8native: %s", msg);
	else
		fprintf(stderr, "pico8native: %s", msg);
}

// Directory this .so lives in (the pak), where pico8shim.so and lib/ sit
static int pak_dir(char* out, size_t size) {
	Dl_info info;
	if (!dladdr((void*)pak_dir, &info) || !info.dli_fname)
		return -1;
	const char* slash = strrchr(info.dli_fname, '/');
	if (!slash)
		return snprintf(out, size, ".") >= (int)size ? -1 : 0;
	return snprintf(out, size, "%.*s", (int)(slash - info.dli_fname), info.dli_fname) >= (int)size ? -1 : 0;
}

static int mkdir_p(const char* path) {
	char buf[PATH_MAX];
	if (snprintf(buf, sizeof(buf), "%s", path) >= (int)sizeof(buf))
		return -1;
	for (char* p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(buf, 0755);
		*p = '/';
	}
	return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static void describe_exit(int status, char* out, size_t size) {
	if (WIFEXITED(status))
		snprintf(out, size, "exited with status %d", WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		snprintf(out, size, "killed by signal %d", WTERMSIG(status));
	else
		snprintf(out, size, "stopped (status 0x%x)", status);
}

// Reaps the child if it has exited; 1 if it is gone
static int child_gone(void) {
	if (p8.child <= 0)
		return 1;
	int status;
	pid_t w = waitpid(p8.child, &status, WNOHANG);
	if (w == 0)
		return 0;
	if (w == p8.child) {
		char how[64];
		describe_exit(status, how, sizeof(how));
		core_log(RETRO_LOG_INFO, "pico8_64 %s\n", how);
	}
	p8.child = -1;
	return 1;
}

// Waits for frame_done, giving up early if the child dies. 0 ok, -1 timeout, -2 child gone.
static int wait_done(int timeout_ms) {
	for (int waited = 0; waited < timeout_ms; waited += WAIT_SLICE_MS) {
		int slice = timeout_ms - waited < WAIT_SLICE_MS ? timeout_ms - waited : WAIT_SLICE_MS;
		if (p8ipc_wait(&p8.ipc->frame_done, slice) == 0)
			return 0;
		if (child_gone())
			return p8ipc_wait(&p8.ipc->frame_done, 0) == 0 ? 0 : -2; // a last post before exiting
	}
	return -1;
}

// Ends the child: QUIT through the shim if it is parked, then SIGKILL after 1 s.
// Safe to call more than once and from atexit.
static void stop_child(int ask) {
	if (p8.child > 0) {
		if (ask && p8.ipc && !p8.dead) {
			// the shim answers QUIT at once and PICO-8 exits on its own well inside 1 s
			p8.ipc->cmd = P8CMD_QUIT;
			sem_post(&p8.ipc->frame_req);
			for (int i = 0; i < QUIT_TIMEOUT_MS / 10 && !child_gone(); i++)
				usleep(10000);
		}
		if (p8.child > 0) {
			if (ask)
				core_log(RETRO_LOG_WARN, "pico8_64 did not quit in time; killing it\n");
			kill(p8.child, SIGKILL);
			waitpid(p8.child, NULL, 0);
			p8.child = -1;
		}
	}
	if (p8.ipc) {
		p8ipc_close(p8.ipc);
		p8.ipc = NULL;
	}
	if (p8.ipc_path[0]) {
		unlink(p8.ipc_path);
		p8.ipc_path[0] = '\0';
	}
}

static void stop_child_atexit(void) {
	stop_child(0); // the frontend is going away: no time for a polite quit
}

static void request_shutdown(const char* why) {
	if (p8.dead)
		return;
	p8.dead = 1;
	const char* reason = p8.ipc && p8.ipc->reason[0] ? p8.ipc->reason : "";
	core_log(RETRO_LOG_ERROR, "%s%s%s\n", why, reason[0] ? "; shim: " : "", reason);
	environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

// fork + execve of pico8_64. Everything the child needs is built here, in the
// parent: between fork and execve the child only makes async-signal-safe calls.
static int spawn(char* const* argv, char* const* envp) {
	pid_t parent = getpid();
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (getppid() != parent) // the frontend died before the line above
			_exit(127);
		// the frontend's display, audio and input fds stay with the frontend
		for (int fd = 3; fd < 1024; fd++)
			close(fd);
		// execve keeps ignored signals and the mask: hand PICO-8 the defaults
		struct sigaction dfl = {.sa_handler = SIG_DFL};
		for (int sig = 1; sig < NSIG; sig++)
			sigaction(sig, &dfl, NULL); // SIGKILL/SIGSTOP and libc's own just fail
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		personality(ADDR_NO_RANDOMIZE);
		execve(argv[0], argv, envp);
		_exit(127);
	}
	p8.child = pid;
	return 0;
}

static int start(const char* cart) {
	const char *system_dir = NULL, *save_dir = NULL;
	environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir);
	environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir);

	char runtime[P8_PATH], bin[P8_PATH + 16];
	if (!system_dir || core_find_runtime(system_dir, runtime, sizeof(runtime)) != 0) {
		core_log(RETRO_LOG_ERROR, "pico8_64 and pico8.dat not found in %s or its sibling PICO\n",
				 system_dir ? system_dir : "(no system dir)");
		return -1;
	}
	snprintf(bin, sizeof(bin), "%s/pico8_64", runtime);

	char pak[P8_PATH];
	if (pak_dir(pak, sizeof(pak)) != 0) {
		core_log(RETRO_LOG_ERROR, "cannot find the core's own directory\n");
		return -1;
	}

	const char* shared = getenv("SHARED_USERDATA_PATH");
	static char home[P8_PATH], cdata[P8_PATH], config[P8_PATH + 16];
	snprintf(home, sizeof(home), "%s/P8-native/home", shared && *shared ? shared : "/mnt/SDCARD/.userdata/shared");
	snprintf(cdata, sizeof(cdata), "%s/cdata", save_dir && *save_dir ? save_dir : "/mnt/SDCARD/Saves/P8");
	if (mkdir_p(home) != 0)
		core_log(RETRO_LOG_WARN, "cannot create %s: %s\n", home, strerror(errno));
	snprintf(config, sizeof(config), "%s/config.txt", home);
	int fixed = core_force_framerate_control(config);
	if (fixed == 1)
		core_log(RETRO_LOG_INFO, "set host_framerate_control 1 in %s\n", config);
	else if (fixed < 0)
		core_log(RETRO_LOG_WARN, "could not check host_framerate_control in %s\n", config);

	snprintf(p8.ipc_path, sizeof(p8.ipc_path), "/tmp/p8native.%d.ipc", (int)getpid());
	p8.ipc = p8ipc_create(p8.ipc_path);
	if (!p8.ipc) {
		core_log(RETRO_LOG_ERROR, "cannot create %s: %s\n", p8.ipc_path, strerror(errno));
		p8.ipc_path[0] = '\0';
		return -1;
	}

	char* argv[8];
	if (core_build_argv(bin, home, cart, p8.splore, argv, 8) < 0) {
		core_log(RETRO_LOG_ERROR, "no cart to run\n");
		stop_child(0);
		return -1;
	}

	// The child gets the toolchain SDL2 shipped in <pak>/lib (it has the offscreen
	// video driver the device SDL2 lacks) ahead of whatever the frontend inherited.
	static char e_ipc[96], e_home[P8_PATH + 32], e_cdata[P8_PATH + 32], e_preload[P8_PATH + 32], e_libpath[P8_PATH * 4];
	const char* inherited = getenv("LD_LIBRARY_PATH");
	snprintf(e_ipc, sizeof(e_ipc), "P8NATIVE_IPC=%s", p8.ipc_path);
	snprintf(e_home, sizeof(e_home), "P8NATIVE_HOME=%s", home);
	snprintf(e_cdata, sizeof(e_cdata), "P8NATIVE_CDATA=%s", cdata);
	snprintf(e_preload, sizeof(e_preload), "LD_PRELOAD=%s/pico8shim.so", pak);
	snprintf(e_libpath, sizeof(e_libpath), "LD_LIBRARY_PATH=%s/lib%s%s", pak, inherited && *inherited ? ":" : "",
			 inherited ? inherited : "");
	const char* set[] = {e_ipc, e_home, e_cdata, e_preload, e_libpath,
						 "SDL_VIDEODRIVER=offscreen", "SDL_AUDIODRIVER=dummy", NULL};
	static char* envp[ENV_MAX];
	if (core_build_env(environ, set, envp, ENV_MAX) < 0) {
		core_log(RETRO_LOG_ERROR, "environment too large\n");
		stop_child(0);
		return -1;
	}

	if (spawn(argv, envp) != 0) {
		core_log(RETRO_LOG_ERROR, "fork failed: %s\n", strerror(errno));
		stop_child(0);
		return -1;
	}
	core_log(RETRO_LOG_INFO, "started %s (pid %d) %s %s\n", bin, (int)p8.child, p8.splore ? "-splore" : "-run",
			 p8.splore ? "" : cart);
	return 0;
}

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;
	struct retro_log_callback logging;
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging) && logging.log)
		log_cb = logging.log;
}
void retro_set_video_refresh(retro_video_refresh_t cb) {
	video_cb = cb;
}
void retro_set_audio_sample(retro_audio_sample_t cb) {
	(void)cb;
}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
	audio_batch_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb) {
	input_poll_cb = cb;
}
void retro_set_input_state(retro_input_state_t cb) {
	input_state_cb = cb;
}

void retro_init(void) {
	static int registered;
	if (!registered && atexit(stop_child_atexit) == 0)
		registered = 1;
}

void retro_deinit(void) {
	stop_child(1);
}

unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info* info) {
	memset(info, 0, sizeof(*info));
	info->library_name = CORE_NAME;
	info->library_version = CORE_VERSION;
	info->valid_extensions = "p8|png";
	info->need_fullpath = true;
	info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = P8_W;
	info->geometry.base_height = P8_H;
	info->geometry.max_width = P8_W;
	info->geometry.max_height = P8_H;
	info->geometry.aspect_ratio = 1.0f;
	info->timing.fps = 60.0;
	info->timing.sample_rate = p8.rate;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
	(void)port, (void)device;
}

void retro_reset(void) {}

bool retro_load_game(const struct retro_game_info* game) {
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		core_log(RETRO_LOG_ERROR, "XRGB8888 is not supported\n");
		return false;
	}
	// PICO-8's buttons as the shim maps them (fake08's layout); Start opens PICO-8's pause menu
	static const struct retro_input_descriptor desc[] = {
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "O"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "X"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Pause"},
		{0, 0, 0, 0, NULL},
	};
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);

	const char* splore = getenv("P8NATIVE_SPLORE");
	p8.splore = splore && strcmp(splore, "1") == 0;
	p8.dead = 0;
	p8.frame = 0;
	p8.rate = DEFAULT_RATE;
	p8.rate_refused = 0;
	return start(game && game->path ? game->path : NULL) == 0;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
	(void)type, (void)info, (void)num;
	return false;
}

void retro_unload_game(void) {
	stop_child(1);
}

unsigned retro_get_region(void) {
	return RETRO_REGION_NTSC;
}

// Save states arrive with Task 7
size_t retro_serialize_size(void) {
	return 0;
}
bool retro_serialize(void* data, size_t size) {
	(void)data, (void)size;
	return false;
}
bool retro_unserialize(const void* data, size_t size) {
	(void)data, (void)size;
	return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char* code) {
	(void)index, (void)enabled, (void)code;
}

void* retro_get_memory_data(unsigned id) {
	(void)id;
	return NULL;
}
size_t retro_get_memory_size(unsigned id) {
	(void)id;
	return 0;
}

static void push_audio(void) {
	uint32_t frames = p8.ipc->audio_frames;
	if (frames > P8IPC_AUDIO_MAX)
		frames = P8IPC_AUDIO_MAX;
	const int16_t* data = p8.ipc->audio;
	while (frames) {
		size_t took = audio_batch_cb(data, frames);
		if (!took || took > frames)
			break;
		data += took * 2;
		frames -= took;
	}

	uint32_t rate = p8.ipc->audio_rate;
	if (rate && rate != p8.rate && !p8.rate_refused) {
		struct retro_system_av_info av;
		retro_get_system_av_info(&av);
		av.timing.sample_rate = rate;
		if (environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av))
			p8.rate = rate;
		else {
			p8.rate_refused = 1;
			core_log(RETRO_LOG_WARN, "PICO-8 audio is %u Hz but the frontend kept %u Hz\n", rate, p8.rate);
		}
	}
}

void retro_run(void) {
	input_poll_cb();
	if (p8.dead || !p8.ipc) {
		video_cb(NULL, P8_W, P8_H, P8_W * sizeof(uint32_t));
		return;
	}

	uint32_t buttons = 0;
	for (unsigned id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; id++)
		if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id))
			buttons |= 1u << id;
	p8.ipc->buttons = buttons;
	// 60 Hz in whole ms: 17, 17, 16 is exactly 50 ms per 3 frames
	p8.ipc->frame_ms = p8.frame % 3 == 2 ? 16 : 17;
	p8.ipc->cmd = P8CMD_NONE;
	sem_post(&p8.ipc->frame_req);

	int r = wait_done(p8.frame == 0 ? FIRST_FRAME_TIMEOUT_MS : FRAME_TIMEOUT_MS);
	p8.frame++;
	if (r != 0) {
		request_shutdown(r == -2 ? "pico8_64 ended" : "pico8_64 stopped answering");
		video_cb(NULL, P8_W, P8_H, P8_W * sizeof(uint32_t));
		return;
	}
	// The shim is parked until the next frame_req, so fb and audio are stable here
	video_cb(p8.ipc->fb, P8_W, P8_H, P8_W * sizeof(uint32_t));
	push_audio();
}
