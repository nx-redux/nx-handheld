#define _FILE_OFFSET_BITS 64
#include "embedded_lyrics.h"
#include "tag_meta.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

// Caps against untrusted files on a small-RAM handheld: real lyrics are a few
// KB, and whole ID3v2 tags (art included) stay well under a few MB.
#define MAX_LYRICS_BYTES (512 * 1024)
#define MAX_ID3_TAG_BYTES (8 * 1024 * 1024)
#define MAX_OGG_PAGES 4096

// ---------------------------------------------------------------------------
// Growable UTF-8 string

typedef struct {
	char* p;
	size_t len;
	size_t cap;
	bool full; // hit MAX_LYRICS_BYTES, further appends are dropped
} StrBuf;

static void sb_putc(StrBuf* sb, char c) {
	if (!sb || sb->full)
		return;
	if (sb->len + 2 > sb->cap) {
		size_t cap = sb->cap ? sb->cap * 2 : 256;
		if (cap > MAX_LYRICS_BYTES) {
			sb->full = true;
			return;
		}
		char* p = realloc(sb->p, cap);
		if (!p) {
			sb->full = true;
			return;
		}
		sb->p = p;
		sb->cap = cap;
	}
	sb->p[sb->len++] = c;
	sb->p[sb->len] = '\0';
}

static void sb_append(StrBuf* sb, const char* s, size_t n) {
	for (size_t i = 0; i < n; i++)
		sb_putc(sb, s[i]);
}

