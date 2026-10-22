#ifndef TOAST_TICK_H
#define TOAST_TICK_H

#include <stdbool.h>
#include <stdint.h>

// Pure core of UI_tickToast (no SDL, host-tested): while `message` is showing
// (non-empty) every tick marks the screen dirty; once `duration_ms` has passed
// since `toast_time`, the message is cleared, with one more dirty frame so the
// next UI_renderToast wipes the toast's GPU layer.
static inline void UI_toastTickAt(char* message, uint32_t toast_time, uint32_t now, uint32_t duration_ms,
								  bool* dirty) {
	if (message[0] == '\0')
		return;
	if (now - toast_time >= duration_ms)
		message[0] = '\0';
	*dirty = true;
}

#endif // TOAST_TICK_H
