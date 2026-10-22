#ifndef USBLINK_STATE_H
#define USBLINK_STATE_H

#include <stdbool.h>

#include "usblink_link.h"

// Where the daemon (usblink.c) keeps its pid and publishes its state.
#define USBLINK_PID_PATH "/tmp/usblink.pid"
#define USBLINK_STATE_PATH "/tmp/usblink.state"

// Shell-sourceable status file the daemon publishes for the UI/scripts:
// USBLINK_LINK=up|down|error, USBLINK_SIDE=host|device|"", USBLINK_LOCAL_IP,
// USBLINK_PEER_IP (only when up), USBLINK_ERROR.
typedef struct {
	char link[8];
	char side[8];
	char local_ip[16];
	char peer_ip[16];
	char error[32];
} UsbLinkState;

int usblink_state_write(const char* path, const UsbLinkState* s);					  // 0 / -1, tmp file + rename
int usblink_state_read(const char* path, UsbLinkState* s);							  // 0 / -1; zeroes *s first
void usblink_state_from_link(const UsbLink* l, const char* error, UsbLinkState* out); // fills link/side/ips

// The text to show for an "error" state: a version mismatch, or no link on
// this device.
const char* usblink_state_error_message(const UsbLinkState* s);

// The apps' side of the daemon (Device Sync, the netplay wizard).
// `usblink.elf start` waits up to ~3 s for the daemon's first state and
// returns 0 when one runs (new or already); `usblink.elf stop` always ends
// the link (also the repair for a SIGKILLed daemon) and is idempotent.
int usblink_ctl_start(void);
void usblink_ctl_stop(void);

#endif
