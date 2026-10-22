#include "ma_config_value.h"
#include <string.h>

int Config_getValue(const char* cfg, const char* key, char* out_value, int* lock) {
	size_t key_len = strlen(key);
	const char* tmp = cfg;
	while ((tmp = strstr(tmp, key))) {
		// only accept matches anchored to the start of a line (optionally
		// behind a `-` lock marker) and followed by " = " — a raw substring
		// hit can land inside a longer key or an unrelated value
		const char* at = tmp;
		tmp += key_len;
		int locked = at > cfg && *(at - 1) == '-';
		const char* line_start = locked ? at - 1 : at;
		if ((line_start == cfg || *(line_start - 1) == '\n') && !strncmp(tmp, " = ", 3)) {
			if (lock != NULL && locked)
				*lock = 1; // prefixed with a `-` means lock
			break;		   // matched
		}
	};
	if (!tmp)
		return 0;
	tmp += 3;

	strncpy(out_value, tmp, 256);
	out_value[256 - 1] = '\0';
	// the value ends at the line ending, "\n" or "\r\n" (a CRLF cfg from a PC
	// editor must not leave a '\r' on it; emu-options reads it the same way)
	out_value[strcspn(out_value, "\r\n")] = '\0';
	return 1;
}
