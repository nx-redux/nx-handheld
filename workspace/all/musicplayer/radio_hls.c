#define _GNU_SOURCE
#include "radio_hls.h"
#include "radio_net.h"
#include "tag_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MPEG-TS constants
#define TS_PACKET_SIZE 188
#define TS_SYNC_BYTE 0x47
#define TS_PAT_PID 0x0000

// Check if URL is an HLS stream
bool radio_hls_is_url(const char* url) {
	const char* ext = strrchr(url, '.');
	if (ext && strcasecmp(ext, ".m3u8") == 0)
		return true;
	// Also check for m3u8 in query string
	if (strstr(url, ".m3u8") != NULL)
		return true;
	return false;
}

// Extract base URL from full URL (for resolving relative paths)
void radio_hls_get_base_url(const char* url, char* base, int base_size) {
	strncpy(base, url, base_size - 1);
	base[base_size - 1] = '\0';

	// Find last slash after the host (a "/" inside "scheme://" is not one,
	// and a short host such as "http://a/x.m3u8" still has its path cut)
	char* host = strstr(base, "://");
	char* last_slash = strrchr(host ? host + 3 : base, '/');
	if (last_slash) {
		*(last_slash + 1) = '\0';
	}
}

// Resolve a potentially relative URL
void radio_hls_resolve_url(const char* base, const char* relative, char* result, int result_size) {
	if (strncmp(relative, "http://", 7) == 0 || strncmp(relative, "https://", 8) == 0) {
		// Absolute URL
		strncpy(result, relative, result_size - 1);
		result[result_size - 1] = '\0';
	} else if (relative[0] == '/') {
		// Root-relative URL - extract host from base
		const char* host_start = strstr(base, "://");
		if (host_start) {
			host_start += 3;
			const char* host_end = strchr(host_start, '/');
			if (host_end) {
				// cut to result_size (strncpy of the host part overran a
				// result buffer shorter than it)
				snprintf(result, result_size, "%.*s%s", (int)(host_end - base), base, relative);
			} else {
				snprintf(result, result_size, "%s%s", base, relative);
			}
		} else {
			// base has no scheme — fall back to the relative path itself rather
			// than leaving `result` unwritten (stale) for the caller.
			snprintf(result, result_size, "%s", relative);
		}
	} else {
		// Relative URL
		snprintf(result, result_size, "%s%s", base, relative);
	}
}

// Copies the next line of an M3U8 body into buf (leading blanks skipped, cut
// at buf_size - 1). Returns the start of the line after it, or NULL at the end.
static const char* next_line(const char* p, char* buf, int buf_size) {
	if (!*p)
		return NULL;
	while (*p == ' ' || *p == '\t')
		p++;
	const char* eol = p;
	while (*eol && *eol != '\n' && *eol != '\r')
		eol++;
	int len = eol - p;
	if (len >= buf_size)
		len = buf_size - 1;
	memcpy(buf, p, len);
	buf[len] = '\0';
	while (*eol == '\n' || *eol == '\r')
		eol++;
	return eol;
}

