#include "tz_country.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

// Forward-declare the platform accessor (avoids pulling in heavy api.h here).
// Returns a malloc'd zone name (caller frees), or NULL.
extern char* PLAT_getCurrentTimezone(void);

#ifndef TZ_ZONE_TAB_PATH
#define TZ_ZONE_TAB_PATH "/usr/share/zoneinfo/zone.tab"
#endif

#ifndef TZ_LOCALTIME_LINK
#define TZ_LOCALTIME_LINK "/tmp/localtime"
#endif

#ifndef TZ_ETC_LOCALTIME_LINK
#define TZ_ETC_LOCALTIME_LINK "/etc/localtime"
#endif

bool TZ_zoneToCountry(const char* zone, const char* zone_tab_path, char* out, int out_sz) {
	if (!zone || !zone[0])
		return false;
	FILE* f = fopen(zone_tab_path, "r");
	if (!f)
		return false;

	char line[512];
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#')
			continue;
		char* cc = strtok(line, "\t");
		if (!cc)
			continue;
		(void)strtok(NULL, "\t"); // coords
		char* zn = strtok(NULL, "\t\n");
		if (!zn)
			continue;
		if (strcmp(zn, zone) == 0) {
			snprintf(out, out_sz, "%s", cc);
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

bool TZ_zoneFromPath(const char* path, char* out, int out_sz) {
	static const char marker[] = "zoneinfo/";
	if (!path || !out || out_sz <= 0)
		return false;
	const char* zone = NULL;
	for (const char* p = strstr(path, marker); p; p = strstr(p + 1, marker))
		zone = p + sizeof(marker) - 1;
	if (!zone || !zone[0] || (int)strlen(zone) >= out_sz)
		return false;
	snprintf(out, out_sz, "%s", zone);
	return true;
}

bool TZ_zoneFromLink(const char* link_path, char* out, int out_sz) {
	char target[PATH_MAX];
	ssize_t len = readlink(link_path, target, sizeof(target) - 1);
	if (len <= 0)
		return false;
	target[len] = '\0';
	if (TZ_zoneFromPath(target, out, out_sz))
		return true;
	char resolved[PATH_MAX];
	return realpath(link_path, resolved) && TZ_zoneFromPath(resolved, out, out_sz);
}

bool TZ_currentCountryCode(char* out, int out_sz) {
	char zone[256];
	if (TZ_zoneFromLink(TZ_LOCALTIME_LINK, zone, sizeof(zone)) ||
		TZ_zoneFromLink(TZ_ETC_LOCALTIME_LINK, zone, sizeof(zone)))
		return TZ_zoneToCountry(zone, TZ_ZONE_TAB_PATH, out, out_sz);

	char* plat_zone = PLAT_getCurrentTimezone();
	if (!plat_zone)
		return false;
	bool ok = TZ_zoneToCountry(plat_zone, TZ_ZONE_TAB_PATH, out, out_sz);
	free(plat_zone);
	return ok;
}
