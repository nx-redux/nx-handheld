// Differential test: gpSP's own rfu.c / serial_proto.c (pinned + patched, built
// against ref_common.h with every global renamed ref_*) against the library.
// Both sides run the same scripted sessions; their traces (packets sent, I/O
// writes, polls, and every call's result) must match byte for byte.
// Build & run: workspace/all/cores/gba_wifi_link/tests/run_tests.sh
#undef NDEBUG // the checks below must always run
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gwl.h"

// ---- Reference side: gpSP's functions (renamed by -D) and the glue gpSP
// keeps in libretro.c / main.c, transcribed here.

#define REF_SERIAL_MODE_DISABLED 0
#define REF_SERIAL_MODE_RFU 2
#define REF_SERIAL_MODE_SERIAL_POKE 3
#define REF_SERIAL_MODE_SERIAL_AW1 4
#define REF_SERIAL_MODE_SERIAL_AW2 5

void ref_rfu_reset(void);
bool ref_rfu_update(unsigned cycles);
uint32_t ref_rfu_transfer(uint32_t value);
void ref_rfu_net_disconnect(void);
void ref_rfu_frame_update(void);
void ref_rfu_net_receive(const void* buf, size_t len, uint16_t client_id);
void ref_serialproto_reset(void);
void ref_serialpoke_frame_update(void);
void ref_serialpoke_master_send(void);
bool ref_serialpoke_update(unsigned cycles);
void ref_serialpoke_net_receive(const void* buf, size_t len, uint16_t client_id);
void ref_serialaw_master_send(void);
bool ref_serialaw_update(unsigned cycles);
void ref_serialaw_net_receive(const void* buf, size_t len, uint16_t client_id);

int ref_serial_mode;
uint32_t ref_cpu_ticks, ref_netplay_num_clients, ref_netplay_client_id;
static bool ref_transport;

static uint32_t ref_random_state;
uint16_t ref_rand_gen(void) {
	ref_random_state = ((ref_random_state * 1103515245) + 12345) & 0x7fffffff;
	return ref_random_state;
}
void ref_rand_seed(uint32_t data) {
	ref_random_state ^= ref_rand_gen() ^ data;
}

// ---- Traces

typedef struct {
	uint32_t w[1 << 17];
	int n;
} Trace;
static Trace tport, tref;
static uint16_t io_port[0x200], io_ref[0x200];

static void put(Trace* t, uint32_t v) {
	assert(t->n < (int)(sizeof(t->w) / sizeof(t->w[0])));
	t->w[t->n++] = v;
}
static void put_send(Trace* t, uint16_t id, const void* buf, size_t len) {
	put(t, 'S');
	put(t, id);
	put(t, (uint32_t)len);
	for (size_t i = 0; i < len; i++)
		put(t, ((const uint8_t*)buf)[i]);
}

typedef struct {
	uint16_t client_id;
	size_t len;
	uint8_t buf[128];
} Packet;
static Packet inbox_port[8], inbox_ref[8];
static int ninbox_port, ninbox_ref;

static void ref_receive(const void* buf, size_t len, uint16_t client_id);

uint16_t ref_io_read(uint32_t off) {
	return io_ref[off / 2];
}
void ref_io_write(uint32_t off, uint16_t v) {
	io_ref[off / 2] = v;
	put(&tref, 'W');
	put(&tref, off);
	put(&tref, v);
}
void ref_netpacket_send(uint16_t client_id, const void* buf, size_t len) {
	if (ref_transport)
		put_send(&tref, client_id, buf, len);
}
void ref_netpacket_poll_receive(void) {
	if (!ref_transport)
		return;
	put(&tref, 'P');
	for (int i = 0; i < ninbox_ref; i++)
		ref_receive(inbox_ref[i].buf, inbox_ref[i].len, inbox_ref[i].client_id);
	ninbox_ref = 0;
}

