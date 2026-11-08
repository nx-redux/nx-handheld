// gwl.c - the netpacket glue gpSP keeps in libretro/libretro.c and main.c,
// rebuilt on GwlHost so rfu.c and serial_proto.c run unchanged.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gwl_shim.h"

static GwlHost host;

int serial_mode = SERIAL_MODE_DISABLED;
u32 netplay_num_clients = 0, netplay_client_id = 0;

uint16_t gwl_io_read(uint32_t off) {
	return host.read_io(host.ctx, off);
}

void gwl_io_write(uint32_t off, uint16_t v) {
	host.write_io(host.ctx, off, v);
}

uint32_t gwl_cycles(void) {
	return host.cycles(host.ctx);
}

// gpSP main.c: the device IDs the adapter hands out come from this generator,
// so it must match gpSP's exactly for sessions to replay the same.
static u32 random_state = 0;

u16 rand_gen(void) {
	random_state = ((random_state * 1103515245) + 12345) & 0x7fffffff;
	return random_state;
}

void rand_seed(u32 data) {
	random_state ^= rand_gen() ^ data;
}

void netpacket_send(uint16_t client_id, const void* buf, size_t len) {
	if (host.send)
		host.send(host.ctx, client_id, buf, len);
}

void netpacket_poll_receive(void) {
	if (host.poll)
		host.poll(host.ctx);
}

static int mode_to_serial(enum GwlMode mode) {
	switch (mode) {
	case GWL_RFU:
		return SERIAL_MODE_RFU;
	case GWL_POKE:
		return SERIAL_MODE_SERIAL_POKE;
	case GWL_AW1:
		return SERIAL_MODE_SERIAL_AW1;
	case GWL_AW2:
		return SERIAL_MODE_SERIAL_AW2;
	default:
		return SERIAL_MODE_DISABLED;
	}
}

void gwl_begin(const GwlHost* h, enum GwlMode mode, uint16_t client_id) {
	host = *h;
	serial_mode = mode_to_serial(mode);
	netplay_num_clients = 0;
	netplay_client_id = client_id;
}

// Like gpSP's netpacket_stop: the link goes quiet but the emulated device
// keeps its registers, so only the transport is dropped.
void gwl_end(void) {
	host.send = NULL;
	host.poll = NULL;
}

bool gwl_connected(uint16_t client_id) {
	(void)client_id;
	static const uint8_t maxpl[] = {
		0,						// SERIAL_MODE_DISABLED
		0,						// SERIAL_MODE_GBP
		MAX_RFU_NETPLAYERS,		// SERIAL_MODE_RFU
		MAX_SERMULT_NETPLAYERS, // SERIAL_MODE_SERIAL_POKE
		MAX_SERMULT_NETPLAYERS, // SERIAL_MODE_SERIAL_AW1
		MAX_SERMULT_NETPLAYERS, // SERIAL_MODE_SERIAL_AW2
		0,						// SERIAL_MODE_AUTO
	};
	const u32 max_clients = maxpl[serial_mode] - 1U;

	if (netplay_num_clients >= max_clients)
		return false;

	netplay_num_clients++;
	return true;
}

void gwl_disconnected(uint16_t client_id) {
	(void)client_id;
	// Without this a client whose host drops stays "connected" and the game
	// never sees the disconnect; gpSP's netpacket_disconnected does the same.
	rfu_net_disconnect();
	netplay_num_clients--;
}

void gwl_receive(const void* buf, size_t len, uint16_t client_id) {
	switch (serial_mode) {
	case SERIAL_MODE_RFU:
		rfu_net_receive(buf, len, client_id);
		break;
	case SERIAL_MODE_SERIAL_POKE:
		serialpoke_net_receive(buf, len, client_id);
		break;
	case SERIAL_MODE_SERIAL_AW1:
	case SERIAL_MODE_SERIAL_AW2:
		serialaw_net_receive(buf, len, client_id);
		break;
	}
}

void gwl_reset_rfu(void) {
	rfu_reset();
}

void gwl_reset_proto(void) {
	serialproto_reset();
}

uint32_t gwl_rfu_transfer(uint32_t sent) {
	return rfu_transfer(sent);
}

void gwl_multi_master_send(void) {
	if (serial_mode == SERIAL_MODE_SERIAL_POKE)
		serialpoke_master_send();
	else if (serial_mode == SERIAL_MODE_SERIAL_AW1 || serial_mode == SERIAL_MODE_SERIAL_AW2)
		serialaw_master_send();
}

// gpSP's retro_run ticks only these two; Advance Wars has no per-frame work.
void gwl_frame_update(void) {
	switch (serial_mode) {
	case SERIAL_MODE_RFU:
		rfu_frame_update();
		break;
	case SERIAL_MODE_SERIAL_POKE:
		serialpoke_frame_update();
		break;
	}
}

bool gwl_update(unsigned cycles) {
	switch (serial_mode) {
	case SERIAL_MODE_RFU:
		return rfu_update(cycles);
	case SERIAL_MODE_SERIAL_POKE:
		return serialpoke_update(cycles);
	case SERIAL_MODE_SERIAL_AW1:
	case SERIAL_MODE_SERIAL_AW2:
		return serialaw_update(cycles);
	}
	return false;
}

enum GwlMode gwl_mode(void) {
	switch (serial_mode) {
	case SERIAL_MODE_RFU:
		return GWL_RFU;
	case SERIAL_MODE_SERIAL_POKE:
		return GWL_POKE;
	case SERIAL_MODE_SERIAL_AW1:
		return GWL_AW1;
	case SERIAL_MODE_SERIAL_AW2:
		return GWL_AW2;
	}
	return GWL_NONE;
}
