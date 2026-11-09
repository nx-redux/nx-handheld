#ifndef NETPLAY_CORE_RULES_H
#define NETPLAY_CORE_RULES_H

#include <stddef.h>

// Per-core rules of a lockstep session beyond input and state, the same on NX Redux Mobile (NetplayCores, NetplayPads)
// so a handheld and a phone run the same emulation.

typedef struct {
	const char* key;
	const char* value;
} NetplayForcedOption;

// Frames each side runs on its own before the connect-time state sync: mupen64plus-next refuses a state until it ran.
int NetplayRules_presyncFrames(const char* core_name);

// The options locked to one value for the session (the user's values untouched); NULL and *count 0 for none.
const NetplayForcedOption* NetplayRules_forcedOptions(const char* core_name, size_t* count);

#endif
