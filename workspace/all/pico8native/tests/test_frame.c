// Host test for shim/frame.c: PICO-8's screen texture → XRGB8888, rows pitch bytes apart.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../p8ipc.h"
#include "../shim/frame.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

// Rows padded past 128 pixels, the padding filled with junk that must not leak in
#define PAD 40

static uint8_t pixels[P8_H * (P8_W * 4 + PAD)];
static uint32_t out[P8_W * P8_H];

static void put32(int pitch, int x, int y, uint32_t v) {
	memcpy(pixels + (size_t)y * pitch + x * 4, &v, 4);
}

static void put16(int pitch, int x, int y, uint16_t v) {
	memcpy(pixels + (size_t)y * pitch + x * 2, &v, 2);
}

static uint32_t at(int x, int y) {
	return out[y * P8_W + x];
}

static void test_argb8888(void) {
	int pitch = P8_W * 4 + PAD;
	memset(pixels, 0xa5, sizeof(pixels));
	for (int y = 0; y < P8_H; y++)
		for (int x = 0; x < P8_W; x++)
			put32(pitch, x, y, 0xff000000u | (uint32_t)(y << 8) | (uint32_t)x);
	put32(pitch, 0, 0, 0xff112233u);
	put32(pitch, 127, 0, 0x80445566u);
	put32(pitch, 0, 127, 0x00778899u);
	put32(pitch, 127, 127, 0xffaabbccu);
	memset(out, 0xee, sizeof(out));
	CHECK(frame_convert(pixels, pitch, P8_FMT_ARGB8888, out) == 0);
	CHECK(at(0, 0) == 0x00112233u);
	CHECK(at(127, 0) == 0x00445566u);
	CHECK(at(0, 127) == 0x00778899u);
	CHECK(at(127, 127) == 0x00aabbccu);
	CHECK(at(5, 9) == 0x00000905u);
}

static void test_abgr8888(void) {
	int pitch = P8_W * 4 + PAD;
	memset(pixels, 0xa5, sizeof(pixels));
	put32(pitch, 0, 0, 0xff332211u);
	put32(pitch, 127, 127, 0xffccbbaau);
	CHECK(frame_convert(pixels, pitch, P8_FMT_ABGR8888, out) == 0);
	CHECK(at(0, 0) == 0x00112233u);
	CHECK(at(127, 127) == 0x00aabbccu);
}

static void test_rgb565(void) {
	int pitch = P8_W * 2 + PAD;
	memset(pixels, 0xa5, sizeof(pixels));
	put16(pitch, 0, 0, 0xf800);
	put16(pitch, 127, 0, 0x07e0);
	put16(pitch, 0, 127, 0x001f);
	put16(pitch, 127, 127, 0x8410);
	CHECK(frame_convert(pixels, pitch, P8_FMT_RGB565, out) == 0);
	CHECK(at(0, 0) == 0x00ff0000u);
	CHECK(at(127, 0) == 0x0000ff00u);
	CHECK(at(0, 127) == 0x000000ffu);
	CHECK(at(127, 127) == 0x00848284u);
}

static void test_refused(void) {
	CHECK(frame_convert(pixels, P8_W * 4, 0x12345678u, out) == -1);
	CHECK(frame_convert(pixels, P8_W * 4 - 1, P8_FMT_ARGB8888, out) == -1); // pitch shorter than a row
	CHECK(frame_convert(NULL, P8_W * 4, P8_FMT_ARGB8888, out) == -1);
	CHECK(strcmp(frame_format_name(P8_FMT_ARGB8888), "ARGB8888") == 0);
	CHECK(strcmp(frame_format_name(0x12345678u), "unknown") == 0);
}

int main(void) {
	test_argb8888();
	test_abgr8888();
	test_rgb565();
	test_refused();
	puts("test_frame: ok");
	return 0;
}
