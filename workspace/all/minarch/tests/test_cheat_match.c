#include <stdio.h>
#include <string.h>
#include "ma_cheat_match.h"

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

static int has(const int* a, int n, int v) {
	for (int i = 0; i < n; i++)
		if (a[i] == v)
			return 1;
	return 0;
}

int main(void) {
	int idx[16];

	// 1. exact single match qualifies and is selected
	{
		const char* c[] = {"Advance Wars (USA, Europe) (Code Breaker).cht"};
		int n = CheatMatch_select("Advance Wars", "Advance Wars (USA, Europe)", c, 1, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "exact single match selected");
	}

	// 2. ambiguity guard: different stems must NOT match
	{
		const char* c[] = {"Super Mario World (USA).cht", "Super Mario Bros (USA).cht"};
		int n = CheatMatch_select("Super Mario", "Super Mario (USA)", c, 2, idx, 16);
		CHECK(n == 0, "different-stem candidates rejected");
	}

	// 3. region preference: USA ROM prefers the USA file over the Japan file
	{
		const char* c[] = {"Sonic (Japan).cht", "Sonic (USA).cht"};
		int n = CheatMatch_select("Sonic", "Sonic (USA)", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "region match preferred");
	}

	// 4. variant merge: same game+region, different cheat source -> both selected
	{
		const char* c[] = {"Contra (USA) (Code Breaker).cht", "Contra (USA) (GameShark).cht"};
		int n = CheatMatch_select("Contra", "Contra (USA)", c, 2, idx, 16);
		CHECK(n == 2 && has(idx, n, 0) && has(idx, n, 1), "same-region variants merged");
	}

	// 5. plain file preferred when ROM has no region tags
	{
		const char* c[] = {"Zelda.cht", "Zelda (USA).cht"};
		int n = CheatMatch_select("Zelda", "Zelda", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "plain file preferred for region-less ROM");
	}

	// 6. source label extraction
	{
		char lbl[64];
		CheatMatch_sourceLabel("Contra (USA) (GameShark).cht", lbl, sizeof(lbl));
		CHECK(strcmp(lbl, "GameShark") == 0, "source label = GameShark");
		CheatMatch_sourceLabel("Zelda (USA).cht", lbl, sizeof(lbl));
		CHECK(lbl[0] == '\0', "no source label when only region tags");
	}

	// 7. PSP-style "[US] [serial]" names: stem ends at " [", the region code
	//    matches the ROM's No-Intro region (#257)
	{
		const char* c[] = {"Grand Theft Auto - Liberty City Stories [EU] [ULES-00151].cht",
						   "Grand Theft Auto - Liberty City Stories [JP] [ULJM-05255].cht",
						   "Grand Theft Auto - Liberty City Stories [US] [ULUS-10041].cht"};
		int n = CheatMatch_select("Grand Theft Auto - Liberty City Stories",
								  "Grand Theft Auto - Liberty City Stories (USA).iso", c, 3, idx, 16);
		CHECK(n == 1 && idx[0] == 2, "PSP [US] file picked for a (USA) ROM");
		n = CheatMatch_select("Grand Theft Auto - Liberty City Stories",
							  "Grand Theft Auto - Liberty City Stories (Europe) (En,Fr,De,Es,It).iso", c, 3, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "PSP [EU] file picked for a (Europe) ROM");
	}

	// 8. a multi-region code like [US-EU] matches either region
	{
		const char* c[] = {"Lumines [JP] [ULJM-05013].cht", "Lumines [US-EU] [ULUS-10046].cht"};
		int n = CheatMatch_select("Lumines", "Lumines (Europe)", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "PSP [US-EU] file picked for a (Europe) ROM");
	}

	// 9. a serial in the ROM name picks the file with that serial
	{
		const char* c[] = {"Daxter [US] [UCUS-98618].cht", "Daxter [US] [UCUS-98646].cht"};
		int n = CheatMatch_select("Daxter", "Daxter (USA) [UCUS-98646]", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "PSP serial in ROM name picks that file");
	}

	// 10. same region: a retail serial wins over a homebrew ID
	{
		const char* c[] = {"Patapon 2 [US] [HB4B12D3BC].cht", "Patapon 2 [US] [UCUS-98732].cht",
						   "Patapon 2 [EU] [UCES-01177].cht"};
		int n = CheatMatch_select("Patapon 2", "Patapon 2 (USA) (En,Fr,Es).iso", c, 3, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "PSP retail serial preferred over homebrew ID");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
