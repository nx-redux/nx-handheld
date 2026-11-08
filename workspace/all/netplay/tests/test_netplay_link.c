// Host-compiled tests: siolink handshake rules and gpsp_serial resolution.
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../gbalink_mode.h"
#include "../siolink_proto.h"
#include "../netplay_ports.h"
#include "usblink_frame.h"

static uint8_t V = SIOLINK_PROTO_VERSION;

static void test_hs_host_hello_cadence_and_fail(void) {
	SioLinkHs h;
	siolink_hs_init(&h, SIOLINK_SIDE_HOST, 1000);
	assert(siolink_hs_tick(&h, 1000) == SIOHS_SEND_HELLO);
	assert(siolink_hs_tick(&h, 1050) == 0);
	assert(siolink_hs_tick(&h, 1100) == SIOHS_SEND_HELLO);
	assert(siolink_hs_tick(&h, 1000 + SIOLINK_HANDSHAKE_MS) == SIOHS_FAIL);
	assert(h.failed && siolink_hs_tick(&h, 999999) == 0);
}

static void test_hs_full(void) {
	SioLinkHs host, dev;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_on_frame(&dev, ULF_HELLO, &V, 1) == SIOHS_SEND_ACK && !dev.up);
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &V, 1) == SIOHS_SEND_CONFIRM && !host.up);
	assert(siolink_hs_confirm_sent(&host) == SIOHS_UP && host.up);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &V, 1) == SIOHS_UP && dev.up);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &V, 1) == 0);	   // once
	assert(siolink_hs_tick(&host, 50) == 0);					   // no hellos once up
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &V, 1) == 0); // late duplicate ACK
	assert(siolink_hs_confirm_sent(&host) == 0);
}

static void test_hs_version_and_malformed(void) {
	uint8_t bad = SIOLINK_PROTO_VERSION + 1;
	SioLinkHs host, dev;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &bad, 1) == SIOHS_FAIL);
	assert(host.failed && host.peer_version == bad && siolink_hs_confirm_sent(&host) == 0);
	assert(siolink_hs_on_frame(&dev, ULF_HELLO, &bad, 1) == SIOHS_SEND_ACK);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &bad, 1) == 0 && !dev.up);
	assert(siolink_hs_on_frame(&dev, ULF_HELLO, &V, 0) == 0);	  // truncated
	assert(siolink_hs_on_frame(&dev, ULF_HELLO_ACK, &V, 1) == 0); // wrong side
	SioLinkHs h2;
	siolink_hs_init(&h2, SIOLINK_SIDE_HOST, 0);
	assert(siolink_hs_on_frame(&h2, ULF_HELLO, &V, 1) == 0);
	assert(siolink_hs_on_frame(&h2, ULF_CONFIRM, &V, 1) == 0);
	assert(siolink_hs_on_frame(&h2, ULF_HELLO_ACK, &V, 0) == 0);
}

static void test_hs_data_always_delivered(void) {
	uint8_t p[SIOLINK_PAYLOAD_MAX + 1] = {'N', 'X', 'L', 'S'};
	SioLinkHs host, dev;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_on_frame(&host, ULF_DATA, p, 24) == SIOHS_DELIVER); // not up yet: still ours
	assert(siolink_hs_on_frame(&dev, ULF_DATA, p, 24) == SIOHS_DELIVER);
	assert(siolink_hs_on_frame(&dev, ULF_DATA, p, SIOLINK_PAYLOAD_MAX + 1) == 0);
	assert(siolink_hs_on_frame(&dev, ULF_DATA, p, 0) == 0);
}

static void test_hs_device_never_fails(void) {
	SioLinkHs dev;
	siolink_hs_init(&dev, SIOLINK_SIDE_DEVICE, 0);
	assert(siolink_hs_tick(&dev, 600000) == 0 && !dev.failed);
	assert(siolink_hs_on_frame(&dev, ULF_CONFIRM, &V, 1) == SIOHS_UP);
}

static void test_hs_confirm_needs_matching_ack(void) {
	SioLinkHs h;
	siolink_hs_init(&h, SIOLINK_SIDE_HOST, 0);
	assert(siolink_hs_confirm_sent(&h) == 0 && !h.up); // no HELLO_ACK yet
	h.peer_version = SIOLINK_PROTO_VERSION + 1;
	assert(siolink_hs_confirm_sent(&h) == 0 && !h.up); // wrong version never comes up
	h.peer_version = 0;
	assert(siolink_hs_on_frame(&h, ULF_HELLO_ACK, &V, 1) == SIOHS_SEND_CONFIRM);
	assert(siolink_hs_confirm_sent(&h) == SIOHS_UP && h.up);
}

