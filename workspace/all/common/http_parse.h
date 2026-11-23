#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H
// The pure parts of http.c's curl runner (no popen), so the host tests can
// cover them.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// curl writes "<body>\n<status>" (-w '\n%{http_code}'). Splits the status off
// the end of data, NUL-terminating the body in place and shortening *size.
// Returns the status (100-599), or -1 when there is none: data is then left
// as it was. data must have room for a terminator at data[*size].
static inline int HTTP_splitStatus(char* data, size_t* size) {
	if (!data || !size || *size == 0)
		return -1;
	size_t i = *size;
	while (i > 0 && data[i - 1] != '\n')
		i--;
	if (i == 0)
		return -1;
	data[*size] = '\0';
	int status = atoi(data + i);
	if (status < 100 || status >= 600)
		return -1;
	data[i - 1] = '\0';
	*size = i - 1;
	return status;
}

// A short message for a curl exit code. stderr is not read (it would mix
// into the body), so the common transport failures are named here.
static inline const char* HTTP_curlErrorText(int exit_code) {
	switch (exit_code) {
	case 6:
		return "Could not resolve host";
	case 7:
		return "Could not connect to server";
	case 18:
		return "Transfer ended early";
	case 28:
		return "Request timed out";
	case 35:
		return "Secure connection failed";
	case 47:
		return "Too many redirects";
	case 52:
		return "Empty reply from server";
	case 56:
		return "Connection lost";
	case 60:
	case 77:
		return "Server certificate could not be verified";
	case 127:
		return "curl not found";
	default:
		return "Network request failed";
	}
}

// Appends src to dst[*len] (size dst_size, kept NUL-terminated), keeping only
// characters a User-Agent product token may hold; a run of others becomes one
// '_' when underscores is set, else is dropped. Stops at the first space when
// first_word is set ("v2.5 4cbc7d4" -> "v2.5").
static inline void HTTP_appendToken(char* dst, size_t dst_size, size_t* len, const char* src, int underscores, int first_word) {
	int pending = 0;
	size_t start = *len;
	for (; src && *src; src++) {
		char c = *src;
		if (first_word && c == ' ' && *len > start)
			break;
		int ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '+' || c == '-';
		if (!ok) {
			pending = underscores;
			continue;
		}
		if (pending && *len > start && *len + 1 < dst_size)
			dst[(*len)++] = '_';
		pending = 0;
		if (*len + 1 < dst_size)
			dst[(*len)++] = c;
	}
	dst[*len] = '\0';
}

// The User-Agent RetroAchievements' hardcore rules ask for:
// "NXRedux/<version> (<platform>) <core>/<core version>". The core part is left
// out without a core name, its "/<version>" without a version.
static inline void HTTP_formatUserAgent(char* buffer, size_t buffer_size, const char* version, const char* platform,
										const char* core_name, const char* core_version) {
	if (buffer_size == 0)
		return;
	int n = snprintf(buffer, buffer_size, "NXRedux/%s (%s)", version, platform);
	if (n < 0 || (size_t)n >= buffer_size)
		return;
	char core[96];
	size_t len = 0;
	core[0] = '\0';
	HTTP_appendToken(core, sizeof(core), &len, core_name, 1, 0);
	if (len == 0)
		return;
	size_t name_len = len;
	if (len + 2 < sizeof(core)) {
		core[len++] = '/';
		core[len] = '\0';
		HTTP_appendToken(core, sizeof(core), &len, core_version, 0, 1);
		if (len == name_len + 1)
			core[--len] = '\0'; // no version
	}
	snprintf(buffer + n, buffer_size - (size_t)n, " %s", core);
}

#endif
