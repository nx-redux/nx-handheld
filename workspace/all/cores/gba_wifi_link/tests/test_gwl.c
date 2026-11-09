// Host-compiled tests for gba_wifi_link against a fake emulator.
// Build & run: workspace/all/cores/gba_wifi_link/tests/run_tests.sh
#undef NDEBUG // the checks below must always run
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gwl.h"

#define OFF_SIODATA32_L 0x120
#define OFF_SIODATA32_H 0x122
#define OFF_SIOCNT 0x128

typedef struct {
	uint16_t client_id;
	size_t len;
	uint8_t buf[128];
} Packet;

static struct {
	uint16_t io[0x200];
	int io_writes;
	uint32_t cycles;
	Packet sent[64];
	int nsent;
	Packet inbox[8]; // delivered on the next poll
	int ninbox;
	int polls;
} fk;

static uint16_t f_read(void* c, uint32_t off) {
	(void)c;
	return fk.io[off / 2];
}
static void f_write(void* c, uint32_t off, uint16_t v) {
	(void)c;
	fk.io[off / 2] = v;
	fk.io_writes++;
}
static uint32_t f_cycles(void* c) {
	(void)c;
	return fk.cycles;
}
static void f_send(void* c, uint16_t id, const void* b, size_t n) {
	(void)c;
	assert(n <= sizeof(fk.sent[0].buf));
	assert(fk.nsent < 64);
	Packet* p = &fk.sent[fk.nsent++];
	p->client_id = id;
	p->len = n;
	memcpy(p->buf, b, n);
}
static void f_poll(void* c) {
	(void)c;
	fk.polls++;
	for (int i = 0; i < fk.ninbox; i++)
		gwl_receive(fk.inbox[i].buf, fk.inbox[i].len, fk.inbox[i].client_id);
	fk.ninbox = 0;
}

// run_tests.sh builds gwl.c with gpSP's receive functions renamed to these,
// so a test can tell whether a packet reached gpSP's code at all.
void gwl_gp_rfu_net_receive(const void* buf, size_t len, uint16_t client_id);
void gwl_gp_serialpoke_net_receive(const void* buf, size_t len, uint16_t client_id);
void gwl_gp_serialaw_net_receive(const void* buf, size_t len, uint16_t client_id);
static int dispatched;
void gwl_spy_rfu_net_receive(const void* buf, size_t len, uint16_t client_id) {
	dispatched++;
	gwl_gp_rfu_net_receive(buf, len, client_id);
}
void gwl_spy_serialpoke_net_receive(const void* buf, size_t len, uint16_t client_id) {
	dispatched++;
	gwl_gp_serialpoke_net_receive(buf, len, client_id);
}
void gwl_spy_serialaw_net_receive(const void* buf, size_t len, uint16_t client_id) {
	dispatched++;
	gwl_gp_serialaw_net_receive(buf, len, client_id);
}

static const GwlHost host = {NULL, f_read, f_write, f_cycles, f_send, f_poll};

static void setup(enum GwlMode mode, uint16_t client_id) {
	memset(&fk, 0, sizeof(fk));
	fk.cycles = 1234;
	dispatched = 0;
	gwl_begin(&host, mode, client_id);
}

static uint32_t xfer(uint32_t w) {
	return gwl_rfu_transfer(w);
}

static void rfu_handshake(void) {
	gwl_reset_rfu();
	assert(xfer(0x0000494E) == 0);
	assert(xfer(0xB6B1494E) == 0x494EB6B1);
	assert(xfer(0xB0BB8001) == 0x8001B6B1);
}

// Sends a command with its payload and returns the adapter's ack word.
static uint32_t rfu_cmd(uint8_t cmd, const uint32_t* data, uint8_t n) {
	assert(xfer(0x99660000u | ((uint32_t)n << 8) | cmd) == 0x80000000);
	for (uint8_t i = 0; i < n; i++)
		assert(xfer(data[i]) == 0x80000000);
	return xfer(0x80000000);
}

static void rfu_host_start(void) {
	static const uint32_t bcst[6] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666};
	rfu_handshake();
	assert(rfu_cmd(0x16, bcst, 6) == 0x99660096);
	assert(rfu_cmd(0x19, NULL, 0) == 0x99660099);
}

