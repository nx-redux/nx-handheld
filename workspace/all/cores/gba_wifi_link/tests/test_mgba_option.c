// test_mgba_option.c - how mGBA's mgba_link_mode resolves at load.
// SPDX-License-Identifier: GPL-2.0-or-later
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "gwl_mode.h"

int main(void) {
	assert(gwl_mgba_pick("cable", GWL_RFU) == GWL_PICK_CABLE);
	assert(gwl_mgba_pick("auto", GWL_POKE) == GWL_PICK_WIFI);
	assert(gwl_mgba_pick("auto", GWL_NONE) == GWL_PICK_NONE);
	assert(gwl_mgba_pick("auto_cable", GWL_RFU) == GWL_PICK_WIFI);
	assert(gwl_mgba_pick("auto_cable", GWL_POKE) == GWL_PICK_CABLE);
	assert(gwl_mgba_pick("auto_cable", GWL_NONE) == GWL_PICK_CABLE);
	assert(gwl_mgba_pick(NULL, GWL_RFU) == GWL_PICK_CABLE);
	assert(gwl_mgba_pick("disabled", GWL_RFU) == GWL_PICK_NONE);
	assert(gwl_mgba_pick("bogus", GWL_RFU) == GWL_PICK_CABLE);
	// A pinned mode runs whatever the game's auto mode is.
	assert(gwl_mgba_pick("mul_aw2", GWL_NONE) == GWL_PICK_WIFI);
	assert(gwl_mgba_pick("rfu", GWL_POKE) == GWL_PICK_WIFI);

	assert(gwl_mgba_wifi_mode("auto", GWL_AW1) == GWL_AW1);
	assert(gwl_mgba_wifi_mode("auto_cable", GWL_RFU) == GWL_RFU);
	assert(gwl_mgba_wifi_mode("auto_cable", GWL_AW1) == GWL_NONE);
	assert(gwl_mgba_wifi_mode("rfu", GWL_POKE) == GWL_RFU);
	assert(gwl_mgba_wifi_mode("mul_poke", GWL_RFU) == GWL_POKE);
	assert(gwl_mgba_wifi_mode("mul_aw1", GWL_RFU) == GWL_AW1);
	assert(gwl_mgba_wifi_mode("mul_aw2", GWL_RFU) == GWL_AW2);
	assert(gwl_mgba_wifi_mode("cable", GWL_RFU) == GWL_NONE);
	assert(gwl_mgba_wifi_mode("disabled", GWL_RFU) == GWL_NONE);
	assert(gwl_mgba_wifi_mode(NULL, GWL_RFU) == GWL_NONE);

	puts("test_mgba_option: ok");
	return 0;
}
