#pragma once

#include "emu_shortcuts.h"
#include "ma_file_list.h"

void setOverclock(int i);
void Config_getPath(char* filename, int override);
void Config_syncFrontend(char* key, int value);
void Config_init(void);
void Config_quit(void);
void Config_load(void);
void Config_free(void);
void Config_readOptions(void);
void Config_reapplyOptions(void);
void Config_readOptionsString(char* cfg);
void Config_readControls(void);
// shortcut labels (emu_shortcuts.h): Settings' global file and this console/game's
// own (0 = follow the global one); Config_applyShortcuts merges them into config.shortcuts
extern int shortcut_global[SHORTCUT_COUNT];
extern int shortcut_override[SHORTCUT_COUNT];
void Config_applyShortcuts(void);
void Config_write(int override);
// Force a core option for this netplay session only: Config_write keeps saving the user's value
void Config_setSessionOption(const char* key, const char* value);
void Config_restore(void);