static void test_hs_data_after_fail(void) {
	// CONFIRM/timeout race: the gadget side came up on a CONFIRM sent just
	// before our timeout. Its DATA must still reach the core.
	uint8_t p[24] = {'N', 'X', 'L', 'S'};
	SioLinkHs host;
	siolink_hs_init(&host, SIOLINK_SIDE_HOST, 0);
	assert(siolink_hs_tick(&host, SIOLINK_HANDSHAKE_MS) == SIOHS_FAIL && host.failed);
	assert(siolink_hs_on_frame(&host, ULF_DATA, p, 24) == SIOHS_DELIVER);
	assert(siolink_hs_on_frame(&host, ULF_HELLO_ACK, &V, 1) == 0 && !host.up);
}

static void test_msg_codec(void) {
	uint8_t p[SIOLINK_PAYLOAD_MAX + 1] = {'N', 'X', 'L', 'S'}, m[SIOLINK_MSG_MAX + 1], type;
	const uint8_t* q;
	int n = siolink_msg_encode(ULF_DATA, p, 24, m, sizeof(m));
	assert(n == 25 && m[0] == ULF_DATA);
	assert(siolink_msg_decode(m, n, &type, &q) == 24 && type == ULF_DATA && memcmp(q, p, 24) == 0);
	assert(siolink_msg_encode(ULF_CONFIRM, &V, 1, m, sizeof(m)) == 2);
	assert(siolink_msg_decode(m, 2, &type, &q) == 1 && type == ULF_CONFIRM && q[0] == V);
	assert(siolink_msg_encode(ULF_DATA, p, SIOLINK_PAYLOAD_MAX, m, sizeof(m)) == SIOLINK_MSG_MAX);
	assert(siolink_msg_encode(ULF_DATA, p, SIOLINK_PAYLOAD_MAX + 1, m, sizeof(m)) == -1);
	assert(siolink_msg_encode(ULF_DATA, p, 24, m, 24) == -1); // out too small
	assert(siolink_msg_encode(ULF_DATA, p, -1, m, sizeof(m)) == -1);
	assert(siolink_msg_encode(0, p, 1, m, sizeof(m)) == -1);	   // 0 is usblink's REGISTER byte
	assert(siolink_msg_encode(ULF_SIO, p, 1, m, sizeof(m)) == -1); // never nested
	m[0] = 0;
	assert(siolink_msg_decode(m, 2, &type, &q) == -1);
	m[0] = ULF_SIO;
	assert(siolink_msg_decode(m, 2, &type, &q) == -1);
	m[0] = ULF_DATA;
	assert(siolink_msg_decode(m, 0, &type, &q) == -1);
	assert(siolink_msg_decode(m, SIOLINK_MSG_MAX + 1, &type, &q) == -1);
	assert(siolink_msg_decode(m, 1, &type, &q) == 0 && type == ULF_DATA); // empty: on_frame ignores it
}

static void test_side_from_state(void) {
	assert(siolink_side_from_state("host") == SIOLINK_SIDE_HOST);
	assert(siolink_side_from_state("device") == SIOLINK_SIDE_DEVICE);
	assert(siolink_side_from_state("") == 0 && siolink_side_from_state(NULL) == 0);
}

static void test_serial_option(void) {
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "auto", "usb", NULL, NULL), "auto_cable"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", "usb", NULL, NULL), "lockstep"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "rfu", "usb", NULL, NULL), "rfu"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "auto", "wifi", NULL, NULL), "auto"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", "hotspot", NULL, NULL), "auto"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", NULL, NULL, NULL), "auto")); // single player
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "mul_poke", NULL, NULL, NULL), "mul_poke"));
	const char* v = "auto";
	assert(gbalink_serial_option("gpsp_rtc", v, "usb", NULL, NULL) == v);
	assert(gbalink_serial_option("gpsp_serial", NULL, "usb", NULL, NULL) == NULL);
}

