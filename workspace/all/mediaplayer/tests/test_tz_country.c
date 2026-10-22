// Host-compiled unit test for tz_country.c.
// Build & run: see tests/run_tests.sh (it points the localtime links and
// zone.tab at /tmp/nx_tz_test/ with -D overrides).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../../common/tz_country.h"

#define TZ_DIR "/tmp/nx_tz_test"

// Stub the platform call so the device convenience wrapper links on host. It
// is only the fallback once both localtime links are unreadable.
static const char* plat_zone = "Asia/Tokyo";
static int plat_calls = 0;
char* PLAT_getCurrentTimezone(void) {
	plat_calls++;
	return plat_zone ? strdup(plat_zone) : NULL;
}

static void relink(const char* target, const char* link_path) {
	unlink(link_path);
	if (target)
		assert(symlink(target, link_path) == 0);
}

static void write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(content, f);
	fclose(f);
}

int main(void) {
	const char* tab = "/tmp/test_zone.tab";
	write_file(tab,
			   "# comment line\n"
			   "AD\t+4230+00131\tEurope/Andorra\n"
			   "MY\t+0310+10142\tAsia/Kuala_Lumpur\n"
			   "US\t+404251-0740023\tAmerica/New_York\tEastern\n");

	char cc[8];
	assert(TZ_zoneToCountry("Asia/Kuala_Lumpur", tab, cc, sizeof(cc)) == true);
	assert(strcmp(cc, "MY") == 0);

	assert(TZ_zoneToCountry("America/New_York", tab, cc, sizeof(cc)) == true);
	assert(strcmp(cc, "US") == 0);

	// Unknown zone -> false.
	assert(TZ_zoneToCountry("Mars/Olympus", tab, cc, sizeof(cc)) == false);
	// Missing file -> false.
	assert(TZ_zoneToCountry("Asia/Kuala_Lumpur", "/tmp/no-such.tab", cc, sizeof(cc)) == false);
	// NULL/empty zone -> false.
	assert(TZ_zoneToCountry("", tab, cc, sizeof(cc)) == false);

	// Zone names from zoneinfo paths.
	char zone[64];
	assert(TZ_zoneFromPath("/usr/share/zoneinfo/Asia/Kuala_Lumpur", zone, sizeof(zone)));
	assert(strcmp(zone, "Asia/Kuala_Lumpur") == 0);
	assert(TZ_zoneFromPath("../zoneinfo/America/Argentina/Buenos_Aires", zone, sizeof(zone)));
	assert(strcmp(zone, "America/Argentina/Buenos_Aires") == 0);
	assert(TZ_zoneFromPath("/a/zoneinfo/posix/zoneinfo/Europe/Oslo", zone, sizeof(zone)));
	assert(strcmp(zone, "Europe/Oslo") == 0);
	assert(!TZ_zoneFromPath("/tmp/localtime", zone, sizeof(zone)));
	assert(!TZ_zoneFromPath("/usr/share/zoneinfo/", zone, sizeof(zone)));
	assert(!TZ_zoneFromPath(NULL, zone, sizeof(zone)));
	assert(!TZ_zoneFromPath("/usr/share/zoneinfo/Asia/Kuala_Lumpur", zone, 8)); // too long: no partial zone

	// The device wrapper reads the links with no platform init (musicplayerd
	// never calls GFX_init, so tg5050's PLAT_getCurrentTimezone is NULL there).
	mkdir(TZ_DIR, 0755);
	mkdir(TZ_DIR "/zoneinfo", 0755);
	mkdir(TZ_DIR "/zoneinfo/Asia", 0755);
	write_file(TZ_DIR "/zoneinfo/Asia/Kuala_Lumpur", "TZif");
	write_file(TZ_DIR "/zone.tab", "MY\t+0310+10142\tAsia/Kuala_Lumpur\nJP\t+353916+1394441\tAsia/Tokyo\n");

	// /tmp/localtime -> zoneinfo file.
	relink("/usr/share/zoneinfo/Asia/Kuala_Lumpur", TZ_DIR "/tmp_localtime");
	relink(NULL, TZ_DIR "/etc_localtime");
	plat_calls = 0;
	assert(TZ_zoneFromLink(TZ_DIR "/tmp_localtime", zone, sizeof(zone)));
	assert(strcmp(zone, "Asia/Kuala_Lumpur") == 0);
	assert(TZ_currentCountryCode(cc, sizeof(cc)) && strcmp(cc, "MY") == 0);
	assert(plat_calls == 0);

	// /tmp/localtime missing; /etc/localtime -> another link -> zoneinfo file
	// (resolved with realpath, so the chain must exist).
	relink(NULL, TZ_DIR "/tmp_localtime");
	relink(TZ_DIR "/zoneinfo/Asia/Kuala_Lumpur", TZ_DIR "/mid_localtime");
	relink(TZ_DIR "/mid_localtime", TZ_DIR "/etc_localtime");
	assert(TZ_currentCountryCode(cc, sizeof(cc)) && strcmp(cc, "MY") == 0);
	assert(plat_calls == 0);

	// No usable link -> the platform accessor.
	relink(NULL, TZ_DIR "/etc_localtime");
	assert(TZ_currentCountryCode(cc, sizeof(cc)) && strcmp(cc, "JP") == 0);
	assert(plat_calls == 1);

	// Nothing resolves -> false (caller skips home-country pinning).
	plat_zone = NULL;
	assert(!TZ_currentCountryCode(cc, sizeof(cc)));

	// A link to a zone missing from zone.tab -> false, no platform fallback.
	plat_zone = "Asia/Tokyo";
	plat_calls = 0;
	relink("/usr/share/zoneinfo/Mars/Olympus", TZ_DIR "/tmp_localtime");
	assert(!TZ_currentCountryCode(cc, sizeof(cc)));
	assert(plat_calls == 0);
	relink(NULL, TZ_DIR "/tmp_localtime");
	relink(NULL, TZ_DIR "/mid_localtime");

	printf("test_tz_country: OK\n");
	return 0;
}
