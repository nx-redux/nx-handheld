// ref_common.h - minimal stand-in for gpSP's common.h, used to build gpSP's own
// rfu.c and serial_proto.c (the reference side of test_gwl_diff.c). run_tests.sh
// copies it next to them as common.h and renames their globals ref_* with -D.
// Config and serial constants come from gpSP's own headers, not from
// gwl_shim.h, so a wrong value in the shim shows up as a diff.
#ifndef GWL_REF_COMMON_H
#define GWL_REF_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short int u16;
typedef unsigned int u32;
typedef signed int s32;
typedef u8 cpu_alert_type;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define netorder32(value) __builtin_bswap32(value)
#define RETRO_NETPACKET_BROADCAST 0xFFFF

#include "gpsp_config.h"
#include "serial.h"

// gba_memory.h (halfword indices into io_registers)
#define REG_SIODATA32_L 0x90
#define REG_SIODATA32_H 0x91
#define REG_SIOMULTI0 0x90
#define REG_SIOMULTI1 0x91
#define REG_SIOMULTI2 0x92
#define REG_SIOMULTI3 0x93
#define REG_SIOCNT 0x94
#define REG_SIOMLT_SEND 0x95
#define REG_RCNT 0x9A

// The test's fake memory; offsets are bytes from 0x04000000, as GwlHost uses.
u16 ref_io_read(u32 off);
void ref_io_write(u32 off, u16 v);
#define read_ioreg(regnum) ref_io_read((regnum) * 2)
#define write_ioreg(regnum, val) ref_io_write((regnum) * 2, (val))

// main.h / libretro.c
extern u32 cpu_ticks;
extern u32 netplay_num_clients, netplay_client_id;
u16 rand_gen();
void rand_seed(u32 data);

#endif
