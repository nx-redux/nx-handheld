// Host test for p8state.c: the header a native PICO-8 save state starts with, and the
// checks the core makes before a state is allowed near PICO-8.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../p8state.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

#define MAX_LEN (1u << 20)

static uint8_t sha_a[32], sha_b[32];

// A state as the shim leaves it: header, then len bytes of snapshot, then padding
static size_t make_state(uint8_t* buf, size_t cap, const uint8_t* sha, const char* cart, uint32_t len, uint32_t ticks) {
	struct p8state_hdr h;
	CHECK(p8state_hdr_init(&h, sha, cart) == 0);
	h.len = len;
	h.ticks = ticks;
	CHECK(sizeof(h) + len <= cap);
	memset(buf, 0, cap);
	memcpy(buf, &h, sizeof(h));
	for (uint32_t i = 0; i < len; i++)
		buf[sizeof(h) + i] = (uint8_t)(i * 7 + 1);
	return sizeof(h) + len;
}

static void test_cart_name(void) {
	CHECK(strcmp(p8state_cart_name("/mnt/SDCARD/Roms/Pico-8 (P8)/celeste.p8.png"), "celeste.p8.png") == 0);
	CHECK(strcmp(p8state_cart_name("celeste.p8"), "celeste.p8") == 0);
	CHECK(strcmp(p8state_cart_name("/a/b/"), "") == 0);
}

