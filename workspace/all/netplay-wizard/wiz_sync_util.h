#ifndef WIZ_SYNC_UTIL_H
#define WIZ_SYNC_UTIL_H

// The host-testable half of wizard_sync.c: name/config/IP validators, rsync progress parsing and
// the all-or-nothing staging directory. Each function is documented in wiz_sync_util.c.

#include <stdbool.h>
#include <stddef.h>

// Must equal the literal in wizard.h's wiz_sync_pull() signature: the parameter
// is char names[][64] and this is what indexes it here.
#define WIZ_SYNC_NAME_MAX 64
// Where a pull lands before any of it is allowed into fetch_to. Inside fetch_to
// so the commit is a same-filesystem rename; leading dot so a directory listing
// (and NXRedux's own file browsers, which hide dotfiles) does not show it.
#define WIZ_SYNC_STAGE_DIR ".netplay-staging"
// Longest path this file builds: fetch_to + '/' + staging + '/' + a 63-char name.
#define WIZ_SYNC_PATH_MAX 512

bool wiz_conf_value_is_safe(const char* value);
bool wiz_ip_is_plain(const char* ip);
bool wiz_sync_name_is_safe(const char* name);
int wiz_find_duplicate(char names[][WIZ_SYNC_NAME_MAX], int count);
int wiz_progress_percent(const char* line);
int wiz_take_percent(char* buf, size_t size, size_t* len, int current);
void wiz_stage_path(char* out, size_t out_size, const char* fetch_to);
void wiz_stage_purge(const char* fetch_to);
int wiz_stage_prepare(const char* fetch_to);
int wiz_stage_commit(const char* fetch_to, char names[][WIZ_SYNC_NAME_MAX], int count);

#endif
