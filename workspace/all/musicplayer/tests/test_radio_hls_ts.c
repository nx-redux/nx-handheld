// Host-compiled unit test for radio_hls.c's MPEG-TS demuxer (PAT -> PMT ->
// audio PID discovery, PES payload extraction, resync, truncated packets and
// the output bound) and its URL helpers (base URL, relative resolution).
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. radio_hls.c tag_meta.c tests/test_radio_hls_ts.c -o /tmp/t && /tmp/t
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../radio_hls.h"
#include "../radio_net.h"

// radio_hls.c's playlist fetch links against this; never called here
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size, char* content_type, int ct_size) {
	return -1;
}

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

#define PKT 188
#define PMT_PID 0x100
#define AUDIO_PID 0x101

static uint8_t ts[PKT * 32];
static int ts_len;

// Appends one packet: 4-byte header, optional adaptation field of adapt_len
// bytes (afc 3), then payload padded with 0xFF
static void put_packet(int pid, bool pusi, int adapt_len, const uint8_t* payload, int payload_len) {
	uint8_t* p = ts + ts_len;
	memset(p, 0xFF, PKT);
	p[0] = 0x47;
	p[1] = (uint8_t)((pusi ? 0x40 : 0) | ((pid >> 8) & 0x1F));
	p[2] = (uint8_t)(pid & 0xFF);
	int at = 4;
	if (adapt_len >= 0) {
		p[3] = 0x30; // adaptation + payload
		p[4] = (uint8_t)adapt_len;
		at = 5 + adapt_len;
	} else {
		p[3] = 0x10; // payload only
	}
	if (payload_len > PKT - at)
		payload_len = PKT - at;
	memcpy(p + at, payload, payload_len);
	ts_len += PKT;
}

static void put_pat(void) {
	const uint8_t pat[] = {0x00,								  // pointer field
						   0x00, 0xB0, 13,						  // table id, section length
						   0x00, 0x01, 0xC1, 0x00, 0x00,		  // ts id, version, section numbers
						   0x00, 0x01,							  // program number
						   0xE0 | (PMT_PID >> 8), PMT_PID & 0xFF, // PMT PID
						   0, 0, 0, 0};							  // CRC (not checked)
	put_packet(0, true, -1, pat, sizeof(pat));
}

// PMT listing a video stream, then an audio stream of the given type
static void put_pmt(int audio_type) {
	const uint8_t pmt[] = {0x00,
						   0x02, 0xB0, 23,								 // table id, section length
						   0x00, 0x01, 0xC1, 0x00, 0x00,				 // program, version, sections
						   0xE1, 0x00,									 // PCR PID
						   0xF0, 0x00,									 // program info length 0
						   0x1B, 0xE1, 0x02, 0xF0, 0x00,				 // H.264 video
						   (uint8_t)audio_type, 0xE0 | (AUDIO_PID >> 8), // audio
						   AUDIO_PID & 0xFF, 0xF0, 0x00,
						   0, 0, 0, 0};
	put_packet(PMT_PID, true, -1, pmt, sizeof(pmt));
}

// PES start: 9-byte header plus 5 bytes of PTS, then audio bytes of value v
static void put_pes_start(uint8_t v, int adapt_len) {
	uint8_t pes[PKT];
	memset(pes, v, sizeof(pes));
	const uint8_t hdr[] = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x80, 0x80, 0x05, 0x21, 0x00, 0x01, 0x00, 0x01};
	memcpy(pes, hdr, sizeof(hdr));
	put_packet(AUDIO_PID, true, adapt_len, pes, sizeof(pes));
}

static void put_pes_cont(uint8_t v) {
	uint8_t raw[PKT];
	memset(raw, v, sizeof(raw));
	put_packet(AUDIO_PID, false, -1, raw, sizeof(raw));
}

static bool all_equal(const uint8_t* p, int n, uint8_t v) {
	for (int i = 0; i < n; i++)
		if (p[i] != v)
			return false;
	return true;
}

static uint8_t out[64 * 1024];

static void test_pat_pmt_pes(void) {
	ts_len = 0;
	put_pat();
	put_pmt(0x0F);
	put_pes_start(0xA1, -1); // 184 - 14 = 170 audio bytes
	put_pes_cont(0xA2);		 // 184 raw bytes
	put_packet(0x1FFF, false, -1, (const uint8_t*)"null", 4);
	put_pes_cont(0xA3);

	int pid = 0;
	bool detected = false;
	int n = radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected);
	CHECK(detected && pid == AUDIO_PID);
	CHECK(n == 170 + 184 + 184);
	CHECK(all_equal(out, 170, 0xA1));
	CHECK(all_equal(out + 170, 184, 0xA2));
	CHECK(all_equal(out + 354, 184, 0xA3));
}

static void test_mp3_stream_type(void) {
	ts_len = 0;
	put_pat();
	put_pmt(0x03);
	put_pes_start(0x11, -1);
	int pid = 0;
	bool detected = false;
	CHECK(radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected) == 170);
	CHECK(detected && pid == AUDIO_PID);
}

