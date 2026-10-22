// Host test for chd_tracks.c: CHD track metadata, LBAs, track selection and sector layout.
#include <stdio.h>
#include <string.h>
#include "chd_tracks.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static int fmt_is(int type, int frame_size, int header, int data) {
	int h = -1, d = -1;
	ChdTrack_sectorFormat(type, frame_size, &h, &d);
	return h == header && d == data;
}

int main(void) {
	// type strings
	CHECK(ChdTrack_parseType("MODE1") == CD_TRACK_MODE1, "MODE1");
	CHECK(ChdTrack_parseType("MODE1_RAW") == CD_TRACK_MODE1_RAW && ChdTrack_parseType("MODE1/2352") == CD_TRACK_MODE1_RAW,
		  "MODE1_RAW and MODE1/2352");
	CHECK(ChdTrack_parseType("MODE2_RAW") == CD_TRACK_MODE2_RAW && ChdTrack_parseType("MODE2/2352") == CD_TRACK_MODE2_RAW,
		  "MODE2_RAW and MODE2/2352");
	CHECK(ChdTrack_parseType("MODE2") == CD_TRACK_MODE2 && ChdTrack_parseType("MODE2_FORM1") == CD_TRACK_MODE2_FORM1 &&
			  ChdTrack_parseType("MODE2_FORM2") == CD_TRACK_MODE2_FORM2 &&
			  ChdTrack_parseType("MODE2_FORM_MIX") == CD_TRACK_MODE2_FORM_MIX,
		  "MODE2 variants");
	CHECK(ChdTrack_parseType("AUDIO") == CD_TRACK_AUDIO, "AUDIO");
	CHECK(ChdTrack_parseType("BOGUS") == CD_TRACK_MODE1 && ChdTrack_parseType("") == CD_TRACK_MODE1, "unknown: MODE1");
	CHECK(ChdTrack_isData(CD_TRACK_MODE2_RAW) && !ChdTrack_isData(CD_TRACK_AUDIO), "data vs audio");

	// metadata strings
	{
		chd_track_info_t t;
		int cum = 0;
		memset(&t, 0, sizeof(t));
		CHECK(ChdTrack_parseMetadata("TRACK:1 TYPE:MODE2_RAW SUBTYPE:NONE FRAMES:1001 PREGAP:150 PGTYPE:V PGSUB:RW POSTGAP:2",
									 0, &t, &cum) == 1,
			  "CD v2 metadata parsed");
		CHECK(t.type == CD_TRACK_MODE2_RAW && t.frames == 1001 && t.pregap_frames == 150 && t.postgap_frames == 2 &&
				  t.start_frame == 0 && !t.is_gdrom,
			  "CD v2 fields");
		CHECK(cum == 1004, "next track starts 4-frame aligned");
		CHECK(ChdTrack_parseMetadata("TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:300", 0, &t, &cum) == 1 &&
				  t.type == CD_TRACK_AUDIO && t.frames == 300 && t.pregap_frames == 0 && t.start_frame == 1004 &&
				  cum == 1304,
			  "CD v1 metadata (no pregap), already aligned");
		chd_track_info_t before = t;
		CHECK(ChdTrack_parseMetadata("garbage", 0, &t, &cum) == 0 && cum == 1304 && !memcmp(&t, &before, sizeof(t)),
			  "unparsable metadata: rejected, nothing changed");
		CHECK(ChdTrack_parseMetadata("TRACK:1 TYPE:MODE1", 0, &t, &cum) == 0, "metadata without FRAMES: rejected");

		cum = 0;
		CHECK(ChdTrack_parseMetadata("TRACK:3 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:500 PAD:0 PREGAP:0 PGTYPE:MODE1 "
									 "PGSUB:RW POSTGAP:0",
									 1, &t, &cum) == 1 &&
				  t.is_gdrom && t.type == CD_TRACK_MODE1_RAW && t.frames == 500 && cum == 500,
			  "GD-ROM metadata");
		CHECK(ChdTrack_parseMetadata("TRACK:1 TYPE:MODE1/2352 SUBTYPE:NONE FRAMES:10", 0, &t, &cum) == 1 &&
				  t.type == CD_TRACK_MODE1_RAW && t.start_frame == 500 && cum == 512,
			  "10 frames pad to 12");
	}

	// LBAs: a CD uses frame offsets, a GD-ROM's track 3 starts at 45000
	{
		chd_track_info_t cd[3];
		memset(cd, 0, sizeof(cd));
		cd[0].start_frame = 0;
		cd[1].start_frame = 1004;
		cd[2].start_frame = 2000;
		ChdTrack_assignLbas(cd, 3);
		CHECK(cd[0].lba_start == 0 && cd[1].lba_start == 1004 && cd[2].lba_start == 2000, "CD: LBA = frame offset");

		chd_track_info_t gd[4];
		memset(gd, 0, sizeof(gd));
		for (int i = 0; i < 4; i++)
			gd[i].is_gdrom = 1;
		gd[0].start_frame = 0;
		gd[1].start_frame = 600;
		gd[2].start_frame = 1200;
		gd[3].start_frame = 5000;
		ChdTrack_assignLbas(gd, 4);
		CHECK(gd[0].lba_start == 0 && gd[1].lba_start == 600, "GD-ROM low-density tracks: frame offset");
		CHECK(gd[2].lba_start == 45000 && gd[3].lba_start == 45000 + 3800, "GD-ROM track 3 at 45000, track 4 after it");
	}

	// track requests
	{
		chd_track_info_t t[4];
		memset(t, 0, sizeof(t));
		t[0].type = CD_TRACK_AUDIO;
		t[0].frames = 100;
		t[1].type = CD_TRACK_MODE1_RAW;
		t[1].frames = 900;
		t[2].type = CD_TRACK_AUDIO;
		t[2].frames = 50;
		t[3].type = CD_TRACK_MODE2_RAW;
		t[3].frames = 300;
		CHECK(ChdTrack_find(t, 4, 1) == 0 && ChdTrack_find(t, 4, 4) == 3, "1-based track numbers");
		CHECK(ChdTrack_find(t, 4, 0) == -1 && ChdTrack_find(t, 4, 5) == -1, "track 0 and past the end: none");
		CHECK(ChdTrack_find(t, 4, (uint32_t)-1) == 1, "-1: first data track");
		CHECK(ChdTrack_find(t, 4, (uint32_t)-2) == 3, "-2: last track");
		CHECK(ChdTrack_find(t, 4, (uint32_t)-3) == 1, "-3: largest track");
		CHECK(ChdTrack_find(t, 4, (uint32_t)-4) == 1, "-4: first data track after audio");
		CHECK(ChdTrack_find(t, 4, (uint32_t)-5) == -1, "other special values: none");
		CHECK(ChdTrack_find(t, 0, 1) == -1 && ChdTrack_find(t, 0, (uint32_t)-2) == -1, "no tracks: none");
		t[1].type = CD_TRACK_AUDIO;
		t[3].type = CD_TRACK_AUDIO;
		CHECK(ChdTrack_find(t, 4, (uint32_t)-1) == -1 && ChdTrack_find(t, 4, (uint32_t)-4) == -1,
			  "all audio: no data track");
		t[0].type = CD_TRACK_MODE1;
		t[1].type = CD_TRACK_MODE1;
		CHECK(ChdTrack_find(t, 4, (uint32_t)-4) == -1, "-4 with no data after audio: none");
	}

	// sector layout per type and frame size
	CHECK(fmt_is(CD_TRACK_MODE1, 2048, 0, 2048) && fmt_is(CD_TRACK_MODE2, 2448, 0, 2048), "cooked: 0/2048");
	CHECK(fmt_is(CD_TRACK_MODE1_RAW, 2352, 16, 2048) && fmt_is(CD_TRACK_MODE1_RAW, 2448, 16, 2048),
		  "MODE1 raw: 16/2048");
	CHECK(fmt_is(CD_TRACK_MODE1_RAW, 2048, 0, 2048), "MODE1 raw in a 2048 frame: cooked");
	CHECK(fmt_is(CD_TRACK_MODE2_RAW, 2352, 24, 2048) && fmt_is(CD_TRACK_MODE2_FORM_MIX, 2448, 24, 2048),
		  "MODE2 raw / form mix: 24/2048");
	CHECK(fmt_is(CD_TRACK_MODE2_FORM1, 2336, 8, 2048) && fmt_is(CD_TRACK_MODE2_FORM2, 2336, 8, 2048),
		  "MODE2 2336: 8/2048");
	CHECK(fmt_is(CD_TRACK_MODE2_FORM1, 2352, 0, 2048), "MODE2 form1 in a 2352 frame: cooked");
	CHECK(fmt_is(CD_TRACK_AUDIO, 2352, 0, 2352) && fmt_is(CD_TRACK_AUDIO, 2448, 0, 2352), "audio: 0/2352");
	CHECK(fmt_is(99, 2448, 16, 2048) && fmt_is(99, 2048, 0, 2048), "unknown type: by frame size");

	// the data clamp never reaches past the frame
	{
		size_t d = 2048;
		CHECK(ChdTrack_clampData(2352, 16, &d) && d == 2048, "fits: unchanged");
		d = 2352;
		CHECK(ChdTrack_clampData(2048, 0, &d) && d == 2048, "audio in a short frame: clamped");
		d = 2048;
		CHECK(ChdTrack_clampData(2060, 24, &d) && d == 2036, "header + data clamped to the frame");
		d = 2048;
		CHECK(!ChdTrack_clampData(16, 16, &d) && !ChdTrack_clampData(8, 24, &d), "header not fitting: unreadable");
		int ok = 1;
		for (size_t frame = 1; frame <= 2448 && ok; frame += 7)
			for (size_t skip = 0; skip <= 24 && ok; skip += 8) {
				size_t data = 2352;
				if (ChdTrack_clampData(frame, skip, &data))
					ok = skip + data <= frame;
			}
		CHECK(ok, "skip + data <= frame for every size");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
