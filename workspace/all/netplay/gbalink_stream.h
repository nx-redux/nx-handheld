#ifndef GBALINK_STREAM_H
#define GBALINK_STREAM_H
// gbalink TCP stream helpers, kept free of gbalink globals so the host tests
// (tests/test_gbalink_stream.c) can cover them.
//
// Wire packet: a 5-byte header (cmd, size big-endian 16, client id big-endian
// 16) followed by `size` payload bytes.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include "network_common.h"

#define GBALINK_WIRE_HDR_SIZE 5

// Makes room at the end of buf[0..cap) by moving the unread bytes
// [*read_idx, *write_idx) to the front when fewer than min_space bytes are
// free at the end. The move is skipped while read_idx is in the first half
// (not worth the memmove yet), unless the end is full: then a partly received
// packet could never complete, since recv would get no room to read into.
static inline void gbalink_stream_compact(uint8_t* buf, size_t cap, size_t* read_idx, size_t* write_idx,
										  size_t min_space) {
	size_t available = *write_idx - *read_idx;
	size_t space_at_end = cap - *write_idx;
	if (space_at_end >= min_space || *read_idx == 0)
		return;
	if (*read_idx <= cap / 2 && space_at_end > 0)
		return;
	memmove(buf, buf + *read_idx, available);
	*read_idx = 0;
	*write_idx = available;
}

static inline long long gbalink_stream_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Reads whole packets from fd, one at a time and never past the end of one,
// until a packet whose cmd is `want` arrives; other packets are discarded.
// Gives up after timeout_ms, or as soon as *running goes false (checked every
// 50 ms). Returns 1 when found, 0 on timeout or stop, -1 when the peer closed
// the connection or a packet was cut off.
static inline int gbalink_wait_cmd(int fd, uint8_t want, int timeout_ms, volatile bool* running) {
	long long deadline = gbalink_stream_now_ms() + timeout_ms;
	while (*running) {
		long long left = deadline - gbalink_stream_now_ms();
		if (left <= 0)
			return 0;
		struct pollfd pfd = {.fd = fd, .events = POLLIN};
		int r = poll(&pfd, 1, left < 50 ? (int)left : 50);
		if (r < 0)
			return -1;
		if (r == 0)
			continue;
		uint8_t hdr[GBALINK_WIRE_HDR_SIZE];
		if (NET_recvAll(fd, hdr, sizeof(hdr), 1000, NULL) != 1)
			return -1;
		size_t size = ((size_t)hdr[1] << 8) | hdr[2];
		uint8_t scratch[64];
		while (size > 0) {
			size_t n = size < sizeof(scratch) ? size : sizeof(scratch);
			if (NET_recvAll(fd, scratch, n, 1000, NULL) != 1)
				return -1;
			size -= n;
		}
		if (hdr[0] == want)
			return 1;
	}
	return 0;
}

#endif
