#!/bin/sh
# Host-side unit tests for the pure netplay link modules (siolink handshake,
# gpsp_serial resolution). Run from anywhere.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/test_netplay_link
cc -std=gnu99 -Wall -Wextra -Werror -I"$D" -I"$D/../usblink" \
	"$D/siolink_proto.c" "$D/gbalink_mode.c" "$D/tests/test_netplay_link.c" -o "$OUT"
"$OUT"
OUT_IO=${TMPDIR:-/tmp}/test_net_io
cc -std=gnu99 -Wall -Wextra -Werror -I"$D" "$D/network_common.c" "$D/tests/test_net_io.c" -o "$OUT_IO"
"$OUT_IO"
OUT_GS=${TMPDIR:-/tmp}/test_gbalink_stream
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I"$D" "$D/network_common.c" "$D/tests/test_gbalink_stream.c" -o "$OUT_GS"
"$OUT_GS"
OUT_DISC=${TMPDIR:-/tmp}/test_discovery
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I"$D" "$D/network_common.c" "$D/tests/test_discovery.c" -o "$OUT_DISC"
"$OUT_DISC"
OUT_FW=${TMPDIR:-/tmp}/test_frame_window
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I"$D" "$D/tests/test_frame_window.c" -o "$OUT_FW"
"$OUT_FW"
