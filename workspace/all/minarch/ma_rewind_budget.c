#include "ma_rewind_budget.h"
#include <string.h>

size_t RewindBudget_capacity(size_t want, size_t state_size, int pool, unsigned long long available) {
	if (!available)
		return want;
	unsigned long long budget = available / 2;
	unsigned long long fixed = (unsigned long long)state_size * (unsigned long long)(5 + (pool > 0 ? pool : 0));
	if (fixed + want <= budget)
		return want;
	if (fixed >= budget)
		return 0;
	unsigned long long room = budget - fixed;
	room -= room % (1024 * 1024);
	if (room < REWIND_BUDGET_MIN_BUFFER)
		return 0;
	return (size_t)room;
}

unsigned long long RewindBudget_parseMeminfo(FILE* meminfo) {
	char line[256];
	unsigned long long kb;
	while (fgets(line, sizeof(line), meminfo)) {
		if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1)
			return kb * 1024;
	}
	return 0;
}

unsigned long long RewindBudget_available(void) {
	FILE* f = fopen("/proc/meminfo", "r");
	if (!f)
		return 0;
	unsigned long long bytes = RewindBudget_parseMeminfo(f);
	fclose(f);
	return bytes;
}
