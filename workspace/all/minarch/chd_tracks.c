#include "chd_tracks.h"
#include <stdio.h>
#include <string.h>

/*****************************************************************************
 * Helper: Parse track type string
 *****************************************************************************/

int ChdTrack_parseType(const char* type_str) {
	if (strcmp(type_str, "MODE1") == 0)
		return CD_TRACK_MODE1;
	if (strcmp(type_str, "MODE1_RAW") == 0 || strcmp(type_str, "MODE1/2352") == 0)
		return CD_TRACK_MODE1_RAW;
	if (strcmp(type_str, "MODE2") == 0)
		return CD_TRACK_MODE2;
	if (strcmp(type_str, "MODE2_FORM1") == 0)
		return CD_TRACK_MODE2_FORM1;
	if (strcmp(type_str, "MODE2_FORM2") == 0)
		return CD_TRACK_MODE2_FORM2;
	if (strcmp(type_str, "MODE2_FORM_MIX") == 0)
		return CD_TRACK_MODE2_FORM_MIX;
	if (strcmp(type_str, "MODE2_RAW") == 0 || strcmp(type_str, "MODE2/2352") == 0)
		return CD_TRACK_MODE2_RAW;
	if (strcmp(type_str, "AUDIO") == 0)
		return CD_TRACK_AUDIO;

	return CD_TRACK_MODE1; // Default
}

/*****************************************************************************
 * Helper: Check if track is data track
 *****************************************************************************/

int ChdTrack_isData(int type) {
	return type != CD_TRACK_AUDIO;
}

/*****************************************************************************
 * Helper: Determine sector header size from track type
 * 
 * Based on rcheevos cdreader.c logic:
 * - MODE1/2352 (MODE1_RAW): 16 bytes (12 sync + 4 header)
 * - MODE2/2352 (MODE2_RAW): 24 bytes (12 sync + 4 header + 8 subheader)
 * - MODE1/2048, MODE2/2048: 0 bytes (cooked)
 * - AUDIO: 0 bytes (raw audio)
 *****************************************************************************/

void ChdTrack_sectorFormat(int track_type, int frame_size, int* header_size, int* data_size) {
	// Default: cooked 2048-byte sectors
	*data_size = 2048;
	*header_size = 0;

	switch (track_type) {
	case CD_TRACK_MODE1_RAW:
		// MODE1/2352: sync(12) + header(4) + data(2048) + ecc(288)
		if (frame_size >= 2352) {
			*header_size = 16;
			*data_size = 2048;
		}
		break;

	case CD_TRACK_MODE2_RAW:
	case CD_TRACK_MODE2_FORM_MIX:
		// MODE2/2352: sync(12) + header(4) + subheader(8) + data(2048) + ecc(280)
		if (frame_size >= 2352) {
			*header_size = 24;
			*data_size = 2048;
		}
		break;

	case CD_TRACK_MODE2_FORM1:
	case CD_TRACK_MODE2_FORM2:
		// MODE2 without sync: subheader(8) + data
		if (frame_size == 2336) {
			*header_size = 8;
			*data_size = 2048;
		}
		break;

	case CD_TRACK_MODE1:
	case CD_TRACK_MODE2:
		// Cooked sectors, no header
		*header_size = 0;
		*data_size = 2048;
		break;

	case CD_TRACK_AUDIO:
		// Audio tracks have no header, all 2352 bytes are data
		*header_size = 0;
		*data_size = 2352;
		break;

	default:
		// Unknown type, try to determine from frame size
		if (frame_size >= 2352) {
			// Assume raw with MODE1 header
			*header_size = 16;
			*data_size = 2048;
		}
		break;
	}
}

/*****************************************************************************
 * Helper: Find requested track
 *****************************************************************************/

