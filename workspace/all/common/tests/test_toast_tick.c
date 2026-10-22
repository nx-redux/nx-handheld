// Host test for ui/toast_tick.h (UI_tickToast's core).
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../ui/toast_tick.h"

int main(void) {
	char msg[16] = "";
	bool dirty = false;

	// No message: nothing to do.
	UI_toastTickAt(msg, 0, 5000, 3000, &dirty);
	assert(!dirty && msg[0] == '\0');

	// Visible: redraw, message kept.
	snprintf(msg, sizeof(msg), "Saved");
	UI_toastTickAt(msg, 1000, 3999, 3000, &dirty);
	assert(dirty && strcmp(msg, "Saved") == 0);

	// Expiry edge: message cleared with one more redraw.
	dirty = false;
	UI_toastTickAt(msg, 1000, 4000, 3000, &dirty);
	assert(dirty && msg[0] == '\0');

	// Already cleared: no further redraws.
	dirty = false;
	UI_toastTickAt(msg, 1000, 4001, 3000, &dirty);
	assert(!dirty);

	// Tick counter wrap: unsigned difference still measures the age.
	snprintf(msg, sizeof(msg), "Wrap");
	UI_toastTickAt(msg, 0xFFFFFF00u, 0x00000010u, 3000, &dirty);
	assert(dirty && strcmp(msg, "Wrap") == 0);

	printf("test_toast_tick: OK\n");
	return 0;
}
