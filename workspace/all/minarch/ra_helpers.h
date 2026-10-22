#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Pure helpers for ra_integration.c (no SDL, no rcheevos), host-tested by
// tests/test_ra_helpers.c.

// Delay before login retry `attempt` (0-based): 1 s, 2 s, 4 s, then 8 s.
uint32_t RA_retryDelayMs(int attempt);

// The game data request (r=achievementsets) is processed on a worker thread.
bool RA_isBackgroundRequest(const char* post_data);

// Whether [address, address + num_bytes) lies inside a mem_size block, without
// wrapping: addresses are server-supplied and can be near UINT32_MAX.
bool RA_memRangeFits(size_t mem_size, uint32_t address, uint32_t num_bytes);

// Reads a mute file (one achievement id per line) into ids, at most max of
// them; lines that are not a positive id are skipped. Returns the count.
int RA_readMutedIds(FILE* f, uint32_t* ids, int max);
