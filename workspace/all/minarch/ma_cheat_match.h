#pragma once
#include <stddef.h>

// Pure cheat-file matcher. No dependency on minarch's core/game state, so it
// is unit-testable with a plain host compiler.
//
// Given a ROM's region-stripped display name (rom_display, e.g. from
// getDisplayName) and its full name carrying region tags (rom_fullname, e.g.
// game.alt_name -> "Advance Wars (USA, Europe)"), rank the candidate .cht
// basenames and select the best-matching game+region group. The selected
// indices are the variant files to merge (same game+region, differing only by
// cheat source such as "(Code Breaker)" vs "(GameShark)"), best-first.
//
// Returns the number of selected indices written to out_indices (capacity
// out_cap). Stems compare case-insensitively; only when none matches that way
// do they compare by letters and digits alone (CheatMatch_looseStem). Returns 0
// when no stem matches either way.
int CheatMatch_select(const char* rom_display, const char* rom_fullname,
					  const char* const* candidate_basenames, int n,
					  int* out_indices, int out_cap);

// The stem of a .cht basename or ROM name (text before the first " (" or " ["),
// as lowercase letters and digits only: "Marvel Vs. Capcom 2 (USA).cht" ->
// "marvelvscapcom2".
void CheatMatch_looseStem(const char* name, char* out, size_t out_len);

// Extract the cheat-source label from a basename, preserving original case,
// e.g. "Contra (USA) (GameShark).cht" -> "GameShark". Writes "" when the file
// carries no recognised source tag (only region/version tags, or none).
void CheatMatch_sourceLabel(const char* basename, char* out, size_t out_len);

// The Cheats/ folder a core tag reads: one folder per console, the one its
// Roms/ folder and the cheat database use. A second core for a console reads
// that console's folder (MGBA -> GBA, SUPA -> SFC, PSX -> PS, DCX -> DC), and
// GPGX picks by the ROM's extension (Game Gear, Master System, SG-1000, Sega CD,
// else Mega Drive). *second is a folder to try when the first has no match
// (Game Boy and Game Boy Color read each other's for dual-mode carts), or NULL.
// rom_path may be NULL (then GPGX reads MD). Any other tag reads its own name.
void CheatMatch_consoleFolders(const char* tag, const char* rom_path, const char** first,
							   const char** second);
