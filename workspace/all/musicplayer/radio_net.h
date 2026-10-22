#ifndef __RADIO_NET_H__
#define __RADIO_NET_H__

#include <stdint.h>
#include <stdbool.h>

// Parse URL into components
// Returns 0 on success, -1 on error
int radio_net_parse_url(const char* url, char* host, int host_size,
						int* port, char* path, int path_size, bool* is_https);

// Fetch content from URL into buffer
// Returns bytes read on success, -1 on error
// content_type and ct_size are optional (can be NULL/0)
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size,
					char* content_type, int ct_size);

// Resolve URL redirects and return the final URL
// Uses radio_net's TLS infrastructure (supports TLS 1.3)
// Returns 0 on success (resolved_url filled), -1 on error
int radio_net_resolve_url(const char* url, char* resolved_url, int resolved_url_size);

// Server certificate checking for a TLS client config: verified against the
// shipped CA bundle when the card has one (as common/http.c does through
// curl), unverified otherwise
struct mbedtls_ssl_config;
void radio_net_tls_verify(struct mbedtls_ssl_config* conf);

#endif
