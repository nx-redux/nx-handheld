#pragma once

#include <SDL2/SDL.h>

extern int state_slot;

void SRAM_read(void);
void SRAM_write(void);
void RTC_read(void);
void RTC_write(void);
int State_getSlotPath(int slot, char* filename);
void State_getPath(char* filename);
int State_read(void);
int State_readWithUndo(void);
int State_write(void);
int State_hasUndo(void);
int State_undoLoad(void);
void State_invalidateUndo(void);
void State_freeUndo(void);
void State_autosave(void);
void State_resume(void);
// The screenshot of the state this launch resumes (nextui's resume slot),
// converted for the screen; NULL for a fresh start, netplay, or no picture.
SDL_Surface* State_loadResumePreview(const char* rom_path, const char* tag_name);
// Retries a resume the core refused before its first frame; call every frame.
void State_resumePending(void);
