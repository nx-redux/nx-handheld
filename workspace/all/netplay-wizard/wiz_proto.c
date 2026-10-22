#include "wiz_proto.h"

#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "network_common.h" // NET_MAX_GAME_NAME
#include "wiz_caps.h"		// WizCaps_reasonLabel

//////////////////////////////////
// Wire protocol helpers
//////////////////////////////////
//
// Pure text in, pure text out — no sockets, no SDL, no wizard state. Kept that
// way on purpose: this is the half of the protocol that can be exercised on a
// development host, and every parse below is fed by a remote peer.

// Copies in to out with each space replaced by WIZ_NET_SPACE_ESC. Always
// NUL-terminates; a value too long for out is truncated, which the receiver
// then reads as a mismatch rather than as a shorter name.
void wiz_esc_spaces(const char* in, char* out, size_t out_size) {
	size_t i = 0;

	if (!out || out_size == 0)
		return;

	for (; in && in[i] && i + 1 < out_size; i++)
		out[i] = (in[i] == ' ') ? WIZ_NET_SPACE_ESC : in[i];

	out[i] = '\0';
}

// In-place inverse. Idempotent for a peer that sent no escapes at all, which is
// what makes a space-free game title interoperable either way.
void wiz_unesc_spaces(char* s) {
	for (; s && *s; s++)
		if (*s == WIZ_NET_SPACE_ESC)
			*s = ' ';
}

// 0 = parsed a syntactically valid HELLO. game/role come back still escaped.
// The optional fifth field is the assigned player number on a host HELLO and
// the WIZ_NET_ANY_GAME flag on a client HELLO; a caller asks for whichever its
// side expects and the other comes back as 0/false.
int wiz_parse_hello(const char* line, int* version, char* game, size_t game_size,
					char* role, size_t role_size, int* player_num, bool* any_game) {
	char verb[16];
	char game_tok[WIZ_NET_TOKEN_MAX];
	char role_tok[16];
	char extra[16] = {0};
	int ver = 0;

	int fields = sscanf(line, "%15s %d %255s %15s %15s", verb, &ver, game_tok, role_tok, extra);
	if (fields < 4)
		return -1;
	if (strcmp(verb, "HELLO") != 0)
		return -1;

	*version = ver;
	snprintf(game, game_size, "%s", game_tok);
	snprintf(role, role_size, "%s", role_tok);
	if (player_num)
		*player_num = (fields >= 5 && isdigit((unsigned char)extra[0])) ? atoi(extra) : 0;
	if (any_game)
		*any_game = (fields >= 5 && strcmp(extra, WIZ_NET_ANY_GAME) == 0);
	return 0;
}

// Two game names count as the same game when they agree after normalization:
// lowercase alphanumerics only, with (...) and [...] tag groups removed first.
// "Marvel vs. Capcom 2 (USA)" pairs with "marvel vs capcom 2" — region tags,
// dump flags, punctuation and spacing never block a pair — while a real title
// difference ("... 2" vs "... 3") still does. The \x1f space escaping is
// non-alphanumeric, so escaped and raw names normalize identically. This is
// the ONLY game gate a wizard session has: the link engines store name/CRC
// for their own discovery broadcasts but never re-verify them on a direct
// connect, so a mismatch that slips past here surfaces only in-game. The
// joiner can waive the gate on purpose (WIZ_NET_ANY_GAME, after
// wiz_client_confirm_other_game): two versions of one game link fine on the
// hardware and there is no way to tell them from a wrong game by name.
void wiz_normalize_name(const char* in, char* out, size_t out_size) {
	size_t o = 0;
	int in_tag = 0;

	for (const char* p = in; *p && o < out_size - 1; p++) {
		char c = *p;
		if (c == '(' || c == '[') {
			in_tag++;
		} else if (c == ')' || c == ']') {
			if (in_tag)
				in_tag--;
		} else if (!in_tag && isalnum((unsigned char)c)) {
			out[o++] = (char)tolower((unsigned char)c);
		}
	}
	out[o] = '\0';
}

bool wiz_names_equivalent(const char* a, const char* b) {
	char na[NET_MAX_GAME_NAME];
	char nb[NET_MAX_GAME_NAME];

	if (!a || !b)
		return false;
	wiz_normalize_name(a, na, sizeof(na));
	wiz_normalize_name(b, nb, sizeof(nb));
	if (!na[0] || !nb[0])
		return false; // a name that is all tags matches nothing
	return strcmp(na, nb) == 0;
}

