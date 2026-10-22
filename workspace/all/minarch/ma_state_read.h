#pragma once

#include <stddef.h>
#include <stdint.h>

// Reading a save state's data: plain C, host-tested by tests/test_state_read.c.

#define RASTATE_HEADER_SIZE 16
#define STATE_READ_MAX (256u << 20)

// Reads up to len bytes into buf: the count read, 0 at the end, -1 on error.
typedef int64_t (*StateReadFn)(void* stream, void* buf, size_t len);

// The size of a RASTATE file's MEM block (header "RASTATE<ver>MEM <le32>"), 0
// for raw core data. header is RASTATE_HEADER_SIZE bytes.
size_t State_memSize(const uint8_t* header);

// Reads the state data: [want] bytes when known, else to the end. The buffer is at least
// [min_size] (the core's serialize_size, zero-filled past the data) but grows past it, up
// to STATE_READ_MAX. *out_size is what to pass to unserialize: max(data, min_size).
// NULL on a read error or a state over STATE_READ_MAX; free() the result.
void* State_readData(StateReadFn rd, void* stream, size_t min_size, size_t want, size_t* out_size);
