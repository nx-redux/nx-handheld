// Host unit test for ra_auth_parse.h (ra_auth.c's login-response parsing). Built with AddressSanitizer by
// run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ra_auth_parse.h"

static RA_AuthResponse parse(const char* json) {
	RA_AuthResponse r;
	memset(&r, 0, sizeof(r));
	RA_Auth_parseLogin(json, &r);
	return r;
}

int main(void) {
	assert(RA_Auth_findJsonBool("{\"Success\":true}", "Success") == 1);
	assert(RA_Auth_findJsonBool("{\"Success\": false}", "Success") == 0);
	assert(RA_Auth_findJsonBool("{\"Success\":1}", "Success") == -1);
	assert(RA_Auth_findJsonBool(NULL, "Success") == -1);

	RA_AuthResponse r = parse("{\"Success\": true, \"User\":\"Bob\",\"Token\":\"tok123\",\"Score\":5}");
	assert(r.result == RA_AUTH_SUCCESS && strcmp(r.token, "tok123") == 0 && strcmp(r.display_name, "Bob") == 0);

	r = parse("{\"Success\":true,\"User\":\"Bob\"}");
	assert(r.result == RA_AUTH_ERROR_PARSE && strcmp(r.error_message, "Token missing in response") == 0);

	r = parse("{\"Success\":false,\"Error\":\"Invalid User/Password combination. Please try again\"}");
	assert(r.result == RA_AUTH_ERROR_INVALID);
	assert(strcmp(r.error_message, "Invalid User/Password combination. Please try again") == 0);

	r = parse("{\"Success\":false}");
	assert(r.result == RA_AUTH_ERROR_INVALID && strcmp(r.error_message, "Invalid credentials") == 0);

	// an Error longer than the buffer is cut and terminated
	char json[600], err[400];
	memset(err, 'e', sizeof(err) - 1);
	err[sizeof(err) - 1] = '\0';
	snprintf(json, sizeof(json), "{\"Success\":false,\"Error\":\"%s\"}", err);
	r = parse(json);
	assert(r.result == RA_AUTH_ERROR_INVALID && strlen(r.error_message) == sizeof(r.error_message) - 1);

	// a token longer than its buffer is cut, not overflowed
	char tok[100];
	memset(tok, 't', sizeof(tok) - 1);
	tok[sizeof(tok) - 1] = '\0';
	snprintf(json, sizeof(json), "{\"Success\":true,\"Token\":\"%s\"}", tok);
	r = parse(json);
	assert(r.result == RA_AUTH_SUCCESS && strlen(r.token) == sizeof(r.token) - 1);

	r = parse("<html>502 Bad Gateway</html>");
	assert(r.result == RA_AUTH_ERROR_PARSE && strcmp(r.error_message, "Invalid response format") == 0);
	printf("test_ra_auth_parse: OK\n");
	return 0;
}
