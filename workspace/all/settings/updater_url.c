#include "updater_url.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

int updater_parse_tag_from_location(const char* location, char* out, size_t out_size) {
	if (!location || !out || out_size == 0)
		return -1;

	static const char segment[] = "/releases/tag/";
	const char* tag = strstr(location, segment);
	if (!tag)
		return -1;
	tag += sizeof(segment) - 1;

	size_t len = strlen(tag);
	while (len > 0 && isspace((unsigned char)tag[len - 1]))
		len--;

	if (len == 0 || len >= out_size)
		return -1;

	memcpy(out, tag, len);
	out[len] = '\0';
	return 0;
}

int updater_parse_location_header(const char* headers_text, char* out, size_t out_size) {
	if (!headers_text || !out || out_size == 0)
		return -1;

	int found = 0;
	const char* line = headers_text;
	while (line && *line) {
		const char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncasecmp(p, "Location:", 9) == 0) {
			p += 9;
			while (*p == ' ' || *p == '\t')
				p++;
			// A URL cannot contain whitespace: cut there, which also drops
			// the " [following]" wget appends to its own status line.
			size_t len = strcspn(p, " \t\r\n");
			if (len > 0 && len < out_size) {
				memcpy(out, p, len);
				out[len] = '\0';
				found = 1;
			}
		}
		line = strchr(line, '\n');
		if (line)
			line++;
	}

	return found ? 0 : -1;
}

int updater_build_fallback_url(const char* owner, const char* repo,
							   const char* tag, const char* device,
							   char* out, size_t out_size) {
	if (!owner || !*owner || !repo || !*repo || !tag || !*tag ||
		!device || !*device || !out || out_size == 0)
		return -1;

	int n = snprintf(out, out_size,
					 "https://github.com/%s/%s/releases/download/%s/NXRedux-%s-%s.zip",
					 owner, repo, tag, tag, device);
	if (n < 0 || (size_t)n >= out_size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}

const char* updater_find_zip_asset_url(const char* json, const char* asset_suffix, char* out, size_t out_size,
									   char* digest_out, size_t digest_size) {
	if (digest_out && digest_size > 0)
		digest_out[0] = '\0';
	if (!json || !asset_suffix || !out || out_size == 0)
		return NULL;

	char suffix[96];
	snprintf(suffix, sizeof(suffix), "-%s", asset_suffix);

	const char* assets = strstr(json, "\"assets\"");
	if (!assets)
		return NULL;

	const char* pos = assets;
	while ((pos = strstr(pos, "\"browser_download_url\"")) != NULL) {
		pos = strchr(pos + 21, '"');
		if (!pos)
			break;

		if (*(pos - 1) == ':') {
			// pos points to opening quote of value
		} else {
			pos++;
			while (*pos == ' ' || *pos == ':')
				pos++;
			if (*pos != '"')
				continue;
		}

		pos++;
		const char* end = strchr(pos, '"');
		if (!end)
			break;

		size_t len = end - pos;
		size_t suf_len = strlen(suffix);
		if (len > suf_len && strncmp(end - suf_len, suffix, suf_len) == 0) {
			if (len >= out_size)
				len = out_size - 1;
			strncpy(out, pos, len);
			out[len] = '\0';

			// Extract this asset's SHA-256 digest if present. GitHub emits
			// "digest": "sha256:<hex>" earlier in the same asset object, so
			// scan backward from the matched URL. Stop at the first '}' so we
			// never pick up a different asset's digest.
			if (digest_out && digest_size > 0) {
				for (const char* q = pos; q > assets; q--) {
					if (*q == '}')
						break;
					if (strncmp(q, "\"digest\"", 8) == 0) {
						const char* s = strstr(q, "sha256:");
						if (s && s < pos) {
							s += 7;
							size_t k = 0;
							while (k < digest_size - 1 && isxdigit((unsigned char)*s))
								digest_out[k++] = (char)tolower((unsigned char)*s++);
							digest_out[k] = '\0';
						}
						break;
					}
				}
			}
			return out;
		}

		pos = end + 1;
	}

	return NULL;
}

void updater_extract_first_paragraph(const char* body, char* out, size_t out_size) {
	if (!body || !out || out_size == 0)
		return;

	out[0] = '\0';
	size_t j = 0;

	for (size_t i = 0; body[i] && j < out_size - 1; i++) {
		if (body[i] == '\\' && body[i + 1] == 'r') {
			i++;
			continue;
		}
		if (body[i] == '\\' && body[i + 1] == 'n') {
			// Check for paragraph break: \n followed by optional \r then \n
			size_t k = i + 2;
			if (body[k] == '\\' && body[k + 1] == 'r')
				k += 2;
			if (body[k] == '\\' && body[k + 1] == 'n')
				break;
			out[j++] = ' ';
			i++;
			continue;
		}
		if (body[i] == '#') {
			while (body[i] == '#')
				i++;
			if (body[i] == ' ')
				i++;
			i--;
			continue;
		}
		out[j++] = body[i];
	}

	out[j] = '\0';
	while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\n' || out[j - 1] == '\r'))
		out[--j] = '\0';
}
