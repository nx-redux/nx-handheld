#ifndef NX_EDGE_H
#define NX_EDGE_H

// The screen's side margin, the one value for both sides of the main menu: Home's cards (the Continue card's left, the
// tools column's right) and the tab row in the bottom bar (the first label's left, the hints' right). In dp; Home's
// layouts take it in their own units, rounded to a whole one (home_layout.h HOME_EDGE: 51), the same px: 34 on the
// Brick, 29 on the Brick Pro, 26 on the Smart Pro S. SDL-free, for the host-tested models too.
#define NX_EDGE_DP 16.0f

// The room between the content's last row and the bottom bar (the tab row): the Grid's tiles (and their room at the
// top of the body), Home's top section. In dp; Home rounds it to its whole units (home_layout.h HOME_PAGE_BOTTOM).
#define NX_BOTTOM_GAP_DP 4.0f

#endif // NX_EDGE_H
