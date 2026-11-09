// gwl_shim.h - stands in for gpSP's common.h in rfu.c and serial_proto.c, so
// those files compile unchanged against GwlHost instead of gpSP's core.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Values below are gpSP's own (gpsp_config.h, serial.h, gba_memory.h, main.c,
// libretro.h) at the pinned commit; they are part of the wire protocol.
#ifndef GWL_SHIM_H
#define GWL_SHIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gwl.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define netorder32(value) (value)
#else
#define netorder32(value) __builtin_bswap32(value)
#endif

// gpsp_config.h
#define MAX_RFU_NETPLAYERS 32
#define MAX_SERMULT_NETPLAYERS 4

// libretro.h
#define RETRO_NETPACKET_BROADCAST 0xFFFF

// serial.h
#define SERIAL_MODE_DISABLED 0
#define SERIAL_MODE_GBP 1
#define SERIAL_MODE_RFU 2
#define SERIAL_MODE_SERIAL_POKE 3
#define SERIAL_MODE_SERIAL_AW1 4
#define SERIAL_MODE_SERIAL_AW2 5
#define SERIAL_MODE_AUTO 6

// gba_memory.h: halfword indices into the I/O block, as gpSP's io_registers[]
// uses them. Kept as indices (not byte offsets) because the gpSP code steps
// through them, e.g. REG_SIOMULTI0 + i; the accessors turn them into offsets.
#define REG_SIODATA32_L 0x90
#define REG_SIODATA32_H 0x91
#define REG_SIOMULTI0 0x90
#define REG_SIOMULTI1 0x91
#define REG_SIOMULTI2 0x92
#define REG_SIOMULTI3 0x93
#define REG_SIOCNT 0x94
#define REG_SIOMLT_SEND 0x95
#define REG_RCNT 0x9A

// gpSP's global names, prefixed so the library links next to any core
// (including gpSP itself) without clashes.
#define serial_mode gwl_gp_serial_mode
#define netplay_client_id gwl_gp_netplay_client_id
#define netplay_num_clients gwl_gp_netplay_num_clients
#define netpacket_send gwl_gp_netpacket_send
#define netpacket_poll_receive gwl_gp_netpacket_poll_receive
#define rand_gen gwl_gp_rand_gen
#define rand_seed gwl_gp_rand_seed
#define ismemzero gwl_gp_ismemzero
#define rfu_reset gwl_gp_rfu_reset
#define rfu_net_disconnect gwl_gp_rfu_net_disconnect
#define rfu_update gwl_gp_rfu_update
#define rfu_transfer gwl_gp_rfu_transfer
#define rfu_frame_update gwl_gp_rfu_frame_update
#define rfu_net_receive gwl_gp_rfu_net_receive
#define serialproto_reset gwl_gp_serialproto_reset
#define serialpoke_frame_update gwl_gp_serialpoke_frame_update
#define serialpoke_master_send gwl_gp_serialpoke_master_send
#define serialpoke_update gwl_gp_serialpoke_update
#define serialpoke_net_receive gwl_gp_serialpoke_net_receive
#define serialaw_master_send gwl_gp_serialaw_master_send
#define serialaw_update gwl_gp_serialaw_update
#define serialaw_net_receive gwl_gp_serialaw_net_receive

uint16_t gwl_io_read(uint32_t off);
void gwl_io_write(uint32_t off, uint16_t v);
uint32_t gwl_cycles(void);

#define read_ioreg(regnum) gwl_io_read((uint32_t)(regnum) * 2)
#define write_ioreg(regnum, val) gwl_io_write((uint32_t)(regnum) * 2, (uint16_t)(val))
#define read_ioreg32(regnum) ((u32)read_ioreg(regnum) | ((u32)read_ioreg((regnum) + 1) << 16))
#define cpu_ticks (gwl_cycles())

// main.h / libretro.c
extern int serial_mode;
extern u32 netplay_num_clients, netplay_client_id;
u16 rand_gen(void);
void rand_seed(u32 data);
void netpacket_send(uint16_t client_id, const void* buf, size_t len);
void netpacket_poll_receive(void);

// serial.h (with patch 001's rfu_net_disconnect)
void rfu_reset(void);
bool rfu_update(unsigned cycles);
u32 rfu_transfer(u32 value);
void rfu_net_disconnect(void);
void rfu_frame_update(void);
void rfu_net_receive(const void* buf, size_t len, uint16_t client_id);
void serialproto_reset(void);
void serialpoke_frame_update(void);
void serialpoke_master_send(void);
bool serialpoke_update(unsigned cycles);
void serialpoke_net_receive(const void* buf, size_t len, uint16_t client_id);
void serialaw_master_send(void);
bool serialaw_update(unsigned cycles);
void serialaw_net_receive(const void* buf, size_t len, uint16_t client_id);
int ismemzero(const void* ptr, size_t bytes);

#endif
