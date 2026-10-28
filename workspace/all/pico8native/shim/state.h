#ifndef P8SHIM_STATE_H
#define P8SHIM_STATE_H

// Save states for PICO-8, which has none of its own: a snapshot of its memory, taken
// and put back while the shim holds it parked at a present (see state.c).
//
// The technique and much of the code are josegonzalez/minui-pico-8-pak's
// shim/state.c (MIT licence, LICENSE-minui-pico-8-pak.txt).

#include <stddef.h>
#include <stdint.h>

// Whether states are on for this run: P8NATIVE_STATES=1, the program is pico8_64 (it
// has the pstate symbol) and the arena is in place. Splore runs without them.
int state_available(void);

// Whether a state can be taken or put back now: states are on, the cart has started
// and run a frame, and no BBS download is running.
int state_ready(void);

// Called on every present PICO-8 makes
void state_frame(void);

// Takes the snapshot into out (cap bytes). Returns its size, or 0 with *why set.
size_t state_capture(uint8_t* out, size_t cap, const char** why);

// Puts back the snapshot in buf (len bytes) taken by this build of PICO-8, in this run
// or another one. 1 if it did; 0 with *why set and PICO-8 untouched if it could not.
int state_load(const uint8_t* buf, size_t len, const char** why);

#endif
