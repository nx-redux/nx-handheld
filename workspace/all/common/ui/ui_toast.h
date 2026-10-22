#ifndef UI_TOAST_H
#define UI_TOAST_H

#include <stdbool.h>
#include <stdint.h>
#include "sdl.h"

#define TOAST_DURATION 3000

// Render toast notification to GPU layer (above all other content including scroll text).
// Call this at the end of your render function. Toast auto-hides after TOAST_DURATION.
void UI_renderToast(SDL_Surface* screen, const char* message, uint32_t toast_time);

// Keeps `message`'s GPU toast layer in sync with its lifetime: forces a redraw
// every frame it's visible, then once more on the expiry edge (clearing the
// message) so the next UI_renderToast call actually wipes the layer -
// otherwise UI_renderToast only re-clears when called again, and nothing would
// call it once the loop goes idle. Call once per loop iteration.
void UI_tickToast(char* message, uint32_t toast_time, bool* dirty);

// Clear toast from GPU layer (call when leaving screen or clearing state).
void UI_clearToast(void);

#endif // UI_TOAST_H
