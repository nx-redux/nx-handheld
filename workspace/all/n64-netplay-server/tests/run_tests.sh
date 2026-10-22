#!/bin/sh
# Run by `make test` (which builds ./m64p-server-native and tests/test_client first).
set -e
cd "$(dirname "$0")/.."
# Pure timer/helper tests: udp.c + state.c, no sockets.
OUT=${TMPDIR:-/tmp}/test_n64_state
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g udp.c state.c tests/test_state.c -o "$OUT"
"$OUT"
# test_client spawns the server itself, from ./m64p-server-native
exec ./tests/test_client
