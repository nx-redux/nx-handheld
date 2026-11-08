#ifndef GBALINK_MODE_H
#define GBALINK_MODE_H

#include <stdbool.h>

// Caps tokens are the launcher's "core=<core>,tag=<tag>,wl=<mode>" (NETPLAY_CAPS
// ours, NETPLAY_PEER_CAPS the peer's); "wl=" is the game's Wireless Adapter /
// multiboot mode, absent from builds that predate the Wi-Fi link modes.

// Resolves the link option value handed to the core, from the user's choice:
//  gpsp_serial: "auto" becomes the hidden "auto_cable" in USB Cable sessions,
//   and the USB-only "lockstep" falls back to "auto" everywhere else.
//  mgba_link_mode: a session whose peer sends no "wl=" is the real cable (an
//   older build can't run the Wi-Fi modes); "cable" becomes "auto" over Wi-Fi,
//   and "auto" becomes "auto_cable" over USB.
//  Both: two different cores (a mixed session) run the game's own mode, so
//   whatever the user picked becomes "auto" ("auto_cable" over USB).
// Every other key passes through unchanged.
const char* gbalink_serial_option(const char* key, const char* value, const char* netplay_mode,
								  const char* our_caps, const char* peer_caps);

// The core option that carries the GBA link mode the host syncs to the
// client: gpSP "gpsp_serial", mGBA "mgba_link_mode", NULL for other cores.
const char* gbalink_link_mode_key(const char* core_name);

// false when this core's link can't run over this transport: mGBA's real
// cable is USB-only, so off USB it needs a Wi-Fi mode (our "wl=" is rfu,
// poke, aw1 or aw2). gpSP and unknown cores: true.
bool gbalink_transport_ok(const char* core_name, const char* netplay_mode, const char* our_caps);

// True when both caps name a core and the two differ.
bool gbalink_mixed(const char* our_caps, const char* peer_caps);

// Whether a joiner whose link mode is our_mode switches to the host_mode the
// host's READY carried (for this session, reloading the core). Only for the
// same core: in a mixed session the host's value belongs to the other core's
// option, and both sides already run auto (gbalink_serial_option).
bool gbalink_adopt_host_mode(const char* core_name, const char* host_mode, const char* our_mode,
							 const char* our_caps, const char* peer_caps);

#endif