// libretro.c netpacket_start / stop / connected / disconnected / receive.
static void ref_begin(int serial_mode, uint16_t client_id) {
	ref_serial_mode = serial_mode;
	ref_transport = true;
	ref_netplay_num_clients = 0;
	ref_netplay_client_id = client_id;
}
static void ref_end(void) {
	ref_transport = false;
}
static bool ref_connected(void) {
	const uint8_t maxpl[] = {0, 0, 32, 4, 4, 4, 0};
	const uint32_t max_clients = maxpl[ref_serial_mode] - 1U;
	if (ref_netplay_num_clients >= max_clients)
		return false;
	ref_netplay_num_clients++;
	return true;
}
static void ref_disconnected(void) {
	ref_rfu_net_disconnect();
	ref_netplay_num_clients--;
}
static void ref_receive(const void* buf, size_t len, uint16_t client_id) {
	switch (ref_serial_mode) {
	case REF_SERIAL_MODE_RFU:
		ref_rfu_net_receive(buf, len, client_id);
		break;
	case REF_SERIAL_MODE_SERIAL_POKE:
		ref_serialpoke_net_receive(buf, len, client_id);
		break;
	case REF_SERIAL_MODE_SERIAL_AW1:
	case REF_SERIAL_MODE_SERIAL_AW2:
		ref_serialaw_net_receive(buf, len, client_id);
		break;
	}
}
// retro_run
static void ref_frame_update(void) {
	if (ref_serial_mode == REF_SERIAL_MODE_RFU)
		ref_rfu_frame_update();
	else if (ref_serial_mode == REF_SERIAL_MODE_SERIAL_POKE)
		ref_serialpoke_frame_update();
}
// serial.c update_serial / write_siocnt call sites
static bool ref_update(unsigned cycles) {
	switch (ref_serial_mode) {
	case REF_SERIAL_MODE_RFU:
		return ref_rfu_update(cycles);
	case REF_SERIAL_MODE_SERIAL_POKE:
		return ref_serialpoke_update(cycles);
	case REF_SERIAL_MODE_SERIAL_AW1:
	case REF_SERIAL_MODE_SERIAL_AW2:
		return ref_serialaw_update(cycles);
	}
	return false;
}
static void ref_master_send(void) {
	if (ref_serial_mode == REF_SERIAL_MODE_SERIAL_POKE)
		ref_serialpoke_master_send();
	else if (ref_serial_mode == REF_SERIAL_MODE_SERIAL_AW1 || ref_serial_mode == REF_SERIAL_MODE_SERIAL_AW2)
		ref_serialaw_master_send();
}

// ---- Port side: the library behind a fake emulator.

static uint32_t now;	 // shared emulated cycle counter
static uint32_t learned; // RFU: slot | devid the host side last handed out
static int port_sends;

