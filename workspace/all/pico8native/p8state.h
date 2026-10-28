#ifndef P8STATE_H
#define P8STATE_H

// The save state blob, as it sits in p8ipc's state[] and in minarch's state buffer:
// struct p8state_hdr, then `len` bytes of the shim's memory snapshot (shim/state.c),
// then (in minarch's buffer only) zero padding up to the size the core reported.
//
// The core fills magic, bin_sha256 and cart before a SAVE; the shim fills len and
// ticks. A state only loads into the same pico8_64 (by hash) running the same cart
// (by file name), and the core checks both before PICO-8 is touched.

#include <stddef.h>
#include <stdint.h>

#define P8STATE_MAGIC "P8S1"
#define P8STATE_CART 256

struct p8state_hdr {
	char magic[4];			 /* "P8S1", no terminator */
	uint32_t len;			 /* bytes of snapshot after the header */
	uint8_t bin_sha256[32];	 /* SHA-256 of the pico8_64 that saved it */
	char cart[P8STATE_CART]; /* the cart's file name, NUL-terminated */
	uint32_t ticks;			 /* PICO-8's clock at the end of the frame it was saved at */
};

_Static_assert(sizeof(struct p8state_hdr) == 300, "p8state_hdr is a file format: no padding");

// The cart's name as a state records it: the file name without its directory
const char* p8state_cart_name(const char* path);

// A header with magic, sha and the name of cart; len and ticks 0. -1 if the name does
// not fit or an argument is NULL.
int p8state_hdr_init(struct p8state_hdr* h, const uint8_t sha[32], const char* cart);

// Checks the state in buf (size bytes) against this pico8_64 and cart, copying its
// header to *out. 0 if it can load; else -1 with *why set to a short reason: not a
// state, the header or data cut short (len past size, or past max_len), another build
// of PICO-8, another cart.
int p8state_check(const void* buf, size_t size, size_t max_len, const uint8_t sha[32], const char* cart,
				  struct p8state_hdr* out, const char** why);

// SHA-256 of a buffer, and of a file (0 ok, -1 if it cannot be read)
void p8state_sha256(const void* data, size_t size, uint8_t out[32]);
int p8state_sha256_file(const char* path, uint8_t out[32]);

#endif