static void test_rfu_init_handshake(void) {
	setup(GWL_RFU, 0);
	rfu_handshake();
	// Ack = 0x99660080 | cmd | response length << 8, then back to waiting.
	assert(rfu_cmd(0x10, NULL, 0) == 0x99660090);
	assert(rfu_cmd(0x3d, NULL, 0) == 0x996600BD);
	// SYSVER answers one word.
	assert(rfu_cmd(0x12, NULL, 0) == 0x99660192);
	assert(xfer(0x80000000) == 0x00830117);
	assert(fk.nsent == 0);
}

static void test_rfu_broadcast_cadence(void) {
	setup(GWL_RFU, 0);
	rfu_host_start();
	fk.nsent = 0;
	for (int i = 0; i < 60; i++)
		gwl_frame_update();
	assert(fk.nsent == 2);
	for (int i = 0; i < 2; i++) {
		assert(fk.sent[i].client_id == 0xFFFF);
		assert(fk.sent[i].len == 36);
		assert(memcmp(fk.sent[i].buf, "RFU1\0\0\0\0", 8) == 0); // header, NET_RFU_BROADCAST
		assert(memcmp(&fk.sent[i].buf[12], "\x11\x11\x11\x11\x22\x22\x22\x22", 8) == 0);
	}
}

static void test_client_caps(void) {
	setup(GWL_RFU, 0);
	for (int i = 1; i <= 31; i++)
		assert(gwl_connected(i));
	assert(!gwl_connected(32));

	setup(GWL_POKE, 0);
	for (int i = 1; i <= 3; i++)
		assert(gwl_connected(i));
	assert(!gwl_connected(4));
}

static void test_disconnect_frees_slot(void) {
	setup(GWL_POKE, 0);
	for (int i = 1; i <= 3; i++)
		assert(gwl_connected(i));
	assert(!gwl_connected(2)); // full, whatever the id
	gwl_disconnected(2);
	assert(gwl_connected(2));
	assert(!gwl_connected(3));
}

static void test_disconnect_tells_the_game(void) {
	setup(GWL_RFU, 0);
	rfu_host_start();
	assert(gwl_connected(1));
	gwl_disconnected(1);
	// Whatever the game asks next is answered as a Wait, which then reports
	// the disconnect, so the game leaves the session instead of hanging.
	assert(rfu_cmd(0x13, NULL, 0) == 0x996600A7);
	fk.io[OFF_SIOCNT / 2] = 0x4080; // slave clock, start set, IRQ on
	assert(gwl_update(100));
	assert(fk.polls == 1);
	assert(fk.io[OFF_SIODATA32_H / 2] == 0x9966 && fk.io[OFF_SIODATA32_L / 2] == 0x0129);
	assert(!(fk.io[OFF_SIOCNT / 2] & 0x80));
}

static void test_none_mode_inert(void) {
	setup(GWL_NONE, 0);
	assert(gwl_mode() == GWL_NONE);
	fk.io[OFF_SIOCNT / 2] = 0x4080;
	for (int i = 0; i < 120; i++) {
		assert(!gwl_update(300000));
		gwl_frame_update();
		gwl_multi_master_send();
	}
	assert(fk.nsent == 0);
	assert(fk.io_writes == 0);
	assert(fk.polls == 0);
}

static void test_end_drops_transport(void) {
	setup(GWL_RFU, 0);
	rfu_host_start();
	gwl_end();
	fk.nsent = 0;
	for (int i = 0; i < 60; i++)
		gwl_frame_update();
	assert(fk.nsent == 0);
	assert(gwl_mode() == GWL_RFU);
}