// Counts the media segments of a playlist body and reads the tags the segment
// window depends on (first pass, before anything is stored).
static void scan_playlist(const char* content, int* count, int* media_sequence, bool* has_endlist) {
	char line_buf[HLS_MAX_URL_LEN];
	*count = 0;
	*media_sequence = 0;
	*has_endlist = false;
	for (const char* p = content; (p = next_line(p, line_buf, sizeof(line_buf)));) {
		if (strncmp(line_buf, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0)
			*media_sequence = atoi(line_buf + 22);
		else if (strncmp(line_buf, "#EXT-X-ENDLIST", 14) == 0)
			*has_endlist = true;
		else if (line_buf[0] != '#' && line_buf[0] != '\0')
			(*count)++;
	}
}

int radio_hls_window_skip(int count, int media_sequence, bool is_live, int last_played_sequence) {
	if (count <= HLS_MAX_SEGMENTS && (is_live || last_played_sequence < 0))
		return 0;
	int skip;
	if (is_live) {
		// a live window longer than the cap: keep its newest end
		skip = count - HLS_MAX_SEGMENTS;
	} else {
		// a long finished playlist is played in windows: resume after the last
		// segment played
		skip = last_played_sequence < 0 ? 0 : last_played_sequence + 1 - media_sequence;
	}
	if (skip < 0)
		skip = 0;
	if (skip > count)
		skip = count;
	return skip;
}

int radio_hls_live_start_index(const HLSContext* ctx) {
	// Start a live stream three segments from its end (as HLS players do), not
	// at the oldest segment the server still lists: that one is minutes behind
	// live and may expire before it is reached.
	if (!ctx->is_live || ctx->segment_count <= 3)
		return 0;
	return ctx->segment_count - 3;
}

char* radio_hls_fetch_playlist(const char* url) {
	uint8_t* buf = malloc(HLS_PLAYLIST_BUF_SIZE);
	if (!buf)
		return NULL;
	int len = radio_net_fetch(url, buf, HLS_PLAYLIST_BUF_SIZE, NULL, 0);
	if (len <= 0) {
		free(buf);
		return NULL;
	}
	if (len >= HLS_PLAYLIST_BUF_SIZE)
		len = HLS_PLAYLIST_BUF_SIZE - 1;
	buf[len] = '\0';
	if (len == HLS_PLAYLIST_BUF_SIZE - 1) {
		// cut off: drop the partial last line (a half URL is not a segment)
		char* nl = strrchr((char*)buf, '\n');
		if (nl)
			nl[1] = '\0';
	}
	return (char*)buf;
}

// Parse M3U8 playlist content
int radio_hls_parse_playlist(HLSContext* ctx, const char* content, const char* base_url) {
	ctx->segment_count = 0;
	ctx->is_live = true; // Assume live until we see ENDLIST
	ctx->target_duration = 10.0f;
	ctx->media_sequence = 0;
	ctx->encrypted = false;
	ctx->has_more = false;

	// the master-playlist recursion passes ctx->base_url itself
	if (base_url != ctx->base_url)
		snprintf(ctx->base_url, HLS_MAX_URL_LEN, "%s", base_url);

	// Only HLS_MAX_SEGMENTS are kept: pick which ones before storing any
	int total = 0, first_sequence = 0;
	bool has_endlist = false;
	scan_playlist(content, &total, &first_sequence, &has_endlist);
	int skip = radio_hls_window_skip(total, first_sequence, !has_endlist, ctx->last_played_sequence);
	int seen = 0;

	const char* line = content;
	float segment_duration = 0;
	char segment_title[128] = "";
	char segment_artist[128] = "";
	char variant_url[HLS_MAX_URL_LEN] = "";
	bool is_master_playlist = false;

	char line_buf[HLS_MAX_URL_LEN];
	while ((line = next_line(line, line_buf, sizeof(line_buf)))) {
		if (line_buf[0] != '\0') {
			if (strncmp(line_buf, "#EXTM3U", 7) == 0) {
				// Valid M3U8 header
			} else if (strncmp(line_buf, "#EXT-X-KEY:", 11) == 0) {
				// Encrypted segments (AES-128 / SAMPLE-AES): we can't decrypt,
				// so mark the stream so the caller can fail with a clear error
				// instead of feeding ciphertext to the decoder as noise.
				if (!strstr(line_buf, "METHOD=NONE"))
					ctx->encrypted = true;
			} else if (strncmp(line_buf, "#EXT-X-STREAM-INF:", 18) == 0) {
				// Master playlist - we need to fetch the variant
				is_master_playlist = true;
			} else if (strncmp(line_buf, "#EXT-X-TARGETDURATION:", 22) == 0) {
				ctx->target_duration = atof(line_buf + 22);
			} else if (strncmp(line_buf, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
				ctx->media_sequence = atoi(line_buf + 22);
			} else if (strncmp(line_buf, "#EXTINF:", 8) == 0) {
				// Segment duration and optional metadata
				segment_duration = atof(line_buf + 8);
				segment_title[0] = '\0';
				segment_artist[0] = '\0';

				// Parse title="..."
				char* title_start = strstr(line_buf, "title=\"");
				if (title_start) {
					title_start += 7;
					char* title_end = strchr(title_start, '"');
					if (title_end) {
						int len = title_end - title_start;
						if (len > 127)
							len = 127;
						strncpy(segment_title, title_start, len);
						segment_title[len] = '\0';
					}
				}

				// Parse artist="..."
				char* artist_start = strstr(line_buf, "artist=\"");
				if (artist_start) {
					artist_start += 8;
					char* artist_end = strchr(artist_start, '"');
					if (artist_end) {
						int len = artist_end - artist_start;
						if (len > 127)
							len = 127;
						strncpy(segment_artist, artist_start, len);
						segment_artist[len] = '\0';
					}
				}
			} else if (strncmp(line_buf, "#EXT-X-ENDLIST", 14) == 0) {
				ctx->is_live = false;
			} else if (line_buf[0] != '#' && line_buf[0] != '\0') {
				// This is a URL
				if (is_master_playlist && variant_url[0] == '\0') {
					// Save first variant URL
					radio_hls_resolve_url(ctx->base_url, line_buf, variant_url, HLS_MAX_URL_LEN);
				} else if (!is_master_playlist) {
					// Media segment (outside the kept window: skipped)
					if (seen++ >= skip && ctx->segment_count < HLS_MAX_SEGMENTS) {
						HLSSegment* seg = &ctx->segments[ctx->segment_count];
						radio_hls_resolve_url(ctx->base_url, line_buf, seg->url, HLS_MAX_URL_LEN);
						seg->duration = segment_duration;
						snprintf(seg->title, sizeof(seg->title), "%s", segment_title);
						snprintf(seg->artist, sizeof(seg->artist), "%s", segment_artist);
						ctx->segment_count++;
					}
					segment_duration = 0;
					segment_title[0] = '\0';
					segment_artist[0] = '\0';
				}
			}
		}
	}

	// segment 0 is the first kept one: its sequence number moves with the
	// window, and later segments are fetched on a re-read of the playlist
	if (!is_master_playlist) {
		ctx->media_sequence += skip;
		ctx->has_more = !ctx->is_live && skip + ctx->segment_count < total;
	}

	// If master playlist, fetch the variant playlist. Bound the recursion so a
	// hostile/mis-authored playlist that points to another master (or itself)
	// can't recurse unboundedly (playlist fetch + stack frame per level). Single
	// stream thread, so a static depth guard is sufficient.
	if (is_master_playlist && variant_url[0]) {
		static int master_depth = 0;
		if (master_depth < 4) {
			char* playlist = radio_hls_fetch_playlist(variant_url);
			if (playlist) {
				// Update base URL for variant
				radio_hls_get_base_url(variant_url, ctx->base_url, HLS_MAX_URL_LEN);
				master_depth++;
				radio_hls_parse_playlist(ctx, playlist, ctx->base_url);
				master_depth--;
				free(playlist);
			}
		}
	}

	return ctx->segment_count;
}

// Parse ID3 tags from HLS segment
int radio_hls_parse_id3_metadata(const uint8_t* data, int len,
								 char* artist, int artist_size,
								 char* title, int title_size) {
	// ID3 tag header: "ID3" + version (2 bytes) + flags (1 byte) + size (4 bytes syncsafe)
	if (len < 10)
		return 0;
	if (data[0] != 'I' || data[1] != 'D' || data[2] != '3')
		return 0;

	uint8_t version_major = data[3];

	// Parse syncsafe size (4 bytes, 7 bits each)
	uint32_t tag_size = ((data[6] & 0x7F) << 21) |
						((data[7] & 0x7F) << 14) |
						((data[8] & 0x7F) << 7) |
						(data[9] & 0x7F);

	// Check for integer overflow (tag_size + 10 could wrap)
	if (tag_size > (uint32_t)(len - 10))
		return 0; // Tag size exceeds available data

	int total_size = 10 + (int)tag_size;
	if (total_size > len || total_size < 10)
		return 0; // Incomplete tag or overflow

	// Initialize output
	if (artist && artist_size > 0)
		artist[0] = '\0';
	if (title && title_size > 0)
		title[0] = '\0';

	// Parse ID3 frames
	int pos = 10;
	while (pos + 10 < total_size) {
		// Frame header: ID (4 bytes) + size (4 bytes) + flags (2 bytes)
		char frame_id[5] = {data[pos], data[pos + 1], data[pos + 2], data[pos + 3], 0};

		// Frame size: syncsafe in ID3v2.4, regular in ID3v2.3
		uint32_t frame_size;
		if (version_major >= 4) {
			frame_size = ((data[pos + 4] & 0x7F) << 21) | ((data[pos + 5] & 0x7F) << 14) |
						 ((data[pos + 6] & 0x7F) << 7) | (data[pos + 7] & 0x7F);
		} else {
			frame_size = (data[pos + 4] << 24) | (data[pos + 5] << 16) |
						 (data[pos + 6] << 8) | data[pos + 7];
		}

		// Bound frame_size against the remaining bytes WITHOUT adding to it:
		// for ID3v2.3 frame_size is a raw 32-bit field, so `pos + 10 +
		// frame_size` would wrap for values near 2^32 and pass this check,
		// making `frame_size - 1` a huge memcpy length. `pos + 10 < total_size`
		// (loop guard) makes the subtraction safely positive.
		if (frame_size == 0 || frame_size > (uint32_t)(total_size - pos - 10))
			break;

		const uint8_t* frame_data = &data[pos + 10];

		// TIT2 = Title
		if (strcmp(frame_id, "TIT2") == 0 && frame_size > 1 && title && title_size > 0) {
			TagMeta_id3Text(frame_data[0], &frame_data[1], frame_size - 1, title, title_size);
		}
		// TPE1 = Artist
		else if (strcmp(frame_id, "TPE1") == 0 && frame_size > 1 && artist && artist_size > 0) {
			TagMeta_id3Text(frame_data[0], &frame_data[1], frame_size - 1, artist, artist_size);
		}
		// TXXX = User-defined text: encoding(1) description value (may carry StreamTitle)
		else if (strcmp(frame_id, "TXXX") == 0 && frame_size > 1 && title && title_size > 0) {
			uint8_t encoding = frame_data[0];
			const uint8_t* body = &frame_data[1];
			uint32_t body_len = frame_size - 1;

			// Find the end of the description: one NUL byte, or a 2-byte NUL
			// on a code-unit boundary for the UTF-16 encodings
			bool wide = encoding == 1 || encoding == 2;
			uint32_t desc_end = 0;
			while (desc_end < body_len) {
				if (wide) {
					if (desc_end + 1 < body_len && body[desc_end] == 0 && body[desc_end + 1] == 0)
						break;
					desc_end += 2;
				} else {
					if (body[desc_end] == 0)
						break;
					desc_end++;
				}
			}
			uint32_t value_start = desc_end + (wide ? 2 : 1);

			char desc[256];
			TagMeta_id3Text(encoding, body, desc_end < body_len ? desc_end : body_len, desc, sizeof(desc));
			if (value_start < body_len && (strstr(desc, "StreamTitle") || strstr(desc, "TITLE"))) {
				TagMeta_id3Text(encoding, body + value_start, body_len - value_start, title, title_size);

				// Try to parse "Artist - Title" format
				char* separator = strstr(title, " - ");
				if (separator && artist && artist_size > 0) {
					*separator = '\0';
					strncpy(artist, title, artist_size - 1);
					artist[artist_size - 1] = '\0';
					memmove(title, separator + 3, strlen(separator + 3) + 1);
				}
			}
		}
		// PRIV = Private frame (some streams put metadata here)
		else if (strcmp(frame_id, "PRIV") == 0 && frame_size > 0 && title && title_size > 0) {
			char priv_data[512];
			int copy_len = frame_size < sizeof(priv_data) - 1 ? frame_size : sizeof(priv_data) - 1;
			memcpy(priv_data, frame_data, copy_len);
			priv_data[copy_len] = '\0';

			char* title_start = strstr(priv_data, "StreamTitle='");
			if (title_start) {
				title_start += 13;
				char* title_end = strchr(title_start, '\'');
				if (title_end) {
					TagMeta_streamText(title_start, title_end - title_start, title, title_size);

					char* separator = strstr(title, " - ");
					if (separator && artist && artist_size > 0) {
						*separator = '\0';
						strncpy(artist, title, artist_size - 1);
						artist[artist_size - 1] = '\0';
						memmove(title, separator + 3, strlen(separator + 3) + 1);
					}
				}
			}
		}

		pos += 10 + frame_size;
	}

	return total_size;
}

// Extract AAC audio from MPEG-TS data
int radio_hls_demux_ts(const uint8_t* ts_data, int ts_len,
					   uint8_t* aac_out, int aac_out_size,
					   int* audio_pid, bool* pid_detected) {
	int aac_pos = 0;
	int pmt_pid = -1;
	int local_audio_pid = -1;

	// Use cached audio PID if available
	if (pid_detected && *pid_detected && audio_pid) {
		local_audio_pid = *audio_pid;
	}

	int pos = 0;
	while (pos + TS_PACKET_SIZE <= ts_len && aac_pos < aac_out_size - 1024) {
		// Find sync byte
		while (pos < ts_len && ts_data[pos] != TS_SYNC_BYTE) {
			pos++;
		}

		if (pos + TS_PACKET_SIZE > ts_len)
			break;

		const uint8_t* pkt = &ts_data[pos];

		// Parse TS header
		int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
		int payload_start = (pkt[1] & 0x40) != 0;
		int adaptation_field = (pkt[3] >> 4) & 0x03;

		int header_len = 4;
		if (adaptation_field == 2 || adaptation_field == 3) {
			// Bounds check for adaptation field length byte
			if (4 >= TS_PACKET_SIZE)
				break;
			int adapt_len = pkt[4];
			// Validate adaptation field length doesn't exceed packet
			if (adapt_len > TS_PACKET_SIZE - 5)
				break;
			header_len += 1 + adapt_len; // Skip adaptation field
		}

		if (adaptation_field == 1 || adaptation_field == 3) {
			// Has payload - validate header_len first
			if (header_len >= TS_PACKET_SIZE) {
				pos += TS_PACKET_SIZE;
				continue; // Invalid packet, skip
			}
			const uint8_t* payload = pkt + header_len;
			int payload_len = TS_PACKET_SIZE - header_len;

			if (pid == TS_PAT_PID && payload_start && !(pid_detected && *pid_detected)) {
				// Parse PAT to find PMT PID. Need pat[0..11] readable, so
				// require 12 bytes past section_start (the old `+8` check let
				// pat[10]/pat[11] read up to 3 bytes past the packet).
				int section_start = payload[0] + 1;
				if (section_start + 12 <= payload_len) {
					const uint8_t* pat = payload + section_start;
					if (pat[0] == 0x00) { // table_id for PAT
						int section_len = ((pat[1] & 0x0F) << 8) | pat[2];
						if (section_len >= 9) {
							pmt_pid = ((pat[10] & 0x1F) << 8) | pat[11];
						}
					}
				}
			} else if (pmt_pid > 0 && pid == pmt_pid && payload_start && !(pid_detected && *pid_detected)) {
				// Parse PMT to find audio stream PID
				int section_start = payload[0] + 1;
				if (section_start + 12 < payload_len) {
					const uint8_t* pmt = payload + section_start;
					if (pmt[0] == 0x02) { // table_id for PMT
						int section_len = ((pmt[1] & 0x0F) << 8) | pmt[2];
						int prog_info_len = ((pmt[10] & 0x0F) << 8) | pmt[11];

						int es_pos = 12 + prog_info_len;
						// Bound the ES walk by the ACTUAL packet payload, not
						// just the wire-supplied section_len (up to 4095) — pmt
						// points inside a ≤184-byte payload, so trusting
						// section_len reads far past the packet/segment buffer.
						int es_avail = payload_len - section_start;
						while (es_pos + 5 <= section_len + 3 - 4 &&
							   es_pos + 5 <= es_avail) {
							int stream_type = pmt[es_pos];
							int es_pid = ((pmt[es_pos + 1] & 0x1F) << 8) | pmt[es_pos + 2];
							int es_info_len = ((pmt[es_pos + 3] & 0x0F) << 8) | pmt[es_pos + 4];

							// AAC stream types: 0x0F (ADTS), 0x11 (LATM)
							if (stream_type == 0x0F || stream_type == 0x11) {
								local_audio_pid = es_pid;
								if (audio_pid)
									*audio_pid = local_audio_pid;
								if (pid_detected)
									*pid_detected = true;
								break;
							}
							// Also check for MP3 (0x03, 0x04)
							if (stream_type == 0x03 || stream_type == 0x04) {
								local_audio_pid = es_pid;
								if (audio_pid)
									*audio_pid = local_audio_pid;
								if (pid_detected)
									*pid_detected = true;
								break;
							}

							es_pos += 5 + es_info_len;
						}
					}
				}
			} else if (local_audio_pid > 0 && pid == local_audio_pid) {
				// Extract audio data from PES packet
				const uint8_t* pes = payload;
				int pes_len = payload_len;

				if (payload_start && pes_len >= 9) {
					// Check PES start code (need pes[0..8] readable)
					if (pes[0] == 0x00 && pes[1] == 0x00 && pes[2] == 0x01) {
						// Parse PES header
						int pes_header_len = 9 + pes[8];
						if (pes_header_len < pes_len) {
							int audio_len = pes_len - pes_header_len;
							if (aac_pos + audio_len < aac_out_size) {
								memcpy(aac_out + aac_pos, pes + pes_header_len, audio_len);
								aac_pos += audio_len;
							}
						}
					}
				} else {
					// Continuation of PES packet - raw audio data
					if (aac_pos + pes_len < aac_out_size) {
						memcpy(aac_out + aac_pos, pes, pes_len);
						aac_pos += pes_len;
					}
				}
			}
		}

		pos += TS_PACKET_SIZE;
	}

	return aac_pos;
}
