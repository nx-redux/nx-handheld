#ifndef SEARCH_H
#define SEARCH_H

#include <stdbool.h>

// START at a root: the keyboard for a query, then its results pushed over the root as a game list
// (FAUX_SEARCH_PATH, "Search: <query>"), so they draw in Layouts > Game list layout and take a game list's buttons.
// False when the keyboard was cancelled or the query left empty (nothing pushed).
bool Search_open(void);

#endif // SEARCH_H
