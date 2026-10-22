#pragma once

// Reads `key = value` from a cfg string (plain C, host-tested by
// tests/test_config_value.c). The key must start a line, optionally behind a
// `-` lock marker (then *lock is set to 1 when lock is not NULL), and be
// followed by " = ". out_value (at least 256 bytes) gets the value up to the
// line ending, truncated to 255 chars. Returns 1 when found, else 0.
int Config_getValue(const char* cfg, const char* key, char* out_value, int* lock);
