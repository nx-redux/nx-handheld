#!/bin/sh
# Host-side tests for gba_wifi_link. Run from anywhere.
#   check_upstream.sh  rfu.c / serial_proto.c are gpSP's, unchanged
#   test_gwl           behaviour against a fake emulator
#   test_gwl_diff      the library against gpSP's own code, same sessions
#   gen_games.sh       gwl_games.h still matches gpSP's gba_over.h
#   test_gwl_mode      the per-game auto mode rule and ROM probe
#   test_mgba_option   how mGBA's mgba_link_mode resolves at load
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}
B=${T%/}/test_gba_wifi_link
SRC=$(sh "$D/tests/check_upstream.sh" | tail -n 1)
# gpSP's rfu.c leaves a struct partly to zero-initialisation.
CC="cc -std=gnu99 -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=address"
rm -rf "$B"
mkdir -p "$B/ref"
LIB="$D/gwl.c $D/rfu.c $D/serial_proto.c"

# gwl.c's calls into gpSP's receive functions go through test spies.
SPY=""
for S in rfu_net_receive serialpoke_net_receive serialaw_net_receive; do
	SPY="$SPY -Dgwl_gp_$S=gwl_spy_$S"
done
$CC -I"$D" $SPY -c "$D/gwl.c" -o "$B/gwl_spy.o"
$CC -I"$D" "$B/gwl_spy.o" "$D/rfu.c" "$D/serial_proto.c" "$D/tests/test_gwl.c" -o "$B/test_gwl"
"$B/test_gwl"

# The reference: gpSP's files next to a stub common.h, every global renamed
# ref_* so both copies link into one binary.
cp "$SRC/rfu.c" "$SRC/serial_proto.c" "$B/ref/"
cp "$D/tests/ref_common.h" "$B/ref/common.h"
REN=""
for S in serial_mode netplay_client_id netplay_num_clients cpu_ticks \
	netpacket_send netpacket_poll_receive rand_gen rand_seed ismemzero \
	rfu_reset rfu_net_disconnect rfu_update rfu_transfer rfu_frame_update rfu_net_receive \
	serialproto_reset serialpoke_frame_update serialpoke_master_send serialpoke_update \
	serialpoke_net_receive serialaw_frame_update serialaw_master_send serialaw_update \
	serialaw_net_receive; do
	REN="$REN -D$S=ref_$S"
done
for F in rfu serial_proto; do
	$CC $REN -I"$SRC" -c "$B/ref/$F.c" -o "$B/ref/$F.o"
done
$CC -I"$D" $LIB "$D/tests/test_gwl_diff.c" "$B/ref/rfu.o" "$B/ref/serial_proto.o" -o "$B/test_gwl_diff"
"$B/test_gwl_diff"

sh "$D/tests/gen_games.sh" --check
$CC -I"$D" "$D/gwl_mode.c" "$D/tests/test_gwl_mode.c" -o "$B/test_gwl_mode"
"$B/test_gwl_mode"
$CC -I"$D" "$D/gwl_mode.c" "$D/tests/test_mgba_option.c" -o "$B/test_mgba_option"
"$B/test_mgba_option"
