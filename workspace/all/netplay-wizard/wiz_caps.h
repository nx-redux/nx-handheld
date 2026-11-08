// wiz_caps.h - an optional capability token the two wizards trade.
//
// A pak's launcher can pass --caps <token> (e.g. Dreamcast's
// "dcbios=<fingerprint>", so both sides can agree on a BIOS). The wizard
// appends it to its HELLO line as a trailing "caps=<token>" field and writes
// the peer's token to the session file as NETPLAY_PEER_CAPS. A wizard that
// predates the field ignores it (every HELLO parse reads a fixed number of
// fields), and a peer that sends none yields an empty token.
//
// Libc only, so the host unit test links it directly.

#ifndef WIZ_CAPS_H
#define WIZ_CAPS_H

#include <stdbool.h>
#include <stddef.h>

// Longest token, including the terminator.
#define WIZ_CAPS_MAX 64

// Non-empty, shorter than WIZ_CAPS_MAX, only [A-Za-z0-9._=,-]: safe on the
// wire (no spaces) and in the session file, which launch.sh sources.
bool WizCaps_isValid(const char* token);
// The token of the "caps=" field in a received HELLO line, or "" when the
// line has none or its token is invalid. Only fields after the fourth count,
// so a game named "caps=..." is never mistaken for it.
void WizCaps_find(const char* line, char* out, size_t out_size);
// " caps=<token>" to append to an outgoing HELLO, or "" for no/invalid token.
void WizCaps_field(const char* token, char* out, size_t out_size);

// netplay-prelaunch.sh passes "core=<EMU_EXE>,tag=<EMU_TAG>": two different
// cores (MD's PicoDrive vs GPGX's Genesis Plus GX, gpSP vs mGBA) never sync, so
// both wizards refuse such a pair in the HELLO exchange and say why.
// The value of the "<key>=" element of a comma-separated token, or "".
void WizCaps_value(const char* caps, const char* key, char* out, size_t out_size);
// True only when both tokens name a core and the two differ. A side with none
// (Dreamcast's dcbios=, or an older build) is let through; tags never refuse.
bool WizCaps_coreMismatch(const char* ours, const char* theirs);
// True when the two sides can't link: different cores, unless both run the same
// Wi-Fi link mode ("wl=", gwl_mixed_ok; over USB only the Wireless Adapter
// mode). A side without "wl=" (an older build, or no caps) keeps the plain
// core rule, except against mGBA off USB (WizCaps_mgbaNeedsWl).
bool WizCaps_linkMismatch(const char* ours, const char* theirs, bool usb);
// True off USB when one side runs mGBA and the other sent no "wl=": mGBA links
// there only in a Wi-Fi mode, and that side would fall back to the real cable.
bool WizCaps_mgbaNeedsWl(const char* ours, const char* theirs, bool usb);
// The name a player knows a core by ("Genesis Plus GX"); unknown = as-is.
const char* WizCaps_coreName(const char* core);
// "<name> (<tag> folder)", or just the name when there is no tag.
void WizCaps_coreLabel(const char* core, const char* tag, char* out, size_t out_size);
// The REJECT reason that names our core and folder: "core-<core>.<tag>", or
// "core-<core>", or bare "core" — whichever first fits out_size (the reply is
// parsed with %31s, so pass 32).
void WizCaps_coreReason(const char* caps, char* out, size_t out_size);
// The label a "core-..." REJECT reason names, or "" when it names none.
void WizCaps_reasonLabel(const char* reason, char* out, size_t out_size);

// Two different cores that both sent a Wi-Fi mode ("wl=") yet can't link: the
// modes differ, or the transport turns theirs into the real cable (the
// Pokémon and Advance Wars modes over USB). Only builds that send "wl=" get
// here, so they all know this reason: "link-<sender core>.<sender wl>.<receiver
// wl>", the sender's core first so a phone on Auto can still switch to it.
// False (and out "") when the refusal is about something else, or the reason
// would not fit out_size (pass 32, the %31s the reply is parsed with).
bool WizCaps_linkReason(const char* sender, const char* receiver, bool usb, char* out, size_t out_size);
// Splits a "link-..." reason; false for any other reason or a malformed one.
bool WizCaps_parseLinkReason(const char* reason, char* core, size_t core_size, char* sender_wl,
							 size_t sender_size, char* receiver_wl, size_t receiver_size);
// What a player calls a Wi-Fi mode ("Wireless Adapter"); "" for none/unknown.
const char* WizCaps_linkModeName(const char* wl);

#endif
