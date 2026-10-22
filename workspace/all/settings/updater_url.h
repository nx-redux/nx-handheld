#ifndef UPDATER_URL_H
#define UPDATER_URL_H

#include <stddef.h>

/**
 * Pure string helpers for the rate-limit-free update check
 * (github.com web endpoints instead of api.github.com).
 * Host-testable: no SDL / platform dependencies.
 */

/**
 * Extract the release tag from a GitHub releases/latest redirect target,
 * e.g. "https://github.com/<o>/<r>/releases/tag/v1.8.0" -> "v1.8.0".
 * Trailing whitespace/CR/LF is stripped.
 *
 * @return 0 on success, -1 if there is no /releases/tag/<tag> segment,
 *         the tag is empty, or it does not fit in out.
 */
int updater_parse_tag_from_location(const char* location, char* out, size_t out_size);

/**
 * Find the value of the last "Location:" header in wget -S stderr output
 * (lines have leading whitespace; header name is case-insensitive).
 *
 * @return 0 on success, -1 if no Location header is present or the value
 *         is empty / does not fit in out.
 */
int updater_parse_location_header(const char* headers_text, char* out, size_t out_size);

/**
 * Build the predictable release-asset download URL
 * "https://github.com/<owner>/<repo>/releases/download/<tag>/NXRedux-<tag>-<device>.zip".
 * Only valid for releases whose assets use tag-based names (v1.9.0+).
 *
 * @return 0 on success, -1 if any piece is missing or the URL does not fit.
 */
int updater_build_fallback_url(const char* owner, const char* repo,
							   const char* tag, const char* device,
							   char* out, size_t out_size);

/**
 * Find the release asset whose browser_download_url ends in "-<asset_suffix>"
 * in a GitHub releases API JSON body and copy that URL into out (truncated
 * to out_size). When digest_out is given, it receives that same asset's
 * lowercase "sha256:" hex digest, or "" when the asset has none.
 *
 * @return out on a match, NULL otherwise.
 */
const char* updater_find_zip_asset_url(const char* json, const char* asset_suffix, char* out, size_t out_size,
									   char* digest_out, size_t digest_size);

/**
 * Copy the first paragraph of a JSON-escaped release body (literal "\n"
 * sequences) into out: single line breaks become spaces, "\n\n" or
 * "\n\r\n" ends the paragraph, '#' heading markers are dropped and
 * trailing whitespace is trimmed.
 */
void updater_extract_first_paragraph(const char* body, char* out, size_t out_size);

#endif // UPDATER_URL_H