// NULL = the peer is a match. Otherwise the REJECT reason, which is also the
// key wiz_reject_message() turns into a sentence. Order matters: version is
// checked first because a different version may not even mean the same thing by
// "game" or "role".
const char* wiz_hello_mismatch(int version, const char* game, const char* role,
							   const char* own_game, const char* expect_role,
							   bool any_game) {
	if (version != WIZ_PROTO_VERSION)
		return "version";
	if (!any_game && !wiz_names_equivalent(game, own_game))
		return "game";
	if (!role || strcmp(role, expect_role) != 0)
		return "role";
	return NULL;
}

// 0 = the line was a REJECT and reason holds its argument.
int wiz_parse_reject(const char* line, char* reason, size_t reason_size) {
	char verb[16];
	char reason_tok[32];

	int fields = sscanf(line, "%15s %31s", verb, reason_tok);
	if (fields < 1 || strcmp(verb, "REJECT") != 0)
		return -1;

	// A bare "REJECT" is legal on the wire; it just says nothing useful.
	snprintf(reason, reason_size, "%s", fields == 2 ? reason_tok : "");
	return 0;
}

// The user-facing half of a REJECT. Every reason is something the two devices
// disagree on, so each line names the disagreement rather than the error.
const char* wiz_reject_message(const char* reason) {
	if (!reason)
		return "The host refused the connection.";
	if (strcmp(reason, "version") == 0)
		return "The host is running a\ndifferent netplay version.";
	if (strcmp(reason, "game") == 0)
		return "The host is running a\ndifferent game.";
	if (strcmp(reason, "role") == 0)
		return "Both devices are hosting.\n\nOne of them must join instead.";
	if (strcmp(reason, "sync") == 0)
		return "The host could not share\nits saves.";
	if (strcmp(reason, "core") == 0 || strncmp(reason, "core-", 5) == 0) {
		// "core-<core>.<tag>" names the host's emulator and folder.
		static char message[192];
		char label[96];
		WizCaps_reasonLabel(reason, label, sizeof(label));
		if (label[0])
			snprintf(message, sizeof(message), "The host is using\n%s.\n\n%s", label, WIZ_NET_CORE_HINT);
		else
			snprintf(message, sizeof(message), "The host is using a\ndifferent emulator.\n\n%s",
					 WIZ_NET_CORE_HINT);
		return message;
	}
	return "The host refused the connection.";
}

// 0 = the line was SYNC-READY and n holds its count (unvalidated; the caller
// bounds it against its own storage).
int wiz_parse_sync_ready(const char* line, int* n) {
	char verb[16];
	int count = 0;

	if (sscanf(line, "%15s %d", verb, &count) != 2)
		return -1;
	if (strcmp(verb, "SYNC-READY") != 0)
		return -1;

	*n = count;
	return 0;
}

// 0 = the line was FILE and name holds its still-escaped argument. name must be
// at least WIZ_NET_TOKEN_MAX: a token wider than a filename may fit is read in
// full so wiz_name_is_safe() can refuse it, rather than silently truncated into
// a different, valid-looking name.
int wiz_parse_file(const char* line, char* name, size_t name_size) {
	char verb[16];
	char name_tok[WIZ_NET_TOKEN_MAX];

	if (sscanf(line, "%15s %255s", verb, name_tok) != 2)
		return -1;
	if (strcmp(verb, "FILE") != 0)
		return -1;

	snprintf(name, name_size, "%s", name_tok);
	return 0;
}

