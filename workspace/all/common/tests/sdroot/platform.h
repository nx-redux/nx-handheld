// Test-only platform.h: the real tg5040 one with the SD card moved to a host temp dir, so code that reads the card
// (Simple Mode PIN, recents, pins, m3u playlists) runs against fixtures. Put -Isdroot before the platform dir.
#ifndef NX_TEST_SDROOT_PLATFORM_H
#define NX_TEST_SDROOT_PLATFORM_H

#include "../../../../tg5040/platform/platform.h"

#undef SDCARD_PATH
#define SDCARD_PATH "/tmp/nx_test_sdroot"

#endif
