// Host test for gbalink_stream.h: stream-buffer compaction (a partly received
// packet near the end of the buffer must not wedge the link) and the host's
// READY wait on a not-yet-shared socket.
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../gbalink_stream.h"

#define CAP (2048 + GBALINK_WIRE_HDR_SIZE) // gbalink's stream_buf

static void test_compact(void) {
	uint8_t buf[CAP];
	for (size_t i = 0; i < CAP; i++)
		buf[i] = (uint8_t)i;

	// The wedge: a 1500-byte packet starts at 1000 and the end is full.
	// Before, read_idx <= CAP/2 blocked the move and recv got no room.
	size_t r = 1000, w = CAP;
	gbalink_stream_compact(buf, CAP, &r, &w, 1024);
	assert(r == 0 && w == CAP - 1000);
	assert(buf[0] == (uint8_t)1000 && buf[w - 1] == (uint8_t)(CAP - 1));

	// Room left at the end and read_idx in the first half: not moved yet.
	r = 600, w = CAP - 10;
	gbalink_stream_compact(buf, CAP, &r, &w, 1024);
	assert(r == 600 && w == CAP - 10);

	// Past the halfway point: moved.
	r = CAP / 2 + 1, w = CAP - 10;
	gbalink_stream_compact(buf, CAP, &r, &w, 1024);
	assert(r == 0 && w == CAP - 10 - (CAP / 2 + 1));

	// Enough room at the end, or nothing before read_idx: untouched.
	r = 1500, w = 1600;
	gbalink_stream_compact(buf, CAP, &r, &w, 64);
	assert(r == 1500 && w == 1600);
	r = 0, w = CAP;
	gbalink_stream_compact(buf, CAP, &r, &w, 1024);
	assert(r == 0 && w == CAP);
}

static void put_packet(int fd, uint8_t cmd, const void* data, uint16_t size) {
	uint8_t hdr[GBALINK_WIRE_HDR_SIZE] = {cmd, (uint8_t)(size >> 8), (uint8_t)size, 0, 0};
	assert(write(fd, hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));
	if (size)
		assert(write(fd, data, size) == size);
}

static long ms_since(struct timeval* t0) {
	struct timeval t1;
	gettimeofday(&t1, NULL);
	return (t1.tv_sec - t0->tv_sec) * 1000 + (t1.tv_usec - t0->tv_usec) / 1000;
}

static void test_wait_cmd(void) {
	enum { HEARTBEAT = 0x04,
		   READY = 0x05,
		   SIO = 0x01 };
	volatile bool running = true;
	int sv[2];

	// Other packets (one with a payload over the 64-byte scratch) are skipped;
	// the bytes after READY stay unread for the stream parser.
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	uint8_t big[300];
	memset(big, 0xAB, sizeof(big));
	put_packet(sv[1], HEARTBEAT, NULL, 0);
	put_packet(sv[1], SIO, big, sizeof(big));
	put_packet(sv[1], READY, "x", 1);
	put_packet(sv[1], SIO, "after", 5);
	assert(gbalink_wait_cmd(sv[0], READY, 1000, &running) == 1);
	uint8_t next[GBALINK_WIRE_HDR_SIZE + 5];
	assert(recv(sv[0], next, sizeof(next), 0) == (ssize_t)sizeof(next));
	assert(next[0] == SIO && memcmp(next + GBALINK_WIRE_HDR_SIZE, "after", 5) == 0);
	close(sv[0]);
	close(sv[1]);

	// A silent peer times out on time.
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	struct timeval t0;
	gettimeofday(&t0, NULL);
	assert(gbalink_wait_cmd(sv[0], READY, 200, &running) == 0);
	long el = ms_since(&t0);
	assert(el >= 150 && el < 1000);

	// Stopping (running false) ends the wait at once.
	running = false;
	gettimeofday(&t0, NULL);
	assert(gbalink_wait_cmd(sv[0], READY, 5000, &running) == 0);
	assert(ms_since(&t0) < 200);
	running = true;

	// A peer that closes is an error, not a 5 s wait.
	close(sv[1]);
	gettimeofday(&t0, NULL);
	assert(gbalink_wait_cmd(sv[0], READY, 5000, &running) == -1);
	assert(ms_since(&t0) < 1000);
	close(sv[0]);

	// A header cut off part way is an error too.
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	assert(write(sv[1], "\x05\x00", 2) == 2);
	close(sv[1]);
	assert(gbalink_wait_cmd(sv[0], READY, 5000, &running) == -1);
	close(sv[0]);
}

int main(void) {
	test_compact();
	test_wait_cmd();
	printf("gbalink_stream: all tests passed\n");
	return 0;
}
