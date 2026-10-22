#include "ra_auth.h"
#include "ra_auth_parse.h"
#include "http.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// RetroAchievements API endpoints
#define RA_API_URL "https://retroachievements.org/dorequest.php"

// Async authentication context
typedef struct {
	RA_AuthCallback callback;
	void* userdata;
} RA_AsyncAuthContext;

// HTTP callback for async auth
static void ra_auth_http_callback(HTTP_Response* http_response, void* userdata) {
	RA_AsyncAuthContext* ctx = (RA_AsyncAuthContext*)userdata;
	RA_AuthResponse response = {0};

	if (!http_response) {
		response.result = RA_AUTH_ERROR_UNKNOWN;
		strncpy(response.error_message, "No response received",
				sizeof(response.error_message) - 1);
	} else if (http_response->error) {
		response.result = RA_AUTH_ERROR_NETWORK;
		strncpy(response.error_message, http_response->error,
				sizeof(response.error_message) - 1);
	} else if (http_response->http_status != 200) {
		response.result = RA_AUTH_ERROR_NETWORK;
		snprintf(response.error_message, sizeof(response.error_message),
				 "HTTP error %d", http_response->http_status);
	} else if (!http_response->data || http_response->size == 0) {
		response.result = RA_AUTH_ERROR_PARSE;
		strncpy(response.error_message, "Empty response",
				sizeof(response.error_message) - 1);
	} else {
		// Parse the JSON response
		RA_Auth_parseLogin(http_response->data, &response);
	}

	// Call the user's callback
	if (ctx->callback) {
		ctx->callback(&response, ctx->userdata);
	}

	// Cleanup
	HTTP_freeResponse(http_response);
	free(ctx);
}


RA_AuthResult RA_authenticateSync(const char* username, const char* password,
								  RA_AuthResponse* response) {
	if (!response)
		return RA_AUTH_ERROR_UNKNOWN;
	memset(response, 0, sizeof(RA_AuthResponse));

	if (!username || !password) {
		response->result = RA_AUTH_ERROR_INVALID;
		strncpy(response->error_message, "Username and password required",
				sizeof(response->error_message) - 1);
		return response->result;
	}

	// URL-encode username and password
	char* enc_username = HTTP_urlEncode(username);
	char* enc_password = HTTP_urlEncode(password);

	if (!enc_username || !enc_password) {
		free(enc_username);
		free(enc_password);
		response->result = RA_AUTH_ERROR_UNKNOWN;
		strncpy(response->error_message, "Memory allocation failed",
				sizeof(response->error_message) - 1);
		return response->result;
	}

	// Build POST data, sized to fit: URL-encoding can triple a password, and
	// a truncated one would just fail the login as "invalid credentials"
	size_t post_size = strlen(enc_username) + strlen(enc_password) + sizeof("r=login&u=&p=");
	char* post_data = malloc(post_size);
	if (post_data)
		snprintf(post_data, post_size, "r=login&u=%s&p=%s", enc_username, enc_password);

	free(enc_username);
	free(enc_password);
	if (!post_data) {
		response->result = RA_AUTH_ERROR_UNKNOWN;
		strncpy(response->error_message, "Memory allocation failed",
				sizeof(response->error_message) - 1);
		return response->result;
	}

	// Make synchronous POST request
	HTTP_Response* http_response = HTTP_post(RA_API_URL, post_data, NULL);
	free(post_data);

	if (!http_response) {
		response->result = RA_AUTH_ERROR_UNKNOWN;
		strncpy(response->error_message, "No response received",
				sizeof(response->error_message) - 1);
		return response->result;
	}

	if (http_response->error) {
		response->result = RA_AUTH_ERROR_NETWORK;
		strncpy(response->error_message, http_response->error,
				sizeof(response->error_message) - 1);
		HTTP_freeResponse(http_response);
		return response->result;
	}

	if (http_response->http_status != 200) {
		response->result = RA_AUTH_ERROR_NETWORK;
		snprintf(response->error_message, sizeof(response->error_message),
				 "HTTP error %d", http_response->http_status);
		HTTP_freeResponse(http_response);
		return response->result;
	}

	if (!http_response->data || http_response->size == 0) {
		response->result = RA_AUTH_ERROR_PARSE;
		strncpy(response->error_message, "Empty response",
				sizeof(response->error_message) - 1);
		HTTP_freeResponse(http_response);
		return response->result;
	}

	// Parse the JSON response
	RA_Auth_parseLogin(http_response->data, response);

	HTTP_freeResponse(http_response);
	return response->result;
}
