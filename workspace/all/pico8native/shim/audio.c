#include <string.h>

#include "audio.h"

uint32_t audio_frames_for(uint32_t* remainder, uint32_t rate, uint32_t ms) {
	uint64_t milli = (uint64_t)rate * ms + *remainder; // in 1/1000 frames
	*remainder = (uint32_t)(milli % 1000);
	return (uint32_t)(milli / 1000);
}

int audio_convert(const void* src, uint32_t frames, uint16_t format, uint8_t channels, int16_t* out) {
	if (channels != 1 && channels != 2)
		return -1;
	uint32_t n = frames * channels;
	int16_t* tmp = out;
	if (channels == 1)
		tmp = out + frames; // the second half, read before the first half is written over it
	switch (format) {
	case P8_AUDIO_S16LSB:
		memmove(tmp, src, n * 2);
		break;
	case P8_AUDIO_F32LSB: {
		const float* f = src;
		for (uint32_t i = 0; i < n; i++) {
			float v = f[i] * 32767.0f;
			tmp[i] = v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768
															: (int16_t)v;
		}
		break;
	}
	case P8_AUDIO_U8: {
		const uint8_t* u = src;
		for (uint32_t i = 0; i < n; i++)
			tmp[i] = (int16_t)(((int32_t)u[i] - 128) * 256);
		break;
	}
	case P8_AUDIO_S8: {
		const int8_t* s = src;
		for (uint32_t i = 0; i < n; i++)
			tmp[i] = (int16_t)(s[i] * 256);
		break;
	}
	default:
		return -1;
	}
	if (channels == 1)
		for (uint32_t i = 0; i < frames; i++)
			out[i * 2] = out[i * 2 + 1] = tmp[i];
	return 0;
}

void audio_pull(struct p8audio* a, int16_t* out, uint32_t frames) {
	if (frames > P8IPC_AUDIO_MAX)
		frames = P8IPC_AUDIO_MAX;
	int bps = (a->format & 0xff) / 8;
	if (!a->callback || a->paused || (a->channels != 1 && a->channels != 2) || bps < 1 || bps > 4) {
		memset(out, 0, frames * 4);
		return;
	}
	uint32_t block = a->samples ? a->samples : 1024;
	if (block > P8AUDIO_BLOCK_MAX)
		block = P8AUDIO_BLOCK_MAX;
	uint8_t silence = a->format == P8_AUDIO_U8 ? 0x80 : 0;
	while (a->fifo_frames < frames) {
		int len = (int)(block * a->channels * bps);
		memset(a->block, silence, len); // SDL hands the callback a silent buffer
		a->callback(a->userdata, a->block, len);
		if (audio_convert(a->block, block, a->format, a->channels, a->fifo + a->fifo_frames * 2) != 0) {
			memset(out, 0, frames * 4);
			return;
		}
		a->fifo_frames += block;
	}
	memcpy(out, a->fifo, frames * 4);
	a->fifo_frames -= frames;
	memmove(a->fifo, a->fifo + frames * 2, a->fifo_frames * 4);
}
