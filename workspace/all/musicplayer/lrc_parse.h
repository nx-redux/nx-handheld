#ifndef __LRC_PARSE_H__
#define __LRC_PARSE_H__

#include "lyrics.h"

// Parse LRC text into lines sorted by time. A line with several timestamps
// ("[00:12.00][01:30.50]text") gives one line per timestamp; ".5", ".50" and
// ".500" fractions all read as 500 ms. Tag lines ([ar:...]) and lines with no
// text are skipped. Returns the number of lines stored (at most max_lines).
int LrcParse_text(const char* lrc_text, LyricLine* lines, int max_lines);

#endif
