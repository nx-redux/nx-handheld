// Host test for shim/audio.c: exact per-frame sample counts (ruling R3), format conversion,
// and pulling PICO-8's callback a block at a time without gaps or repeats.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shim/audio.h"
#include "../shim/input.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

static struct p8audio au;
static int16_t out[P8IPC_AUDIO_MAX * 2];
static int calls, bad_len;
static int16_t counter;

// A mono S16 callback writing a running count, refusing (flagging) any len but a whole block
static void count_cb(void* userdata, uint8_t* stream, int len) {
	(void)userdata;
	calls++;
	if (len != au.samples * 2)
		bad_len = 1;
	int16_t* s = (int16_t*)stream;
	for (int i = 0; i < len / 2; i++)
		s[i] = counter++;
}

static void test_frames_for(void) {
	uint32_t rem = 0, total = 0, ms = 0;
	// 22050 Hz over 17/17/16 ms frames: per-frame rounding would drift, the remainder must not
	for (int i = 0; i < 3000; i++) {
		uint32_t f = i % 3 == 2 ? 16 : 17;
		uint32_t n = audio_frames_for(&rem, 22050, f);
		CHECK(n == 352 || n == 353 || n == 374 || n == 375);
		total += n;
		ms += f;
	}
	CHECK(ms == 50000);
	CHECK(total == 22050u * 50); // exactly 50 s worth
	rem = 0;
	total = 0;
	for (int i = 0; i < 1000; i++)
		total += audio_frames_for(&rem, 44100, 33);
	CHECK(total == 44100u * 33); // 33 s at 44.1 kHz, 1455.3 per frame
}

static void test_convert(void) {
	int16_t o[8];
	int16_t mono[2] = {100, -200};
	CHECK(audio_convert(mono, 2, P8_AUDIO_S16LSB, 1, o) == 0);
	CHECK(o[0] == 100 && o[1] == 100 && o[2] == -200 && o[3] == -200);
	int16_t st[4] = {1, 2, 3, 4};
	CHECK(audio_convert(st, 2, P8_AUDIO_S16LSB, 2, o) == 0);
	CHECK(memcmp(o, st, sizeof(st)) == 0);
	float f[4] = {1.0f, -1.0f, 0.5f, 2.0f};
	CHECK(audio_convert(f, 2, P8_AUDIO_F32LSB, 2, o) == 0);
	CHECK(o[0] == 32767 && o[1] == -32767 && o[2] == 16383 && o[3] == 32767); // clipped
	uint8_t u8[2] = {128, 255};
	CHECK(audio_convert(u8, 2, P8_AUDIO_U8, 1, o) == 0);
	CHECK(o[0] == 0 && o[1] == 0 && o[2] == 127 * 256 && o[3] == 127 * 256);
	CHECK(audio_convert(st, 2, 0x9010 /* S16MSB */, 2, o) == -1);
	CHECK(audio_convert(st, 2, P8_AUDIO_S16LSB, 6, o) == -1);
}

static void test_pull(void) {
	memset(&au, 0, sizeof(au));
	au.callback = count_cb;
	au.freq = 22050;
	au.format = P8_AUDIO_S16LSB;
	au.channels = 1;
	au.samples = 1024;
	au.paused = 1;

	// Paused (as SDL opens a device): silence, the callback is not run
	memset(out, 0x55, sizeof(out));
	audio_pull(&au, out, 368);
	CHECK(calls == 0 && out[0] == 0 && out[367 * 2 + 1] == 0);

	au.paused = 0;
	int16_t expect = 0;
	uint32_t rem = 0;
	for (int i = 0; i < 300; i++) {
		uint32_t n = audio_frames_for(&rem, au.freq, i % 3 == 2 ? 16 : 17);
		audio_pull(&au, out, n);
		for (uint32_t k = 0; k < n; k++) {
			CHECK(out[k * 2] == expect && out[k * 2 + 1] == expect); // mono to both sides, no gap
			expect++;
		}
	}
	CHECK(!bad_len);
	CHECK(calls >= 107 && calls <= 109); // 110250 frames / 1024 per block
	CHECK(au.fifo_frames < au.samples);

	// A callback-less (unopened) device gives silence
	struct p8audio none;
	memset(&none, 0, sizeof(none));
	out[0] = 1;
	audio_pull(&none, out, 10);
	CHECK(out[0] == 0);
}

static void test_input(void) {
	uint32_t b = (1u << P8_RETRO_B) | (1u << P8_RETRO_LEFT) | (1u << P8_RETRO_START);
	CHECK(input_pad_button(b, P8_PAD_A) == 1); // B → O
	CHECK(input_pad_button(b, P8_PAD_B) == 0);
	CHECK(input_pad_button(b, P8_PAD_LEFT) == 1);
	CHECK(input_pad_button(b, P8_PAD_RIGHT) == 0);
	CHECK(input_pad_button(b, P8_PAD_START) == 1);
	CHECK(input_pad_button(1u << P8_RETRO_A, P8_PAD_B) == 1); // A → X
	CHECK(input_pad_button(0xffffffffu, 2 /* X */) == 0);	  // nothing else reaches PICO-8
	CHECK(input_pad_button(0xffffffffu, 4 /* BACK */) == 0);
	CHECK(input_pad_button(0xffffffffu, -1) == 0);
}

int main(void) {
	test_frames_for();
	test_convert();
	test_pull();
	test_input();
	puts("test_audio: ok");
	return 0;
}