static uint32_t be32(const uint8_t* p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// Delivers a packet from a heap block of exactly len bytes, so that a read
// past its end trips AddressSanitizer. words fill the start, big-endian.
static void deliver(uint16_t from, size_t len, const uint32_t* words, int nwords) {
	uint8_t* b = calloc(1, len);
	for (int i = 0; i < nwords && (size_t)(i * 4 + 4) <= len; i++) {
		b[i * 4] = words[i] >> 24;
		b[i * 4 + 1] = words[i] >> 16;
		b[i * 4 + 2] = words[i] >> 8;
		b[i * 4 + 3] = words[i];
	}
	gwl_receive(b, len, from);
	free(b);
}

#define RFU1 0x52465531

// Host with client 1 connected; returns the slot | devid it was given.
static uint32_t rfu_host_with_client(void) {
	setup(GWL_RFU, 0);
	rfu_host_start();
	assert(gwl_connected(1));
	const uint32_t req[] = {RFU1, 0x01, 0};
	deliver(1, 16, req, 3);
	assert(fk.nsent == 1 && be32(fk.sent[0].buf + 4) == 0x02); // CONNECT_ACK
	return be32(fk.sent[0].buf + 8);
}

static void test_rfu_bad_client_data_dropped(void) {
	uint32_t id = rfu_host_with_client();
	int before = dispatched;
	// More bytes than the 16-byte slot holds, even with a packet that long.
	const uint32_t big[] = {RFU1, 0x06, 200u << 24 | id};
	deliver(1, 12 + 200, big, 3);
	// Claims 8 bytes but carries 4.
	const uint32_t cut[] = {RFU1, 0x06, 8u << 24 | id, 0x01020304};
	deliver(1, 12 + 4, cut, 4);
	// Too short for its header.
	deliver(1, 11, cut, 2);
	assert(dispatched == before + 1);			  // only the short one: rfu.c drops it itself
	assert(rfu_cmd(0x26, NULL, 0) == 0x996601A6); // RECV_DATA: nothing queued
	assert(xfer(0x80000000) == 0);

	// A well-formed packet still gets through.
	const uint32_t ok[] = {RFU1, 0x06, 4u << 24 | id, 0x01020304};
	deliver(1, 16, ok, 4);
	assert(rfu_cmd(0x26, NULL, 0) == 0x996602A6);
	assert(xfer(0x80000000) == 0x400);
	assert(xfer(0x80000000) == 0x04030201);
}

static void test_rfu_short_broadcast_dropped(void) {
	setup(GWL_RFU, 0);
	rfu_handshake();
	const uint32_t bc[] = {RFU1, 0x00, 0x1234, 1, 2, 3, 4, 5, 6};
	deliver(2, 12 + 8, bc, 9);
	assert(dispatched == 0);
	assert(rfu_cmd(0x1d, NULL, 0) == 0x9966009D); // BCRD_FETCH: no peers
	deliver(2, 36, bc, 9);
	assert(dispatched == 1);
	assert(rfu_cmd(0x1d, NULL, 0) == 0x9966079D); // one peer: id + 6 words
}

static void test_out_of_range_ids_dropped(void) {
	const uint32_t req[] = {RFU1, 0x01, 0};
	rfu_host_with_client();
	fk.nsent = 0;
	assert(!gwl_connected(32));
	deliver(32, 16, req, 3);
	deliver(0xFFFF, 16, req, 3);
	assert(fk.nsent == 0); // no CONNECT_ACK / NACK
	assert(gwl_connected(31));
	deliver(31, 16, req, 3);
	assert(fk.nsent == 1);

	const uint32_t mpk[] = {0x4d504b31, 0x80000002, 1, 2, 3, 4};
	setup(GWL_POKE, 0);
	assert(!gwl_connected(4));
	deliver(4, 24, mpk, 6);
	deliver(0xFFFF, 24, mpk, 6);
	assert(dispatched == 0);
	deliver(3, 24, mpk, 6);
	assert(dispatched == 1);

	const uint32_t maw[] = {0x4d415731, 0x4fff0002, 0x00010002};
	for (enum GwlMode m = GWL_AW1; m <= GWL_AW2; m++) {
		setup(m, 0);
		assert(!gwl_connected(4));
		deliver(4, 12, maw, 3);
		deliver(0xFFFF, 12, maw, 3);
		assert(dispatched == 0);
		deliver(1, 12, maw, 3);
		assert(dispatched == 1);
	}

	// Our own id indexes the same slots: past them the cable link stays off.
	setup(GWL_POKE, 4);
	assert(gwl_mode() == GWL_NONE);
	setup(GWL_RFU, 4);
	assert(gwl_mode() == GWL_RFU);
}

int main(void) {
	test_rfu_init_handshake();
	test_rfu_broadcast_cadence();
	test_client_caps();
	test_disconnect_frees_slot();
	test_disconnect_tells_the_game();
	test_none_mode_inert();
	test_end_drops_transport();
	test_rfu_bad_client_data_dropped();
	test_rfu_short_broadcast_dropped();
	test_out_of_range_ids_dropped();
	printf("gba_wifi_link tests: OK\n");
	return 0;
}
