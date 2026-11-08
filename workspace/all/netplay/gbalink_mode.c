#include "gbalink_mode.h"

#include <stdbool.h>
#include <string.h>
#include <strings.h>

#include "gwl_mode.h"

// The value of "<key>=" in a comma-separated caps token, copied into out.
static bool caps_value(const char* caps, const char* key, char* out, size_t out_size) {
	out[0] = '\0';
	if (!caps)
		return false;
	size_t key_len = strlen(key);
	for (const char* p = caps; *p;) {
		const char* end = strchr(p, ',');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		if (len > key_len + 1 && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
			size_t n = len - key_len - 1;
			if (n >= out_size)
				n = out_size - 1;
			memcpy(out, p + key_len + 1, n);
			out[n] = '\0';
			return true;
		}
		if (!end)
			break;
		p = end + 1;
	}
	return false;
}

bool gbalink_mixed(const char* our_caps, const char* peer_caps) {
	char a[64], b[64];
	caps_value(our_caps, "core", a, sizeof(a));
	caps_value(peer_caps, "core", b, sizeof(b));
	return a[0] && b[0] && strcmp(a, b) != 0;
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
		char wl[16];
		// A peer that never heard of the Wi-Fi modes only speaks the real cable.
		if (!caps_value(peer_caps, "wl", wl, sizeof(wl)))
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
	char wl[16];
	caps_value(our_caps, "wl", wl, sizeof(wl));
	return gwl_mode_from_token(wl) != GWL_NONE;
}
