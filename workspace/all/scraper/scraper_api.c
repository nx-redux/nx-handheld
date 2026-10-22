#include "scraper_api.h"
#include "scraper_parse.h"
#include "http.h"
#include "cjson/cJSON.h"
#include "utils.h"
#include "api.h"
#include "wifi.h"
#include "ca_bundle.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#define SS_API_BASE "https://api.screenscraper.fr/api2"
#define SS_SOFTNAME "nxredux-scraper"
#define SS_RATE_LIMIT_MS 1200 // 1.2 seconds between requests

// Dev credentials (XOR key 0x5A) come from the git-ignored ss_credentials.h,
// generated at build time by scripts/gen-ss-credentials.sh from the
// SS_DEV_ID / SS_DEV_PASSWORD secrets.
#define SS_XOR_KEY 0x5A
#ifdef HAVE_SS_CREDENTIALS
#include "ss_credentials.h"

static void ss_decode(const unsigned char* enc, int len, char* out) {
	for (int i = 0; i < len; i++)
		out[i] = enc[i] ^ SS_XOR_KEY;
	out[len] = '\0';
}

// Decode both obfuscated dev credentials into caller-provided buffers.
static void ss_decodeDevCredentials(char* devid, char* devpass) {
	ss_decode(ss_devid_enc, sizeof(ss_devid_enc), devid);
	ss_decode(ss_devpass_enc, sizeof(ss_devpass_enc), devpass);
}
#else
#warning "ss_credentials.h missing: building without ScreenScraper dev credentials (run scripts/gen-ss-credentials.sh)"
static void ss_decodeDevCredentials(char* devid, char* devpass) {
	devid[0] = '\0';
	devpass[0] = '\0';
}
#endif

// The UI thread (account info) and the scrape worker (searches) both make requests: one limiter for both, so the
// interval holds across them.
static pthread_mutex_t rate_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t last_request_time = 0;
static char ss_username[64] = "";
static char ss_password[128] = "";

// Shell-escape a string for safe use in system() calls (wraps in single quotes)
static char* shell_escape(const char* str) {
	return shellQuoteAlloc(str);
}

void ScraperAPI_init(void) {
	pthread_mutex_lock(&rate_lock);
	last_request_time = 0;
	pthread_mutex_unlock(&rate_lock);
	ss_username[0] = '\0';
	ss_password[0] = '\0';
}

void ScraperAPI_setUserCredentials(const char* username, const char* password) {
	if (username)
		snprintf(ss_username, sizeof(ss_username), "%s", username);
	else
		ss_username[0] = '\0';
	if (password)
		snprintf(ss_password, sizeof(ss_password), "%s", password);
	else
		ss_password[0] = '\0';
}

bool ScraperAPI_hasUserCredentials(void) {
	return ss_username[0] != '\0' && ss_password[0] != '\0';
}

void ScraperAPI_rateLimit(void) {
	// held across the wait: a second caller waits its own full interval after this one
	pthread_mutex_lock(&rate_lock);
	if (last_request_time != 0) {
		uint64_t now = getMicroseconds();
		uint64_t elapsed_ms = (now - last_request_time) / 1000;
		if (elapsed_ms < SS_RATE_LIMIT_MS) {
			usleep((SS_RATE_LIMIT_MS - elapsed_ms) * 1000);
		}
	}
	last_request_time = getMicroseconds();
	pthread_mutex_unlock(&rate_lock);
}

bool ScraperAPI_isOnline(void) {
	// Live check (interface up + wpa_state), not PWR_isOnline(): the cached
	// status is stale-false for the first poll interval of a fresh process,
	// which made these gates report "No Network" right after launch.
	return Wifi_isConnected();
}