// Two players per session: ports past 1 (multitap, 4-player games) have
// nobody on them and read nothing, as in local play, not player 2's input.
static void test_port_input(void) {
	assert(netplay_port_input(0, 0x11, 0x22) == 0x11);
	assert(netplay_port_input(1, 0x11, 0x22) == 0x22);
	assert(netplay_port_input(2, 0x11, 0x22) == 0);
	assert(netplay_port_input(3, 0x11, 0x22) == 0);
	assert(netplay_port_input(7, 0x11, 0x22) == 0);
}

static void test_port_analog(void) {
	const int16_t p1[NETPLAY_ANALOG_AXES] = {100, -200, 300, -400};
	const int16_t p2[NETPLAY_ANALOG_AXES] = {-1, 2, -3, 4};
	// left X/Y, right X/Y for each player on its own port
	assert(netplay_port_analog(0, 0, 0, p1, p2) == 100 && netplay_port_analog(0, 0, 1, p1, p2) == -200);
	assert(netplay_port_analog(0, 1, 0, p1, p2) == 300 && netplay_port_analog(0, 1, 1, p1, p2) == -400);
	assert(netplay_port_analog(1, 0, 0, p1, p2) == -1 && netplay_port_analog(1, 1, 1, p1, p2) == 4);
	// nobody on ports 2+, and the analog-button index (2) is not a stick
	assert(netplay_port_analog(2, 0, 0, p1, p2) == 0 && netplay_port_analog(3, 1, 1, p1, p2) == 0);
	assert(netplay_port_analog(0, 2, 0, p1, p2) == 0 && netplay_analog_slot(2, 0) == -1);
	assert(netplay_analog_slot(0, 2) == -1 && netplay_analog_slot(1, 1) == 3);
}

static void test_link_mode_key(void) {
	assert(!strcmp(gbalink_link_mode_key("gpsp"), "gpsp_serial"));
	assert(!strcmp(gbalink_link_mode_key("GPSP"), "gpsp_serial"));
	assert(!strcmp(gbalink_link_mode_key("mgba"), "mgba_link_mode"));
	assert(gbalink_link_mode_key("fceumm") == NULL);
	assert(gbalink_link_mode_key(NULL) == NULL);
}

static void test_transport_ok(void) {
	assert(gbalink_transport_ok("mgba", "usb", NULL, NULL));
	assert(!gbalink_transport_ok("mgba", "wifi", NULL, NULL));
	assert(!gbalink_transport_ok("mgba", "hotspot", NULL, NULL));
	assert(!gbalink_transport_ok("mgba", NULL, NULL, NULL));
	assert(gbalink_transport_ok("gpsp", "wifi", NULL, NULL));
	assert(gbalink_transport_ok("gpsp", NULL, NULL, NULL));
}

static void test_transport_ok_wifi_modes(void) {
	const char* peer = "core=mgba,tag=MGBA,wl=poke";
	assert(gbalink_transport_ok("mgba", "wifi", "core=mgba,tag=MGBA,wl=poke", peer));
	assert(gbalink_transport_ok("mgba", "hotspot", "core=mgba,tag=MGBA,wl=aw2", "core=gpsp,tag=GBA,wl=aw2"));
	assert(!gbalink_transport_ok("mgba", "wifi", "core=mgba,tag=MGBA,wl=none", peer));
	assert(!gbalink_transport_ok("mgba", "wifi", "core=mgba,tag=MGBA", peer));
	assert(gbalink_transport_ok("mgba", "usb", "core=mgba,tag=MGBA,wl=none", NULL));
	assert(gbalink_transport_ok("gpsp", "wifi", "core=gpsp,tag=GBA,wl=none", NULL));
	// a peer without wl= (an older build, or no caps at all) would run the real cable off USB
	assert(!gbalink_transport_ok("mgba", "wifi", "core=mgba,tag=MGBA,wl=poke", "core=gpsp,tag=GBA"));
	assert(!gbalink_transport_ok("mgba", "hotspot", "core=mgba,tag=MGBA,wl=rfu", NULL));
	assert(!gbalink_transport_ok("mgba", "wifi", "core=mgba,tag=MGBA,wl=rfu", ""));
	assert(gbalink_transport_ok("mgba", "usb", "core=mgba,tag=MGBA,wl=rfu", NULL));
	assert(gbalink_transport_ok("gpsp", "wifi", "core=gpsp,tag=GBA,wl=rfu", NULL));
}

