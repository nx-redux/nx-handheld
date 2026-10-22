// Host unit test for audio_downmix.h. Built with AddressSanitizer by
// run_tests.sh: the output buffer is exactly frames*2 samples, so a decoder
// path that wrote frames*channels (the old 5.1 WAV/FLAC bug) fails loudly.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio_downmix.h"

static void test_stereo_passthrough(void) {
	int16_t in[4] = {100, -200, 32767, -32768};
	int16_t* out = malloc(4 * sizeof(int16_t));
	AudioDownmix_toStereo(in, 2, 2, out);
	assert(out[0] == 100 && out[1] == -200 && out[2] == 32767 && out[3] == -32768);
	free(out);
}

static void test_mono_duplicates(void) {
	int16_t in[3] = {5, -7, 32767};
	int16_t* out = malloc(6 * sizeof(int16_t));
	AudioDownmix_toStereo(in, 1, 3, out);
	assert(out[0] == 5 && out[1] == 5 && out[2] == -7 && out[3] == -7 && out[4] == 32767 && out[5] == 32767);
	free(out);
}

static void test_51_no_overflow_or_clip(void) {
	const int FRAMES = 1024, CH = 6;
	int16_t* in = malloc(FRAMES * CH * sizeof(int16_t));
	for (int i = 0; i < FRAMES * CH; i++)
		in[i] = 32767;
	int16_t* out = malloc(FRAMES * 2 * sizeof(int16_t)); // stereo-sized, as in player.c
	AudioDownmix_toStereo(in, CH, FRAMES, out);
	for (int i = 0; i < FRAMES * 2; i++)
		assert(out[i] > 30000 && out[i] <= 32767);
	// LFE (channel 3) alone is dropped; left-only content stays left.
	int16_t lfe[6] = {0, 0, 0, 32767, 0, 0};
	int16_t lr[2];
	AudioDownmix_toStereo(lfe, CH, 1, lr);
	assert(lr[0] == 0 && lr[1] == 0);
	int16_t left[6] = {20000, 0, 0, 0, 0, 0};
	AudioDownmix_toStereo(left, CH, 1, lr);
	assert(lr[0] > 0 && lr[1] == 0);
	free(in);
	free(out);
}

static void test_many_channels(void) {
	int16_t in[10] = {1000, 2000, 3, 4, 5, 6, 7, 8, 9, 10};
	int16_t out[2];
	AudioDownmix_toStereo(in, 10, 1, out);
	assert(out[0] == 1000 && out[1] == 2000);
}

int main(void) {
	test_stereo_passthrough();
	test_mono_duplicates();
	test_51_no_overflow_or_clip();
	test_many_channels();
	printf("test_audio_downmix: OK\n");
	return 0;
}
