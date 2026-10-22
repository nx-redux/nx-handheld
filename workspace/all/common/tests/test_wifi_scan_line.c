// Host unit test for wifi_scan_line.h (pure, no SDL). Run via run_tests.sh,
// which builds it with AddressSanitizer so buffer overruns fail loudly.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../wifi_scan_line.h"

// Regression: a 32-byte UTF-8 SSID prints as 128 chars of `\xNN`; the old
// %127[^\n] wrote all of it into the 64-byte ssid field.
static void test_escaped_ssid_does_not_overflow(void) {
	char line[512];
	int n = snprintf(line, sizeof(line), "04:b4:fe:32:f9:73\t2462\t-63\t[WPA2-PSK-CCMP][ESS]\t");
	for (int i = 0; i < 32; i++)
		n += snprintf(line + n, sizeof(line) - n, "\\xe4");

	char bssid[WIFI_SCAN_BSSID_SIZE];
	char features[WIFI_SCAN_FEATURES_SIZE];
	char ssid[WIFI_SCAN_SSID_SIZE];
	int freq = 0, rssi = 0;
	assert(WifiScan_parseLine(line, bssid, &freq, &rssi, features, ssid) == 5);
	assert(strlen(ssid) == WIFI_SCAN_SSID_SIZE - 1);
	assert(strcmp(bssid, "04:b4:fe:32:f9:73") == 0);
	assert(freq == 2462 && rssi == -63);
}

static void test_plain_line(void) {
	char bssid[WIFI_SCAN_BSSID_SIZE];
	char features[WIFI_SCAN_FEATURES_SIZE];
	char ssid[WIFI_SCAN_SSID_SIZE];
	int freq = 0, rssi = 0;
	assert(WifiScan_parseLine("aa:bb:cc:dd:ee:ff\t5180\t-40\t[ESS]\tmy net \r", bssid, &freq, &rssi, features,
							  ssid) == 5);
	assert(strcmp(features, "[ESS]") == 0);
	assert(strcmp(ssid, "my net \r") == 0); // CR trimmed by the caller, spaces kept
}

static void test_hidden_ssid(void) {
	char bssid[WIFI_SCAN_BSSID_SIZE];
	char features[WIFI_SCAN_FEATURES_SIZE];
	char ssid[WIFI_SCAN_SSID_SIZE] = "";
	int freq = 0, rssi = 0;
	assert(WifiScan_parseLine("aa:bb:cc:dd:ee:ff\t5180\t-40\t[ESS]\t", bssid, &freq, &rssi, features, ssid) == 4);
	assert(ssid[0] == '\0');
}

int main(void) {
	test_escaped_ssid_does_not_overflow();
	test_plain_line();
	test_hidden_ssid();
	printf("test_wifi_scan_line: OK\n");
	return 0;
}
