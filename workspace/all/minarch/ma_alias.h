#pragma once

#include <stdbool.h>

// The map.txt alias for a rom: map.txt beside it holds "<file name>\t<alias>"
// lines (LF or CRLF). On a hit, alias (at least MAX_PATH bytes) gets the alias
// and it returns true; else alias is left as is. Host-tested by tests/test_alias.c.
bool getAlias(char* path, char* alias);
