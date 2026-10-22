#ifndef UI_CONTEXTMENU_ADD_H
#define UI_CONTEXTMENU_ADD_H

#include <stdio.h>
#include "ui_contextmenu.h"

// Append one item to a context-menu items array, bounded by BOTH the array's
// real capacity (`cap`) and the widget's copy cap (CONTEXTMENU_MAX_ITEMS); a
// full array drops the item.
static inline void ContextMenu_addItemCapped(ContextMenuItem* items, int* count, int cap, const char* label, int id) {
	if (cap > CONTEXTMENU_MAX_ITEMS)
		cap = CONTEXTMENU_MAX_ITEMS;
	if (*count >= cap)
		return;
	snprintf(items[*count].label, CONTEXTMENU_MAX_TEXT, "%s", label);
	items[*count].id = id;
	(*count)++;
}

// The capacity comes from the passed array, which must be a real array in
// scope (not a pointer), so a caller can't add past the end of its own small
// stack array.
#define ContextMenu_addItem(items, count, label, id) \
	ContextMenu_addItemCapped((items), (count), (int)(sizeof(items) / sizeof((items)[0])), (label), (id))

#endif // UI_CONTEXTMENU_ADD_H
