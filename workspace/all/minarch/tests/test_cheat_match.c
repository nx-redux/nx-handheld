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

	// 11. a region-less ROM takes the full (USA) set, not a source-only "(Rumbles)"
	//     pack (real database names; the Rumbles files hold 6/4/1 cheats)
	{
		const char* c[] = {"Contra III - The Alien Wars (USA) (Action Replay).cht",
						   "Contra III - The Alien Wars (USA) (Game Genie).cht",
						   "Contra III - The Alien Wars (USA).cht", "Contra Iii - The Alien Wars (Rumbles).cht"};
		int n = CheatMatch_select("Contra III - The Alien Wars", "Contra III - The Alien Wars.sfc", c, 4, idx, 16);
		CHECK(n == 3 && has(idx, n, 0) && has(idx, n, 1) && has(idx, n, 2), "region-less Contra III gets the (USA) set");
		const char* sor[] = {"Streets Of Rage 2 (Rumbles).cht", "Streets of Rage 2 (USA) (Action Replay).cht",
							 "Streets of Rage 2 (USA).cht"};
		n = CheatMatch_select("Streets of Rage 2", "Streets of Rage 2.md", sor, 3, idx, 16);
		CHECK(n == 2 && has(idx, n, 1) && has(idx, n, 2), "region-less Streets of Rage 2 gets the (USA) set");
		const char* cz[] = {"Comix Zone (Europe) (En,Fr,De,Es).cht", "Comix Zone (Japan) (En,Ja).cht",
							"Comix Zone (Rumbles).cht", "Comix Zone (USA) (GameGenie).cht", "Comix Zone (USA).cht",
							"Comix Zone (Unknown) (Beta) (1995-04-04).cht"};
		n = CheatMatch_select("Comix Zone", "Comix Zone.md", cz, 6, idx, 16);
		CHECK(n == 2 && has(idx, n, 3) && has(idx, n, 4), "region-less Comix Zone gets the (USA) set");
		// a ROM that names its region still gets that region, even over USA
		n = CheatMatch_select("Comix Zone", "Comix Zone (Europe).md", cz, 6, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "(Europe) Comix Zone keeps the Europe file");
		// only a Rumbles pack: it is still used
		const char* only[] = {"Comix Zone (Rumbles).cht"};
		n = CheatMatch_select("Comix Zone", "Comix Zone.md", only, 1, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "a lone Rumbles pack is still picked");
	}

	// 12. no region to go by: USA, then World, then Europe
	{
		const char* c[] = {"Tetris (Japan) (En).cht", "Tetris (World) (Rev 1).cht"};
		int n = CheatMatch_select("Tetris", "Tetris.gb", c, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "region-less Tetris picks World over Japan");
		const char* psp[] = {"Lumines [EU] [ULES-00043].cht", "Lumines [US] [ULUS-10046].cht"};
		n = CheatMatch_select("Lumines", "Lumines.iso", psp, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "region-less PSP game picks [US] over [EU]");
	}

	// 13. names spelled differently in the database: letters and digits only,
	//     and only when nothing matches exactly
	{
		const char* c[] = {"Soul Calibur.cht"};
		int n = CheatMatch_select("Soulcalibur", "Soulcalibur.chd", c, 1, idx, 16);
		CHECK(n == 1 && idx[0] == 0, "Soulcalibur finds Soul Calibur.cht");
		const char* mvc[] = {"Marvel Vs. Capcom (Japanese).cht", "Marvel Vs. Capcom 2.cht", "Marvel Vs. Capcom.cht"};
		n = CheatMatch_select("Marvel vs Capcom 2", "Marvel vs Capcom 2.chd", mvc, 3, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "Marvel vs Capcom 2 finds Marvel Vs. Capcom 2.cht, not the first game");
		const char* both[] = {"Soul-Calibur.cht", "Soulcalibur.cht"};
		n = CheatMatch_select("Soulcalibur", "Soulcalibur.chd", both, 2, idx, 16);
		CHECK(n == 1 && idx[0] == 1, "an exact stem wins over a loose one");
		const char* other[] = {"Tetris 2 (USA).cht"};
		n = CheatMatch_select("Tetris", "Tetris.gb", other, 1, idx, 16);
		CHECK(n == 0, "loose matching still tells Tetris from Tetris 2");
		char loose[64];
		CheatMatch_looseStem("Marvel Vs. Capcom 2 (USA) (GameShark).cht", loose, sizeof(loose));
		CHECK(strcmp(loose, "marvelvscapcom2") == 0, "loose stem drops case, spaces, punctuation and tags");
		CheatMatch_looseStem("Quake III - Arena", loose, sizeof(loose));
		CHECK(strcmp(loose, "quakeiiiarena") == 0, "loose stem of a ROM display name");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
