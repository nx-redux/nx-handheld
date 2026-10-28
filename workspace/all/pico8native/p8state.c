#include "p8state.h"

#include <stdio.h>
#include <string.h>

const char* p8state_cart_name(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

int p8state_hdr_init(struct p8state_hdr* h, const uint8_t sha[32], const char* cart) {
	if (!h || !sha || !cart)
		return -1;
	const char* name = p8state_cart_name(cart);
	if (strlen(name) >= sizeof(h->cart))
		return -1;
	memset(h, 0, sizeof(*h));
	memcpy(h->magic, P8STATE_MAGIC, sizeof(h->magic));
	memcpy(h->bin_sha256, sha, sizeof(h->bin_sha256));
	memcpy(h->cart, name, strlen(name));
	return 0;
}

int p8state_check(const void* buf, size_t size, size_t max_len, const uint8_t sha[32], const char* cart,
				  struct p8state_hdr* out, const char** why) {
	struct p8state_hdr h;
	if (!buf || size < sizeof(h)) {
		*why = "cut short";
		return -1;
	}
	memcpy(&h, buf, sizeof(h));
	if (memcmp(h.magic, P8STATE_MAGIC, sizeof(h.magic)) != 0 || !memchr(h.cart, '\0', sizeof(h.cart))) {
		*why = "not a native PICO-8 state";
		return -1;
	}
	if (h.len > max_len || h.len > size - sizeof(h)) {
		*why = "cut short";
		return -1;
	}
	if (memcmp(h.bin_sha256, sha, sizeof(h.bin_sha256)) != 0) {
		*why = "saved by another build of pico8_64";
		return -1;
	}
	if (strcmp(h.cart, p8state_cart_name(cart)) != 0) {
		*why = "saved for another cart";
		return -1;
	}
	*out = h;
	return 0;
}

/* ---- SHA-256 (FIPS 180-4) ---- */

struct sha256 {
	uint32_t h[8];
	uint64_t total;
	uint8_t block[64];
	size_t fill;
};

static const uint32_t K[64] = {
	0x428a2f98,
	0x71374491,
	0xb5c0fbcf,
	0xe9b5dba5,
	0x3956c25b,
	0x59f111f1,
	0x923f82a4,
	0xab1c5ed5,
	0xd807aa98,
	0x12835b01,
	0x243185be,
	0x550c7dc3,
	0x72be5d74,
	0x80deb1fe,
	0x9bdc06a7,
	0xc19bf174,
	0xe49b69c1,
	0xefbe4786,
	0x0fc19dc6,
	0x240ca1cc,
	0x2de92c6f,
	0x4a7484aa,
	0x5cb0a9dc,
	0x76f988da,
	0x983e5152,
	0xa831c66d,
	0xb00327c8,
	0xbf597fc7,
	0xc6e00bf3,
	0xd5a79147,
	0x06ca6351,
	0x14292967,
	0x27b70a85,
	0x2e1b2138,
	0x4d2c6dfc,
	0x53380d13,
	0x650a7354,
	0x766a0abb,
	0x81c2c92e,
	0x92722c85,
	0xa2bfe8a1,
	0xa81a664b,
	0xc24b8b70,
	0xc76c51a3,
	0xd192e819,
	0xd6990624,
	0xf40e3585,
	0x106aa070,
	0x19a4c116,
	0x1e376c08,
	0x2748774c,
	0x34b0bcb5,
	0x391c0cb3,
	0x4ed8aa4a,
	0x5b9cca4f,
	0x682e6ff3,
	0x748f82ee,
	0x78a5636f,
	0x84c87814,
	0x8cc70208,
	0x90befffa,
	0xa4506ceb,
	0xbef9a3f7,
	0xc67178f2,
};

#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void sha256_block(struct sha256* s, const uint8_t* p) {
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d;
	s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

static void sha256_init(struct sha256* s) {
	static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
								   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(s->h, iv, sizeof(iv));
	s->total = 0;
	s->fill = 0;
}

static void sha256_update(struct sha256* s, const uint8_t* p, size_t n) {
	s->total += n;
	while (n) {
		size_t take = 64 - s->fill < n ? 64 - s->fill : n;
		memcpy(s->block + s->fill, p, take);
		s->fill += take;
		p += take;
		n -= take;
		if (s->fill == 64) {
			sha256_block(s, s->block);
			s->fill = 0;
		}
	}
}

static void sha256_final(struct sha256* s, uint8_t out[32]) {
	uint64_t bits = s->total * 8;
	uint8_t pad = 0x80, zero = 0;
	sha256_update(s, &pad, 1);
	while (s->fill != 56)
		sha256_update(s, &zero, 1);
	uint8_t len[8];
	for (int i = 0; i < 8; i++)
		len[i] = (uint8_t)(bits >> (56 - i * 8));
	sha256_update(s, len, 8);
	for (int i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(s->h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)s->h[i];
	}
}

void p8state_sha256(const void* data, size_t size, uint8_t out[32]) {
	struct sha256 s;
	sha256_init(&s);
	sha256_update(&s, data, size);
	sha256_final(&s, out);
}

int p8state_sha256_file(const char* path, uint8_t out[32]) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return -1;
	struct sha256 s;
	sha256_init(&s);
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		sha256_update(&s, buf, n);
	int bad = ferror(f);
	fclose(f);
	if (bad)
		return -1;
	sha256_final(&s, out);
	return 0;
}
