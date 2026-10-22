#ifndef __RADIO_NET_H__
#define __RADIO_NET_H__

#include <stdint.h>
#include <stdbool.h>

// URL parsing: radio_net_parse_url
#include "radio_url.h"

// Fetch content from URL into buffer
// Returns bytes read on success, -1 on error
// content_type and ct_size are optional (can be NULL/0)
int radio_net_fetch(const char* url, uint8_t* buffer, int buffer_size,
					char* content_type, int ct_size);

// Resolve URL redirects and return the final URL
// Uses radio_net's TLS infrastructure (supports TLS 1.3)
// Returns 0 on success (resolved_url filled), -1 on error
int radio_net_resolve_url(const char* url, char* resolved_url, int resolved_url_size);

// TLS client session for host (SNI) on the given contexts, which this
// initializes: seeded RNG, client defaults, and the server certificate
// verified against the shipped CA bundle when the card has one (as
// common/http.c does through curl), unverified otherwise. Returns 0, or the
// failing mbedtls error (logged). Either way the caller releases the contexts
// with radio_net_tls_free.
struct mbedtls_net_context;
struct mbedtls_ssl_context;
struct mbedtls_ssl_config;
struct mbedtls_entropy_context;
struct mbedtls_ctr_drbg_context;
int radio_net_tls_setup(struct mbedtls_net_context* net, struct mbedtls_ssl_context* ssl,
						struct mbedtls_ssl_config* conf, struct mbedtls_entropy_context* entropy,
						struct mbedtls_ctr_drbg_context* ctr_drbg, const char* pers,
						const char* host);
// Frees what radio_net_tls_setup initialized; close_notify first tells the
// server the session is over (only after a completed handshake)
void radio_net_tls_free(struct mbedtls_net_context* net, struct mbedtls_ssl_context* ssl,
						struct mbedtls_ssl_config* conf, struct mbedtls_entropy_context* entropy,
						struct mbedtls_ctr_drbg_context* ctr_drbg, bool close_notify);

#endif
