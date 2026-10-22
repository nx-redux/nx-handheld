// Host-compiled unit test for the ID3v2 frame walker in tag_meta.c (used by
// the player's MP3 tag reader and the embedded lyrics reader).
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tag_meta.c tests/test_tag_meta_id3.c -o /tmp/t && /tmp/t
#include <stdio.h>
#include <string.h>

#include "../tag_meta.h"

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

typedef struct {
	int count;
	char ids[8][5];
	char text[8][32];
	size_t lens[8];
	uint8_t raw[8][16];
} Seen;

static void on_frame(const char* id, const uint8_t* data, size_t len, void* ctx) {
	Seen* s = ctx;
	if (s->count >= 8)
		return;
	snprintf(s->ids[s->count], 5, "%s", id);
	s->lens[s->count] = len;
	memcpy(s->raw[s->count], data, len < 16 ? len : 16);
	if (id[0] == 'T' && len > 1)
		TagMeta_id3Text(data[0], data + 1, len - 1, s->text[s->count], sizeof(s->text[0]));
	s->count++;
}

static int put_frame(uint8_t* out, const char* id, int ver, uint8_t fflags, const uint8_t* body, int len) {
	memcpy(out, id, 4);
	if (ver == 4) {
		out[4] = (len >> 21) & 0x7F;
		out[5] = (len >> 14) & 0x7F;
		out[6] = (len >> 7) & 0x7F;
		out[7] = len & 0x7F;
	} else {
		out[4] = (uint8_t)(len >> 24);
		out[5] = (uint8_t)(len >> 16);
		out[6] = (uint8_t)(len >> 8);
		out[7] = (uint8_t)len;
	}
	out[8] = 0;
	out[9] = fflags;
	memcpy(out + 10, body, len);
	return 10 + len;
}

static void test_extended_header_v23(void) {
	// v2.3 extended header (size 6, excluding its own 4 bytes) before TIT2
	uint8_t tag[64] = {0, 0, 0, 6, 0, 0, 0, 0, 0, 0};
	int n = 10;
	n += put_frame(tag + n, "TIT2", 3, 0, (const uint8_t*)"\0Song", 5);
	Seen s = {0};
	TagMeta_id3Frames(3, 0x40, tag, n, on_frame, &s);
	CHECK(s.count == 1 && strcmp(s.ids[0], "TIT2") == 0 && strcmp(s.text[0], "Song") == 0);

	// without the flag, the same bytes are not read as a frame
	Seen none = {0};
	TagMeta_id3Frames(3, 0, tag, n, on_frame, &none);
	CHECK(none.count == 0);
}

static void test_extended_header_v24(void) {
	// v2.4 extended header: syncsafe size that includes itself
	uint8_t tag[64] = {0, 0, 0, 6, 1, 0};
	int n = 6;
	n += put_frame(tag + n, "TPE1", 4, 0, (const uint8_t*)"\3Band", 5);
	Seen s = {0};
	TagMeta_id3Frames(4, 0x40, tag, n, on_frame, &s);
	CHECK(s.count == 1 && strcmp(s.text[0], "Band") == 0);
}

static void test_unsync_v23(void) {
	// v2.3 tag-level unsynchronisation: FF 00 in the stream is FF
	uint8_t tag[64];
	const uint8_t body[] = {0x00, 0xFF, 0x00, 0xE0, 0x01};
	int n = put_frame(tag, "PRIV", 3, 0, body, sizeof(body));
	tag[7] = 4; // sizes count the bytes before unsynchronisation
	Seen s = {0};
	TagMeta_id3Frames(3, 0x80, tag, n, on_frame, &s);
	CHECK(s.count == 1 && s.lens[0] == 4);
	CHECK(s.raw[0][0] == 0x00 && s.raw[0][1] == 0xFF && s.raw[0][2] == 0xE0 && s.raw[0][3] == 0x01);
}

static void test_v24_dli_and_frame_unsync(void) {
	// v2.4 frame with a data length indicator (0x01) and unsynchronisation (0x02)
	uint8_t tag[64];
	const uint8_t body[] = {0, 0, 0, 3, 0xFF, 0x00, 0xFB, 0x90};
	int n = put_frame(tag, "APIC", 4, 0x03, body, sizeof(body));
	n += put_frame(tag + n, "TALB", 4, 0, (const uint8_t*)"\0Disc", 5);
	Seen s = {0};
	TagMeta_id3Frames(4, 0, tag, n, on_frame, &s);
	CHECK(s.count == 2);
	CHECK(s.lens[0] == 3 && s.raw[0][0] == 0xFF && s.raw[0][1] == 0xFB && s.raw[0][2] == 0x90);
	CHECK(strcmp(s.ids[1], "TALB") == 0 && strcmp(s.text[1], "Disc") == 0);
}

static void test_skips_compressed_and_bounds(void) {
	uint8_t tag[64];
	int n = put_frame(tag, "TIT2", 3, 0x80, (const uint8_t*)"\0Zip", 4); // compressed
	n += put_frame(tag + n, "TPE1", 3, 0, (const uint8_t*)"\0Ok", 3);
	Seen s = {0};
	TagMeta_id3Frames(3, 0, tag, n, on_frame, &s);
	CHECK(s.count == 1 && strcmp(s.ids[0], "TPE1") == 0);

	// a frame claiming more than the tag holds ends the walk
	uint8_t bad[20];
	put_frame(bad, "TIT2", 3, 0, (const uint8_t*)"\0Abcdefghi", 10);
	bad[7] = 200;
	Seen b = {0};
	TagMeta_id3Frames(3, 0, bad, sizeof(bad), on_frame, &b);
	CHECK(b.count == 0);

	// an extended header bigger than the tag is not walked past
	uint8_t ext[12] = {0, 0, 0x7F, 0x7F};
	Seen e = {0};
	TagMeta_id3Frames(4, 0x40, ext, sizeof(ext), on_frame, &e);
	CHECK(e.count == 0);

	// v2.2 (3-character ids) is not walked
	Seen v = {0};
	TagMeta_id3Frames(2, 0, tag, n, on_frame, &v);
	CHECK(v.count == 0);
}

int main(void) {
	test_extended_header_v23();
	test_extended_header_v24();
	test_unsync_v23();
	test_v24_dli_and_frame_unsync();
	test_skips_compressed_and_bounds();
	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("test_tag_meta_id3: all passed\n");
	return 0;
}
