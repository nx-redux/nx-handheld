#pragma once

#include "ma_option_types.h"

// Option helpers with no SDL or globals, host-tested by tests/test_option_list.c.

// The index of value in item->values; 0 when it is not there (or NULL).
int Option_getValueIndex(Option* item, const char* value);

// Fills item from a legacy retro_variable: value is "Name; a|b|c" (name, then
// the values; a missing "; " names the option after its key). The values and
// labels point into item->var.
void Option_initVar(Option* item, const char* key, const char* value);

// Frees what an option owns (from OptionList_*init or Option_initVar).
void Option_free(Option* item);

Option* OptionList_getOption(OptionList* list, const char* key);
// The option's current value string, NULL for an unknown key or an out-of-range value.
char* OptionList_getOptionValue(OptionList* list, const char* key);