static void test_mixed(void) {
	assert(gbalink_mixed("core=gpsp,tag=GBA,wl=rfu", "core=mgba,tag=MGBA,wl=rfu"));
	assert(!gbalink_mixed("core=gpsp,tag=GBA,wl=rfu", "core=gpsp,tag=GBA,wl=rfu"));
	assert(!gbalink_mixed("core=gpsp,tag=GBA", "dcbios=abc")); // a side with no core
	assert(!gbalink_mixed(NULL, "core=mgba,tag=MGBA"));
	assert(!gbalink_mixed("core=gpsp", NULL));
}

static void test_adopt_host_mode(void) {
	const char* gpsp = "core=gpsp,tag=GBA,wl=rfu";
	const char* mgba = "core=mgba,tag=MGBA,wl=rfu";
	// same core, different pick: the joiner takes the host's for the session
	assert(gbalink_adopt_host_mode("gpsp", "rfu", "auto", gpsp, gpsp));
	assert(gbalink_adopt_host_mode("mgba", "auto", "cable", mgba, mgba));
	assert(gbalink_adopt_host_mode("mgba", "auto", NULL, mgba, mgba));
	// no caps at all (an in-game join): as before
	assert(gbalink_adopt_host_mode("gpsp", "rfu", "auto", NULL, NULL));
	// same pick, no host mode, or a core without a link-mode option
	assert(!gbalink_adopt_host_mode("gpsp", "auto", "auto", gpsp, gpsp));
	assert(!gbalink_adopt_host_mode("gpsp", "", "auto", gpsp, gpsp));
	assert(!gbalink_adopt_host_mode("gpsp", NULL, "auto", gpsp, gpsp));
	assert(!gbalink_adopt_host_mode("fceumm", "rfu", "auto", NULL, NULL));
	// mixed: the host's value is another core's option, never adopted
	assert(!gbalink_adopt_host_mode("mgba", "auto_cable", "cable", mgba, gpsp));
	assert(!gbalink_adopt_host_mode("gpsp", "cable", "auto", gpsp, mgba));
}

static void test_mgba_option(void) {
	const char* ours = "core=mgba,tag=MGBA,wl=rfu";
	const char* same = "core=mgba,tag=MGBA,wl=rfu";
	// an older peer sends no wl=: the real cable, whatever was picked
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "auto", "usb", ours, "core=mgba,tag=MGBA"), "cable"));
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "rfu", "wifi", ours, NULL), "cable"));
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "auto", "usb", ours, same), "auto_cable"));
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "cable", "wifi", ours, same), "auto"));
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "mul_poke", "wifi", ours, same), "mul_poke"));
	// mixed with gpSP
	const char* gpsp = "core=gpsp,tag=GBA,wl=rfu";
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "cable", "wifi", ours, gpsp), "auto"));
	assert(!strcmp(gbalink_serial_option("mgba_link_mode", "rfu", "usb", ours, gpsp), "auto_cable"));
	// gpSP: mixed overrides, same-core is as before
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "disabled", "wifi", gpsp, ours), "auto"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "disabled", "usb", gpsp, ours), "auto_cable"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "auto", "usb", gpsp, gpsp), "auto_cable"));
	assert(!strcmp(gbalink_serial_option("gpsp_serial", "lockstep", "wifi", gpsp, gpsp), "auto"));
}

static int option_failures;

static void expect_option(const char* key, const char* value, const char* mode, const char* ours, const char* peer,
						  const char* want) {
	const char* got = gbalink_serial_option(key, value, mode, ours, peer);
	if (!got || strcmp(got, want) != 0) {
		fprintf(stderr, "FAIL %s=%s mode=%s ours=%s peer=%s: want %s, got %s\n", key, value, mode ? mode : "(none)",
				ours ? ours : "(none)", peer ? peer : "(none)", want, got ? got : "(null)");
		option_failures++;
	}
}

