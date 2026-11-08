#include "gbalink_mode.h"

#include <stdbool.h>
#include <string.h>
#include <strings.h>

#include "gwl_mode.h"
#include "wiz_caps.h"

// Longest "wl=" value ("none", "rfu", "poke", "aw1", "aw2") with room to spare.
#define WL_TOKEN_MAX 16

bool gbalink_mixed(const char* our_caps, const char* peer_caps) {
	return WizCaps_coreMismatch(our_caps, peer_caps);
}

// gpsp_serial "auto" in a USB Cable session: the gpSP 004 patch resolves
// "auto_cable" like "auto" (wireless adapter for games that have one), then
// real cable for everything else; mGBA's mgba_link_mode does the same. gpSP's
// "lockstep" is USB-only (decision 1 of the lockstep spec): anywhere else it
// behaves as "auto", so a value persisted by an earlier USB session never
// changes Wi-Fi or single-player play.
const char* gbalink_serial_option(const char* key, const char* value, const char* netplay_mode,
								  const char* our_caps, const char* peer_caps) {
	if (!key || !value)
		return value;
	bool gpsp = strcmp(key, "gpsp_serial") == 0;
	bool mgba = strcmp(key, "mgba_link_mode") == 0;
	if (!gpsp && !mgba)
		return value;
	bool usb = netplay_mode && strcmp(netplay_mode, "usb") == 0;

	if (mgba && netplay_mode) {
		char wl[WL_TOKEN_MAX];
		// A peer that never heard of the Wi-Fi modes only speaks the real cable.
		WizCaps_value(peer_caps, "wl", wl, sizeof(wl));
		if (!wl[0])
			return "cable";
	}
	if (gbalink_mixed(our_caps, peer_caps))
		return usb ? "auto_cable" : "auto";
	if (usb && strcmp(value, "auto") == 0)
		return "auto_cable";
	if (!usb && gpsp && strcmp(value, "lockstep") == 0)
		return "auto";
	if (!usb && mgba && netplay_mode && strcmp(value, "cable") == 0)
		return "auto";
	return value;
}

const char* gbalink_link_mode_key(const char* core_name) {
	if (!core_name)
		return NULL;
	if (strcasecmp(core_name, "gpsp") == 0)
		return "gpsp_serial";
	if (strcasecmp(core_name, "mgba") == 0)
		return "mgba_link_mode";
	return NULL;
}

bool gbalink_transport_ok(const char* core_name, const char* netplay_mode, const char* our_caps) {
	if (!core_name || strcasecmp(core_name, "mgba") != 0)
		return true;
	if (netplay_mode && strcmp(netplay_mode, "usb") == 0)
		return true;
	char wl[WL_TOKEN_MAX];
	WizCaps_value(our_caps, "wl", wl, sizeof(wl));
	return gwl_mode_from_token(wl) != GWL_NONE;
}