// A name the protocol may carry, in either direction. This is a WHITELIST, not
// a list of things to reject: only [A-Za-z0-9._-] survives. Everything else —
// every shell metacharacter, space, slash, quote, control byte, high byte — is
// out by construction rather than by enumeration, so the rule cannot be
// outgrown by a character nobody thought of.
//
// It has to be this strict because these names are remote input on their way to
// wiz_sync_pull() (wizard_sync.c), and this repo's idiom for driving an external tool
// is system() — wizard_wifi.c does it eight times, and so does the hotspot ping
// below. A name like `id`.bin or a;id.bin passes --fetch-files happily
// (fnmatch("*.bin", "a;id.bin", 0) == 0 — the pattern list is NOT a defence),
// and would be LAN-triggerable command execution the moment it reached a shell.
//
// THIS IS THE WIRE-LEVEL GUARANTEE, NOT THE ONLY ONE. wizard_sync.c must still
// treat these names as data: posix_spawn/execv with no shell, or quoted. A whitelist
// is the second line of defence, never a licence to interpolate.
//
// Rules the character class already subsumes, spelled out because they are the
// reasons: '/' would leave the directory the peer does not choose; a space
// would split one argv element into two; control characters (\n, \r, and
// WIZ_NET_SPACE_ESC itself) would break the line framing or return from the
// escape round trip as something else. Left explicit below: "."/".." (all
// whitelisted characters, but still traversal), a leading '-' (whitelisted, but
// rsync would read it as an option), and the length bound that
// wiz_sync_pull()'s names[][64] imposes.
//
// Deliberately tighter than fnmatch allows. Dreamcast save names
// (vmu_save_A1.bin, dc_nvmem.bin) fit comfortably; a name with a space is
// refused rather than transferred, and symmetrically so — the host will not
// offer it and the client will not accept it.
//
// Applied to the *decoded* name on both sides, so there is exactly one
// canonical form that ever gets checked.
bool wiz_name_is_safe(const char* name) {
	if (!name || !name[0])
		return false;
	if (strlen(name) >= WIZ_NET_NAME_MAX)
		return false;
	if (name[0] == '-')
		return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;

	// Explicit ranges, not isalnum(): that one is locale-dependent, and this
	// rule must mean the same thing wherever it runs.
	for (const char* c = name; *c; c++) {
		if (*c >= 'A' && *c <= 'Z')
			continue;
		if (*c >= 'a' && *c <= 'z')
			continue;
		if (*c >= '0' && *c <= '9')
			continue;
		if (*c == '.' || *c == '_' || *c == '-')
			continue;
		return false;
	}

	return true;
}

// Pattern check against --fetch-files. Note what this is NOT: with FNM_PATHNAME
// unset a '*' spans '/' and every metacharacter, so it constrains shape only —
// wiz_name_is_safe() is what constrains content, and it runs first at both call
// sites. No patterns means nothing is whitelisted; callers treat that as "this
// side wants no files" rather than as "all files".
bool wiz_name_matches(const char* name, const char patterns[][64], int count) {
	for (int i = 0; i < count; i++)
		if (fnmatch(patterns[i], name, 0) == 0)
			return true;
	return false;
}

// Same-game test for the host list's ordering: the same normalized-name
// equivalence the HELLO gate uses (escape-tolerant — normalization strips
// \x1f). Only ordering rides on this: the broadcast field truncates titles
// at 64 bytes while own_game is full-length, so an ultra-long title may sort
// as "other game"; the HELLO gate compares full names and decides for real.
bool wiz_game_matches_broadcast(const char* broadcast_game, const char* own_game) {
	return wiz_names_equivalent(broadcast_game, own_game);
}

//////////////////////////////////
// Control channel line reader
//////////////////////////////////

void wiz_reader_init(WizLineReader* r, int fd) {
	r->fd = fd;
	r->len = 0;
}

bool wiz_fd_readable(int fd, int timeout_ms) {
	fd_set fds;
	struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};

	if (fd < 0)
		return false;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	return select(fd + 1, &fds, NULL, NULL, &tv) > 0;
}

// Non-blocking. 1 = a line was copied to out (terminator stripped), 0 = nothing
// complete yet, -1 = the peer closed, errored, or sent a line too long to be
// one of ours.
int wiz_reader_poll(WizLineReader* r, char* out, size_t out_size) {
	while (1) {
		char* nl = memchr(r->buf, '\n', r->len);
		if (nl) {
			size_t line_len = (size_t)(nl - r->buf);
			size_t rest = r->len - line_len - 1;
			size_t copy = line_len;

			// Tolerate CRLF: a scripted peer (netcat, a test harness) is as
			// likely to send it as not.
			if (copy > 0 && r->buf[copy - 1] == '\r')
				copy--;
			if (copy >= out_size)
				copy = out_size - 1;

			memcpy(out, r->buf, copy);
			out[copy] = '\0';

			memmove(r->buf, nl + 1, rest);
			r->len = rest;
			return 1;
		}

		if (r->len == sizeof(r->buf))
			return -1; // no terminator in a full buffer: not this protocol

		if (!wiz_fd_readable(r->fd, 0))
			return 0;

		ssize_t got = recv(r->fd, r->buf + r->len, sizeof(r->buf) - r->len, 0);
		if (got == 0)
			return -1; // orderly close
		if (got < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			return -1;
		}

		r->len += (size_t)got;
	}
}
