// Host unit test for bt_addr.h (generic_bt.c address check and device-line parser).
#include <assert.h>
#include <stdio.h>
#include <string.h>
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

	// `bluetoothctl devices` lines
	char addr[18], name[8];
	assert(BtAddr_parseDeviceLine("Device 04:B4:FE:32:F9:73 Pad", addr, name, sizeof(name)));
	assert(strcmp(addr, "04:B4:FE:32:F9:73") == 0 && strcmp(name, "Pad") == 0);
	assert(BtAddr_parseDeviceLine("Device 04:B4:FE:32:F9:73 Long Name Here", addr, name, sizeof(name)));
	assert(strcmp(name, "Long Na") == 0); // cut to name_size - 1
	// no name: the 24-byte line ends at the address, the 25-byte one at its space
	assert(BtAddr_parseDeviceLine("Device 04:B4:FE:32:F9:73", addr, name, sizeof(name)) && name[0] == '\0');
	assert(BtAddr_parseDeviceLine("Device 04:B4:FE:32:F9:73 ", addr, name, sizeof(name)) && name[0] == '\0');
	assert(BtAddr_parseDeviceLine("Device 04:B4:FE:32:F9:73 X", addr, NULL, 0));
	assert(!BtAddr_parseDeviceLine("Device ;reboot;# x", addr, name, sizeof(name)) && name[0] == '\0');
	assert(!BtAddr_parseDeviceLine("Device 04:B4:FE:32:F9:7 Pad", addr, name, sizeof(name)));
	assert(!BtAddr_parseDeviceLine("Controller 04:B4:FE:32:F9:73 Pad", addr, name, sizeof(name)));
	assert(!BtAddr_parseDeviceLine("Device ", addr, name, sizeof(name)));
	assert(!BtAddr_parseDeviceLine(NULL, addr, name, sizeof(name)));
	printf("test_bt_addr: OK\n");
	return 0;
}