int ChdTrack_find(const chd_track_info_t* tracks, int num_tracks, uint32_t track_request) {
	if (num_tracks == 0)
		return -1;

	// Handle special track requests
	if (track_request == (uint32_t)-1) {
		// RC_HASH_CDTRACK_FIRST_DATA - first data track
		for (int i = 0; i < num_tracks; i++) {
			if (ChdTrack_isData(tracks[i].type))
				return i;
		}
		return -1;
	} else if (track_request == (uint32_t)-2) {
		// RC_HASH_CDTRACK_LAST - last track
		return num_tracks - 1;
	} else if (track_request == (uint32_t)-3) {
		// RC_HASH_CDTRACK_LARGEST - largest track
		int largest_idx = 0;
		int largest_frames = 0;
		for (int i = 0; i < num_tracks; i++) {
			if (tracks[i].frames > largest_frames) {
				largest_frames = tracks[i].frames;
				largest_idx = i;
			}
		}
		return largest_idx;
	} else if (track_request == (uint32_t)-4) {
		// RC_HASH_CDTRACK_FIRST_OF_SECOND_SESSION - CHD track metadata carries
		// no session numbers, so approximate: the first data track that follows
		// an audio track. That matches the Jaguar CD layout this request exists
		// for (session 1 is audio, session 2 opens with the boot data track);
		// a wrong pick fails safely in the caller's ATARI-header check.
		for (int i = 1; i < num_tracks; i++) {
			if (ChdTrack_isData(tracks[i].type) && !ChdTrack_isData(tracks[i - 1].type))
				return i;
		}
		return -1;
	} else if (track_request > 0 && track_request <= (uint32_t)num_tracks) {
		// Specific track number (1-based)
		return track_request - 1;
	}

	return -1;
}

/*****************************************************************************
 * Helper: Parse one track's CHD metadata string
 *****************************************************************************/

int ChdTrack_parseMetadata(const char* metadata, int is_gdrom, chd_track_info_t* out, int* cumulative_frames) {
	int track_num, frames, pad = 0, pregap = 0, postgap = 0;
	char type_str[32] = {0};
	char subtype_str[32] = {0};
	char pgtype_str[32] = {0};
	char pgsub_str[32] = {0};

	int parsed;
	if (is_gdrom) {
		// "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"
		parsed = sscanf(metadata, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d",
						&track_num, type_str, subtype_str, &frames, &pad, &pregap, pgtype_str, pgsub_str, &postgap);
	} else {
		// Full format 2: "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"
		parsed = sscanf(metadata, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d",
						&track_num, type_str, subtype_str, &frames, &pregap, pgtype_str, pgsub_str, &postgap);
	}

	if (parsed < 4) {
		// Try format 1 (no pregap info)
		parsed = sscanf(metadata, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d",
						&track_num, type_str, subtype_str, &frames);
		pregap = 0;
	}

	if (parsed < 4)
		return 0;

	out->type = ChdTrack_parseType(type_str);
	out->frames = frames;
	out->pregap_frames = pregap;
	out->postgap_frames = postgap;
	out->is_gdrom = is_gdrom;

	// CHD format: 'frames' is the actual data frames, NOT including pregap.
	// The cumulative frame_offset points to the first frame of this track's allocation.
	out->start_frame = *cumulative_frames;

	// Calculate padding for 4-frame alignment (per CHD spec)
	int padding = ((frames + 3) & ~3) - frames;
	*cumulative_frames += frames + padding;
	return 1;
}

// Absolute LBAs. rcheevos addresses sectors by disc LBA: first_track_sector()
// must return the track's LBA and read_sector() receives LBAs (ISO9660
// directory records hold absolute LBAs, and the Dreamcast hasher follows
// them). A CHD stores tracks back to back with no notion of disc position,
// so derive it: on a CD the track's frame offset doubles as its LBA (track
// 1 sits at 0, which is all the PS1/Sega CD/PC Engine hashers rely on). On
// a GD-ROM the high-density area (track 3 onward) always starts at LBA
// 45000 regardless of how short the low-density session was, so the
// ~44000-sector gap the CHD omits has to be added back or every ISO lookup
// in the data track lands outside the image.
void ChdTrack_assignLbas(chd_track_info_t* tracks, int num_tracks) {
	int gd_base = -1;
	for (int i = 0; i < num_tracks; i++) {
		if (tracks[i].is_gdrom && i + 1 >= 3) {
			if (gd_base < 0)
				gd_base = tracks[i].start_frame;
			tracks[i].lba_start = 45000 + (tracks[i].start_frame - gd_base);
		} else {
			tracks[i].lba_start = tracks[i].start_frame;
		}
	}
}

int ChdTrack_clampData(size_t frame_size, size_t header_skip, size_t* data_size) {
	if (header_skip >= frame_size)
		return 0;
	if (*data_size > frame_size - header_skip)
		*data_size = frame_size - header_skip;
	return 1;
}
