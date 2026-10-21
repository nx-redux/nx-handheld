#include "ma_cheat_mem.h"
#include <string.h>

// Mirrors RetroArch's cheat_manager_apply_retro_cheats (cheat_manager.c), with
// three deliberate differences: reads use the cheat's own byte order (RetroArch
// reads with its search setting, writes with the cheat's), repeated values wrap
// with & instead of RetroArch's % (which turned a repeated 0xFF into 0), and a
// sub-byte repeat moves the bit mask by the bit width (RetroArch shifts it by
// the mask value, 3 for 2-bit and 15 for 4-bit items).

void CheatMem_reset(CheatMem* mem) {
	memset(mem, 0, sizeof(*mem));
}

void CheatMem_addRegion(CheatMem* mem, void* ptr, size_t len) {
	if (!ptr || !len || mem->count >= CHEAT_MEM_MAX_REGIONS)
		return;
	mem->ptr[mem->count] = ptr;
	mem->len[mem->count] = len;
	mem->count++;
	mem->total += len;
}

void CheatMemPatch_init(struct CheatMemPatch* p) {
	memset(p, 0, sizeof(*p));
	p->repeat_count = 1;
	p->repeat_add_to_address = 1;
}

static void size_meta(unsigned size, unsigned* bytes, unsigned* bits, unsigned* mask) {
	switch (size) {
	case 0:
		*bytes = 1;
		*bits = 1;
		*mask = 0x01;
		break;
	case 1:
		*bytes = 1;
		*bits = 2;
		*mask = 0x03;
		break;
	case 2:
		*bytes = 1;
		*bits = 4;
		*mask = 0x0F;
		break;
	case 4:
		*bytes = 2;
		*bits = 8;
		*mask = 0xFFFF;
		break;
	case 5:
		*bytes = 4;
		*bits = 8;
		*mask = 0xFFFFFFFF;
		break;
	case 3:
	default:
		*bytes = 1;
		*bits = 8;
		*mask = 0xFF;
		break;
	}
}

// The bytes [address, address + bytes) when they lie in one region, else NULL.
static uint8_t* locate(const CheatMem* mem, size_t address, unsigned bytes) {
	size_t start = 0;
	for (int i = 0; i < mem->count; i++) {
		if (address - start < mem->len[i]) {
			if (bytes > mem->len[i] - (address - start))
				return NULL;
			return mem->ptr[i] + (address - start);
		}
		start += mem->len[i];
	}
	return NULL;
}

static unsigned read_value(const uint8_t* p, unsigned bytes, int big_endian) {
	unsigned v = 0;
	for (unsigned b = 0; b < bytes; b++)
		v |= (unsigned)p[big_endian ? b : bytes - 1 - b] << (8 * (bytes - 1 - b));
	return v;
}

static void write_value(uint8_t* p, unsigned bytes, int big_endian, unsigned v) {
	for (unsigned b = 0; b < bytes; b++)
		p[big_endian ? b : bytes - 1 - b] = (uint8_t)(v >> (8 * (bytes - 1 - b)));
}

int CheatMem_apply(const CheatMem* mem, const struct CheatMemPatch* const* patches, int n) {
	int written = 0;
	int run = 1;

	if (!mem->count)
		return 0;

	for (int i = 0; i < n; i++) {
		const struct CheatMemPatch* c = patches[i];
		unsigned bytes, bits, mask;
		size_meta(c->size, &bytes, &bits, &mask);

		if (!run) {
			run = 1;
			continue;
		}

		size_t idx = c->address;
		uint8_t* p = locate(mem, idx, bytes);
		if (!p) {
			// a condition that cannot be read does not hold
			if (c->type >= CHEAT_MEM_RUN_NEXT_IF_EQ)
				run = 0;
			continue;
		}
		unsigned cur = read_value(p, bytes, c->big_endian);

		unsigned set = 0;
		int do_set = 0;
		switch (c->type) {
		case CHEAT_MEM_SET_TO_VALUE:
			do_set = 1;
			set = c->value;
			break;
		case CHEAT_MEM_INCREASE_VALUE:
			do_set = 1;
			set = cur + c->value;
			break;
		case CHEAT_MEM_DECREASE_VALUE:
			do_set = 1;
			set = cur - c->value;
			break;
		case CHEAT_MEM_RUN_NEXT_IF_EQ:
			run = cur == c->value;
			break;
		case CHEAT_MEM_RUN_NEXT_IF_NEQ:
			run = cur != c->value;
			break;
		case CHEAT_MEM_RUN_NEXT_IF_LT:
			run = c->value < cur;
			break;
		case CHEAT_MEM_RUN_NEXT_IF_GT:
			run = c->value > cur;
			break;
		default:
			break;
		}
		if (!do_set)
			continue;

		written++;
		unsigned bit_mask = c->bit_mask;
		for (unsigned r = 0; r < c->repeat_count; r++) {
			p = locate(mem, idx, bytes);
			if (!p)
				break;
			if (bits < 8) {
				// replace only the bits set in bit_mask
				uint8_t v = *p;
				for (unsigned bit = 0; bit < 8; bit++)
					if ((bit_mask >> bit) & 1)
						v = (uint8_t)((v & ~(1u << bit)) | (((set >> bit) & 1u) << bit));
				*p = v;
			} else {
				write_value(p, bytes, c->big_endian, set);
			}

			set = (set + c->repeat_add_to_value) & mask;
			if (bits < 8) {
				for (unsigned a = 0; a < c->repeat_add_to_address; a++) {
					bit_mask = (bit_mask << bits) & 0xFF;
					if (!bit_mask) {
						bit_mask = mask;
						idx++;
					}
				}
			} else {
				idx += (size_t)c->repeat_add_to_address * bytes;
			}
			idx %= mem->total;
		}
	}
	return written;
}
