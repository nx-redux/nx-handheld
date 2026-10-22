#include <stdio.h>
#include <string.h>
#include "ma_cheat_mem.h"

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

static struct CheatMemPatch patch(unsigned address, unsigned value, unsigned size, unsigned type) {
	struct CheatMemPatch p;
	CheatMemPatch_init(&p);
	p.memory = 1;
	p.address = address;
	p.value = value;
	p.size = size;
	p.type = type;
	return p;
}

static int apply1(CheatMem* m, struct CheatMemPatch* p) {
	const struct CheatMemPatch* list[] = {p};
	return CheatMem_apply(m, list, 1);
}

int main(void) {
	uint8_t a[16], b[16];
	CheatMem m;

	// 1. 8-bit set (the FBNeo 1941 "inf energy p1" shape)
	{
		memset(a, 0, sizeof(a));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch p = patch(3, 7, 3, CHEAT_MEM_SET_TO_VALUE);
		int w = apply1(&m, &p);
		CHECK(w == 1 && a[3] == 7, "8-bit set writes the value");
	}

	// 2. 16-bit little and big endian
	{
		memset(a, 0, sizeof(a));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch p = patch(0, 0x1234, 4, CHEAT_MEM_SET_TO_VALUE);
		apply1(&m, &p);
		CHECK(a[0] == 0x34 && a[1] == 0x12, "16-bit little endian");
		p.big_endian = 1;
		apply1(&m, &p);
		CHECK(a[0] == 0x12 && a[1] == 0x34, "16-bit big endian");
	}

	// 3. 32-bit increase reads then writes in the cheat's byte order
	{
		memset(a, 0, sizeof(a));
		a[4] = 0x00;
		a[5] = 0x00;
		a[6] = 0x01;
		a[7] = 0x00; // BE 0x100
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch p = patch(4, 5, 5, CHEAT_MEM_INCREASE_VALUE);
		p.big_endian = 1;
		apply1(&m, &p);
		CHECK(a[6] == 0x01 && a[7] == 0x05, "32-bit big endian increase");
	}

	// 4. address spans two regions; a value straddling them is not written
	{
		memset(a, 0, sizeof(a));
		memset(b, 0, sizeof(b));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		CheatMem_addRegion(&m, b, sizeof(b));
		struct CheatMemPatch p = patch(16 + 2, 9, 3, CHEAT_MEM_SET_TO_VALUE);
		apply1(&m, &p);
		CHECK(b[2] == 9 && a[2] == 0, "offset past region 1 lands in region 2");
		struct CheatMemPatch q = patch(15, 0xABCD, 4, CHEAT_MEM_SET_TO_VALUE);
		int w = apply1(&m, &q);
		CHECK(a[15] == 0 && b[0] == 0, "value straddling regions is skipped");
		(void)w;
		struct CheatMemPatch r = patch(64, 1, 3, CHEAT_MEM_SET_TO_VALUE);
		CHECK(apply1(&m, &r) == 0, "address past all regions is skipped");
	}

	// 5. RUN_NEXT_IF_EQ gates the following cheat
	{
		memset(a, 0, sizeof(a));
		a[0] = 3;
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch cond = patch(0, 5, 3, CHEAT_MEM_RUN_NEXT_IF_EQ);
		struct CheatMemPatch set = patch(1, 99, 3, CHEAT_MEM_SET_TO_VALUE);
		struct CheatMemPatch after = patch(2, 42, 3, CHEAT_MEM_SET_TO_VALUE);
		const struct CheatMemPatch* list[] = {&cond, &set, &after};
		CheatMem_apply(&m, list, 3);
		CHECK(a[1] == 0 && a[2] == 42, "false condition skips only the next cheat");
		a[0] = 5;
		CheatMem_apply(&m, list, 3);
		CHECK(a[1] == 99, "true condition runs the next cheat");
	}

	// 6. LT / GT compare value against memory the RetroArch way round
	{
		memset(a, 0, sizeof(a));
		a[0] = 10;
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch lt = patch(0, 5, 3, CHEAT_MEM_RUN_NEXT_IF_LT); // 5 < 10
		struct CheatMemPatch set = patch(1, 1, 3, CHEAT_MEM_SET_TO_VALUE);
		const struct CheatMemPatch* list[] = {&lt, &set};
		CheatMem_apply(&m, list, 2);
		CHECK(a[1] == 1, "IF_LT: value < memory runs next");
		a[1] = 0;
		struct CheatMemPatch gt = patch(0, 5, 3, CHEAT_MEM_RUN_NEXT_IF_GT); // 5 > 10 false
		const struct CheatMemPatch* list2[] = {&gt, &set};
		CheatMem_apply(&m, list2, 2);
		CHECK(a[1] == 0, "IF_GT: value > memory false skips next");
	}

	// 7. repeat: fill 4 bytes, value +1 each, address +1 item
	{
		memset(a, 0, sizeof(a));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch p = patch(8, 1, 3, CHEAT_MEM_SET_TO_VALUE);
		p.repeat_count = 4;
		p.repeat_add_to_value = 1;
		apply1(&m, &p);
		CHECK(a[8] == 1 && a[9] == 2 && a[10] == 3 && a[11] == 4 && a[12] == 0, "repeat fills consecutive bytes");
		struct CheatMemPatch q = patch(0, 0xFF, 3, CHEAT_MEM_SET_TO_VALUE);
		q.repeat_count = 2;
		apply1(&m, &q);
		CHECK(a[0] == 0xFF && a[1] == 0xFF, "repeated 0xFF stays 0xFF");
	}

	// 8. sub-byte sizes write only the masked bits
	{
		memset(a, 0, sizeof(a));
		a[0] = 0xF0;
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch p = patch(0, 0x01, 0, CHEAT_MEM_SET_TO_VALUE); // 1-bit
		p.bit_mask = 0x01;
		apply1(&m, &p);
		CHECK(a[0] == 0xF1, "1-bit set touches only bit 0");
		struct CheatMemPatch q = patch(0, 0x00, 2, CHEAT_MEM_SET_TO_VALUE); // 4-bit high nibble
		q.bit_mask = 0xF0;
		apply1(&m, &q);
		CHECK(a[0] == 0x01, "4-bit set clears the high nibble only");
	}

	// 9. no regions: nothing happens
	{
		CheatMem_reset(&m);
		struct CheatMemPatch p = patch(0, 1, 3, CHEAT_MEM_SET_TO_VALUE);
		CHECK(apply1(&m, &p) == 0, "no memory, no writes");
	}

	// 10. a corrupt repeat count is capped: returns promptly, writes in bounds
	{
		memset(a, 0, sizeof(a));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		struct CheatMemPatch p = patch(0, 0x5A, 3, CHEAT_MEM_SET_TO_VALUE);
		p.repeat_count = 0xFFFFFFFFu;
		p.repeat_add_to_address = 1;
		int w = apply1(&m, &p);
		int all = 1;
		for (int i = 0; i < 16; i++)
			all &= a[i] == 0x5A;
		CHECK(w == 1 && all, "huge repeat_count capped, wraps over the region");
	}

	// 11. a huge sub-byte address step is reduced, not looped 2^32 times:
	// a 2-bit step cycles after 4 positions * 16 bytes = 64 steps
	{
		struct CheatMemPatch p = patch(0, 0x0F, 1, CHEAT_MEM_SET_TO_VALUE); // 2-bit
		p.bit_mask = 0x03;
		p.repeat_count = 6;
		p.repeat_add_to_value = 1;

		memset(b, 0, sizeof(b));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, b, sizeof(b));
		p.repeat_add_to_address = 3 % 64; // 2-bit: 4 * 16 = 64-step cycle
		apply1(&m, &p);

		memset(a, 0, sizeof(a));
		CheatMem_reset(&m);
		CheatMem_addRegion(&m, a, sizeof(a));
		p.repeat_add_to_address = 0xFFFFFFC3u; // % 64 == 3
		apply1(&m, &p);
		CHECK(!memcmp(a, b, sizeof(a)) && memcmp(a, (uint8_t[16]){0}, sizeof(a)),
			  "huge 2-bit address step = its remainder");
	}

	// 12. cheat file counts
	CHECK(!CheatFile_countOk(0), "count 0 rejected");
	CHECK(CheatFile_countOk(1) && CheatFile_countOk(CHEAT_FILE_MAX_COUNT), "normal counts accepted");
	CHECK(!CheatFile_countOk(CHEAT_FILE_MAX_COUNT + 1) && !CheatFile_countOk(4294967295ul),
		  "huge counts rejected");

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
