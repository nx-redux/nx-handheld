/**
 * chd_reader.c - CHD file reader for rcheevos hashing
 * 
 * Provides CD reader callbacks for rcheevos to hash CHD disc images.
 */

#include "chd_reader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include "chd_tracks.h"

/*****************************************************************************
 * CHD track handle
 *****************************************************************************/

typedef struct {
	chd_file* chd;
	uint32_t track_num;

	// Track info
	chd_track_info_t tracks[CD_MAX_TRACKS];
	int num_tracks;

	// Current track info
	int track_start_frame;	  // First frame of this track in CHD
	uint32_t track_lba_start; // Absolute LBA rcheevos addresses this track by
	int track_frames;		  // Number of frames in track
	int track_type;			  // CD_TRACK_* type
	int track_pregap;		  // Pregap frames for this track

	// Sector format info (determined from track type)
	int sector_header_size; // Bytes to skip to reach raw data (0, 16, or 24)
	int raw_data_size;		// Size of raw data (2048 or 2352 for audio)

	// Hunk info
	uint32_t hunk_bytes;
	uint32_t frame_size; // Bytes per frame (typically 2448 or 2352)
	int frames_per_hunk;

	// Sector buffer
	uint8_t* hunk_buffer;
	uint32_t cached_hunk;
} chd_track_handle_t;


/*****************************************************************************
 * Helper: Parse CHD track metadata
 *****************************************************************************/

static int parse_chd_tracks(chd_file* chd, chd_track_info_t* tracks, int* num_tracks) {
	char metadata[256];
	uint32_t metadata_size;
	int track_idx = 0;
	int cumulative_frames = 0;

	// Try CDROM_TRACK_METADATA2 first (newer format with pregap info)
	for (int i = 0; i < CD_MAX_TRACKS; i++) {
		int is_gdrom = 0;
		chd_error err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, i,
										 metadata, sizeof(metadata),
										 &metadata_size, NULL, NULL);
		if (err != CHDERR_NONE) {
			// Try older format
			err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, i,
								   metadata, sizeof(metadata),
								   &metadata_size, NULL, NULL);
			if (err != CHDERR_NONE) {
				// Try GD-ROM format
				err = chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, i,
									   metadata, sizeof(metadata),
									   &metadata_size, NULL, NULL);
				if (err != CHDERR_NONE)
					break;
				is_gdrom = 1;
			}
		}

		// chd_get_metadata reports the full entry length, not the bytes copied
		if (metadata_size >= sizeof(metadata))
			metadata_size = sizeof(metadata) - 1;
		metadata[metadata_size] = '\0';

		if (ChdTrack_parseMetadata(metadata, is_gdrom, &tracks[track_idx], &cumulative_frames))
			track_idx++;
	}

	ChdTrack_assignLbas(tracks, track_idx);

	*num_tracks = track_idx;
	return track_idx > 0 ? 0 : -1;
}


/*****************************************************************************
 * CD Reader callbacks
 *****************************************************************************/

void* chd_open_track(const char* path, uint32_t track) {
	return chd_open_track_iterator(path, track, NULL);
}

void* chd_open_track_iterator(const char* path, uint32_t track, const void* iterator) {
	(void)iterator; // Unused

	// Check if this is a CHD file
	const char* ext = strrchr(path, '.');
	if (!ext || strcasecmp(ext, ".chd") != 0) {
		return NULL; // Not a CHD file, let default reader handle it
	}

	chd_file* chd = NULL;
	chd_error err = chd_open(path, CHD_OPEN_READ, NULL, &chd);
	if (err != CHDERR_NONE) {
		return NULL;
	}

	// Allocate handle
	chd_track_handle_t* handle = (chd_track_handle_t*)calloc(1, sizeof(chd_track_handle_t));
	if (!handle) {
		chd_close(chd);
		return NULL;
	}

	handle->chd = chd;
	handle->track_num = track;
	handle->cached_hunk = (uint32_t)-1;

	// Get hunk info
	const chd_header* header = chd_get_header(chd);
	handle->hunk_bytes = header->hunkbytes;

	// CD frames are typically 2448 bytes (2352 sector + 96 subcode) or 2352 bytes
	// Check unit bytes if available, otherwise assume CD_FRAME_SIZE
	handle->frame_size = header->unitbytes ? header->unitbytes : CD_FRAME_SIZE;
	handle->frames_per_hunk = handle->hunk_bytes / handle->frame_size;
	if (handle->frames_per_hunk == 0) {
		// malformed/non-CD CHD (unitbytes > hunkbytes) — would divide by zero
		// in chd_read_sector
		chd_close(chd);
		free(handle);
		return NULL;
	}

	// Allocate hunk buffer
	handle->hunk_buffer = (uint8_t*)malloc(handle->hunk_bytes);
	if (!handle->hunk_buffer) {
		chd_close(chd);
		free(handle);
		return NULL;
	}

	// Parse track metadata
	if (parse_chd_tracks(chd, handle->tracks, &handle->num_tracks) != 0) {
		free(handle->hunk_buffer);
		chd_close(chd);
		free(handle);
		return NULL;
	}

	// Find requested track
	int track_idx = ChdTrack_find(handle->tracks, handle->num_tracks, track);
	if (track_idx < 0) {
		free(handle->hunk_buffer);
		chd_close(chd);
		free(handle);
		return NULL;
	}

	handle->track_num = track_idx + 1; // Convert to 1-based
	handle->track_start_frame = handle->tracks[track_idx].start_frame;
	handle->track_lba_start = (uint32_t)handle->tracks[track_idx].lba_start;
	handle->track_frames = handle->tracks[track_idx].frames;
	handle->track_type = handle->tracks[track_idx].type;
	handle->track_pregap = handle->tracks[track_idx].pregap_frames;

	// Determine sector format based on track type
	ChdTrack_sectorFormat(handle->track_type, handle->frame_size, &handle->sector_header_size,
						  &handle->raw_data_size);

	return handle;
}

