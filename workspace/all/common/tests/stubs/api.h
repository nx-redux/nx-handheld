// Test-only stand-in for common/api.h (which pulls in SDL): defines.h and the logging macros nextui/recents.c and
// nextui/shortcuts.c use, so they build on the host. Put -Istubs before -I.. (the real api.h).
#ifndef NX_TEST_STUB_API_H
#define NX_TEST_STUB_API_H

#include "defines.h" // the real api.h brings it in

#define LOG_info(...) ((void)0)
#define LOG_warn(...) ((void)0)

#endif
