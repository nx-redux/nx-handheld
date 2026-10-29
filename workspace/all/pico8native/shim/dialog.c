#include "dialog.h"

enum dlg_action dlg_game(struct dlg* d, unsigned held, int volume_pressed) {
	unsigned was = d->was;
	d->was = held;
	if (d->open)
		return DLG_NONE;
	if (held & DLG_MENU) {
		if (!(was & DLG_MENU))
			d->spoiled = 0;
		if (volume_pressed || (held & ~DLG_MENU))
			d->spoiled = 1;
		return DLG_NONE;
	}
	if (!(was & DLG_MENU) || d->spoiled)
		return DLG_NONE;
	d->open = 1;
	d->menu_in_dialog = 0;
	return DLG_OPEN;
}

enum dlg_action dlg_open_step(struct dlg* d, unsigned held) {
	unsigned pressed = held & ~d->was;
	unsigned released = d->was & ~held;
	d->was = held;
	if (!d->open)
		return DLG_NONE;
	if (pressed & DLG_MENU)
		d->menu_in_dialog = 1;
	if (pressed & DLG_A)
		return DLG_QUIT;
	if ((pressed & DLG_B) || ((released & DLG_MENU) && d->menu_in_dialog))
		return DLG_CANCEL;
	return DLG_NONE;
}

void dlg_close(struct dlg* d, uint32_t pad_held) {
	d->open = 0;
	d->hidden |= pad_held;
	d->spoiled = 1; // a Menu still down from the dialog opens nothing when let go
}

void dlg_released(struct dlg* d, uint32_t pad_held) {
	d->hidden &= pad_held;
}
