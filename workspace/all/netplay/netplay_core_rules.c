#include "netplay_core_rules.h"

#include <string.h>

// Everything that changes the emulation, or how a raw stick or button reaches the game. GLideN64's copies to RDRAM put
// a different image where the game reads it, and its threaded renderer makes that timing-dependent: both off. Rice and
// GLideN64 still differ in the framebuffer region of RDRAM only, which the game never reads back.
static const NetplayForcedOption n64_options[] = {
	{"mupen64plus-cpucore", "dynamic_recompiler"},
	{"mupen64plus-rsp-plugin", "hle"},
	{"mupen64plus-Framerate", "Original"},
	{"mupen64plus-virefresh", "Auto"},
	{"mupen64plus-CountPerOp", "0"},
	{"mupen64plus-CountPerOpDenomPot", "0"},
	{"mupen64plus-ForceDisableExtraMem", "False"},
	{"mupen64plus-IgnoreTLBExceptions", "False"},
	{"mupen64plus-alt-map", "True"},
	{"mupen64plus-astick-deadzone", "15"},
	{"mupen64plus-astick-sensitivity", "100"},
	{"mupen64plus-r-cbutton", "C1"},
	{"mupen64plus-l-cbutton", "C2"},
	{"mupen64plus-d-cbutton", "C3"},
	{"mupen64plus-u-cbutton", "C4"},
	{"mupen64plus-pak1", "memory"},
	{"mupen64plus-pak2", "none"},
	{"mupen64plus-pak3", "none"},
	{"mupen64plus-pak4", "none"},
	{"mupen64plus-EnableCopyColorToRDRAM", "Off"},
	{"mupen64plus-EnableCopyDepthToRDRAM", "Off"},
	{"mupen64plus-ThreadedRenderer", "False"},
};

static int is_mupen(const char* core_name) {
	return core_name && strcmp(core_name, "mupen64plus_next") == 0;
}

int NetplayRules_presyncFrames(const char* core_name) {
	return is_mupen(core_name) ? 30 : 0;
}

const NetplayForcedOption* NetplayRules_forcedOptions(const char* core_name, size_t* count) {
	if (is_mupen(core_name)) {
		*count = sizeof(n64_options) / sizeof(n64_options[0]);
		return n64_options;
	}
	*count = 0;
	return NULL;
}