static void sb_put_codepoint(StrBuf* sb, uint32_t cp) {
	if (cp < 0x80) {
		sb_putc(sb, (char)cp);
	} else if (cp < 0x800) {
		sb_putc(sb, (char)(0xC0 | (cp >> 6)));
		sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		sb_putc(sb, (char)(0xE0 | (cp >> 12)));
		sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
		sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
	} else {
		sb_putc(sb, (char)(0xF0 | (cp >> 18)));
		sb_putc(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
		sb_putc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
		sb_putc(sb, (char)(0x80 | (cp & 0x3F)));
	}
}

// Take ownership of the buffer contents (NULL when empty)
static char* sb_take(StrBuf* sb) {
	char* p = sb->p;
	if (p && sb->len == 0) {
		free(p);
		p = NULL;
	}
	sb->p = NULL;
	sb->len = sb->cap = 0;
	return p;
}

// ---------------------------------------------------------------------------
// Result: best lyrics seen so far (a timed candidate beats a plain one)

typedef struct {
	char* text;
	bool synced;
} Result;

bool EmbeddedLyrics_isLrc(const char* text) {
	const char* p = text;
	while (p && *p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (p[0] == '[' && p[1] >= '0' && p[1] <= '9') {
			const char* q = p + 1;
			while (*q >= '0' && *q <= '9')
				q++;
			if (q[0] == ':' && q[1] >= '0' && q[1] <= '9')
				return true;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return false;
}

// Normalise CRLF / CR line endings to LF in place, then drop surrounding
// blank space. Returns false when nothing is left.
static bool normalise_text(char* text) {
	char* w = text;
	for (const char* r = text; *r; r++) {
		if (*r == '\r') {
			*w++ = '\n';
			if (r[1] == '\n')
				r++;
		} else {
			*w++ = *r;
		}
	}
	*w = '\0';
	while (w > text && (w[-1] == '\n' || w[-1] == ' ' || w[-1] == '\t'))
		*--w = '\0';
	size_t lead = strspn(text, "\n \t");
	if (lead)
		memmove(text, text + lead, strlen(text + lead) + 1);
	return text[0] != '\0';
}

// Offer a candidate (takes ownership of text)
static void offer(Result* res, char* text) {
	if (!text)
		return;
	if (!normalise_text(text)) {
		free(text);
		return;
	}
	bool synced = EmbeddedLyrics_isLrc(text);
	if (!res->text || (synced && !res->synced)) {
		free(res->text);
		res->text = text;
		res->synced = synced;
	} else {
		free(text);
	}
}

static uint32_t be32(const uint8_t* p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t le32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t syncsafe32(const uint8_t* p) {
	return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
		   ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

// ---------------------------------------------------------------------------
// ID3v2.3 / 2.4 (MP3): SYLT and USLT frames

// Decode one ID3 string in encoding enc (0 Latin-1, 1 UTF-16 w/ BOM,
// 2 UTF-16BE, 3 UTF-8) from p, stopping at its terminator or len. Appends
// UTF-8 to sb (NULL to skip). Returns bytes consumed including terminator.
static size_t id3_decode_string(uint8_t enc, const uint8_t* p, size_t len, StrBuf* sb) {
	size_t i = 0;
	if (enc == 1 || enc == 2) {
		bool be = enc == 2;
		if (enc == 1 && len >= 2) {
			if (p[0] == 0xFE && p[1] == 0xFF) {
				be = true;
				i = 2;
			} else if (p[0] == 0xFF && p[1] == 0xFE) {
				i = 2;
			}
		}
		while (i + 1 < len) {
			uint32_t u = be ? ((uint32_t)p[i] << 8 | p[i + 1]) : ((uint32_t)p[i + 1] << 8 | p[i]);
			i += 2;
			if (u == 0)
				return i;
			if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) {
				uint32_t lo = be ? ((uint32_t)p[i] << 8 | p[i + 1]) : ((uint32_t)p[i + 1] << 8 | p[i]);
				if (lo >= 0xDC00 && lo < 0xE000) {
					i += 2;
					u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				}
			}
			sb_put_codepoint(sb, u);
		}
		return len;
	}
	while (i < len) {
		uint8_t c = p[i++];
		if (c == 0)
			return i;
		if (enc == 0)
			sb_put_codepoint(sb, c); // Latin-1 maps 1:1 onto U+0000..U+00FF
		else
			sb_putc(sb, (char)c);
	}
	return len;
}

// USLT: encoding(1) language(3) descriptor text
static char* id3_uslt(const uint8_t* d, size_t len) {
	if (len < 5)
		return NULL;
	uint8_t enc = d[0];
	size_t off = 4;
	off += id3_decode_string(enc, d + off, len - off, NULL);
	if (off >= len)
		return NULL;
	StrBuf sb = {0};
	id3_decode_string(enc, d + off, len - off, &sb);
	return sb_take(&sb);
}

typedef struct {
	char* text;
	uint32_t ms;
} SyltEntry;

static void append_lrc_line(StrBuf* out, uint32_t ms, const char* text) {
	char stamp[32];
	snprintf(stamp, sizeof(stamp), "[%02u:%02u.%02u]", ms / 60000, (ms / 1000) % 60, (ms % 1000) / 10);
	sb_append(out, stamp, strlen(stamp));
	// Keep the line on one row: inner newlines become spaces
	for (const char* p = text; *p; p++)
		sb_putc(out, (*p == '\n' || *p == '\r') ? ' ' : *p);
	sb_putc(out, '\n');
}

// SYLT: encoding(1) language(3) timestamp_format(1) content_type(1) descriptor
// then [text, timestamp(4)] pairs. Only millisecond timestamps (format 2) are
// usable; MPEG-frame timestamps need the bitstream and are skipped.
// Entries are whole lines unless the writer marks new lines with a leading
// newline (karaoke/syllable style), in which case entries are joined.
static char* id3_sylt(const uint8_t* d, size_t len) {
	if (len < 7 || d[4] != 2)
		return NULL;
	uint8_t enc = d[0];
	size_t off = 6;
	off += id3_decode_string(enc, d + off, len - off, NULL);

	SyltEntry* entries = NULL;
	size_t count = 0, cap = 0;
	while (off < len) {
		StrBuf sb = {0};
		off += id3_decode_string(enc, d + off, len - off, &sb);
		if (off + 4 > len) {
			free(sb.p);
			break;
		}
		uint32_t ms = be32(d + off);
		off += 4;
		if (count == cap) {
			cap = cap ? cap * 2 : 64;
			if (cap > 8192) {
				free(sb.p);
				break;
			}
			SyltEntry* grown = realloc(entries, cap * sizeof(SyltEntry));
			if (!grown) {
				free(sb.p);
				break;
			}
			entries = grown;
		}
		entries[count].text = sb.p ? sb.p : calloc(1, 1);
		entries[count].ms = ms;
		count++;
	}

	bool joined = false;
	for (size_t i = 1; i < count; i++) {
		if (entries[i].text && (entries[i].text[0] == '\n' || entries[i].text[0] == '\r'))
			joined = true;
	}

	StrBuf out = {0};
	StrBuf line = {0};
	uint32_t line_ms = 0;
	bool have_line = false;
	for (size_t i = 0; i < count; i++) {
		const char* t = entries[i].text ? entries[i].text : "";
		if (!joined) {
			append_lrc_line(&out, entries[i].ms, t + strspn(t, "\r\n"));
			continue;
		}
		if (!have_line || t[0] == '\n' || t[0] == '\r') {
			if (have_line)
				append_lrc_line(&out, line_ms, line.p ? line.p : "");
			line.len = 0;
			if (line.p)
				line.p[0] = '\0';
			line_ms = entries[i].ms;
			have_line = true;
			t += strspn(t, "\r\n");
		}
		sb_append(&line, t, strlen(t));
	}
	if (joined && have_line)
		append_lrc_line(&out, line_ms, line.p ? line.p : "");
	free(line.p);

	for (size_t i = 0; i < count; i++)
		free(entries[i].text);
	free(entries);
	return sb_take(&out);
}

// USLT / SYLT frames of an ID3 tag offer their lyrics
static void id3_lyrics_frame(const char* id, const uint8_t* data, size_t len, void* ctx) {
	if (strcmp(id, "SYLT") == 0)
		offer(ctx, id3_sylt(data, len));
	else if (strcmp(id, "USLT") == 0)
		offer(ctx, id3_uslt(data, len));
}

// Parse an ID3v2 tag at the start of f. *tag_end receives the offset just
// past the tag (0 when there is none) so a FLAC stream behind it can be found.
static void read_id3(FILE* f, Result* res, uint64_t* tag_end) {
	uint8_t h[10];
	*tag_end = 0;
	if (fseeko(f, 0, SEEK_SET) != 0 || fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3) != 0)
		return;
	uint8_t ver = h[3];
	uint8_t flags = h[5];
	uint32_t size = syncsafe32(h + 6);
	*tag_end = 10 + (uint64_t)size + ((flags & 0x10) ? 10 : 0);
	if ((ver != 3 && ver != 4) || size == 0 || size > MAX_ID3_TAG_BYTES)
		return;

	uint8_t* tag = malloc(size);
	if (!tag)
		return;
	if (fread(tag, 1, size, f) == size)
		TagMeta_id3Frames(ver, flags, tag, size, id3_lyrics_frame, res);
	free(tag);
}

// ---------------------------------------------------------------------------
// Vorbis comments (FLAC, Ogg Vorbis, Opus), read through a byte source

typedef struct {
	// Read (buf != NULL) or skip (buf == NULL) up to n bytes, returns the count
	size_t (*read)(void* ctx, uint8_t* buf, size_t n);
	void* ctx;
} Reader;

static bool rd_exact(Reader* r, uint8_t* buf, size_t n) {
	return r->read(r->ctx, buf, n) == n;
}

static bool is_lyrics_key(const char* key, size_t len) {
	static const char* keys[] = {"LYRICS", "UNSYNCEDLYRICS", "UNSYNCED LYRICS", "SYNCEDLYRICS"};
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (strlen(keys[i]) == len && strncasecmp(key, keys[i], len) == 0)
			return true;
	}
	return false;
}

// vendor_len(4) vendor count(4) then count x [len(4) "KEY=value"], all LE
static void read_vorbis_comments(Reader* r, Result* res) {
	uint8_t b[4];
	if (!rd_exact(r, b, 4))
		return;
	uint32_t vendor_len = le32(b);
	if (r->read(r->ctx, NULL, vendor_len) != vendor_len || !rd_exact(r, b, 4))
		return;
	uint32_t count = le32(b);

	for (uint32_t i = 0; i < count; i++) {
		if (!rd_exact(r, b, 4))
			return;
		uint32_t len = le32(b);
		char prefix[32];
		size_t head = len < sizeof(prefix) ? len : sizeof(prefix);
		if (!rd_exact(r, (uint8_t*)prefix, head))
			return;
		const char* eq = memchr(prefix, '=', head);
		size_t rest = len - head;
		if (!eq || !is_lyrics_key(prefix, (size_t)(eq - prefix)) || len > MAX_LYRICS_BYTES) {
			if (r->read(r->ctx, NULL, rest) != rest)
				return;
			continue;
		}
		size_t in_prefix = head - (size_t)(eq + 1 - prefix);
		char* text = malloc(in_prefix + rest + 1);
		if (!text) {
			if (r->read(r->ctx, NULL, rest) != rest)
				return;
			continue;
		}
		memcpy(text, eq + 1, in_prefix);
		if (!rd_exact(r, (uint8_t*)text + in_prefix, rest)) {
			free(text);
			return;
		}
		text[in_prefix + rest] = '\0';
		offer(res, text);
	}
}

// A byte source over a FILE window
typedef struct {
	FILE* f;
	uint64_t left;
} FileWindow;

static size_t file_window_read(void* ctx, uint8_t* buf, size_t n) {
	FileWindow* w = ctx;
	if (n > w->left)
		n = (size_t)w->left;
	if (buf) {
		n = fread(buf, 1, n, w->f);
	} else if (n && fseeko(w->f, (off_t)n, SEEK_CUR) != 0) {
		return 0;
	}
	w->left -= n;
	return n;
}

// FLAC: "fLaC" then metadata blocks [last|type(1) length(3)]; type 4 holds
// the Vorbis comments
static void read_flac(FILE* f, uint64_t offset, Result* res) {
	uint8_t h[4];
	if (fseeko(f, (off_t)offset, SEEK_SET) != 0 || fread(h, 1, 4, f) != 4 || memcmp(h, "fLaC", 4) != 0)
		return;
	for (int blocks = 0; blocks < 128; blocks++) {
		if (fread(h, 1, 4, f) != 4)
			return;
		bool last = (h[0] & 0x80) != 0;
		uint32_t len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
		off_t next = ftello(f) + len;
		if ((h[0] & 0x7F) == 4) {
			FileWindow w = {f, len};
			Reader r = {file_window_read, &w};
			read_vorbis_comments(&r, res);
		}
		if (last || fseeko(f, next, SEEK_SET) != 0)
			return;
	}
}

// ---------------------------------------------------------------------------
// Ogg (Vorbis, Opus): the comment header is the second packet of the first
// logical stream and may span many pages (covers are stored in it too), so it
// is streamed rather than reassembled.

typedef struct {
	FILE* f;
	uint32_t serial;
	bool have_serial;
	uint8_t lacing[255];
	int nsegs;
	int seg;		 // current segment index in the page
	bool seg_open;	 // current segment has been entered
	size_t seg_left; // bytes left in the current segment
	bool packet_end; // current packet is complete
	bool eof;
	int pages;
} OggStream;

static bool ogg_next_page(OggStream* s) {
	uint8_t h[27];
	while (s->pages++ < MAX_OGG_PAGES) {
		if (fread(h, 1, 27, s->f) != 27 || memcmp(h, "OggS", 4) != 0)
			return false;
		int nsegs = h[26];
		uint8_t lacing[255];
		if (fread(lacing, 1, (size_t)nsegs, s->f) != (size_t)nsegs)
			return false;
		uint32_t serial = le32(h + 14);
		if (!s->have_serial) {
			s->serial = serial;
			s->have_serial = true;
		}
		if (serial != s->serial) { // another logical stream: skip its data
			long body = 0;
			for (int i = 0; i < nsegs; i++)
				body += lacing[i];
			if (fseeko(s->f, body, SEEK_CUR) != 0)
				return false;
			continue;
		}
		memcpy(s->lacing, lacing, (size_t)nsegs);
		s->nsegs = nsegs;
		s->seg = 0;
		s->seg_open = false;
		s->seg_left = 0;
		return true;
	}
	return false;
}

// Read (buf != NULL) or skip up to n bytes of the current packet
static size_t ogg_packet_read(void* ctx, uint8_t* buf, size_t n) {
	OggStream* s = ctx;
	size_t done = 0;
	while (done < n && !s->packet_end && !s->eof) {
		if (s->seg_left == 0) {
			if (s->seg_open) { // finished the current segment
				uint8_t lv = s->lacing[s->seg++];
				s->seg_open = false;
				if (lv < 255) {
					s->packet_end = true;
					break;
				}
			}
			if (s->seg >= s->nsegs) {
				if (!ogg_next_page(s))
					s->eof = true;
				continue;
			}
			s->seg_left = s->lacing[s->seg];
			s->seg_open = true;
			continue;
		}
		size_t take = n - done < s->seg_left ? n - done : s->seg_left;
		if (buf ? fread(buf + done, 1, take, s->f) != take : fseeko(s->f, (off_t)take, SEEK_CUR) != 0) {
			s->eof = true;
			break;
		}
		done += take;
		s->seg_left -= take;
	}
	return done;
}

static void ogg_finish_packet(OggStream* s) {
	while (!s->packet_end && !s->eof)
		ogg_packet_read(s, NULL, (size_t)-1);
	s->packet_end = false;
}

static void read_ogg(FILE* f, Result* res) {
	if (fseeko(f, 0, SEEK_SET) != 0)
		return;
	OggStream s = {0};
	s.f = f;
	if (!ogg_next_page(&s))
		return;

	uint8_t magic[8];
	size_t n = ogg_packet_read(&s, magic, 8);
	size_t tags_magic_len;
	const char* tags_magic;
	if (n >= 7 && memcmp(magic, "\x01vorbis", 7) == 0) {
		tags_magic = "\x03vorbis";
		tags_magic_len = 7;
	} else if (n == 8 && memcmp(magic, "OpusHead", 8) == 0) {
		tags_magic = "OpusTags";
		tags_magic_len = 8;
	} else {
		return;
	}
	ogg_finish_packet(&s);

	if (ogg_packet_read(&s, magic, tags_magic_len) != tags_magic_len ||
		memcmp(magic, tags_magic, tags_magic_len) != 0)
		return;
	Reader r = {ogg_packet_read, &s};
	read_vorbis_comments(&r, res);
}

// ---------------------------------------------------------------------------
// MP4 / M4A: moov/udta/meta/ilst/©lyr/data

// Find the first child box of the given type in [start, end); returns its
// payload range
static bool mp4_find(FILE* f, uint64_t start, uint64_t end, const char* type, uint64_t* ps, uint64_t* pe) {
	uint64_t pos = start;
	for (int boxes = 0; boxes < 4096 && pos + 8 <= end; boxes++) {
		uint8_t h[16];
		if (fseeko(f, (off_t)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8)
			return false;
		uint64_t size = be32(h);
		uint64_t hdr = 8;
		if (size == 1) {
			if (fread(h + 8, 1, 8, f) != 8)
				return false;
			size = ((uint64_t)be32(h + 8) << 32) | be32(h + 12);
			hdr = 16;
		} else if (size == 0) {
			size = end - pos;
		}
		if (size < hdr || size > end - pos)
			return false;
		if (memcmp(h + 4, type, 4) == 0) {
			*ps = pos + hdr;
			*pe = pos + size;
			return true;
		}
		pos += size;
	}
	return false;
}

static void read_mp4(FILE* f, Result* res) {
	if (fseeko(f, 0, SEEK_END) != 0)
		return;
	uint64_t file_end = (uint64_t)ftello(f);
	uint64_t s, e;
	if (!mp4_find(f, 0, file_end, "moov", &s, &e) || !mp4_find(f, s, e, "udta", &s, &e) ||
		!mp4_find(f, s, e, "meta", &s, &e))
		return;
	// iTunes "meta" is a full box (4 bytes version/flags) unless a QuickTime
	// writer put the hdlr box right at the start
	uint8_t peek[8];
	if (e - s < 8 || fseeko(f, (off_t)s, SEEK_SET) != 0 || fread(peek, 1, 8, f) != 8)
		return;
	if (memcmp(peek + 4, "hdlr", 4) != 0)
		s += 4;
	if (!mp4_find(f, s, e, "ilst", &s, &e) || !mp4_find(f, s, e, "\xa9lyr", &s, &e) ||
		!mp4_find(f, s, e, "data", &s, &e))
		return;
	// data: type(4) locale(4) UTF-8 text
	if (e - s <= 8 || e - s - 8 > MAX_LYRICS_BYTES || fseeko(f, (off_t)(s + 8), SEEK_SET) != 0)
		return;
	size_t len = (size_t)(e - s - 8);
	char* text = malloc(len + 1);
	if (!text)
		return;
	if (fread(text, 1, len, f) != len) {
		free(text);
		return;
	}
	text[len] = '\0';
	offer(res, text);
}

// ---------------------------------------------------------------------------

char* EmbeddedLyrics_read(const char* filepath, bool* synced) {
	if (synced)
		*synced = false;
	if (!filepath || !filepath[0])
		return NULL;
	FILE* f = fopen(filepath, "rb");
	if (!f)
		return NULL;

	Result res = {0};
	uint8_t head[12];
	if (fread(head, 1, sizeof(head), f) == sizeof(head)) {
		if (memcmp(head, "ID3", 3) == 0) {
			uint64_t tag_end;
			read_id3(f, &res, &tag_end);
			if (!res.text && tag_end) // FLAC files sometimes carry an ID3v2 prefix
				read_flac(f, tag_end, &res);
		} else if (memcmp(head, "fLaC", 4) == 0) {
			read_flac(f, 0, &res);
		} else if (memcmp(head, "OggS", 4) == 0) {
			read_ogg(f, &res);
		} else if (memcmp(head + 4, "ftyp", 4) == 0) {
			read_mp4(f, &res);
		}
	}
	fclose(f);

	if (synced)
		*synced = res.synced;
	return res.text;
}