static bool computeCRC32(const char* filepath, uint32_t* out_crc, long* out_size) {
	FILE* f = fopen(filepath, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	*out_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uLong crc = crc32(0L, Z_NULL, 0);
	unsigned char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		crc = crc32(crc, buf, n);
	fclose(f);
	*out_crc = (uint32_t)crc;
	return true;
}

// Parse game info from API JSON response
static bool parseSearchResponse(HTTP_Response* resp, ScraperGameInfo* info) {
	if (!resp)
		return false;
	if (resp->http_status != 200 || !resp->data) {
		HTTP_freeResponse(resp);
		return false;
	}

	bool found = ScraperParse_gameInfo(resp->data, info);
	HTTP_freeResponse(resp);
	if (!found)
		return false;

	// which media were found, not their URLs: ScreenScraper's carry the dev and user passwords as query parameters
	LOG_info("Scraper: game='%s' ss=%s box=%s box2d=%s wheel=%s\n", info->game_name,
			 info->screenshot_url[0] ? "yes" : "no", info->boxart_url[0] ? "yes" : "no",
			 info->boxart2d_url[0] ? "yes" : "no", info->wheel_url[0] ? "yes" : "no");

	return true;
}

bool ScraperAPI_search(const char* rom_filename, const char* rom_path,
					   int system_id, ScraperGameInfo* info) {
	if (!rom_filename || !info)
		return false;

	memset(info, 0, sizeof(ScraperGameInfo));
	info->found = false;

	// URL-encode the filename (with extension for romnom)
	char* encoded_name = HTTP_urlEncode(rom_filename);
	if (!encoded_name)
		return false;

	// Decode dev credentials
	char devid[64], devpass[64];
	ss_decodeDevCredentials(devid, devpass);

	// Build base API URL (filename only)
	char url[2048];
	int base_len = snprintf(url, sizeof(url),
							"%s/jeuInfos.php?devid=%s&devpassword=%s&softname=%s"
							"&romnom=%s&systemeid=%d&output=json",
							SS_API_BASE, devid, devpass, SS_SOFTNAME,
							encoded_name, system_id);

	// Append user credentials if configured
	if (base_len > 0 && base_len < (int)sizeof(url) &&
		ss_username[0] != '\0' && ss_password[0] != '\0') {
		char* enc_user = HTTP_urlEncode(ss_username);
		char* enc_pass = HTTP_urlEncode(ss_password);
		if (enc_user && enc_pass) {
			snprintf(url + base_len, sizeof(url) - base_len,
					 "&ssid=%s&sspassword=%s", enc_user, enc_pass);
		}
		free(enc_user);
		free(enc_pass);
	}
	free(encoded_name);

	// Save URL length before any CRC append
	int url_len = strlen(url);

	// Try 1: search by filename only
	ScraperAPI_rateLimit();
	HTTP_Response* resp = HTTP_get(url);
	if (parseSearchResponse(resp, info))
		return true;

	// Try 2: fallback with CRC32 + file size for non-standard filenames
	if (rom_path) {
		uint32_t crc_val;
		long file_size;
		if (computeCRC32(rom_path, &crc_val, &file_size)) {
			snprintf(url + url_len, sizeof(url) - url_len,
					 "&crc=%08x&romtaille=%ld", crc_val, file_size);
			ScraperAPI_rateLimit();
			resp = HTTP_get(url);
			if (parseSearchResponse(resp, info))
				return true;
		}
	}

	return false;
}

ScraperUserInfo ScraperAPI_fetchUserInfo(void) {
	ScraperUserInfo info = {0, 0, false};

	if (ss_username[0] == '\0' || ss_password[0] == '\0')
		return info;

	// Decode dev credentials
	char devid[64], devpass[64];
	ss_decodeDevCredentials(devid, devpass);

	char* enc_user = HTTP_urlEncode(ss_username);
	char* enc_pass = HTTP_urlEncode(ss_password);
	if (!enc_user || !enc_pass) {
		free(enc_user);
		free(enc_pass);
		return info;
	}

	char url[2048];
	snprintf(url, sizeof(url),
			 "%s/ssuserInfos.php?devid=%s&devpassword=%s&softname=%s"
			 "&ssid=%s&sspassword=%s&output=json",
			 SS_API_BASE, devid, devpass, SS_SOFTNAME,
			 enc_user, enc_pass);
	free(enc_user);
	free(enc_pass);

	ScraperAPI_rateLimit();

	HTTP_Response* resp = HTTP_get(url);
	if (!resp)
		return info;
	if (resp->http_status != 200 || !resp->data) {
		HTTP_freeResponse(resp);
		return info;
	}

	cJSON* root = cJSON_Parse(resp->data);
	HTTP_freeResponse(resp);
	if (!root)
		return info;

	cJSON* response = cJSON_GetObjectItem(root, "response");
	if (!response) {
		cJSON_Delete(root);
		return info;
	}

	cJSON* ssuser = cJSON_GetObjectItem(response, "ssuser");
	if (!ssuser) {
		cJSON_Delete(root);
		return info;
	}

	cJSON* req_today = cJSON_GetObjectItem(ssuser, "requeststoday");
	cJSON* max_req = cJSON_GetObjectItem(ssuser, "maxrequestsperday");

	if (req_today && cJSON_IsString(req_today))
		info.requests_today = atoi(req_today->valuestring);
	else if (req_today && cJSON_IsNumber(req_today))
		info.requests_today = req_today->valueint;

	if (max_req && cJSON_IsString(max_req))
		info.max_requests_per_day = atoi(max_req->valuestring);
	else if (max_req && cJSON_IsNumber(max_req))
		info.max_requests_per_day = max_req->valueint;

	info.valid = true;
	cJSON_Delete(root);
	return info;
}

bool ScraperAPI_downloadFile(const char* url, const char* dest_path) {
	if (!url || !dest_path || url[0] == '\0')
		return false;

	// Shell-escape both arguments to prevent injection
	char* escaped_url = shell_escape(url);
	char* escaped_path = shell_escape(dest_path);
	if (!escaped_url || !escaped_path) {
		free(escaped_url);
		free(escaped_path);
		return false;
	}

	// TLS checked against the shipped CA bundle like every other fetch (-k only when there is none)
	const char* ca_bundle = nx_ca_bundle_path();
	char* escaped_ca = ca_bundle ? shell_escape(ca_bundle) : NULL;
	char tls_opt[MAX_PATH * 4 + 16];
	if (escaped_ca)
		snprintf(tls_opt, sizeof(tls_opt), "--cacert %s", escaped_ca);
	else
		snprintf(tls_opt, sizeof(tls_opt), "-k");
	free(escaped_ca);

	// Use curl for binary downloads since HTTP module is text-oriented. --fail: an HTTP error (quota, 404) is a
	// failed download, not an error page saved as the image.
	char cmd[4096];
	int n = snprintf(cmd, sizeof(cmd), "curl -s --fail %s -L -o %s --connect-timeout 10 --max-time 30 %s", tls_opt,
					 escaped_path, escaped_url);
	free(escaped_url);
	free(escaped_path);
	if (n < 0 || (size_t)n >= sizeof(cmd)) {
		LOG_error("Scraper download: command too long for %s\n", dest_path);
		return false;
	}

	// Log only the destination: ScreenScraper media URLs carry the dev and
	// user passwords as query parameters.
	LOG_info("Scraper download: %s\n", dest_path);
	int ret = system(cmd);
	if (ret != 0) {
		LOG_error("Scraper download failed: curl exit=%d\n", ret);
		return false;
	}

	// Verify file exists and has content
	FILE* f = fopen(dest_path, "rb");
	if (!f) {
		LOG_error("Scraper download: file not created: %s\n", dest_path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);

	LOG_info("Scraper download: %s -> %ld bytes\n", dest_path, size);
	return size > 0;
}
