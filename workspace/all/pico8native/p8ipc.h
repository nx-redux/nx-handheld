#ifndef P8IPC_H
#define P8IPC_H

// Shared-memory channel between pico8native_libretro.so (the core) and the
// LD_PRELOAD shim inside pico8_64. They run in lockstep: one retro_run posts
// frame_req, the shim runs one PICO-8 frame and posts frame_done.
// The core creates the file at /tmp/p8native.<pid>.ipc and passes it in env
// P8NATIVE_IPC. Commands (save/load/quit) run while the shim is parked
// between frame_done and the next frame_req: the core sets cmd and posts
// frame_req, the shim runs the command instead of a frame and posts frame_done.

#include <stddef.h>
#include <stdint.h>
#include <semaphore.h>

#define P8IPC_MAGIC 0x50384e31u /* "P8N1" */
#define P8IPC_VERSION 1
#define P8_W 128
#define P8_H 128
#define P8IPC_AUDIO_MAX 4096		/* stereo frames per video frame, upper bound */
#define P8IPC_STATE_MAX (24u << 20) /* compressed state bytes */

enum p8cmd { P8CMD_NONE,
			 P8CMD_SAVE,
			 P8CMD_LOAD,
			 P8CMD_QUIT };

struct p8ipc {
	uint32_t magic, version;
	sem_t frame_req;		  /* core posts: run one frame */
	sem_t frame_done;		  /* shim posts: frame presented, video/audio filled */
	uint32_t buttons;		  /* RETRO_DEVICE_ID_JOYPAD_* bitmask, set before frame_req */
	uint32_t frame_ms;		  /* ms PICO-8's clock advances for this frame (16 or 17 at 60 Hz) */
	uint32_t fb[P8_W * P8_H]; /* XRGB8888, written by shim */
	uint32_t fb_serial;		  /* +1 per presented frame */
	int16_t audio[P8IPC_AUDIO_MAX * 2];
	uint32_t audio_frames; /* stereo frames in audio[] this frame */
	uint32_t audio_rate;   /* Hz PICO-8 opened with */
	uint32_t cmd;		   /* enum p8cmd, set by core while the shim is parked */
	int32_t cmd_result;	   /* >0 ok (SAVE: bytes in state[]), 0 failed */
	uint32_t state_len;	   /* LOAD: bytes in state[] */
	char reason[256];	   /* last failure, for the core's log */
	uint8_t state[P8IPC_STATE_MAX];
};

// state[] gets memcpy'd as a block; keep it 8-byte aligned
_Static_assert(offsetof(struct p8ipc, state) % 8 == 0, "p8ipc state[] must be 8-byte aligned");

struct p8ipc* p8ipc_create(const char* path); /* core: create+truncate+map file, init sems */
struct p8ipc* p8ipc_attach(const char* path); /* shim: map existing; NULL if magic/version wrong */
void p8ipc_close(struct p8ipc* ipc);
int p8ipc_wait(sem_t* sem, int timeout_ms); /* 0 ok, -1 timeout/EINTR exhausted */

#endif
