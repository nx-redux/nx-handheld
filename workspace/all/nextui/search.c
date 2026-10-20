#include "search.h"

#include "api.h"
#include "content.h"
#include "defines.h"
#include "gamelist.h"
#include "imgloader.h"
#include "launcher.h"
#include "list_window.h"
#include "menutabs.h"
#include "types.h"
#include "ui_keyboard.h"

#include <stdlib.h>
#include <string.h>

bool Search_open(void) {
	// START opened us, so a START tap on the keyboard closes us again (#112)
	char* query = UIKeyboard_openEx("Search", KB_START_CANCELS);
	// the keyboard cleared every GPU layer; if we end up staying on the game
	// list (cancel / empty query) nothing else re-uploads its background
	requestBackgroundReupload();
	PAD_poll();
	PAD_reset();

	if (!query || strlen(query) == 0) {
		free(query);
		return false;
	}
	Content_setSearchQuery(query);
	free(query);

	MenuTabs_leaveFocus(); // B back to the root returns to the lit content (as openDirectory)
	Directory* results = Directory_new(FAUX_SEARCH_PATH, 0);
	ListWindow_fromTop(results->entries->count, GameList_rowCountAt(false), &results->start, &results->end);
	Array_push(stack, results);
	top = results;
	return true;
}
