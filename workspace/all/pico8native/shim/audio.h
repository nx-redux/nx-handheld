#ifndef P8SHIM_AUDIO_H
#define P8SHIM_AUDIO_H

// PICO-8's SDL audio callback, pulled once per frame instead of by an audio device.

#include <stdint.h>

#include "../p8ipc.h"

// SDL_AUDIO_* values (SDL_audio.h)
#define P8_AUDIO_U8 0x0008
#define P8_AUDIO_S8 0x8008
#define P8_AUDIO_S16LSB 0x8010
#define P8_AUDIO_F32LSB 0x8120

#define P8AUDIO_BLOCK_MAX 4096 /* frames asked of the callback at once */

struct p8audio {
	void (*callback)(void* userdata, uint8_t* stream, int len);
	void* userdata;
	uint32_t freq;
	uint16_t format;
	uint8_t channels;
	uint16_t samples; /* frames per callback, as PICO-8 asked */
	int paused;
	uint32_t remainder; /* rate*ms left over below one frame, in 1/1000 frames (ruling R3) */
	int16_t fifo[(P8IPC_AUDIO_MAX + P8AUDIO_BLOCK_MAX) * 2];
	uint32_t fifo_frames;
	uint8_t block[P8AUDIO_BLOCK_MAX * 2 * 4];
};

// Stereo frames for the next ms of audio at rate, carrying the fraction over in *remainder
// so the total over any run is exactly rate * total_ms / 1000, rounded down.
uint32_t audio_frames_for(uint32_t* remainder, uint32_t rate, uint32_t ms);

// Converts frames of format/channels (1 or 2) into interleaved stereo S16. -1 if unsupported.
int audio_convert(const void* src, uint32_t frames, uint16_t format, uint8_t channels, int16_t* out);

// Fills out with frames stereo S16 frames from the callback, which is always asked for whole
// blocks of a->samples frames as a device would; what is left over waits for the next pull.
// Silence while paused, unopened or in a format audio_convert does not take.
void audio_pull(struct p8audio* a, int16_t* out, uint32_t frames);

#endif
