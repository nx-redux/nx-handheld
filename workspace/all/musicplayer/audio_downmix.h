#ifndef __AUDIO_DOWNMIX_H__
#define __AUDIO_DOWNMIX_H__

// Interleaved s16 PCM of any channel count -> interleaved stereo, for the
// WAV/FLAC stream decoders (player.c), which hand back as many channels as
// the file has. Pure, so it is unit-tested on the host: see
// tests/test_audio_downmix.c.
//
// Channel order is the WAV/FLAC default for each count:
//   3: L R C   4: L R BL BR   5: L R C BL BR   6: L R C LFE BL BR
//   7: L R C LFE BC SL SR     8: L R C LFE BL BR SL SR
// Centre channels go to both sides at -3 dB, LFE is dropped, and the sum is
// scaled so a full-scale signal on every channel does not clip. Counts above
// 8 keep only the first two channels.

#include <stddef.h>
#include <stdint.h>

// Per-channel weights in Q15 for the left / right output.
#define AUDIO_DOWNMIX_W1 32768
#define AUDIO_DOWNMIX_W707 23170

static inline void AudioDownmix_weights(int channels, int ch, int* wl, int* wr) {
	// 'L' left, 'R' right, 'C' both at -3 dB, 'M' both at full level, 'X' dropped
	static const char* const layouts[9] = {"", "M", "LR", "LRC", "LRLR", "LRCLR", "LRCXLR", "LRCXCLR", "LRCXLRLR"};
	char role;
	if (channels >= 1 && channels <= 8)
		role = layouts[channels][ch];
	else
		role = ch == 0 ? 'L' : (ch == 1 ? 'R' : 'X');
	*wl = *wr = 0;
	switch (role) {
	case 'L':
		*wl = AUDIO_DOWNMIX_W1;
		break;
	case 'R':
		*wr = AUDIO_DOWNMIX_W1;
		break;
	case 'C':
		*wl = *wr = AUDIO_DOWNMIX_W707;
		break;
	case 'M':
		*wl = *wr = AUDIO_DOWNMIX_W1;
		break;
	default:
		break;
	}
}

// Writes `frames` stereo frames to `out` (frames * 2 samples) from `in`
// (frames * channels samples). `in` and `out` must not overlap.
static inline void AudioDownmix_toStereo(const int16_t* in, int channels, size_t frames, int16_t* out) {
	if (channels <= 0)
		return;
	int wl[8], wr[8];
	int64_t sum_l = 0, sum_r = 0;
	int used = channels < 8 ? channels : 8;
	for (int c = 0; c < used; c++) {
		AudioDownmix_weights(channels, c, &wl[c], &wr[c]);
		sum_l += wl[c];
		sum_r += wr[c];
	}
	// Normalise by the heavier side so the mix never exceeds full scale.
	int64_t norm = sum_l > sum_r ? sum_l : sum_r;
	if (norm < AUDIO_DOWNMIX_W1)
		norm = AUDIO_DOWNMIX_W1;
	for (size_t f = 0; f < frames; f++) {
		const int16_t* s = in + f * (size_t)channels;
		int64_t l = 0, r = 0;
		for (int c = 0; c < used; c++) {
			l += (int64_t)s[c] * wl[c];
			r += (int64_t)s[c] * wr[c];
		}
		out[f * 2] = (int16_t)(l / norm);
		out[f * 2 + 1] = (int16_t)(r / norm);
	}
}

#endif
