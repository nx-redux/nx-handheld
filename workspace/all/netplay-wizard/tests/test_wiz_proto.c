// Host test for wiz_proto.c: the netplay wizard's control-channel line protocol. Every parse here
// is fed by a remote peer. Run by tests/run_tests.sh.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wiz_proto.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (!(cond)) {                 \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static void test_escape(void) {
	char out[64];
	wiz_esc_spaces("Marvel vs. Capcom 2", out, sizeof(out));
	CHECK(!strchr(out, ' ') && strchr(out, WIZ_NET_SPACE_ESC), "esc: spaces replaced");
	wiz_unesc_spaces(out);
	CHECK(!strcmp(out, "Marvel vs. Capcom 2"), "esc: round trip");
	wiz_unesc_spaces(out);
	CHECK(!strcmp(out, "Marvel vs. Capcom 2"), "unesc: idempotent without escapes");
	char small[5];
	wiz_esc_spaces("a b c d", small, sizeof(small));
	CHECK(!strcmp(small, "a\x1f"
						 "b\x1f"),
		  "esc: truncated and terminated");
	wiz_esc_spaces(NULL, out, sizeof(out));
	CHECK(out[0] == '\0', "esc: NULL in");
	wiz_unesc_spaces(NULL);
}

static void test_hello(void) {
	int ver = -1, player = -1;
	bool any = true;
	char game[WIZ_NET_TOKEN_MAX], role[16];

	CHECK(wiz_parse_hello("HELLO 2 Tetris host", &ver, game, sizeof(game), role, sizeof(role), &player, &any) == 0,
		  "hello: 4 fields");
	CHECK(ver == 2 && !strcmp(game, "Tetris") && !strcmp(role, "host") && player == 0 && !any,
		  "hello: 4-field values, no player/any");
	CHECK(wiz_parse_hello("HELLO 2 Tetris host 3", &ver, game, sizeof(game), role, sizeof(role), &player, &any) ==
				  0 &&
			  player == 3 && !any,
		  "hello: player number");
	CHECK(wiz_parse_hello("HELLO 2 Tetris client any", &ver, game, sizeof(game), role, sizeof(role), &player,
						  &any) == 0 &&
			  any && player == 0,
		  "hello: any-game flag");
	CHECK(wiz_parse_hello("HELLO 2 Tetris client any", &ver, game, sizeof(game), role, sizeof(role), NULL, NULL) ==
			  0,
		  "hello: NULL optional outputs");
	CHECK(wiz_parse_hello("HELLO 2 Tetris", &ver, game, sizeof(game), role, sizeof(role), &player, &any) == -1,
		  "hello: 3 fields rejected");
	CHECK(wiz_parse_hello("REJECT game", &ver, game, sizeof(game), role, sizeof(role), &player, &any) == -1,
		  "hello: other verb rejected");
	CHECK(wiz_parse_hello("HELLOX 2 Tetris host", &ver, game, sizeof(game), role, sizeof(role), &player, &any) ==
			  -1,
		  "hello: verb prefix rejected");
	CHECK(wiz_parse_hello("", &ver, game, sizeof(game), role, sizeof(role), &player, &any) == -1, "hello: empty");

	// A 300-char game token: the game is cut at 255 and the rest spills into the role, so the
	// HELLO gate refuses it instead of matching a truncated title.
	char line[400], longgame[301];
	memset(longgame, 'g', 300);
	longgame[300] = '\0';
	snprintf(line, sizeof(line), "HELLO 2 %s client", longgame);
	CHECK(wiz_parse_hello(line, &ver, game, sizeof(game), role, sizeof(role), &player, &any) == 0,
		  "hello: long game parses");
	CHECK(strlen(game) == 255, "hello: long game cut at the token width");
	CHECK(wiz_hello_mismatch(ver, game, role, longgame, "client", false) != NULL, "hello: long game is refused");

	// game/role are cut to the caller's buffers
	char g4[4], r3[3];
	CHECK(wiz_parse_hello("HELLO 2 Tetris host", &ver, g4, sizeof(g4), r3, sizeof(r3), NULL, NULL) == 0 &&
			  !strcmp(g4, "Tet") && !strcmp(r3, "ho"),
		  "hello: outputs truncated to their size");
}

