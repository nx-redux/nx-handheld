#ifndef __RADIO_URL_H__
#define __RADIO_URL_H__

// Splitting a stream URL into host, port and path (radio_net.c, radio.c),
// split out so it can be unit-tested on the host: see
// tests/test_radio_url.c.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Parse URL into host, port, path, and detect HTTPS
// Returns 0 on success, -1 on error
static inline int radio_net_parse_url(const char* url, char* host, int host_size,
									  int* port, char* path, int path_size, bool* is_https) {
	if (!url || !host || !path || host_size < 1 || path_size < 1) {
		return -1;
	}

	*is_https = false;
	*port = 80;
	host[0] = '\0';
	path[0] = '\0';

	// Skip protocol
	const char* start = url;
	if (strncmp(url, "https://", 8) == 0) {
		start = url + 8;
		*is_https = true;
		*port = 443;
	} else if (strncmp(url, "http://", 7) == 0) {
		start = url + 7;
	}

	// Find path
	const char* path_start = strchr(start, '/');
	if (path_start) {
		snprintf(path, path_size, "%s", path_start);
	} else {
		strncpy(path, "/", path_size - 1);
		path[path_size - 1] = '\0';
		path_start = start + strlen(start);
	}

	// Find port
	const char* port_start = strchr(start, ':');
	if (port_start && port_start < path_start) {
		*port = atoi(port_start + 1);
		int host_len = port_start - start;
		if (host_len >= host_size)
			host_len = host_size - 1;
		memcpy(host, start, host_len);
		host[host_len] = '\0';
	} else {
		int host_len = path_start - start;
		if (host_len >= host_size)
			host_len = host_size - 1;
		memcpy(host, start, host_len);
		host[host_len] = '\0';
	}

	return 0;
}

#endif
