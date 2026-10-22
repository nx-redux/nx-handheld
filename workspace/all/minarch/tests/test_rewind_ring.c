// Host test for ma_rewind_ring.c: the rewind ring buffer's write / drop / pop logic.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ma_rewind_ring.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static void ring_init(RewindContext* c, size_t capacity, int entry_capacity) {
	memset(c, 0, sizeof(*c));
	c->buffer = calloc(1, capacity);
	c->capacity = capacity;
	c->entries = calloc(entry_capacity, sizeof(RewindEntry));
	c->entry_capacity = entry_capacity;
}

static void ring_free(RewindContext* c) {
	free(c->buffer);
	free(c->entries);
}

// Writes an entry of len bytes, all set to tag
static int put(RewindContext* c, size_t len, uint8_t tag) {
	uint8_t data[4096];
	memset(data, tag, len);
	return RewindRing_write(c, data, len, tag & 1);
}

// The tag of the k-th live entry from the oldest (-1 if its bytes are not all one tag)
static int tag_at(const RewindContext* c, int k) {
	const RewindEntry* e = &c->entries[(c->entry_tail + k) % c->entry_capacity];
	const uint8_t* p = c->buffer + e->offset;
	for (size_t i = 1; i < e->size; i++)
		if (p[i] != p[0])
			return -1;
	return p[0];
}

// Live entries are in bounds and never overlap
static int ring_valid(const RewindContext* c) {
	for (int a = 0; a < c->entry_count; a++) {
		const RewindEntry* ea = &c->entries[(c->entry_tail + a) % c->entry_capacity];
		if (!ea->size || ea->offset + ea->size > c->capacity)
			return 0;
		for (int b = a + 1; b < c->entry_count; b++) {
			const RewindEntry* eb = &c->entries[(c->entry_tail + b) % c->entry_capacity];
			if (ea->offset < eb->offset + eb->size && eb->offset < ea->offset + ea->size)
				return 0;
		}
	}
	return 1;
}

