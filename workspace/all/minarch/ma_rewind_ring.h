#pragma once

#include "ma_rewind.h"

// The rewind ring buffer: compressed entries in rewind_ctx.buffer, indexed by
// the rewind_ctx.entries table. Plain C over a RewindContext (only its ring
// fields: buffer, capacity, head, tail, entries, entry_*), host-tested by
// tests/test_rewind_ring.c. Callers hold rewind_ctx.lock.

RewindBufferState RewindRing_state(const RewindContext* ctx);
size_t RewindRing_freeSpace(const RewindContext* ctx);
void RewindRing_dropOldest(RewindContext* ctx);

enum {
	REWIND_RING_TOO_BIG = -1, // the entry is not smaller than the whole buffer
	REWIND_RING_NO_ROOM = -2, // could not make room (a logic error)
};
// Stores an entry, dropping the oldest entries it would overlap or that leave
// too little room. 1 when stored, else REWIND_RING_TOO_BIG or REWIND_RING_NO_ROOM.
int RewindRing_write(RewindContext* ctx, const uint8_t* data, size_t len, int is_keyframe);

// The newest entry's index (the ring must not be empty).
int RewindRing_newestIndex(const RewindContext* ctx);
// Pops the newest entry (at idx, from RewindRing_newestIndex) and reclaims its
// ring bytes, so rewind-then-play cycles don't shrink the usable history.
void RewindRing_popNewest(RewindContext* ctx, int idx, const RewindEntry* e);
