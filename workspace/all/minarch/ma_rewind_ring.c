#include "ma_rewind_ring.h"
#include <string.h>

RewindBufferState RewindRing_state(const RewindContext* ctx) {
	if (ctx->entry_count == 0)
		return REWIND_BUF_EMPTY;
	// head == tail with entries means the ring buffer wrapped and is full
	if (ctx->head == ctx->tail)
		return REWIND_BUF_FULL;
	return REWIND_BUF_HAS_DATA;
}

size_t RewindRing_freeSpace(const RewindContext* ctx) {
	RewindBufferState state = RewindRing_state(ctx);
	if (state == REWIND_BUF_FULL)
		return 0;
	if (state == REWIND_BUF_EMPTY)
		return ctx->capacity;
	if (ctx->head >= ctx->tail)
		return ctx->capacity - (ctx->head - ctx->tail);
	else
		return ctx->tail - ctx->head;
}

void RewindRing_dropOldest(RewindContext* ctx) {
	if (!ctx->entry_count)
		return;
	RewindEntry* e = &ctx->entries[ctx->entry_tail];
	ctx->tail = (e->offset + e->size) % ctx->capacity;
	ctx->entry_tail = (ctx->entry_tail + 1) % ctx->entry_capacity;
	ctx->entry_count -= 1;
	if (ctx->entry_count == 0) {
		ctx->head = ctx->tail = 0;
	}
}

// Check if an entry overlaps with range [range_start, range_end) in a non-wrapping buffer region
static int entry_overlaps_range(const RewindContext* ctx, int entry_idx, size_t range_start, size_t range_end) {
	const RewindEntry* e = &ctx->entries[entry_idx];
	size_t e_start = e->offset;
	size_t e_end = e->offset + e->size;
	// Check for overlap: ranges overlap if start < other_end AND other_start < end
	return (e_start < range_end) && (range_start < e_end);
}

int RewindRing_write(RewindContext* ctx, const uint8_t* compressed, size_t dest_len, int is_keyframe) {
	if (dest_len >= ctx->capacity) {
		return REWIND_RING_TOO_BIG;
	}

	// If the entry table is full, drop the oldest entry *before* writing so we don't
	// overwrite its metadata (entry_head == entry_tail when full).
	if (ctx->entry_count == ctx->entry_capacity) {
		RewindRing_dropOldest(ctx);
	}

	size_t write_offset = ctx->head;

	// If this write would go past the end of the buffer, wrap to 0
	if (write_offset + dest_len > ctx->capacity) {
		write_offset = 0;
		ctx->head = 0;
		if (ctx->entry_count == 0) {
			ctx->tail = 0;
		}
	}

	// Drop entries until none overlap the region we're about to write:
	// [write_offset, write_offset + dest_len). Entries can only be dropped from
	// the tail (the delta chain must stay contiguous), but after a wrap the
	// overlapping entries are the *newer* ones at low offsets — behind a
	// non-overlapping oldest entry at high offsets — so find the newest
	// overlapping entry and drop everything up to and including it.
	int drops = 0;
	for (int k = 0; k < ctx->entry_count; k++) {
		int idx = (ctx->entry_tail + k) % ctx->entry_capacity;
		if (entry_overlaps_range(ctx, idx, write_offset, write_offset + dest_len))
			drops = k + 1;
	}
	while (drops-- > 0)
		RewindRing_dropOldest(ctx);

	// Still need to make room based on free space calculation
	while (ctx->entry_count > 0 && RewindRing_freeSpace(ctx) <= dest_len) {
		RewindRing_dropOldest(ctx);
	}

	// Safety check: if we still can't fit, there's a logic error
	if (RewindRing_freeSpace(ctx) <= dest_len && ctx->entry_count > 0) {
		return REWIND_RING_NO_ROOM;
	}

	memcpy(ctx->buffer + write_offset, compressed, dest_len);

	RewindEntry* e = &ctx->entries[ctx->entry_head];
	e->offset = write_offset;
	e->size = dest_len;
	e->is_keyframe = is_keyframe ? 1 : 0;

	ctx->head = write_offset + dest_len;
	if (ctx->head >= ctx->capacity)
		ctx->head = 0;

	ctx->entry_head = (ctx->entry_head + 1) % ctx->entry_capacity;
	// the table-full pre-drop above guarantees room here
	ctx->entry_count += 1;
	return 1;
}

int RewindRing_newestIndex(const RewindContext* ctx) {
	int idx = ctx->entry_head - 1;
	if (idx < 0)
		idx += ctx->entry_capacity;
	return idx;
}

void RewindRing_popNewest(RewindContext* ctx, int idx, const RewindEntry* e) {
	ctx->entry_head = idx;
	ctx->entry_count -= 1;
	if (ctx->entry_count == 0)
		ctx->head = ctx->tail = 0;
	else
		ctx->head = e->offset;
}