static void test_init(void) {
	struct p8state_hdr h;
	memset(&h, 0xaa, sizeof(h));
	CHECK(p8state_hdr_init(&h, sha_a, "/x/celeste.p8.png") == 0);
	CHECK(memcmp(h.magic, "P8S1", 4) == 0);
	CHECK(h.len == 0 && h.ticks == 0);
	CHECK(memcmp(h.bin_sha256, sha_a, 32) == 0);
	CHECK(strcmp(h.cart, "celeste.p8.png") == 0);
	// the rest of the name field is zero, so equal states are equal bytes
	for (size_t i = strlen(h.cart); i < sizeof(h.cart); i++)
		CHECK(h.cart[i] == 0);

	char longname[400];
	memset(longname, 'c', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	CHECK(p8state_hdr_init(&h, sha_a, longname) == -1);
	longname[P8STATE_CART - 1] = '\0'; // 255 characters: fits with its terminator
	CHECK(p8state_hdr_init(&h, sha_a, longname) == 0);
	CHECK(p8state_hdr_init(&h, sha_a, NULL) == -1);
	CHECK(p8state_hdr_init(&h, NULL, "a.p8") == -1);
}

static void test_round_trip(void) {
	static uint8_t buf[4096];
	size_t used = make_state(buf, sizeof(buf), sha_a, "/r/celeste.p8.png", 1000, 123456);
	struct p8state_hdr h;
	const char* why = NULL;
	// exact size, and padded as minarch's buffer is
	CHECK(p8state_check(buf, used, MAX_LEN, sha_a, "/other/dir/celeste.p8.png", &h, &why) == 0);
	CHECK(p8state_check(buf, sizeof(buf), MAX_LEN, sha_a, "celeste.p8.png", &h, &why) == 0);
	CHECK(h.len == 1000 && h.ticks == 123456);
	CHECK(strcmp(h.cart, "celeste.p8.png") == 0);
	CHECK(memcmp(h.bin_sha256, sha_a, 32) == 0);
	// a misaligned buffer is fine (minarch's are calloc'd, but nothing relies on it)
	static uint8_t odd[4097];
	memcpy(odd + 1, buf, used);
	CHECK(p8state_check(odd + 1, used, MAX_LEN, sha_a, "celeste.p8.png", &h, &why) == 0);
}

static void expect_refused(const uint8_t* buf, size_t size, const uint8_t* sha, const char* cart, const char* reason) {
	struct p8state_hdr h;
	const char* why = NULL;
	CHECK(p8state_check(buf, size, MAX_LEN, sha, cart, &h, &why) == -1);
	CHECK(why && strstr(why, reason));
}

static void test_refused(void) {
	static uint8_t buf[4096];
	size_t used = make_state(buf, sizeof(buf), sha_a, "celeste.p8.png", 1000, 5);

	expect_refused(buf, used, sha_b, "celeste.p8.png", "another build");
	expect_refused(buf, used, sha_a, "jelpi.p8.png", "another cart");
	expect_refused(buf, used, sha_a, "celeste.p8", "another cart");

	// len past the buffer: cut short by one byte, and the header alone
	expect_refused(buf, used - 1, sha_a, "celeste.p8.png", "cut short");
	expect_refused(buf, sizeof(struct p8state_hdr), sha_a, "celeste.p8.png", "cut short");
	expect_refused(buf, sizeof(struct p8state_hdr) - 1, sha_a, "celeste.p8.png", "cut short");
	expect_refused(buf, 0, sha_a, "celeste.p8.png", "cut short");

	// len larger than the buffer, near 2^32 (no wrap in the sum)
	struct p8state_hdr h;
	memcpy(&h, buf, sizeof(h));
	h.len = 0xffffffffu;
	memcpy(buf, &h, sizeof(h));
	expect_refused(buf, sizeof(buf), sha_a, "celeste.p8.png", "cut short");
	// len that fits this buffer but not a state the shim could ever have written
	h.len = MAX_LEN + 1;
	memcpy(buf, &h, sizeof(h));
	static uint8_t big[MAX_LEN + 1024];
	memcpy(big, buf, sizeof(h));
	expect_refused(big, sizeof(big), sha_a, "celeste.p8.png", "cut short");

	// magic
	make_state(buf, sizeof(buf), sha_a, "celeste.p8.png", 10, 5);
	buf[3] = '2';
	expect_refused(buf, sizeof(buf), sha_a, "celeste.p8.png", "not a");
	memset(buf, 0, sizeof(buf)); // an all-zero buffer, as a state file of another core cut to size
	expect_refused(buf, sizeof(buf), sha_a, "celeste.p8.png", "not a");

	// a cart name with no terminator in the field
	make_state(buf, sizeof(buf), sha_a, "celeste.p8.png", 10, 5);
	memset(buf + offsetof(struct p8state_hdr, cart), 'x', P8STATE_CART);
	expect_refused(buf, sizeof(buf), sha_a, "celeste.p8.png", "not a");

	// NULL buffer
	expect_refused(NULL, 100, sha_a, "celeste.p8.png", "cut short");
}

static void hex(const uint8_t* d, char* out) {
	for (int i = 0; i < 32; i++)
		sprintf(out + i * 2, "%02x", d[i]);
}

static void test_sha256(void) {
	uint8_t d[32];
	char s[65];
	p8state_sha256("abc", 3, d);
	hex(d, s);
	CHECK(strcmp(s, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
	p8state_sha256("", 0, d);
	hex(d, s);
	CHECK(strcmp(s, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
	// two blocks, across the 56-byte padding edge
	const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	p8state_sha256(two, strlen(two), d);
	hex(d, s);
	CHECK(strcmp(s, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);

	// a file, in pieces larger than one read: a million 'a's
	char path[64];
	snprintf(path, sizeof(path), "/tmp/nx_test_state_sha.%d", (int)getpid());
	FILE* f = fopen(path, "wb");
	CHECK(f);
	for (int i = 0; i < 1000000; i++)
		fputc('a', f);
	fclose(f);
	CHECK(p8state_sha256_file(path, d) == 0);
	unlink(path);
	hex(d, s);
	CHECK(strcmp(s, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
	CHECK(p8state_sha256_file(path, d) == -1); // gone
}

int main(void) {
	for (int i = 0; i < 32; i++) {
		sha_a[i] = (uint8_t)i;
		sha_b[i] = (uint8_t)i;
	}
	sha_b[31] ^= 1;
	test_cart_name();
	test_init();
	test_round_trip();
	test_refused();
	test_sha256();
	printf("test_state_header: ok\n");
	return 0;
}