size_t chd_read_sector(void* track_handle, uint32_t sector, void* buffer, size_t requested_bytes) {
	chd_track_handle_t* handle = (chd_track_handle_t*)track_handle;
	if (!handle || !handle->chd)
		return 0;

	// Convert the absolute disc LBA rcheevos hands us into a CHD frame number:
	// rcheevos reads either first_track_sector() + offset or an absolute LBA
	// it found in the ISO9660 directory, and both are relative to this
	// track's lba_start (see parse_chd_tracks), so subtract it and add the
	// track's frame offset inside the CHD.
	//
	// IMPORTANT: CHD allocates frames sequentially including pregap frames.
	// Even if PGTYPE='V' (virtual/silence), the frames are still allocated.
	// The FRAMES metadata field is the actual data frames AFTER pregap.
	// So we must always skip over pregap frames to reach the actual data.
	if (sector < handle->track_lba_start)
		return 0; // before this track: not addressable through this handle
	uint32_t frame = handle->track_start_frame + (sector - handle->track_lba_start);

	// Always skip pregap for data tracks - the pregap frames are allocated
	// in the CHD regardless of whether they contain real data or silence.
	if (ChdTrack_isData(handle->track_type) && handle->track_pregap > 0) {
		frame += handle->track_pregap;
	}

	// Calculate which hunk contains this frame
	uint32_t hunk_num = frame / handle->frames_per_hunk;
	uint32_t frame_in_hunk = frame % handle->frames_per_hunk;

	// Read hunk if not cached
	if (hunk_num != handle->cached_hunk) {
		chd_error err = chd_read(handle->chd, hunk_num, handle->hunk_buffer);
		if (err != CHDERR_NONE) {
			return 0;
		}
		handle->cached_hunk = hunk_num;
	}

	// Calculate offset into hunk
	uint32_t offset = frame_in_hunk * handle->frame_size;
	uint8_t* src = handle->hunk_buffer + offset;

	// Use pre-calculated sector format from track type
	size_t header_skip = handle->sector_header_size;
	size_t data_size = handle->raw_data_size;

	// For raw sectors, verify sync pattern and allow per-sector mode override
	// This handles discs with mixed mode sectors
	if (handle->frame_size >= 2352 && header_skip > 0) {
		// Check for sync pattern: 00 FF FF FF FF FF FF FF FF FF FF 00
		if (src[0] == 0x00 && src[1] == 0xFF && src[11] == 0x00) {
			// Sync pattern found at offset 0 - standard layout
			int mode = src[15];
			if (mode == 1) {
				header_skip = 16; // Sync(12) + Header(4)
				data_size = 2048;
			} else if (mode == 2) {
				header_skip = 24; // Sync(12) + Header(4) + Subheader(8)
				data_size = 2048;
			}
		} else if (handle->frame_size == 2448 &&
				   src[96] == 0x00 && src[97] == 0xFF && src[107] == 0x00) {
			// Sync pattern found at offset 96 - subcode-first layout
			int mode = src[96 + 15];
			src += 96; // Adjust source pointer to skip subcode

			if (mode == 1) {
				header_skip = 16;
				data_size = 2048;
			} else if (mode == 2) {
				header_skip = 24;
				data_size = 2048;
			}
		}
	}

	// Clamp to the physical frame: a hostile CHD can declare a unitbytes
	// smaller than the track type's nominal data size (e.g. an audio track
	// with frame_size < 2352), which would over-read the hunk buffer.
	if (!ChdTrack_clampData(handle->frame_size, header_skip, &data_size))
		return 0;

	// Copy the data
	size_t to_copy = requested_bytes;
	if (to_copy > data_size)
		to_copy = data_size;

	memcpy(buffer, src + header_skip, to_copy);

	return to_copy;
}

void chd_close_track(void* track_handle) {
	chd_track_handle_t* handle = (chd_track_handle_t*)track_handle;
	if (!handle)
		return;

	if (handle->hunk_buffer)
		free(handle->hunk_buffer);

	if (handle->chd)
		chd_close(handle->chd);

	free(handle);
}

uint32_t chd_first_track_sector(void* track_handle) {
	chd_track_handle_t* handle = (chd_track_handle_t*)track_handle;
	if (!handle)
		return 0;

	// Absolute disc LBA of the track (0 for track 1 of a CD, 45000 for the
	// first high-density track of a GD-ROM); chd_read_sector expects LBAs
	// on the same basis. See parse_chd_tracks.
	return handle->track_lba_start;
}
