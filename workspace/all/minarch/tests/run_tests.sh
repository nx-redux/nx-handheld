#!/bin/sh
# Host-side unit tests for minarch/. No SDL, no cross toolchain.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
CC="cc -std=gnu99 -Wall -Werror -fsanitize=address -g"
# common/defines.h (via utils.c, ma_cheats.h, config.h) needs a platform.h
PLAT="-I../../common -I../../../tg5040/platform -DPLATFORM=\"tg5040\" -Wno-deprecated-declarations"

$CC -I.. -o /tmp/nx_test_archive_cache ../ma_archive_cache.c test_archive_cache.c
/tmp/nx_test_archive_cache
$CC -I.. -o /tmp/nx_test_cheat_match ../ma_cheat_match.c test_cheat_match.c
/tmp/nx_test_cheat_match
$CC -I.. -o /tmp/nx_test_cheat_mem ../ma_cheat_mem.c test_cheat_mem.c
/tmp/nx_test_cheat_mem
$CC -I.. $PLAT -o /tmp/nx_test_cheat_parse ../ma_cheat_parse.c ../ma_cheat_mem.c ../../common/utils.c test_cheat_parse.c
/tmp/nx_test_cheat_parse
$CC -I.. -o /tmp/nx_test_core_progress ../ma_core_progress.c test_core_progress.c -lpthread
/tmp/nx_test_core_progress
$CC -I.. -o /tmp/nx_test_ff_show test_ff_show.c
/tmp/nx_test_ff_show
$CC -I../../common -o /tmp/nx_test_flip_schedule test_flip_schedule.c
/tmp/nx_test_flip_schedule
$CC -I.. $PLAT -o /tmp/nx_test_save_paths ../ma_save_paths.c test_save_paths.c
/tmp/nx_test_save_paths
$CC -I.. -o /tmp/nx_test_rewind_budget ../ma_rewind_budget.c test_rewind_budget.c
/tmp/nx_test_rewind_budget
$CC -I.. -o /tmp/nx_test_rewind_ring ../ma_rewind_ring.c test_rewind_ring.c
/tmp/nx_test_rewind_ring
$CC -I.. -o /tmp/nx_test_state_read ../ma_state_read.c test_state_read.c
/tmp/nx_test_state_read
$CC -I.. -o /tmp/nx_test_config_value ../ma_config_value.c test_config_value.c
/tmp/nx_test_config_value
$CC -I.. -o /tmp/nx_test_file_list ../ma_file_list.c test_file_list.c
/tmp/nx_test_file_list
$CC -I.. -o /tmp/nx_test_option_list ../ma_option_list.c test_option_list.c
/tmp/nx_test_option_list
$CC -I.. $PLAT -o /tmp/nx_test_alias ../ma_alias.c ../../common/utils.c test_alias.c
/tmp/nx_test_alias
$CC -I.. -o /tmp/nx_test_ra_helpers ../ra_helpers.c test_ra_helpers.c
/tmp/nx_test_ra_helpers

# The rest include headers from libraries the minarch build fetches (not in git):
# libretro-common (libretro.h), rcheevos and libchdr. Run a minarch build once first.
if [ -d ../libretro-common/include ]; then
	$CC -I.. -I../libretro-common/include -o /tmp/nx_test_avinfo ../ma_avinfo.c test_avinfo.c
	/tmp/nx_test_avinfo
	$CC -I.. -I../libretro-common/include -o /tmp/nx_test_emutime ../ma_emutime.c test_emutime.c -lm
	/tmp/nx_test_emutime
	$CC -I.. -I../../common -I../libretro-common/include -o /tmp/nx_test_hwrender ../ma_hwrender.c ../ma_emutime.c test_hwrender.c
	/tmp/nx_test_hwrender
else
	echo "SKIP: test_avinfo, test_emutime, test_hwrender (libretro-common not fetched)"
fi
if [ -d ../rcheevos/src/include ]; then
	# included as <rcheevos/...>
	mkdir -p /tmp/nx_test_rcinc
	ln -sfn "$PWD/../rcheevos/src/include" /tmp/nx_test_rcinc/rcheevos
	$CC -I.. -I/tmp/nx_test_rcinc -o /tmp/nx_test_ra_consoles test_ra_consoles.c
	/tmp/nx_test_ra_consoles
else
	echo "SKIP: test_ra_consoles (rcheevos not fetched)"
fi
if [ -d ../libchdr/include ]; then
	$CC -I.. -I../libchdr/include -o /tmp/nx_test_chd_tracks ../chd_tracks.c test_chd_tracks.c
	/tmp/nx_test_chd_tracks
else
	echo "SKIP: test_chd_tracks (libchdr not fetched)"
fi
