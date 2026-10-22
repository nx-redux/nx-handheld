// Host unit test for bt_addr.h (generic_bt.c address check).
#include <assert.h>
#include <stdio.h>
#include "../bt_addr.h"

int main(void) {
	assert(BtAddr_isValid("04:B4:FE:32:f9:73"));
	assert(BtAddr_isValid("00:00:00:00:00:00"));
	// Regression: a device name carrying "\nDevice ;reboot;#" made ";reboot;#"
	// the address that went into `sh -c`.
	assert(!BtAddr_isValid(";reboot;#"));
	assert(!BtAddr_isValid("04:B4:FE:32:F9:7"));
	assert(!BtAddr_isValid("04:B4:FE:32:F9:733"));
	assert(!BtAddr_isValid("04-B4-FE-32-F9-73"));
	assert(!BtAddr_isValid("0G:B4:FE:32:F9:73"));
	assert(!BtAddr_isValid("04:B4:FE:32:F9:7;"));
	assert(!BtAddr_isValid(""));
	assert(!BtAddr_isValid(NULL));
	printf("test_bt_addr: OK\n");
	return 0;
}
