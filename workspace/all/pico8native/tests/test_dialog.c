// shim/dialog.c: when Menu opens the quit dialog, what closes it, and which buttons stay
// from PICO-8 afterwards.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shim/dialog.h"

#define CHECK(c)                                                                  \
	do {                                                                          \
		if (!(c)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

// Steps the game with each mask in turn; the action of the last
static enum dlg_action game(struct dlg* d, const unsigned* masks, int n) {
	enum dlg_action a = DLG_NONE;
	for (int i = 0; i < n; i++) {
		a = dlg_game(d, masks[i], 0);
		if (i < n - 1)
			CHECK(a == DLG_NONE);
	}
	return a;
}

static void open_dialog(struct dlg* d) {
	memset(d, 0, sizeof(*d));
	unsigned tap[] = {0, DLG_MENU, DLG_MENU, 0};
	CHECK(game(d, tap, 4) == DLG_OPEN);
	CHECK(d->open);
}

static void test_open(void) {
	struct dlg d;
	open_dialog(&d);

	// Menu held: nothing until it is let go
	memset(&d, 0, sizeof(d));
	CHECK(dlg_game(&d, DLG_MENU, 0) == DLG_NONE);
	CHECK(!d.open);

	// a chord with another button, before or after Menu, opens nothing
	memset(&d, 0, sizeof(d));
	unsigned chord[] = {DLG_MENU, DLG_MENU | DLG_OTHER, DLG_MENU, 0};
	CHECK(game(&d, chord, 4) == DLG_NONE);
	memset(&d, 0, sizeof(d));
	unsigned chord_a[] = {DLG_A, DLG_A | DLG_MENU, DLG_MENU, 0};
	CHECK(game(&d, chord_a, 4) == DLG_NONE);

	// Menu+volume (NextUI's brightness) opens nothing
	memset(&d, 0, sizeof(d));
	CHECK(dlg_game(&d, DLG_MENU, 0) == DLG_NONE);
	CHECK(dlg_game(&d, DLG_MENU, 1) == DLG_NONE);
	CHECK(dlg_game(&d, 0, 0) == DLG_NONE);

	// ...and the next lone tap does
	CHECK(dlg_game(&d, DLG_MENU, 0) == DLG_NONE);
	CHECK(dlg_game(&d, 0, 0) == DLG_OPEN);

	// the game's steps do nothing while it is open
	CHECK(dlg_game(&d, DLG_MENU, 0) == DLG_NONE);
	CHECK(dlg_game(&d, 0, 0) == DLG_NONE);
}

static void test_choices(void) {
	struct dlg d;

	open_dialog(&d);
	CHECK(dlg_open_step(&d, 0) == DLG_NONE);
	CHECK(dlg_open_step(&d, DLG_A) == DLG_QUIT);

	open_dialog(&d);
	CHECK(dlg_open_step(&d, 0) == DLG_NONE);
	CHECK(dlg_open_step(&d, DLG_B) == DLG_CANCEL);

	// Menu cancels when the tap ends
	open_dialog(&d);
	CHECK(dlg_open_step(&d, DLG_MENU) == DLG_NONE);
	CHECK(dlg_open_step(&d, DLG_MENU) == DLG_NONE);
	CHECK(dlg_open_step(&d, 0) == DLG_CANCEL);

	// other buttons do nothing
	open_dialog(&d);
	CHECK(dlg_open_step(&d, DLG_OTHER) == DLG_NONE);
	CHECK(dlg_open_step(&d, 0) == DLG_NONE);

	// a button already down when it opened is not a press: A held through the Menu tap
	// is a chord and opens nothing, so open with A down by stepping the game first
	memset(&d, 0, sizeof(d));
	unsigned tap[] = {DLG_MENU, 0};
	CHECK(game(&d, tap, 2) == DLG_OPEN);
	d.was = DLG_A | DLG_B; // A and B went down between the release and the dialog's first look
	CHECK(dlg_open_step(&d, DLG_A | DLG_B) == DLG_NONE);
	CHECK(dlg_open_step(&d, 0) == DLG_NONE);
	CHECK(dlg_open_step(&d, DLG_B) == DLG_CANCEL);

	// steps while closed do nothing
	memset(&d, 0, sizeof(d));
	CHECK(dlg_open_step(&d, DLG_A) == DLG_NONE);
}

static void test_hidden(void) {
	struct dlg d;
	open_dialog(&d);
	CHECK(dlg_open_step(&d, DLG_B) == DLG_CANCEL);
	dlg_close(&d, 1u << 1); // pad B still held
	CHECK(!d.open);
	CHECK(dlg_hides(&d, 1));
	CHECK(!dlg_hides(&d, 0));
	CHECK(!dlg_hides(&d, -1) && !dlg_hides(&d, 40));
	dlg_released(&d, 1u << 1); // still held
	CHECK(dlg_hides(&d, 1));
	dlg_released(&d, 0);
	CHECK(!dlg_hides(&d, 1));
	// pressed again later: PICO-8's
	dlg_released(&d, 1u << 1);
	CHECK(!dlg_hides(&d, 1));

	// Menu still held from a cancel by Menu+B opens nothing when let go
	open_dialog(&d);
	CHECK(dlg_open_step(&d, DLG_MENU) == DLG_NONE);
	CHECK(dlg_open_step(&d, DLG_MENU | DLG_B) == DLG_CANCEL);
	dlg_close(&d, 1u << 5 | 1u << 1);
	CHECK(dlg_game(&d, DLG_MENU, 0) == DLG_NONE);
	CHECK(dlg_game(&d, 0, 0) == DLG_NONE);
	CHECK(dlg_game(&d, DLG_MENU, 0) == DLG_NONE);
	CHECK(dlg_game(&d, 0, 0) == DLG_OPEN);
}

int main(void) {
	test_open();
	test_choices();
	test_hidden();
	printf("test_dialog: ok\n");
	return 0;
}