int main(void) {
	RewindContext c;

	// empty ring
	ring_init(&c, 100, 8);
	CHECK(RewindRing_state(&c) == REWIND_BUF_EMPTY && RewindRing_freeSpace(&c) == 100, "empty: all free");
	CHECK(put(&c, 100, 1) == REWIND_RING_TOO_BIG && c.entry_count == 0, "entry as big as the buffer refused");
	CHECK(put(&c, 150, 1) == REWIND_RING_TOO_BIG, "entry bigger than the buffer refused");

	// fill in order
	CHECK(put(&c, 30, 1) == 1 && put(&c, 30, 2) == 1 && put(&c, 30, 3) == 1, "three writes");
	CHECK(c.entry_count == 3 && c.head == 90 && c.tail == 0 && RewindRing_state(&c) == REWIND_BUF_HAS_DATA,
		  "head after the third entry");
	CHECK(RewindRing_freeSpace(&c) == 10, "free space = capacity - used");

	// wrap-around: the next write goes to 0 and drops only the entry it overlaps
	CHECK(put(&c, 20, 4) == 1, "write that wraps");
	CHECK(c.entry_count == 3 && tag_at(&c, 0) == 2 && tag_at(&c, 1) == 3 && tag_at(&c, 2) == 4,
		  "wrap dropped only the oldest (overlapping) entry");
	CHECK(c.entries[RewindRing_newestIndex(&c)].offset == 0 && c.head == 20 && c.tail == 30, "wrapped entry at 0");
	CHECK(RewindRing_freeSpace(&c) == 10, "free space between head and tail after a wrap");
	CHECK(ring_valid(&c), "valid after wrap");

	// a write filling exactly to tail would make head == tail: room must stay > len
	CHECK(put(&c, 10, 5) == 1 && tag_at(&c, 0) == 3, "write needing the exact free space drops the oldest first");
	CHECK(ring_valid(&c), "valid after exact fit");

	// full: head == tail with entries
	ring_free(&c);
	ring_init(&c, 100, 8);
	put(&c, 50, 1);
	put(&c, 50, 2);
	CHECK(c.head == 0 && c.tail == 0 && c.entry_count == 1, "two halves: the first is dropped to keep head != tail");
	{
		RewindContext f;
		ring_init(&f, 100, 8);
		f.entry_count = 2;
		f.entries[0] = (RewindEntry){0, 50, 1};
		f.entries[1] = (RewindEntry){50, 50, 0};
		f.entry_head = 2;
		f.head = f.tail = 0;
		CHECK(RewindRing_state(&f) == REWIND_BUF_FULL && RewindRing_freeSpace(&f) == 0, "head == tail with entries: full, no space");
		ring_free(&f);
	}

	// entry table full: the oldest is dropped before the write
	ring_free(&c);
	ring_init(&c, 1000, 4);
	for (int i = 1; i <= 4; i++)
		put(&c, 10, (uint8_t)i);
	CHECK(c.entry_count == 4 && c.entry_head == c.entry_tail, "entry table full");
	CHECK(put(&c, 10, 5) == 1 && c.entry_count == 4 && tag_at(&c, 0) == 2 && tag_at(&c, 3) == 5,
		  "table full: oldest dropped, newest stored");
	CHECK(ring_valid(&c), "valid after a table-full write");

	// drop oldest down to empty resets head and tail
	while (c.entry_count)
		RewindRing_dropOldest(&c);
	CHECK(c.head == 0 && c.tail == 0 && RewindRing_state(&c) == REWIND_BUF_EMPTY, "dropping all resets the ring");
	RewindRing_dropOldest(&c);
	CHECK(c.entry_count == 0, "drop on empty is a no-op");

	// pop newest restores head to the popped entry's offset
	put(&c, 10, 1);
	put(&c, 20, 2);
	put(&c, 30, 3);
	{
		int idx = RewindRing_newestIndex(&c);
		RewindEntry e = c.entries[idx];
		RewindRing_popNewest(&c, idx, &e);
		CHECK(c.entry_count == 2 && c.head == 30 && tag_at(&c, 1) == 2, "pop newest: head back to its offset");
		idx = RewindRing_newestIndex(&c);
		e = c.entries[idx];
		RewindRing_popNewest(&c, idx, &e);
		idx = RewindRing_newestIndex(&c);
		e = c.entries[idx];
		RewindRing_popNewest(&c, idx, &e);
		CHECK(c.entry_count == 0 && c.head == 0 && c.tail == 0, "popping the last entry resets the ring");
	}
	CHECK(put(&c, 10, 7) == 1 && c.entries[RewindRing_newestIndex(&c)].offset == 0, "write after popping all starts at 0");

	// newest index wraps at the table start
	ring_free(&c);
	ring_init(&c, 1000, 4);
	for (int i = 1; i <= 5; i++)
		put(&c, 10, (uint8_t)i);
	CHECK(c.entry_head == 1 && RewindRing_newestIndex(&c) == 0 && tag_at(&c, 3) == 5, "newest index at slot 0");

	// variable sizes, writes and pops: live entries never overlap or lose their bytes
	ring_free(&c);
	ring_init(&c, 997, 16);
	{
		unsigned seed = 12345;
		int ok = 1, content = 1;
		uint8_t shadow[16] = {0};
		for (int i = 0; i < 20000 && ok; i++) {
			seed = seed * 1103515245u + 12345u;
			size_t len = 1 + (seed >> 8) % 400;
			if ((seed >> 20) % 7 == 0 && c.entry_count) {
				int idx = RewindRing_newestIndex(&c);
				RewindEntry e = c.entries[idx];
				RewindRing_popNewest(&c, idx, &e);
			} else {
				uint8_t tag = (uint8_t)(1 + i % 250);
				shadow[c.entry_head] = tag; // the slot the entry lands in
				if (put(&c, len, tag) != 1)
					ok = 0;
			}
			ok = ok && ring_valid(&c);
			for (int k = 0; k < c.entry_count && content; k++)
				content = tag_at(&c, k) == shadow[(c.entry_tail + k) % c.entry_capacity];
		}
		CHECK(ok, "20,000 random writes/pops: entries in bounds and never overlapping");
		CHECK(content, "20,000 random writes/pops: live entries keep their bytes");
	}
	ring_free(&c);

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
