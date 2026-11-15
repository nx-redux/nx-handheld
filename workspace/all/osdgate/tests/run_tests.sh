#!/bin/sh
# Host unit test for workspace/all/osdgate/osdgate.c via LD_PRELOAD / DYLD_INSERT_LIBRARIES.
set -eu
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/fake_sdl.c" <<'EOF'
#include <stdint.h>
#include <string.h>

typedef struct {
	uint32_t type;
	uint8_t pad[52];
} FakeEvent;

static FakeEvent q[32];
static int q_head = 0, q_tail = 0;
static uint8_t keys[512];

void fake_sdl_push(uint32_t type) {
	q[q_tail].type = type;
	memset(q[q_tail].pad, 0, sizeof(q[q_tail].pad));
	q_tail = (q_tail + 1) % 32;
}

void fake_sdl_set_key(int scancode, uint8_t pressed) {
	if (scancode >= 0 && scancode < 512)
		keys[scancode] = pressed;
}

int SDL_PollEvent(void* event) {
	if (q_head == q_tail)
		return 0;
	if (event)
		memcpy(event, &q[q_head], sizeof(FakeEvent));
	q_head = (q_head + 1) % 32;
	return 1;
}

int SDL_PeepEvents(void* events, int numevents, int action, uint32_t minType, uint32_t maxType) {
	(void)action; (void)minType; (void)maxType;
	int n = 0;
	while (n < numevents && q_head != q_tail) {
		memcpy((uint8_t*)events + (size_t)n * sizeof(FakeEvent), &q[q_head], sizeof(FakeEvent));
		q_head = (q_head + 1) % 32;
		n++;
	}
	return n;
}

const uint8_t* SDL_GetKeyboardState(int* numkeys) {
	if (numkeys)
		*numkeys = 512;
	return keys;
}
EOF

if [ "$(uname -s)" = "Darwin" ]; then
	# macOS two-level namespace: RTLD_NEXT searches libosdgate's own LC_LOAD_DYLIBs
	cc -dynamiclib -fpic -std=gnu99 "$TMP/fake_sdl.c" -o "$TMP/libfakesdl.dylib"
	cc -dynamiclib -fpic -std=gnu99 "$SRC/osdgate.c" "$TMP/libfakesdl.dylib" -o "$TMP/libosdgate.dylib" -ldl
	cc -std=gnu99 -Wall -Wextra "$SRC/tests/test_osdgate.c" "$TMP/libosdgate.dylib" "$TMP/libfakesdl.dylib" -o "$TMP/test_osdgate"
	OSDGATE_STATE_PATH="$TMP/osdd_show_up" "$TMP/test_osdgate"
else
	cc -shared -fpic -std=gnu99 "$TMP/fake_sdl.c" -o "$TMP/libfakesdl.so"
	cc -shared -fpic -std=gnu99 "$SRC/osdgate.c" -o "$TMP/libosdgate.so" -ldl
	cc -std=gnu99 -Wall -Wextra "$SRC/tests/test_osdgate.c" "$TMP/libfakesdl.so" -Wl,-rpath,"$TMP" -o "$TMP/test_osdgate"
	OSDGATE_STATE_PATH="$TMP/osdd_show_up" \
		LD_PRELOAD="$TMP/libosdgate.so" \
		"$TMP/test_osdgate"
fi
