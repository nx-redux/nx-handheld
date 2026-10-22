#ifndef __WIFI_SCAN_LINE_H__
#define __WIFI_SCAN_LINE_H__

// Pure parser for one `wpa_cli scan_results` line, split out of
// generic_wifi.c so it can be unit-tested on the host: see
// tests/test_wifi_scan_line.c.
//
//   bssid <TAB> frequency <TAB> signal level <TAB> flags <TAB> ssid
//
// wpa_cli prints every SSID byte outside 0x20..0x7e as `\xNN`, so a 32-byte
// UTF-8 SSID prints as up to 128 chars. Each field is bounded by its buffer:
// bssid >= WIFI_SCAN_BSSID_SIZE, features >= WIFI_SCAN_FEATURES_SIZE and
// ssid >= WIFI_SCAN_SSID_SIZE (SSID_MAX); a longer SSID is truncated.

#include <stdio.h>

#define WIFI_SCAN_BSSID_SIZE 18
#define WIFI_SCAN_FEATURES_SIZE 128
#define WIFI_SCAN_SSID_SIZE 64

// Returns the number of fields parsed (sscanf semantics); < 4 means malformed.
static inline int WifiScan_parseLine(const char* line, char* bssid, int* freq, int* rssi, char* features,
									 char* ssid) {
	return sscanf(line, "%17[0-9a-fA-F:]\t%d\t%d\t%127[^\t]\t%63[^\n]", bssid, freq, rssi, features, ssid);
}

#endif