// Every mgba_link_mode row: no session, a peer without wl=, mixed, USB, off USB.
static void test_mgba_option_table(void) {
	const char* ours = "core=mgba,tag=MGBA,wl=rfu";
	const char* same = "core=mgba,tag=MGBA,wl=rfu";
	const char* old = "core=mgba,tag=MGBA";
	const char* gpsp = "core=gpsp,tag=GBA,wl=rfu";
	static const char* const values[] = {"cable", "auto", "auto_cable", "rfu", "mul_poke", "mul_aw1", "mul_aw2", "disabled"};
	const size_t n = sizeof(values) / sizeof(values[0]);
	option_failures = 0;
	for (size_t i = 0; i < n; i++) {
		// outside a session the core never scans the ROM for its mode
		expect_option("mgba_link_mode", values[i], NULL, NULL, NULL, "cable");
		expect_option("mgba_link_mode", values[i], "", NULL, NULL, "cable");
		expect_option("mgba_link_mode", values[i], "usb", ours, old, "cable");
		expect_option("mgba_link_mode", values[i], "wifi", ours, old, "cable");
		expect_option("mgba_link_mode", values[i], "hotspot", ours, NULL, "cable");
		expect_option("mgba_link_mode", values[i], "usb", ours, gpsp, "auto_cable");
		expect_option("mgba_link_mode", values[i], "wifi", ours, gpsp, "auto");
	}
	// USB, both new, same core: the real cable picks the adapter for adapter games
	expect_option("mgba_link_mode", "cable", "usb", ours, same, "auto_cable");
	expect_option("mgba_link_mode", "auto", "usb", ours, same, "auto_cable");
	expect_option("mgba_link_mode", "auto_cable", "usb", ours, same, "auto_cable");
	expect_option("mgba_link_mode", "rfu", "usb", ours, same, "rfu");
	expect_option("mgba_link_mode", "mul_poke", "usb", ours, same, "mul_poke");
	expect_option("mgba_link_mode", "disabled", "usb", ours, same, "disabled");
	// off USB, both new, same core: never the lockstep cable
	expect_option("mgba_link_mode", "cable", "wifi", ours, same, "auto");
	expect_option("mgba_link_mode", "auto", "wifi", ours, same, "auto");
	expect_option("mgba_link_mode", "auto_cable", "wifi", ours, same, "auto");
	expect_option("mgba_link_mode", "auto_cable", "hotspot", ours, same, "auto");
	expect_option("mgba_link_mode", "rfu", "wifi", ours, same, "rfu");
	expect_option("mgba_link_mode", "mul_aw2", "hotspot", ours, same, "mul_aw2");
	expect_option("mgba_link_mode", "disabled", "wifi", ours, same, "disabled");
	assert(option_failures == 0);
}

// Every gpsp_serial row; unlike mGBA a peer without wl= changes nothing.
static void test_gpsp_option_table(void) {
	const char* gpsp = "core=gpsp,tag=GBA,wl=rfu";
	const char* mgba = "core=mgba,tag=MGBA,wl=rfu";
	static const char* const values[] = {"auto", "auto_cable", "lockstep", "rfu", "mul_poke", "disabled"};
	const size_t n = sizeof(values) / sizeof(values[0]);
	option_failures = 0;
	for (size_t i = 0; i < n; i++) {
		expect_option("gpsp_serial", values[i], "usb", gpsp, mgba, "auto_cable");
		expect_option("gpsp_serial", values[i], "wifi", gpsp, mgba, "auto");
	}
	const char* modes[] = {"usb", "wifi", NULL};
	for (size_t m = 0; m < 3; m++) {
		bool usb = modes[m] && !strcmp(modes[m], "usb");
		for (size_t i = 0; i < n; i++) {
			const char* want = values[i];
			if (usb && !strcmp(want, "auto"))
				want = "auto_cable";
			if (!usb && !strcmp(want, "lockstep"))
				want = "auto";
			expect_option("gpsp_serial", values[i], modes[m], gpsp, gpsp, want);
			expect_option("gpsp_serial", values[i], modes[m], NULL, NULL, want);
		}
	}
	assert(option_failures == 0);
}

int main(void) {
	test_port_analog();
	test_port_input();
	test_hs_host_hello_cadence_and_fail();
	test_hs_full();
	test_hs_version_and_malformed();
	test_hs_data_always_delivered();
	test_hs_device_never_fails();
	test_hs_confirm_needs_matching_ack();
	test_hs_data_after_fail();
	test_msg_codec();
	test_side_from_state();
	test_serial_option();
	test_link_mode_key();
	test_transport_ok();
	test_transport_ok_wifi_modes();
	test_mixed();
	test_adopt_host_mode();
	test_mgba_option();
	test_mgba_option_table();
	test_gpsp_option_table();
	printf("netplay link tests: OK\n");
	return 0;
}
