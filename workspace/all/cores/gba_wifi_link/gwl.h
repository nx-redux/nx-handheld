// gwl.h - gpSP's GBA Wireless Adapter (RFU) and link-cable protocol emulation
// (Pokemon, Advance Wars) for any emulator, wire-compatible with gpSP netplay.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// rfu.c and serial_proto.c are gpSP's files unchanged (tests/check_upstream.sh
// enforces it); gwl_shim.h gives them the gpSP environment they expect, built
// on GwlHost. gwl.c replaces gpSP's libretro netpacket glue. The emulator's SIO
// driver owns the register writes and transfer timing (gpSP's serial.c) and
// calls in here at the same points serial.c calls rfu_* / serialpoke_* /
// serialaw_*.
//
// One link per process: gpSP keeps its state in globals and so does this.
#ifndef GBA_WIFI_LINK_H
#define GBA_WIFI_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum GwlMode { GWL_NONE = 0,
			   GWL_RFU,
			   GWL_POKE,
			   GWL_AW1,
			   GWL_AW2 };

typedef struct {
	void* ctx;
	// off is from 0x04000000: SIOMULTI0-3 / SIODATA32 0x120-0x126, SIOCNT 0x128,
	// SIOMLT_SEND 0x12A, RCNT 0x134. Writes must land in the emulator's
	// registers as-is (no write side effects), like gpSP's write_ioreg.
	uint16_t (*read_io)(void* ctx, uint32_t off);
	void (*write_io)(void* ctx, uint32_t off, uint16_t v);
	uint32_t (*cycles)(void* ctx);											  // emulated cycle counter (gpSP cpu_ticks)
	void (*send)(void* ctx, uint16_t client_id, const void* buf, size_t len); // 0xFFFF: broadcast
	void (*poll)(void* ctx);												  // deliver pending packets now (may call gwl_receive)
} GwlHost;

void gwl_begin(const GwlHost* host, enum GwlMode mode, uint16_t client_id); // netpacket start
void gwl_end(void);															// netpacket stop
bool gwl_connected(uint16_t client_id);										// false: refuse the peer (too many for the mode)
void gwl_disconnected(uint16_t client_id);
void gwl_receive(const void* buf, size_t len, uint16_t client_id);
void gwl_reset_rfu(void);				  // RCNT GPIO pulls PD high
void gwl_reset_proto(void);				  // entering or leaving Multi mode
uint32_t gwl_rfu_transfer(uint32_t sent); // one Normal-32 word, internal clock
void gwl_multi_master_send(void);		  // Multi start as parent (Pokemon / Advance Wars)
void gwl_frame_update(void);			  // once per emulated frame
bool gwl_update(unsigned cycles);		  // true: raise the SIO IRQ
enum GwlMode gwl_mode(void);

#endif
