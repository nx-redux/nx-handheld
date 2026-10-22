#ifndef __HTTP_LOCATION_H__
#define __HTTP_LOCATION_H__

// Resolving an HTTP redirect's Location header against the URL that was
// requested (radio_net.c, radio.c, http_download.c), split out so it can be
// unit-tested on the host: see tests/test_http_location.c.

#include <stdio.h>
#include <string.h>
#include <strings.h>

// Writes the absolute URL for location (absolute, "//host/x", "/x", "?q" or
// "x" relative to the directory of base) into out, cut to fit out_size.
static inline void HttpLocation_resolve(const char* base, const char* location, char* out, size_t out_size) {
	if (out_size == 0)
		return;
	const char* scheme_end = strstr(base, "://");
	if (strncasecmp(location, "http://", 7) == 0 || strncasecmp(location, "https://", 8) == 0 || !scheme_end) {
		snprintf(out, out_size, "%s", location);
		return;
	}
	const char* host = scheme_end + 3;
	const char* host_end = host + strcspn(host, "/?#");
	if (location[0] == '/' && location[1] == '/') { // scheme-relative
		snprintf(out, out_size, "%.*s%s", (int)(scheme_end + 1 - base), base, location);
	} else if (location[0] == '/') { // root-relative
		snprintf(out, out_size, "%.*s%s", (int)(host_end - base), base, location);
	} else if (location[0] == '?') { // same path, new query
		const char* path_end = host_end + strcspn(host_end, "?#");
		snprintf(out, out_size, "%.*s%s", (int)(path_end - base), base, location);
	} else { // relative to the base's directory
		const char* path_end = host_end + strcspn(host_end, "?#");
		const char* dir_end = NULL;
		for (const char* p = host_end; p < path_end; p++) {
			if (*p == '/')
				dir_end = p;
		}
		if (dir_end)
			snprintf(out, out_size, "%.*s%s", (int)(dir_end + 1 - base), base, location);
		else
			snprintf(out, out_size, "%.*s/%s", (int)(host_end - base), base, location);
	}
}

#endif
