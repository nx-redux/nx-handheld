#pragma once

#include <stddef.h>
#include <stdint.h>
#include <libchdr/cdrom.h>

// CHD track metadata and sector-layout helpers for chd_reader.c: plain C (only
// libchdr's CD_TRACK_* constants), host-tested by tests/test_chd_tracks.c.

typedef struct {
	int type;			// CD_TRACK_* type
	int frames;			// Total frames in track
	int pregap_frames;	// Pregap frames
	int postgap_frames; // Postgap frames
	int start_frame;	// Starting frame (cumulative)
	int is_gdrom;		// Track came from GD-ROM metadata
	int lba_start;		// Absolute disc LBA of the track's first sector (see
						// chd_first_track_sector)
} chd_track_info_t;

// CD_TRACK_* for a metadata TYPE string ("MODE1_RAW", "MODE2/2352", "AUDIO", ...);
// unknown types are MODE1.
int ChdTrack_parseType(const char* type_str);

// 1 for a data track, 0 for audio.
int ChdTrack_isData(int type);

// Parses one track's metadata string (CD v1/v2, or GD-ROM with is_gdrom) into
// out, starting at *cumulative_frames, which then moves past the track (padded
// to 4 frames). Returns 1 when parsed, 0 otherwise (out and the count untouched).
int ChdTrack_parseMetadata(const char* metadata, int is_gdrom, chd_track_info_t* out, int* cumulative_frames);

// Sets each track's lba_start: its frame offset on a CD; from track 3 of a
// GD-ROM, 45000 + its offset from the first high-density track.
void ChdTrack_assignLbas(chd_track_info_t* tracks, int num_tracks);

// Bytes to skip to a sector's data and the data size, from the track type and
// the CHD's frame size (cooked: 0/2048, MODE1 raw: 16/2048, MODE2 raw: 24/2048,
// MODE2 2336: 8/2048, audio: 0/2352).
void ChdTrack_sectorFormat(int track_type, int frame_size, int* header_size, int* data_size);

// The 0-based index for a track request: 1-based track number or rcheevos'
// RC_HASH_CDTRACK_* (-1 first data, -2 last, -3 largest, -4 first of the
// second session); -1 when there is none.
int ChdTrack_find(const chd_track_info_t* tracks, int num_tracks, uint32_t track_request);

// Clamps *data_size so header_skip + data stays inside one frame. 0 when the
// header alone does not fit (the sector is unreadable).
int ChdTrack_clampData(size_t frame_size, size_t header_skip, size_t* data_size);
