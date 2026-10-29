#ifndef P8SHIM_PATHS_H
#define P8SHIM_PATHS_H

// Cart save data lives in $P8NATIVE_CDATA (Saves/P8/cdata, shared with fake08), not in
// PICO-8's home: the shim's file hooks send $P8NATIVE_HOME/cdata[/...] there.

#include <stddef.h>
#include <sys/types.h>

// Returns buf holding the redirected path for $P8NATIVE_HOME/cdata and anything under it,
// else path itself (also when either variable is unset or buf is too small).
// The first redirect creates $P8NATIVE_CDATA and its parents.
const char* paths_redirect(const char* path, char* buf, size_t size);

// mkdir used to create $P8NATIVE_CDATA; the shim points it at the real one so its own mkdir hook is skipped
extern int (*paths_mkdir)(const char* path, mode_t mode);

#endif
