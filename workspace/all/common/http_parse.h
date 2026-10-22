#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H
// The pure parts of http.c's curl runner (no popen), so the host tests can
// cover them.
#include <stddef.h>
#include <stdlib.h>

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

#endif
