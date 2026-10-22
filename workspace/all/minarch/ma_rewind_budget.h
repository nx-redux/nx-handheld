#ifndef MA_REWIND_BUDGET_H
#define MA_REWIND_BUDGET_H
// RAM budget for the rewind buffer, kept free of minarch globals so the host
// tests (tests/test_rewind_budget.c) can cover it.
//
// Besides the ring buffer, rewind holds about (5 + pool) copies of the save
// state (state, LZ4 scratch, two previous states, delta, capture pool). With a
// 30-40 MB state (flycast, PPSSPP) and a 128-256 MB buffer that is more than a
// 1 GB device can spare next to the running core.
#include <stddef.h>
#include <stdio.h>

// Smallest ring buffer worth keeping; below this rewind is turned off.
#define REWIND_BUDGET_MIN_BUFFER (1024 * 1024)

// Ring buffer size to allocate for `want` bytes, given the state size, the
// capture pool depth and the bytes the system has available (0 = unknown,
// no limit). Rewind may use half of what is available: returns `want` when it
// fits, a smaller whole-MB size when only that fits, or 0 when not even
// REWIND_BUDGET_MIN_BUFFER fits.
size_t RewindBudget_capacity(size_t want, size_t state_size, int pool, unsigned long long available);

// Parses MemAvailable from a /proc/meminfo stream. Returns bytes, or 0 if absent.
unsigned long long RewindBudget_parseMeminfo(FILE* meminfo);

// MemAvailable of this system in bytes, or 0 if it can't be read.
unsigned long long RewindBudget_available(void);
#endif