static void test_names(void) {
	char out[64];
	wiz_normalize_name("Marvel vs. Capcom 2 (USA) [!]", out, sizeof(out));
	CHECK(!strcmp(out, "marvelvscapcom2"), "normalize: tags and punctuation stripped");
	wiz_normalize_name("A (nested (tag)) B", out, sizeof(out));
	CHECK(!strcmp(out, "ab"), "normalize: nested tags");
	wiz_normalize_name("A) B", out, sizeof(out));
	CHECK(!strcmp(out, "ab"), "normalize: stray close paren ignored");
	char tiny[4];
	wiz_normalize_name("abcdef", tiny, sizeof(tiny));
	CHECK(!strcmp(tiny, "abc"), "normalize: truncated");

	CHECK(wiz_names_equivalent("Marvel vs. Capcom 2 (USA)", "marvel vs capcom 2"), "equivalent: tags/case");
	CHECK(wiz_names_equivalent("Sonic\x1fthe\x1fHedgehog", "Sonic the Hedgehog (Europe)"),
		  "equivalent: escaped spaces");
	CHECK(!wiz_names_equivalent("Street Fighter 2", "Street Fighter 3"), "equivalent: 2 vs 3 differ");
	CHECK(!wiz_names_equivalent("(USA) [!]", "(USA) [!]"), "equivalent: all-tag name matches nothing");
	CHECK(!wiz_names_equivalent(NULL, "x") && !wiz_names_equivalent("x", NULL), "equivalent: NULL");
	CHECK(wiz_game_matches_broadcast("Tetris (World)", "TETRIS"), "broadcast: same rule");

	// mismatch order: version, then game (unless waived), then role
	CHECK(wiz_hello_mismatch(WIZ_PROTO_VERSION, "Tetris", "client", "Tetris", "client", false) == NULL,
		  "mismatch: none");
	CHECK(!strcmp(wiz_hello_mismatch(WIZ_PROTO_VERSION + 1, "Other", "host", "Tetris", "client", false), "version"),
		  "mismatch: version first");
	CHECK(!strcmp(wiz_hello_mismatch(WIZ_PROTO_VERSION, "Other", "host", "Tetris", "client", false), "game"),
		  "mismatch: game before role");
	CHECK(!strcmp(wiz_hello_mismatch(WIZ_PROTO_VERSION, "Other", "host", "Tetris", "client", true), "role"),
		  "mismatch: any-game waives the game, role still checked");
	CHECK(wiz_hello_mismatch(WIZ_PROTO_VERSION, "Other", "client", "Tetris", "client", true) == NULL,
		  "mismatch: any-game");
	CHECK(!strcmp(wiz_hello_mismatch(WIZ_PROTO_VERSION, "Tetris", NULL, "Tetris", "client", false), "role"),
		  "mismatch: NULL role");
}

