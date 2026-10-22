#ifndef __RA_AUTH_PARSE_H__
#define __RA_AUTH_PARSE_H__

// The login-response parsing behind ra_auth.c, split out so it can be unit-tested on the host: see
// tests/test_ra_auth_parse.c. Minimal JSON helpers for RA login responses.

#include <stdio.h>
#include <string.h>
#include "ra_auth.h"
#include "utils.h" // json_extract_string

// 1 for "key":true, 0 for "key":false (a space after the colon allowed), -1 when neither is there.
static inline int RA_Auth_findJsonBool(const char* json, const char* key) {
	if (!json || !key)
		return -1;

	// Search for "key":true or "key":false
	char search_true[128];
	char search_false[128];
	snprintf(search_true, sizeof(search_true), "\"%s\":true", key);
	snprintf(search_false, sizeof(search_false), "\"%s\":false", key);

	if (strstr(json, search_true))
		return 1;
	if (strstr(json, search_false))
		return 0;

	// Try with space after colon
	snprintf(search_true, sizeof(search_true), "\"%s\": true", key);
	snprintf(search_false, sizeof(search_false), "\"%s\": false", key);

	if (strstr(json, search_true))
		return 1;
	if (strstr(json, search_false))
		return 0;

	return -1;
}

// Parse a login2 response into *response (zeroed by the caller): success needs a Token; a "Success":false
// carries the server's Error (or "Invalid credentials"); anything else is a parse error.
static inline void RA_Auth_parseLogin(const char* json, RA_AuthResponse* response) {
	if (!json || !response)
		return;

	// Check for Success field
	int success = RA_Auth_findJsonBool(json, "Success");

	if (success == 1) {
		response->result = RA_AUTH_SUCCESS;

		// Extract Token
		json_extract_string(json, "Token", response->token, sizeof(response->token));

		// Extract User (display name)
		json_extract_string(json, "User", response->display_name, sizeof(response->display_name));

		if (strlen(response->token) == 0) {
			// Token missing in success response - shouldn't happen but handle it
			response->result = RA_AUTH_ERROR_PARSE;
			strncpy(response->error_message, "Token missing in response",
					sizeof(response->error_message) - 1);
		}
	} else if (success == 0) {
		response->result = RA_AUTH_ERROR_INVALID;

		// Try to extract error message
		if (!json_extract_string(json, "Error", response->error_message,
								 sizeof(response->error_message))) {
			strncpy(response->error_message, "Invalid credentials",
					sizeof(response->error_message) - 1);
		}
	} else {
		// Couldn't parse Success field
		response->result = RA_AUTH_ERROR_PARSE;
		strncpy(response->error_message, "Invalid response format",
				sizeof(response->error_message) - 1);
	}
}

#endif
