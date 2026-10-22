#ifndef WIZ_PROTO_H
#define WIZ_PROTO_H

// The wizard's control-channel line protocol (HELLO / REJECT / SYNC-READY / FILE), split out of
// wizard_net.c: pure text in, text out, plus the socket line reader. No SDL, no wizard state, so it
// runs on a development host (tests/run_tests.sh). Each function is documented in wiz_proto.c.

#include <stdbool.h>
#include <stddef.h>

#define WIZ_PROTO_VERSION 2 // 2: lockstep input carries sticks (NETPLAY_PROTOCOL_VERSION 3)

// Longest control line accepted, terminator included. The longest line the
// protocol can produce is "HELLO 1 <game> client" with a 255-char game token.
#define WIZ_NET_LINE_MAX 512
// Widest single token. The sscanf widths below are written out as literals
// (a format string cannot interpolate a macro), so this and they must agree:
// every "%255s" in this file is WIZ_NET_TOKEN_MAX - 1.
#define WIZ_NET_TOKEN_MAX 256
// wiz_sync_pull() takes char names[][64]; a name that does not fit is refused
// rather than truncated.
#define WIZ_NET_NAME_MAX 64

// Field separator substitute. Filenames and game titles carry spaces; \x1f
// (unit separator) is a control character, so neither can contain one.
#define WIZ_NET_SPACE_ESC '\x1f'

// The second half of both core-mismatch messages.
#define WIZ_NET_CORE_HINT "Pick the game from the same\nfolder on both devices."

// Client HELLO flag: "join even though the titles differ" (see the header).
#define WIZ_NET_ANY_GAME "any"

// Lines arrive without regard for packet boundaries — the host emits
// SYNC-READY and every FILE back to back, and TCP is free to deliver them in
// one read. The reader keeps whatever came early instead of over-reading past a
// line into the next one.
typedef struct {
	int fd;
	char buf[WIZ_NET_LINE_MAX];
	size_t len;
} WizLineReader;

void wiz_esc_spaces(const char* in, char* out, size_t out_size);
void wiz_unesc_spaces(char* s);
int wiz_parse_hello(const char* line, int* version, char* game, size_t game_size, char* role, size_t role_size,
					int* player_num, bool* any_game);
void wiz_normalize_name(const char* in, char* out, size_t out_size);
bool wiz_names_equivalent(const char* a, const char* b);
const char* wiz_hello_mismatch(int version, const char* game, const char* role, const char* own_game,
							   const char* expect_role, bool any_game);
int wiz_parse_reject(const char* line, char* reason, size_t reason_size);
const char* wiz_reject_message(const char* reason);
int wiz_parse_sync_ready(const char* line, int* n);
int wiz_parse_file(const char* line, char* name, size_t name_size);
bool wiz_name_is_safe(const char* name);
bool wiz_name_matches(const char* name, const char patterns[][64], int count);
bool wiz_game_matches_broadcast(const char* broadcast_game, const char* own_game);
// true when fd has something to read within timeout_ms (0 = just poll); false for fd < 0.
bool wiz_fd_readable(int fd, int timeout_ms);
void wiz_reader_init(WizLineReader* r, int fd);
int wiz_reader_poll(WizLineReader* r, char* out, size_t out_size);

#endif
