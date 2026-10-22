// Host-compiled unit test for radio_net_parse_url (radio_url.h): scheme,
// default and explicit ports, missing path and the host/path buffer bounds.
// Build & run (from workspace/all/musicplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_radio_url.c -o /tmp/t && /tmp/t
#include <stdio.h>
#include <string.h>

#include "../radio_url.h"

static int failures;

#define CHECK(cond)                                                         \
	do {                                                                    \
		if (!(cond)) {                                                      \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                     \
		}                                                                   \
	} while (0)

static char host[256], path[512];
static int port;
static bool https;

static int parse(const char* url) {
	memset(host, 'X', sizeof(host));
	memset(path, 'X', sizeof(path));
	port = -1;
	https = true;
	return radio_net_parse_url(url, host, sizeof(host), &port, path, sizeof(path), &https);
}

static void test_schemes_and_ports(void) {
	CHECK(parse("http://radio.example.com/live.mp3") == 0);
	CHECK(strcmp(host, "radio.example.com") == 0 && port == 80 && !https);
	CHECK(strcmp(path, "/live.mp3") == 0);

	CHECK(parse("https://radio.example.com/a/b?x=1") == 0);
	CHECK(strcmp(host, "radio.example.com") == 0 && port == 443 && https);
	CHECK(strcmp(path, "/a/b?x=1") == 0);

	CHECK(parse("http://10.0.0.2:8000/stream") == 0);
	CHECK(strcmp(host, "10.0.0.2") == 0 && port == 8000 && !https);
	CHECK(strcmp(path, "/stream") == 0);

	CHECK(parse("https://h.com:8443/") == 0);
	CHECK(strcmp(host, "h.com") == 0 && port == 8443 && https);
	CHECK(strcmp(path, "/") == 0);

	// No scheme: treated as plain http
	CHECK(parse("h.com/x") == 0);
	CHECK(strcmp(host, "h.com") == 0 && port == 80 && !https && strcmp(path, "/x") == 0);
}

static void test_no_path(void) {
	CHECK(parse("http://h.com") == 0);
	CHECK(strcmp(host, "h.com") == 0 && strcmp(path, "/") == 0 && port == 80);

	CHECK(parse("http://h.com:9000") == 0);
	CHECK(strcmp(host, "h.com") == 0 && strcmp(path, "/") == 0 && port == 9000);
}

static void test_colon_in_path_is_not_a_port(void) {
	CHECK(parse("http://h.com/a:b") == 0);
	CHECK(strcmp(host, "h.com") == 0 && port == 80 && strcmp(path, "/a:b") == 0);
}

static void test_bounds(void) {
	char small_host[6], small_path[4];
	int p;
	bool s;
	CHECK(radio_net_parse_url("http://longhostname.com/abcdef", small_host, sizeof(small_host), &p,
							  small_path, sizeof(small_path), &s) == 0);
	CHECK(strcmp(small_host, "longh") == 0);
	CHECK(strcmp(small_path, "/ab") == 0);

	CHECK(radio_net_parse_url("http://longhostname.com:81/x", small_host, sizeof(small_host), &p,
							  small_path, sizeof(small_path), &s) == 0);
	CHECK(strcmp(small_host, "longh") == 0 && p == 81);

	char one[1];
	CHECK(radio_net_parse_url("http://h.com", small_host, sizeof(small_host), &p, one, 1, &s) == 0);
	CHECK(one[0] == '\0');

	CHECK(radio_net_parse_url(NULL, host, sizeof(host), &p, path, sizeof(path), &s) == -1);
	CHECK(radio_net_parse_url("http://h", host, 0, &p, path, sizeof(path), &s) == -1);
	CHECK(radio_net_parse_url("http://h", host, sizeof(host), &p, path, 0, &s) == -1);
}

int main(void) {
	test_schemes_and_ports();
	test_no_path();
	test_colon_in_path_is_not_a_port();
	test_bounds();
	if (failures) {
		fprintf(stderr, "test_radio_url: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_radio_url: all tests passed\n");
	return 0;
}
