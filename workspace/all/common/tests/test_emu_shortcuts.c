// Host-side tests for emu_shortcuts.c, the global emulator shortcut file (built by run_tests.sh)

#include "emu_shortcuts.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DIR_ "/tmp/nx_shortcuts_test"

static char* slurp(const char* path) {
	FILE* f = fopen(path, "rb");
	assert(f);
	static char buf[8192];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

int main(void) {
	char path[256];

	// path: device suffix only when DEVICE is set
	assert(EmuShortcuts_getPath(path, sizeof(path), "/u", "brick"));
	assert(!strcmp(path, "/u/minarch-shortcuts-brick.cfg"));
	assert(EmuShortcuts_getPath(path, sizeof(path), "/u", NULL));
	assert(!strcmp(path, "/u/minarch-shortcuts.cfg"));
	assert(EmuShortcuts_getPath(path, sizeof(path), "/u", ""));
	assert(!strcmp(path, "/u/minarch-shortcuts.cfg"));
	assert(!EmuShortcuts_getPath(path, 8, "/u", "brick"));

	// stored names never change (existing files depend on them)
	assert(!strcmp(shortcut_names[SHORTCUT_SAVE_STATE], "Save State"));
	assert(!strcmp(shortcut_names[SHORTCUT_SAVE_QUIT], "Save & Quit"));
	assert(!strcmp(shortcut_names[SHORTCUT_TOGGLE_TURBO_R2], "Toggle Turbo R2"));
	assert(!strcmp(shortcut_names[SHORTCUT_TOGGLE_DPAD_JOYSTICK], "Toggle D-pad Joystick"));
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		assert(shortcut_names[i] && EmuShortcuts_find(shortcut_names[i]) == i);
	assert(EmuShortcuts_find("Toggle Turbo Z") == -1);

	// labels: NONE, the 18 buttons, then MENU+ the same 18
	assert(button_labels[SHORTCUT_LABEL_COUNT] == NULL);
	assert(EmuShortcuts_labelIndex("NONE") == 0);
	assert(EmuShortcuts_labelIndex("UP") == 1);
	assert(EmuShortcuts_labelIndex("R4") == SHORTCUT_BUTTON_COUNT);
	assert(EmuShortcuts_labelIndex("MENU+UP") == SHORTCUT_BUTTON_COUNT + 1);
	assert(EmuShortcuts_labelIndex("MENU+R4") == SHORTCUT_LABEL_COUNT - 1);
	assert(EmuShortcuts_labelIndex("MENU+FOO") == -1);
	// minarch's built-in Game Switcher combo is the one reserved label
	assert(SHORTCUT_LABEL_MENU_SELECT == EmuShortcuts_labelIndex("MENU+SELECT"));
	assert(EmuShortcuts_isReserved(EmuShortcuts_labelIndex("MENU+SELECT")));
	assert(!EmuShortcuts_isReserved(EmuShortcuts_labelIndex("MENU+START")));
	assert(!EmuShortcuts_isReserved(EmuShortcuts_labelIndex("SELECT")));

	// label <-> (local BTN_ID, MENU modifier)
	int local, mod;
	EmuShortcuts_fromLabel(0, &local, &mod);
	assert(local == -1 && mod == 0);
	EmuShortcuts_fromLabel(EmuShortcuts_labelIndex("X"), &local, &mod);
	assert(local == 6 && mod == 0); // BTN_ID_X
	EmuShortcuts_fromLabel(EmuShortcuts_labelIndex("MENU+X"), &local, &mod);
	assert(local == 6 && mod == 1);
	assert(EmuShortcuts_toLabel(6, 1) == EmuShortcuts_labelIndex("MENU+X"));
	assert(EmuShortcuts_toLabel(6, 0) == EmuShortcuts_labelIndex("X"));
	assert(EmuShortcuts_toLabel(-1, 1) == 0);
	for (int l = 0; l < SHORTCUT_LABEL_COUNT; l++) {
		EmuShortcuts_fromLabel(l, &local, &mod);
		assert(EmuShortcuts_toLabel(local, mod) == l);
	}

	// parse: known names only, unknown label = NONE, CRLF and other keys tolerated
	int labels[SHORTCUT_COUNT];
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		labels[i] = 0;
	EmuShortcuts_parse("minarch_screen_scaling = Native\r\n"
					   "bind Save State = MENU+R1\r\n"
					   "bind A Button = B\n"
					   "bind Bogus Shortcut = MENU+A\n"
					   "bind Load State = MENU+L1\n"
					   "bind Toggle FF = SOMETHING\n"
					   "bind Screenshot = NONE\n"
					   "bind Toggle Turbo A = X",
					   labels);
	assert(labels[SHORTCUT_SAVE_STATE] == EmuShortcuts_labelIndex("MENU+R1"));
	assert(labels[SHORTCUT_LOAD_STATE] == EmuShortcuts_labelIndex("MENU+L1"));
	assert(labels[SHORTCUT_TOGGLE_FF] == 0);
	assert(labels[SHORTCUT_SCREENSHOT] == 0);
	assert(labels[SHORTCUT_TOGGLE_TURBO_A] == EmuShortcuts_labelIndex("X")); // last line, no newline
	assert(labels[SHORTCUT_RESET_GAME] == 0);

	// read: missing file = all NONE
	system("rm -rf " DIR_ " && mkdir -p " DIR_);
	assert(EmuShortcuts_getPath(path, sizeof(path), DIR_, "brick"));
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		labels[i] = 7;
	assert(!EmuShortcuts_read(path, labels));
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		assert(labels[i] == 0);

	// write/read round trip, every shortcut written (NONE included)
	int want[SHORTCUT_COUNT];
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		want[i] = (i * 5) % SHORTCUT_LABEL_COUNT;
	want[SHORTCUT_HOLD_FF] = 99; // out of range writes as NONE
	assert(EmuShortcuts_write(path, want));
	assert(access(DIR_ "/minarch-shortcuts-brick.cfg.tmp", F_OK) != 0);
	want[SHORTCUT_HOLD_FF] = 0;
	assert(EmuShortcuts_read(path, labels));
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		assert(labels[i] == want[i]);
	char* text = slurp(path);
	assert(strstr(text, "bind Save State = NONE\n"));
	assert(strstr(text, "bind Hold FF = NONE\n"));
	int lines = 0;
	for (char* p = text; *p; p++)
		lines += *p == '\n';
	assert(lines == SHORTCUT_COUNT);

	// unwritable dir fails cleanly
	assert(!EmuShortcuts_write(DIR_ "/missing/x.cfg", want));

	// merge: overrides win, NONE keeps the global, a global on an overridden button is dropped
	int global[SHORTCUT_COUNT] = {0}, over[SHORTCUT_COUNT] = {0}, out[SHORTCUT_COUNT];
	int menu_x = EmuShortcuts_labelIndex("MENU+X"), menu_y = EmuShortcuts_labelIndex("MENU+Y");
	int l2 = EmuShortcuts_labelIndex("L2");
	global[SHORTCUT_SAVE_STATE] = menu_x;
	global[SHORTCUT_LOAD_STATE] = menu_y;
	global[SHORTCUT_TOGGLE_FF] = l2;
	EmuShortcuts_merge(global, over, out);
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		assert(out[i] == global[i]);	// no overrides: the global file as is
	over[SHORTCUT_LOAD_STATE] = menu_x; // Load State moves to Save State's button
	over[SHORTCUT_SCREENSHOT] = EmuShortcuts_labelIndex("R2");
	EmuShortcuts_merge(global, over, out);
	assert(out[SHORTCUT_LOAD_STATE] == menu_x);
	assert(out[SHORTCUT_SAVE_STATE] == 0); // its global button is taken here
	assert(out[SHORTCUT_SCREENSHOT] == over[SHORTCUT_SCREENSHOT]);
	assert(out[SHORTCUT_TOGGLE_FF] == l2); // untouched global
	over[SHORTCUT_SAVE_STATE] = menu_y;	   // swap: both overridden
	EmuShortcuts_merge(global, over, out);
	assert(out[SHORTCUT_SAVE_STATE] == menu_y && out[SHORTCUT_LOAD_STATE] == menu_x);

	// labelOwner: another shortcut on the label, never the asking one or NONE
	for (int i = 0; i < SHORTCUT_COUNT; i++)
		labels[i] = 0;
	labels[SHORTCUT_TOGGLE_FF] = EmuShortcuts_labelIndex("R3");
	assert(EmuShortcuts_labelOwner(labels, EmuShortcuts_labelIndex("R3"), SHORTCUT_TOGGLE_DPAD_JOYSTICK) == SHORTCUT_TOGGLE_FF);
	assert(EmuShortcuts_labelOwner(labels, EmuShortcuts_labelIndex("R3"), SHORTCUT_TOGGLE_FF) == -1);
	assert(EmuShortcuts_labelOwner(labels, EmuShortcuts_labelIndex("MENU+R3"), -1) == -1);
	assert(EmuShortcuts_labelOwner(labels, 0, -1) == -1); // NONE is never taken

	system("rm -rf " DIR_);
	printf("test_emu_shortcuts: all passed\n");
	return 0;
}
