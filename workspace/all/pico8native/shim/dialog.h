#ifndef P8SHIM_DIALOG_H
#define P8SHIM_DIALOG_H

// The "Quit Splore?" dialog's input, kept apart from SDL so the host tests can run it
// (standalone.c draws it and feeds it the pad).
//
// Menu opens it when a press of Menu alone ends, so NextUI's Menu+volume brightness and
// any other chord leave it closed. While it is open: A quits, B cancels, and a tap of Menu
// cancels too. A button already held when it opens is not a press. Every button still
// held when it closes stays from PICO-8 until let go, so neither the press that closed it
// nor its release reaches the game.

#include <stdint.h>

// What the dialog reads, as a mask
#define DLG_MENU 1u
#define DLG_A 2u
#define DLG_B 4u
#define DLG_OTHER 8u // any other button: a chord with Menu

enum dlg_action {
	DLG_NONE,
	DLG_OPEN,	// open the dialog (dlg_open_step from here on)
	DLG_QUIT,	// A: PICO-8 gets SDL_QUIT
	DLG_CANCEL, // B or Menu: back to the game
};

struct dlg {
	int open;
	unsigned was;		// the mask at the last step
	int spoiled;		// this press of Menu went with another button or a volume key
	int menu_in_dialog; // Menu went down while the dialog was open
	uint32_t hidden;	// pad buttons (bit per button) kept from PICO-8 until released
};

// While the game runs, with what is held now and whether a volume key went down since
// the last call. DLG_OPEN once a lone press of Menu is let go; the dialog is open then.
enum dlg_action dlg_game(struct dlg* d, unsigned held, int volume_pressed);

// While open. DLG_QUIT or DLG_CANCEL close it (call dlg_close with what the pad holds).
enum dlg_action dlg_open_step(struct dlg* d, unsigned held);

// The dialog closed with the pad buttons in pad_held down: those are hidden until released
void dlg_close(struct dlg* d, uint32_t pad_held);

// The pad now holds pad_held: hidden buttons let go come back
void dlg_released(struct dlg* d, uint32_t pad_held);

static inline int dlg_hides(const struct dlg* d, int button) {
	return button >= 0 && button < 32 && ((d->hidden >> button) & 1);
}

#endif
