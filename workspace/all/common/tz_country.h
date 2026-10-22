#ifndef __TZ_COUNTRY_H__
#define __TZ_COUNTRY_H__

#include <stdbool.h>

// Look up the ISO-3166-1 alpha-2 country code for an IANA zone name (e.g.
// "Asia/Kuala_Lumpur" -> "MY") using a zone.tab file
// (columns: country \t coords \t zone [\t comment]).
// Returns true and fills out (<= out_sz) on a match; false otherwise.
bool TZ_zoneToCountry(const char* zone, const char* zone_tab_path, char* out, int out_sz);

// Extract the IANA zone name from a zoneinfo file path, e.g.
// "/usr/share/zoneinfo/Asia/Kuala_Lumpur" -> "Asia/Kuala_Lumpur" (the part after
// the last "zoneinfo/"). Returns false when the path has no zone part.
bool TZ_zoneFromPath(const char* path, char* out, int out_sz);

// Read the zone name a localtime symlink points at: its link target, or, when
// that is another link outside zoneinfo (/etc/localtime -> /tmp/localtime), the
// fully resolved path. Returns false when the link is missing or names no zone.
bool TZ_zoneFromLink(const char* link_path, char* out, int out_sz);

// Resolve the device's current country code from the system timezone: the
// /tmp/localtime (then /etc/localtime) symlink every platform keeps pointed at
// the selected zone, falling back to PLAT_getCurrentTimezone, then
// /usr/share/zoneinfo/zone.tab. Needs no platform init, so a process that
// never calls GFX_init (musicplayerd) resolves it too. Returns false if
// unresolved (caller should then skip home-country pinning).
bool TZ_currentCountryCode(char* out, int out_sz);

#endif // __TZ_COUNTRY_H__