static uint16_t p_read(void* c, uint32_t off) {
	(void)c;
	return io_port[off / 2];
}
static void p_write(void* c, uint32_t off, uint16_t v) {
	(void)c;
	io_port[off / 2] = v;
	put(&tport, 'W');
	put(&tport, off);
	put(&tport, v);
}
static uint32_t p_cycles(void* c) {
	(void)c;
	return now;
}
static uint32_t be32(const uint8_t* p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void p_send(void* c, uint16_t id, const void* buf, size_t len) {
	(void)c;
	const uint8_t* b = buf;
	if (len >= 12 && be32(b) == 0x52465531 && be32(b + 4) == 0x02) // RFU1 CONNECT_ACK
		learned = be32(b + 8);
	port_sends++;
	put_send(&tport, id, buf, len);
}
static void p_poll(void* c) {
	(void)c;
	put(&tport, 'P');
	for (int i = 0; i < ninbox_port; i++)
		gwl_receive(inbox_port[i].buf, inbox_port[i].len, inbox_port[i].client_id);
	ninbox_port = 0;
}
static const GwlHost host = {NULL, p_read, p_write, p_cycles, p_send, p_poll};

// ---- Scripts

enum Op {
	BEGIN,	   // arg: client id (netpacket start, the script's mode)
	END,	   // netpacket stop
	RESET_RFU, // RCNT GPIO reset
	RESET_PROTO,
	XFER,	   // arg: word the game sends (Normal-32, internal clock)
	SETIO,	   // arg: offset << 16 | value, written to both fake memories
	MSEND,	   // Multi start as parent
	FRAME,	   // arg: frame count
	UPDATE,	   // arg: cycles
	RECV,	   // arg: packet index, delivered now
	QUEUE,	   // arg: packet index, delivered on the next poll
	CONNECTED, // arg: client id
	DISC,	   // arg: client id
	STOP,
};
typedef struct {
	enum Op op;
	uint32_t arg;
} Step;

// Wire packets, as big-endian words. learn: word index that gets the learned
// RFU slot | devid ORed in (0 = none).
typedef struct {
	uint16_t from;
	size_t len;
	uint32_t w[12];
	int learn;
} Pkt;

typedef struct {
	const char* name;
	enum GwlMode mode;
	const Step* steps;
	const Pkt* pkts;
	int min_sends, min_irqs;
} Script;

#define SIOCNT 0x128
#define SIOMLT 0x12A
#define IO(off, v) {SETIO, ((uint32_t)(off) << 16) | (v)}
#define CMD(c, n) {XFER, 0x99660000u | ((n) << 8) | (c)}
#define ACK {XFER, 0x80000000}
#define W(v) {XFER, (v)}
#define HANDSHAKE {RESET_RFU, 0}, W(0x0000494E), W(0xB6B1494E), W(0xB0BB8001)

#define RFU1 0x52465531
static const Pkt rfu_pkts[] = {
	/* 0 */ {1, 16, {RFU1, 0x01, 0}, 0},								 // CONNECT_REQ from 1
	/* 1 */ {1, 16, {RFU1, 0x06, 4u << 24, 0x01020304}, 2},				 // CLIENT_SEND 4 bytes
	/* 2 */ {1, 16, {RFU1, 0x07, 0}, 2},								 // CLIENT_ACK
	/* 3 */ {1, 20, {RFU1, 0x06, 8u << 24, 0x0A0B0C0D, 0x0E0F1011}, 2},	 // CLIENT_SEND 8 bytes
	/* 4 */ {2, 36, {RFU1, 0x00, 0x1234, 1, 2, 3, 4, 5, 0x60000006}, 0}, // BROADCAST from 2
	/* 5 */ {2, 16, {RFU1, 0x02, 0x00015678}, 0},						 // CONNECT_ACK slot 1
	/* 6 */ {2, 20, {RFU1, 0x05, 8, 0xCAFEBABE, 0xDEADBEEF}, 0},		 // HOST_SEND 8 bytes
	/* 7 */ {3, 16, {RFU1, 0x01, 0}, 0},								 // CONNECT_REQ, not hosting
	/* 8 */ {2, 16, {RFU1, 0x04, 0x00015678}, 0},						 // DISCONNECT from host
	/* 9 */ {1, 8, {RFU1, 0x05}, 0},									 // malformed (short)
	/* 10 */ {20, 36, {RFU1, 0x00, 0x4321, 7, 8, 9, 10, 11, 12}, 0},	 // BROADCAST from 20
};
static const Step rfu_steps[] = {
	{BEGIN, 0},
	HANDSHAKE,
	CMD(0x10, 0),
	ACK, // INIT1
	CMD(0x3d, 0),
	ACK, // INIT2
	CMD(0x17, 1),
	W(0x0420),
	ACK, // SYSCFG: timeout 0x20 frames, 4 retransmits
	CMD(0x12, 0),
	ACK,
	ACK, // SYSVER
	CMD(0x16, 6),
	W(0x11),
	W(0x22),
	W(0x33),
	W(0x44),
	W(0x55),
	W(0x66),
	ACK, // BCST_DATA
	CMD(0x19, 0),
	ACK, // HOST_START
	{FRAME, 35},
	{CONNECTED, 1},
	{CONNECTED, 2},
	{CONNECTED, 3},
	{CONNECTED, 4},
	{CONNECTED, 5},
	{CONNECTED, 6},
	{CONNECTED, 7},
	{CONNECTED, 8},
	{CONNECTED, 9},
	{CONNECTED, 10},
	{CONNECTED, 11},
	{CONNECTED, 12},
	{CONNECTED, 13},
	{CONNECTED, 14},
	{CONNECTED, 15},
	{CONNECTED, 16},
	{CONNECTED, 17},
	{CONNECTED, 18},
	{CONNECTED, 19},
	{CONNECTED, 20},
	{CONNECTED, 21},
	{CONNECTED, 22},
	{CONNECTED, 23},
	{CONNECTED, 24},
	{CONNECTED, 25},
	{CONNECTED, 26},
	{CONNECTED, 27},
	{CONNECTED, 28},
	{CONNECTED, 29},
	{CONNECTED, 30},
	{CONNECTED, 31},
	{CONNECTED, 32},
	{RECV, 0},
	CMD(0x1a, 0),
	ACK,
	ACK, // HOST_ACCEPT: one client
	CMD(0x13, 0),
	ACK,
	ACK, // SYSSTAT
	CMD(0x14, 0),
	ACK,
	ACK,
	ACK, // SLOTSTAT
	CMD(0x11, 0),
	ACK,
	ACK, // LINKPWR
	CMD(0x24, 2),
	W(5),
	W(0xAABBCCDD),
	ACK, // SEND_DATA 5 bytes to clients
	{RECV, 1},
	{RECV, 2},
	{RECV, 9},
	CMD(0x26, 0),
	ACK,
	ACK,
	ACK, // RECV_DATA
	// SEND_DATAW, then wait for the client's data as RFU master.
	CMD(0x25, 2),
	W(4),
	W(0x01234567),
	ACK,
	IO(SIOCNT, 0x5081),
	{UPDATE, 1000},
	{QUEUE, 3},
	IO(SIOCNT, 0x5080),
	{UPDATE, 1000},
	IO(SIOCNT, 0x5080),
	{UPDATE, 1000},
	CMD(0x26, 0),
	ACK,
	ACK,
	ACK,
	ACK,
	// WAIT with no client data: the retransmit window runs out.
	CMD(0x27, 0),
	ACK,
	IO(SIOCNT, 0x5080),
	{UPDATE, 100000},
	{UPDATE, 100000},
	IO(SIOCNT, 0x5080),
	{UPDATE, 10},
	IO(SIOCNT, 0x5080),
	{UPDATE, 10},
	{FRAME, 10},
	{DISC, 1},
	// Whatever comes next is answered as a WAIT that reports the disconnect.
	CMD(0x13, 0),
	ACK,
	IO(SIOCNT, 0x5080),
	{UPDATE, 50},
	IO(SIOCNT, 0x5080),
	{UPDATE, 50},
	IO(SIOCNT, 0x5080),
	{UPDATE, 50},
	CMD(0x1b, 0),
	ACK,
	ACK, // HOST_STOP while idle: error
	// Host again: drop the client from the game side, then stop.
	CMD(0x19, 0),
	ACK,
	{RECV, 0},
	CMD(0x1b, 0),
	ACK, // HOST_STOP with a client: stays hosting
	CMD(0x30, 1),
	W(1),
	ACK, // DISCONNECT slot 0
	CMD(0x1b, 0),
	ACK, // HOST_STOP, no clients: idle
	CMD(0x24, 2),
	W(4),
	W(0),
	ACK,
	ACK, // SEND_DATA while idle: error
	// Client side: find the host, connect, trade data, leave.
	HANDSHAKE,
	{RECV, 4},
	{RECV, 10},
	{FRAME, 3},
	CMD(0x1c, 0),
	ACK, // BCRD_START
	CMD(0x1d, 0),
	ACK,
	ACK,
	ACK,
	ACK,
	ACK,
	ACK,
	ACK,
	ACK, // BCRD_FETCH: two peers
	ACK,
	ACK,
	ACK,
	ACK,
	ACK,
	ACK,
	ACK,
	CMD(0x1f, 1),
	W(0x1234),
	ACK, // CONNECT
	CMD(0x20, 0),
	ACK,
	ACK, // ISCONNECTED: in progress
	{RECV, 7},
	{RECV, 5},
	CMD(0x20, 0),
	ACK,
	ACK, // ISCONNECTED: slot and id
	CMD(0x21, 0),
	ACK,
	ACK, // CONCOMPL
	CMD(0x19, 0),
	ACK,
	ACK, // HOST_START as a client: error
	{RECV, 6},
	CMD(0x26, 0),
	ACK,
	ACK,
	ACK,
	ACK, // RECV_DATA
	CMD(0x24, 2),
	W(4u << 13),
	W(0x76543210),
	ACK, // SEND_DATA to the host
	CMD(0x27, 0),
	ACK,
	{QUEUE, 6},
	IO(SIOCNT, 0x5080),
	{UPDATE, 500},
	IO(SIOCNT, 0x5080),
	{UPDATE, 500},
	{RECV, 8},
	CMD(0x13, 0),
	ACK,
	ACK, // SYSSTAT after the host left
	CMD(0x1f, 1),
	W(0x1234),
	ACK,
	{RECV, 5},
	// The frontend loses the host: the game must hear about it.
	{DISC, 2},
	CMD(0x26, 0),
	ACK,
	IO(SIOCNT, 0x5080),
	{UPDATE, 50},
	IO(SIOCNT, 0x5080),
	{UPDATE, 50},
	IO(SIOCNT, 0x5080),
	{UPDATE, 50},
	CMD(0x1f, 1),
	W(0x1234),
	ACK,
	{RECV, 5},
	// A WAIT nothing answers runs into the adapter timeout.
	CMD(0x27, 0),
	ACK,
	IO(SIOCNT, 0x5080),
	{UPDATE, 9000000},
	IO(SIOCNT, 0x5080),
	{UPDATE, 10},
	CMD(0x30, 1),
	W(0),
	ACK, // DISCONNECT as a client
	{FRAME, 300},
	{END, 0},
	{FRAME, 40},
	{STOP, 0},
};

#define MPK1 0x4d504b31
static const Pkt poke_pkts[] = {
	/* 0 */ {1, 24, {MPK1, 1}, 0}, // slave in handshake
	/* 1 */ {1, 24, {MPK1, 0x80000002, 0x00010002, 0x00030004, 0x00050006, 0x00070008}, 0},
	/* 2 */ {1, 24, {MPK1, 0x80000002, 0x11112222, 0x33334444, 0x55556666, 0x77778888}, 0},
	/* 3 */ {0, 24, {MPK1, 1}, 0}, // master in handshake
	/* 4 */ {0, 24, {MPK1, 2}, 0}, // master connected
	/* 5 */ {0, 24, {MPK1, 0x80000002, 0xA000A001, 0xA002A003, 0xA004A005, 0xA006A007}, 0},
	/* 6 */ {2, 23, {MPK1, 1}, 0}, // wrong length
};
#define MS(v) IO(SIOMLT, v), {MSEND, 0}
#define SU(v, c)     \
	IO(SIOMLT, v), { \
		UPDATE, c    \
	}
static const Step poke_steps[] = {
	{BEGIN, 0},
	{CONNECTED, 1},
	{CONNECTED, 2},
	{CONNECTED, 3},
	{CONNECTED, 4},
	{RESET_PROTO, 0},
	MS(0xB9A0),
	{RECV, 0},
	{RECV, 6},
	MS(0xB9A0),
	MS(0x8FFF),
	{RECV, 1},
	{RECV, 2},
	MS(0x1234),
	MS(1),
	MS(2),
	MS(3),
	MS(4),
	MS(5),
	MS(6),
	MS(7),
	MS(8),
	MS(0x5678),
	MS(9),
	MS(10),
	MS(11),
	MS(12),
	MS(13),
	MS(14),
	MS(15),
	MS(16),
	MS(0),
	MS(0),
	MS(0),
	MS(0),
	MS(0),
	MS(0),
	MS(0),
	MS(0),
	MS(0),
	{FRAME, 3},
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	MS(0xB9A0),
	{FRAME, 250},
	{UPDATE, 300000},
	// Slave: the library pretends to be the master's clock.
	{RESET_PROTO, 0},
	{BEGIN, 1},
	SU(0xB9A0, 300000),
	{RECV, 3},
	SU(0xB9A0, 300000),
	SU(0xB9A0, 300000),
	{RECV, 4},
	SU(0xB9A0, 30000),
	{RECV, 5},
	SU(0x0101, 30000),
	SU(0x0202, 30000),
	SU(0x0303, 30000),
	SU(0x0404, 30000),
	SU(0x0505, 30000),
	SU(0x0606, 30000),
	SU(0x0707, 30000),
	SU(0x0808, 30000),
	SU(0x0909, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0xB9A0, 30000),
	SU(0x4444, 1000),
	{FRAME, 5},
	{STOP, 0},
};

#define MAW1 0x4d415731
#define AW_PKTS(SYNC)                                                          \
	{                                                                          \
		/* 0 */ {1, 16, {MAW1, 0x4fff0004, 0x00030011, 0x00220033}, 0},        \
		/* 1 */ {1, 8, {MAW1, 0x00000100}, 0},				/* peer in SYNC */ \
		/* 2 */ {1, 12, {MAW1, 0x80000202, 0x80038000}, 0}, /* intersync */    \
		/* 3 */ {0, 14, {MAW1, 0x4fff0003, 0x00020044, 0x00550000}, 0},        \
		/* 4 */ {0, 8, {MAW1, 0x00000100}, 0},                                 \
		/* 5 */ {0, 12, {MAW1, 0x80000202, 0x80078000}, 0},                    \
		/* 6 */ {2, 9, {MAW1, 0x4fff0004}, 0}, /* wrong length */              \
		/* 7 */ {3, 8, {MAW1, (uint32_t)(SYNC) << 16}, 0},                     \
	}
static const Pkt aw1_pkts[] = AW_PKTS(0x5678);
static const Pkt aw2_pkts[] = AW_PKTS(0x9ABC);

// EXTRA: one more body word, for AW2's two-word packet tail.
#define AW_STEPS(SYNC, EXTRA_M, EXTRA_S) \
	{                                    \
		{BEGIN, 0},                      \
		{CONNECTED, 1},                  \
		{RESET_PROTO, 0},                \
		MS(0x1234),                      \
		MS(0x4F00),                      \
		MS(3),                           \
		MS(0xA1),                        \
		MS(0xA2),                        \
		MS(0xA3),                        \
		MS(0xA4),                        \
		EXTRA_M{RECV, 0},                \
		{RECV, 6},                       \
		{RECV, 7},                       \
		MS(0x7FFF),                      \
		MS(0x7FFF),                      \
		MS(0x7FFF),                      \
		MS(0x7FFF),                      \
		MS(0x7FFF),                      \
		MS(0x7FFF),                      \
		MS(0x7FFF),                      \
		MS(SYNC),                        \
		{RECV, 1},                       \
		MS(SYNC),                        \
		MS(0x5FFF),                      \
		MS(0x8001),                      \
		MS(0x8005),                      \
		MS(0x8400),                      \
		{RECV, 2},                       \
		MS(0x8002),                      \
		MS(0x5FFF),                      \
		MS(0x8000),                      \
		MS(0x1111),                      \
		MS(SYNC),                        \
		MS(0x7FFF),                      \
		{BEGIN, 1},                      \
		{RESET_PROTO, 0},                \
		SU(0x2222, 140000),              \
		SU(0x4F00, 140000),              \
		SU(2, 140000),                   \
		SU(0xB1, 140000),                \
		SU(0xB2, 140000),                \
		SU(0xB3, 140000),                \
		EXTRA_S{RECV, 3},                \
		{RECV, 7},                       \
		SU(0x7FFF, 140000),              \
		SU(0x7FFF, 140000),              \
		SU(0x7FFF, 140000),              \
		SU(0x7FFF, 140000),              \
		SU(0x7FFF, 140000),              \
		SU(0x7FFF, 140000),              \
		SU(0x7FFF, 1000),                \
		SU(SYNC, 140000),                \
		{RECV, 4},                       \
		SU(SYNC, 290000),                \
		SU(0x5FFF, 290000),              \
		SU(0x8001, 290000),              \
		SU(0x8009, 290000),              \
		SU(0x8000, 290000),              \
		{RECV, 5},                       \
		SU(0x8003, 290000),              \
		SU(0x5FFF, 290000),              \
		SU(0x8400, 290000),              \
		SU(0x1234, 290000),              \
		SU(0x7FFF, 140000),              \
		{FRAME, 2},                      \
		{STOP, 0},                       \
	}
static const Step aw1_steps[] = AW_STEPS(0x5678, , );
#define AW2_EXTRA_M MS(0xA5),
#define AW2_EXTRA_S SU(0xB4, 140000),
static const Step aw2_steps[] = AW_STEPS(0x9ABC, AW2_EXTRA_M, AW2_EXTRA_S);

static const Script scripts[] = {
	{"RFU host+join+data+disconnect", GWL_RFU, rfu_steps, rfu_pkts, 12, 12},
	{"POKE master/slave", GWL_POKE, poke_steps, poke_pkts, 15, 30},
	{"AW1 exchange", GWL_AW1, aw1_steps, aw1_pkts, 24, 20},
	{"AW2 exchange", GWL_AW2, aw2_steps, aw2_pkts, 24, 20},
};

static void build(const Pkt* p, Packet* out) {
	out->client_id = p->from;
	out->len = p->len;
	memset(out->buf, 0, sizeof(out->buf));
	for (int i = 0; i < 12; i++) {
		uint32_t v = p->w[i] | (p->learn && p->learn == i ? learned : 0);
		out->buf[i * 4] = v >> 24;
		out->buf[i * 4 + 1] = v >> 16;
		out->buf[i * 4 + 2] = v >> 8;
		out->buf[i * 4 + 3] = v;
	}
}

static int ref_mode(enum GwlMode m) {
	static const int map[] = {REF_SERIAL_MODE_DISABLED, REF_SERIAL_MODE_RFU, REF_SERIAL_MODE_SERIAL_POKE,
							  REF_SERIAL_MODE_SERIAL_AW1, REF_SERIAL_MODE_SERIAL_AW2};
	return map[m];
}

static uint32_t run_port(const Script* s, const Step* st) {
	Packet pk;
	switch (st->op) {
	case BEGIN:
		gwl_begin(&host, s->mode, st->arg);
		return 0;
	case END:
		gwl_end();
		return 0;
	case RESET_RFU:
		gwl_reset_rfu();
		return 0;
	case RESET_PROTO:
		gwl_reset_proto();
		return 0;
	case XFER:
		return gwl_rfu_transfer(st->arg);
	case SETIO:
		io_port[(st->arg >> 16) / 2] = st->arg & 0xFFFF;
		return 0;
	case MSEND:
		gwl_multi_master_send();
		return 0;
	case FRAME:
		for (uint32_t i = 0; i < st->arg; i++)
			gwl_frame_update();
		return 0;
	case UPDATE:
		return gwl_update(st->arg);
	case RECV:
		build(&s->pkts[st->arg], &pk);
		gwl_receive(pk.buf, pk.len, pk.client_id);
		return 0;
	case QUEUE:
		build(&s->pkts[st->arg], &inbox_port[ninbox_port++]);
		return 0;
	case CONNECTED:
		return gwl_connected(st->arg);
	case DISC:
		gwl_disconnected(st->arg);
		return 0;
	case STOP:
		break;
	}
	return 0;
}

static uint32_t run_ref(const Script* s, const Step* st) {
	Packet pk;
	switch (st->op) {
	case BEGIN:
		ref_begin(ref_mode(s->mode), st->arg);
		return 0;
	case END:
		ref_end();
		return 0;
	case RESET_RFU:
		ref_rfu_reset();
		return 0;
	case RESET_PROTO:
		ref_serialproto_reset();
		return 0;
	case XFER:
		return ref_rfu_transfer(st->arg);
	case SETIO:
		io_ref[(st->arg >> 16) / 2] = st->arg & 0xFFFF;
		return 0;
	case MSEND:
		ref_master_send();
		return 0;
	case FRAME:
		for (uint32_t i = 0; i < st->arg; i++)
			ref_frame_update();
		return 0;
	case UPDATE:
		return ref_update(st->arg);
	case RECV:
		build(&s->pkts[st->arg], &pk);
		ref_receive(pk.buf, pk.len, pk.client_id);
		return 0;
	case QUEUE:
		build(&s->pkts[st->arg], &inbox_ref[ninbox_ref++]);
		return 0;
	case CONNECTED:
		return ref_connected();
	case DISC:
		ref_disconnected();
		return 0;
	case STOP:
		break;
	}
	return 0;
}

static void run_script(const Script* s) {
	memset(&tport, 0, sizeof(tport));
	memset(&tref, 0, sizeof(tref));
	memset(io_port, 0, sizeof(io_port));
	memset(io_ref, 0, sizeof(io_ref));
	ninbox_port = ninbox_ref = 0;
	now = ref_cpu_ticks = 777;
	learned = 0;
	int irqs = 0;
	port_sends = 0;
	for (int i = 0; s->steps[i].op != STOP; i++) {
		const Step* st = &s->steps[i];
		put(&tport, 'T');
		put(&tport, i);
		put(&tref, 'T');
		put(&tref, i);
		uint32_t rp = run_port(s, st);
		uint32_t rr = run_ref(s, st);
		put(&tport, 'R');
		put(&tport, rp);
		put(&tref, 'R');
		put(&tref, rr);
		if (st->op == UPDATE)
			irqs += rp != 0;
		if (st->op == UPDATE)
			now = ref_cpu_ticks = now + st->arg;
		else
			now = ref_cpu_ticks = now + 64;
		if (tport.n != tref.n || memcmp(tport.w, tref.w, tport.n * sizeof(uint32_t))) {
			fprintf(stderr, "%s: step %d (op %d arg 0x%x) diverges: port %u, gpSP %u\n", s->name, i, st->op,
					st->arg, rp, rr);
			exit(1);
		}
	}
	if (memcmp(io_port, io_ref, sizeof(io_port))) {
		fprintf(stderr, "%s: I/O registers differ at the end\n", s->name);
		exit(1);
	}
	// A script that stops exercising the protocol would pass vacuously.
	if (port_sends < s->min_sends || irqs < s->min_irqs) {
		fprintf(stderr, "%s: only %d sends / %d IRQs (want %d / %d)\n", s->name, port_sends, irqs, s->min_sends,
				s->min_irqs);
		exit(1);
	}
	printf("  %s: %d trace words, %d sends, %d IRQs, identical\n", s->name, tport.n, port_sends, irqs);
}

int main(void) {
	for (size_t i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++)
		run_script(&scripts[i]);
	printf("gba_wifi_link differential tests: OK\n");
	return 0;
}
