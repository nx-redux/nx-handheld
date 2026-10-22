#pragma once

#include "ma_internal.h"
#include "ma_option_list.h"

// OptionList initialization (called from environment_callback)
void OptionList_init(const struct retro_core_option_definition* defs);
void OptionList_v2_init(const struct retro_core_options_v2* opt_defs);
void OptionList_vars(const struct retro_variable* vars);
void OptionList_reset(void);

// OptionList accessors (called from Config_* and frontend)
void OptionList_setOptionValue(OptionList* list, const char* key, const char* value);
void OptionList_setOptionRawValue(OptionList* list, const char* key, int value);
void OptionList_setOptionVisibility(OptionList* list, const char* key, int visible);
