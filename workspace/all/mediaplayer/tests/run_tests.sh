#!/bin/sh
# Host-side unit tests for mediaplayer/. No SDL library linked, no cross toolchain.
# test_video_browser includes common/api.h, so it needs the SDL2 headers (found
# with sdl2-config); it is skipped when sdl2-config is missing.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_iptv_sort ../iptv_curated_sort.c test_iptv_sort.c
/tmp/nx_test_iptv_sort
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_m3u ../m3u.c test_m3u.c
/tmp/nx_test_m3u
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -o /tmp/nx_test_net_cache ../../common/net_cache.c test_net_cache.c
/tmp/nx_test_net_cache
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -DPOSITIONS_DIR='"/tmp/pos-test"' -DPOSITIONS_FILE='"/tmp/pos-test/positions.cfg"' -o /tmp/nx_test_positions ../positions.c test_positions.c
/tmp/nx_test_positions
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -DTZ_LOCALTIME_LINK='"/tmp/nx_tz_test/tmp_localtime"' -DTZ_ETC_LOCALTIME_LINK='"/tmp/nx_tz_test/etc_localtime"' -DTZ_ZONE_TAB_PATH='"/tmp/nx_tz_test/zone.tab"' -o /tmp/nx_test_tz_country ../../common/tz_country.c test_tz_country.c
/tmp/nx_test_tz_country
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_ffplay_parse test_ffplay_parse.c
/tmp/nx_test_ffplay_parse
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_iptv_text test_iptv_text.c
/tmp/nx_test_iptv_text
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -I../../include -I../../../tg5040/platform -DPLATFORM=\"tg5040\" -DIPTV_CHANNELS_DIR='"/tmp/nx_test_iptv_user"' -o /tmp/nx_test_iptv_user ../iptv.c ../../include/parson/parson.c test_iptv_user.c
/tmp/nx_test_iptv_user
if command -v sdl2-config >/dev/null 2>&1; then
	cc -std=gnu99 -Wall -Werror -fsanitize=address -g -DUSE_SDL2 -I.. -I../../common -I../../../tg5040/platform -DPLATFORM=\"tg5040\" $(sdl2-config --cflags) -I"$(sdl2-config --prefix)/include" -o /tmp/nx_test_video_browser ../video_browser.c ../../common/dir_list.c test_video_browser.c
	/tmp/nx_test_video_browser
else
	echo "SKIP test_video_browser: sdl2-config not found (SDL2 headers needed)"
fi