static void test_no_audio_in_pmt(void) {
	ts_len = 0;
	put_pat();
	put_pmt(0x1B); // second stream is video too
	put_pes_start(0x11, -1);
	int pid = 0;
	bool detected = false;
	CHECK(radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected) == 0);
	CHECK(!detected);
}

static void test_cached_pid_and_adaptation(void) {
	// A later segment: the PID is known, so no PAT/PMT needed
	ts_len = 0;
	put_pes_start(0x22, 10); // adaptation field: 184 - 11 - 14 = 159 bytes
	put_pes_cont(0x23);
	int pid = AUDIO_PID;
	bool detected = true;
	int n = radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected);
	CHECK(n == 159 + 184);
	CHECK(all_equal(out, 159, 0x22) && all_equal(out + 159, 184, 0x23));

	// Without the cached PID and no PAT, nothing is extracted
	detected = false;
	CHECK(radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected) == 0);
}

static void test_resync_and_truncated_tail(void) {
	ts_len = 0;
	put_pat();
	put_pmt(0x0F);
	// junk between packets: the demuxer scans for the next sync byte
	memset(ts + ts_len, 0x00, 3);
	ts_len += 3;
	put_pes_start(0x31, -1);
	// half a packet at the end is ignored, not read past
	put_pes_cont(0x32);
	ts_len -= PKT / 2;

	int pid = 0;
	bool detected = false;
	CHECK(radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected) == 170);
	CHECK(all_equal(out, 170, 0x31));
}

static void test_bad_adaptation_length_stops(void) {
	ts_len = 0;
	put_pes_cont(0x41);
	put_packet(AUDIO_PID, false, 0, NULL, 0);
	ts[ts_len - PKT + 4] = 200; // adaptation field longer than the packet
	put_pes_cont(0x42);
	int pid = AUDIO_PID;
	bool detected = true;
	CHECK(radio_hls_demux_ts(ts, ts_len, out, sizeof(out), &pid, &detected) == 184);
}

static void test_output_bound(void) {
	ts_len = 0;
	put_pes_start(0x51, -1);
	for (int i = 0; i < 20; i++)
		put_pes_cont(0x52);
	int pid = AUDIO_PID;
	bool detected = true;
	// The loop stops once within 1 KB of the end; nothing is written past it
	static uint8_t small[1024 + 400 + 16];
	memset(small, 0xEE, sizeof(small));
	int n = radio_hls_demux_ts(ts, ts_len, small, 1024 + 400, &pid, &detected);
	CHECK(n > 0 && n <= 1024 + 400);
	CHECK(all_equal(small + 1024 + 400, 16, 0xEE));
}

static void check_resolve(const char* base, const char* rel, const char* want) {
	char result[256];
	radio_hls_resolve_url(base, rel, result, sizeof(result));
	if (strcmp(result, want) != 0) {
		fprintf(stderr, "FAIL resolve %s + %s: got %s, want %s\n", base, rel, result, want);
		failures++;
	}
}

static void check_base(const char* url, const char* want) {
	char base[256];
	radio_hls_get_base_url(url, base, sizeof(base));
	if (strcmp(base, want) != 0) {
		fprintf(stderr, "FAIL base %s: got %s, want %s\n", url, base, want);
		failures++;
	}
}

static void test_urls(void) {
	check_base("https://cdn.example.com/live/index.m3u8", "https://cdn.example.com/live/");
	check_base("http://h.com/a/b/c.m3u8?token=1", "http://h.com/a/b/");
	check_base("http://a/x.m3u8", "http://a/"); // one-letter host
	check_base("http://h.com", "http://h.com");

	check_resolve("http://h.com/live/", "https://other.com/s.ts", "https://other.com/s.ts");
	check_resolve("http://h.com/live/", "/root/s.ts", "http://h.com/root/s.ts");
	check_resolve("http://h.com:8080/live/", "/s.ts", "http://h.com:8080/s.ts");
	check_resolve("http://h.com", "/s.ts", "http://h.com/s.ts");
	check_resolve("http://h.com/live/", "seg1.ts", "http://h.com/live/seg1.ts");
	check_resolve("live/", "/s.ts", "/s.ts"); // base without scheme

	char small[12];
	radio_hls_resolve_url("http://h.com/live/", "seg-long-name.ts", small, sizeof(small));
	CHECK(strlen(small) == sizeof(small) - 1);
	radio_hls_resolve_url("http://h.com/live/", "/root/very/long.ts", small, sizeof(small));
	CHECK(strlen(small) == sizeof(small) - 1);
}

int main(void) {
	test_pat_pmt_pes();
	test_mp3_stream_type();
	test_no_audio_in_pmt();
	test_cached_pid_and_adaptation();
	test_resync_and_truncated_tail();
	test_bad_adaptation_length_stops();
	test_output_bound();
	test_urls();
	if (failures) {
		fprintf(stderr, "test_radio_hls_ts: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_radio_hls_ts: all tests passed\n");
	return 0;
}
