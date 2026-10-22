// Host-compiled unit test for audiomon_parse.h: BlueZ object path -> MAC (the
// settings lookup is common/settings_kv.h, tested in common/tests).
// Build & run (from workspace/all/audiomon):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_audiomon_parse.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../audiomon_parse.h"

static void test_path_to_mac(void) {
	char mac[18];
	assert(audiomon_path_to_mac("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/sep1/fd0", mac, sizeof(mac)) == 17);
	assert(strcmp(mac, "AA:BB:CC:DD:EE:FF") == 0);
	assert(audiomon_path_to_mac("/org/bluez/hci0/dev_01_23_45_67_89_AB", mac, sizeof(mac)) == 17);
	assert(strcmp(mac, "01:23:45:67:89:AB") == 0);

	assert(audiomon_path_to_mac("/org/bluez/hci0", mac, sizeof(mac)) == 0);
	assert(audiomon_path_to_mac("/org/bluez/hci0/dev_AA_BB/sep1", mac, sizeof(mac)) == 0);
	assert(mac[0] == '\0');
	assert(audiomon_path_to_mac("/org/bluez/hci0/dev_", mac, sizeof(mac)) == 0);

	char small[17];
	assert(audiomon_path_to_mac("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF", small, sizeof(small)) == 0);

	// Anything past 17 characters is not copied
	char big[32];
	memset(big, 'X', sizeof(big));
	assert(audiomon_path_to_mac("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF_00", big, sizeof(big)) == 17);
	assert(strcmp(big, "AA:BB:CC:DD:EE:FF") == 0 && big[18] == 'X');
}

int main(void) {
	test_path_to_mac();
	printf("test_audiomon_parse: all tests passed\n");
	return 0;
}