static void test_other_lines(void) {
	char reason[32];
	CHECK(wiz_parse_reject("REJECT game", reason, sizeof(reason)) == 0 && !strcmp(reason, "game"), "reject: reason");
	CHECK(wiz_parse_reject("REJECT", reason, sizeof(reason)) == 0 && !strcmp(reason, ""), "reject: bare");
	CHECK(wiz_parse_reject("HELLO 2 x host", reason, sizeof(reason)) == -1, "reject: other verb");
	CHECK(wiz_parse_reject("", reason, sizeof(reason)) == -1, "reject: empty line");
	CHECK(strstr(wiz_reject_message("role"), "Both devices are hosting") != NULL, "reject message: role");
	CHECK(strstr(wiz_reject_message("core"), WIZ_NET_CORE_HINT) != NULL, "reject message: core");
	CHECK(!strcmp(wiz_reject_message("nonsense"), "The host refused the connection."), "reject message: unknown");
	CHECK(!strcmp(wiz_reject_message(NULL), "The host refused the connection."), "reject message: NULL");

	int n = -1;
	CHECK(wiz_parse_sync_ready("SYNC-READY 3", &n) == 0 && n == 3, "sync-ready: count");
	CHECK(wiz_parse_sync_ready("SYNC-READY -5", &n) == 0 && n == -5, "sync-ready: count is the caller's to bound");
	CHECK(wiz_parse_sync_ready("SYNC-READY", &n) == -1, "sync-ready: no count");
	CHECK(wiz_parse_sync_ready("FILE 3", &n) == -1, "sync-ready: other verb");

	char name[WIZ_NET_TOKEN_MAX];
	CHECK(wiz_parse_file("FILE vmu_save_A1.bin", name, sizeof(name)) == 0 && !strcmp(name, "vmu_save_A1.bin"),
		  "file: name");
	CHECK(wiz_parse_file("FILE", name, sizeof(name)) == -1, "file: no name");
	CHECK(wiz_parse_file("SYNC-READY 1", name, sizeof(name)) == -1, "file: other verb");
	char line[300], longname[200];
	memset(longname, 'n', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	snprintf(line, sizeof(line), "FILE %s", longname);
	CHECK(wiz_parse_file(line, name, sizeof(name)) == 0 && strlen(name) == 199 && !wiz_name_is_safe(name),
		  "file: long name read whole, then refused");
}

static void test_name_is_safe(void) {
	CHECK(wiz_name_is_safe("vmu_save_A1.bin") && wiz_name_is_safe("dc_nvmem.bin") && wiz_name_is_safe("a-b.c"),
		  "safe: plain names");
	const char* bad[] = {"", "..", ".", "-rf", "a/b", "a b", "a;id.bin", "`id`.bin", "a\x1f"
																					 "b",
						 "caf\xc3\xa9",
						 "a\nb", "a'b", "$x", NULL};
	for (int i = 0; bad[i]; i++) {
		char msg[64];
		snprintf(msg, sizeof(msg), "safe: rejects bad name #%d", i);
		CHECK(!wiz_name_is_safe(bad[i]), msg);
	}
	CHECK(!wiz_name_is_safe(NULL), "safe: NULL");
	char n63[64], n64[65];
	memset(n63, 'a', 63);
	n63[63] = '\0';
	memset(n64, 'a', 64);
	n64[64] = '\0';
	CHECK(wiz_name_is_safe(n63), "safe: 63 chars");
	CHECK(!wiz_name_is_safe(n64), "safe: 64 chars refused");
	CHECK(wiz_name_is_safe("..a") && wiz_name_is_safe("a-"), "safe: dots/dash not leading-dash are fine");

	const char pats[][64] = {"*.bin", "vmu_*"};
	CHECK(wiz_name_matches("dc_nvmem.bin", pats, 2) && wiz_name_matches("vmu_x", pats, 2), "matches: patterns");
	CHECK(!wiz_name_matches("save.srm", pats, 2), "matches: no match");
	CHECK(!wiz_name_matches("x.bin", pats, 0), "matches: no patterns means nothing");
}

static void test_reader(void) {
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
		CHECK(0, "socketpair");
		return;
	}
	WizLineReader r;
	wiz_reader_init(&r, sv[0]);
	char out[64];

	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 0, "reader: nothing yet");

	// two lines in one write
	const char two[] = "SYNC-READY 2\nFILE a.bin\n";
	CHECK(write(sv[1], two, strlen(two)) == (ssize_t)strlen(two), "reader: write two");
	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 1 && !strcmp(out, "SYNC-READY 2"), "reader: first of two");
	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 1 && !strcmp(out, "FILE a.bin"), "reader: second of two");
	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 0, "reader: drained");

	// a line split across writes, with CRLF
	CHECK(write(sv[1], "HEL", 3) == 3, "reader: write head");
	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 0, "reader: partial line held");
	CHECK(write(sv[1], "LO 2 x host\r\n", 13) == 13, "reader: write tail");
	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 1 && !strcmp(out, "HELLO 2 x host"), "reader: joined, CR stripped");

	// a line longer than out is truncated; the next line is intact
	const char lng[] = "0123456789abcdef\nnext\n";
	CHECK(write(sv[1], lng, strlen(lng)) == (ssize_t)strlen(lng), "reader: write long");
	char small[8];
	CHECK(wiz_reader_poll(&r, small, sizeof(small)) == 1 && !strcmp(small, "0123456"), "reader: long line cut");
	CHECK(wiz_reader_poll(&r, small, sizeof(small)) == 1 && !strcmp(small, "next"), "reader: following line kept");

	// an empty line is a line
	CHECK(write(sv[1], "\n", 1) == 1, "reader: write empty");
	CHECK(wiz_reader_poll(&r, out, sizeof(out)) == 1 && out[0] == '\0', "reader: empty line");

	// a full buffer with no newline is not this protocol
	char junk[WIZ_NET_LINE_MAX];
	memset(junk, 'x', sizeof(junk));
	CHECK(write(sv[1], junk, sizeof(junk)) == (ssize_t)sizeof(junk), "reader: write junk");
	int rc = 0;
	for (int i = 0; i < 4 && rc == 0; i++)
		rc = wiz_reader_poll(&r, out, sizeof(out));
	CHECK(rc == -1, "reader: full buffer without newline");

	// orderly close
	WizLineReader r2;
	wiz_reader_init(&r2, sv[0]);
	close(sv[1]);
	char drain[WIZ_NET_LINE_MAX];
	while (recv(sv[0], drain, sizeof(drain), MSG_DONTWAIT) > 0) {
	}
	CHECK(wiz_reader_poll(&r2, out, sizeof(out)) == -1, "reader: peer closed");
	close(sv[0]);

	WizLineReader r3;
	wiz_reader_init(&r3, -1);
	CHECK(wiz_reader_poll(&r3, out, sizeof(out)) == 0, "reader: no fd reads nothing");
}

int main(void) {
	test_escape();
	test_hello();
	test_names();
	test_other_lines();
	test_name_is_safe();
	test_reader();
	if (fails) {
		printf("test_wiz_proto: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_wiz_proto: all tests passed\n");
	return 0;
}
