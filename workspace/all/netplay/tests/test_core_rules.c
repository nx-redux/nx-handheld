// Host-compiled tests: per-core lockstep session rules (presync frames, forced options).
// Build & run: workspace/all/netplay/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../netplay_core_rules.h"

static const char* forced(const char* core, const char* key) {
	size_t n = 0;
	const NetplayForcedOption* o = NetplayRules_forcedOptions(core, &n);
	for (size_t i = 0; i < n; i++)
		if (strcmp(o[i].key, key) == 0)
			return o[i].value;
	return NULL;
}

static void test_presync_only_mupen(void) {
	assert(NetplayRules_presyncFrames("mupen64plus_next") == 30);
	assert(NetplayRules_presyncFrames("snes9x") == 0);
	assert(NetplayRules_presyncFrames("fbneo") == 0);
	assert(NetplayRules_presyncFrames(NULL) == 0);
}

static void test_n64_option_set(void) {
	// NX Redux Mobile's NetplayPads N64_LOCKSTEP forces the same 22 values
	static const char* want[][2] = {
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
	size_t n = 0;
	assert(NetplayRules_forcedOptions("mupen64plus_next", &n) != NULL);
	assert(n == sizeof(want) / sizeof(want[0]));
	for (size_t i = 0; i < n; i++) {
		const char* v = forced("mupen64plus_next", want[i][0]);
		assert(v && strcmp(v, want[i][1]) == 0);
	}
}

static void test_other_cores_force_nothing(void) {
	size_t n = 7;
	assert(NetplayRules_forcedOptions("snes9x", &n) == NULL && n == 0);
	n = 7;
	assert(NetplayRules_forcedOptions(NULL, &n) == NULL && n == 0);
}

int main(void) {
	test_presync_only_mupen();
	test_n64_option_set();
	test_other_cores_force_nothing();
	printf("test_core_rules: ALL PASS\n");
	return 0;
}
