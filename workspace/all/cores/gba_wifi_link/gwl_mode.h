// gwl_mode.h - which link mode a GBA ROM gets: gpSP's own auto selection, so a
// game picks the same mode on gpSP and on any emulator using gba_wifi_link.
// libc only: also built into the Brick frontend and the Android native lib.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GBA_WIFI_LINK_MODE_H
#define GBA_WIFI_LINK_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gwl.h"

// rom: the ROM from its start, len bytes (at least min(1 MiB, rom_size) when
// there are that many). gpSP decides from the first 1 MiB, except that its
// save-type guess scans the whole ROM when the first MiB has no marker, so
// pass more than 1 MiB when you have it. rom_size is the file size.
enum GwlMode gwl_auto_mode(const uint8_t* rom, size_t len, size_t rom_size);
// Reads the file itself. Zip, unreadable and truncated files give GWL_NONE.
enum GwlMode gwl_probe_file(const char* path);

// "none", "rfu", "poke", "aw1", "aw2": the wl= value in the netplay caps.
const char* gwl_mode_token(enum GwlMode m);
enum GwlMode gwl_mode_from_token(const char* t); // unknown or NULL: GWL_NONE

// Whether two devices can link: the same mode on both, not none. Over USB only
// the Wireless Adapter mode works.
bool gwl_mixed_ok(const char* our_wl, const char* peer_wl, bool usb);

// mGBA's mgba_link_mode, resolved at load: which link the core installs.
// Values: cable (the default: the real link cable), auto, auto_cable (the
// Wireless Adapter when that is the game's auto mode, else the real cable),
// rfu, mul_poke, mul_aw1, mul_aw2, disabled. NULL and unknown values are
// cable, so a frontend that never sets the option keeps the real cable.
enum GwlLinkPick { GWL_PICK_NONE,
				   GWL_PICK_CABLE,
				   GWL_PICK_WIFI };
enum GwlLinkPick gwl_mgba_pick(const char* option, enum GwlMode auto_mode);
// The mode to run when gwl_mgba_pick says GWL_PICK_WIFI, else GWL_NONE.
enum GwlMode gwl_mgba_wifi_mode(const char* option, enum GwlMode auto_mode);

#endif
