#!/usr/bin/env bash
# Host test for N64.pak launch.sh (both platforms): minarch.elf stubbed on PATH.
set -euo pipefail
cd "$(dirname "$0")/../.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FAIL=0; fail() { echo "FAIL: $*"; FAIL=1; }
mkdir -p "$TMP/udisk" "$TMP/bin" "$TMP/logs" "$TMP/sd/.userdata/shared" "$TMP/sd/.userdata/tg5040" "$TMP/sd/.userdata/tg5050"
cat > "$TMP/bin/minarch.elf" <<'EOF'
#!/bin/sh
{ echo "CORE=$1"; echo "ROM=$2"; echo "LEGACY=${NX_M64P_LEGACY_SAVE_DIR-UNSET}"; echo "CORENP=${NX_CORE_NETPLAY-UNSET}"; echo "TX=${NX_M64P_TX_PATH-UNSET}"; echo "TXC=${NX_M64P_TX_CACHE_PATH-UNSET}"; echo "ROLE=${NETPLAY_ROLE-UNSET}"; } > "$T/minarch_env"
EOF
# the wizard: records its arguments, pairs as the host
cat > "$TMP/bin/netplay.elf" <<'EOF'
#!/bin/sh
echo "$*" >> "$T/wizard_args"
while [ $# -gt 0 ]; do
	[ "$1" = "--session-file" ] && printf 'NETPLAY_ROLE=host\nNETPLAY_PEER_IP=10.0.0.2\nNETPLAY_MODE=wifi\n' > "$2"
	shift
done
EOF
chmod +x "$TMP/bin/minarch.elf" "$TMP/bin/netplay.elf"
export T="$TMP"
for PLAT in tg5040 tg5050; do
	PAK="skeleton/SYSTEM/$PLAT/paks/Emus/N64.pak"
	rm -f "$TMP/minarch_env" "$TMP/wizard_args"
	PATH="$TMP/bin:$PATH" SDCARD_PATH="$TMP/sd" SAVES_PATH="$TMP/sd/Saves" LOGS_PATH="$TMP/logs" \
		USERDATA_PATH="$TMP/sd/.userdata/$PLAT" SHARED_USERDATA_PATH="$TMP/sd/.userdata/shared" \
		SHARED_SYSTEM_PATH="$PWD/skeleton/SYSTEM/shared" NETPLAY_LAUNCH_FLAG="$TMP/no_flag" NX_N64_SYSFS_ROOT="$TMP/sys" NX_UDISK="$TMP/udisk" \
		sh "$PWD/$PAK/launch.sh" "$TMP/sd/Roms/Nintendo 64 (N64)/Mario Kart 64.z64" >/dev/null 2>&1 || true
	[ -f "$TMP/minarch_env" ] || { fail "$PLAT: minarch not launched"; continue; }
	grep -q "CORE=.*/N64.pak/mupen64plus_next_libretro.so" "$TMP/minarch_env" || fail "$PLAT: core path"
	grep -q "ROM=.*Mario Kart 64.z64" "$TMP/minarch_env" || fail "$PLAT: rom passed"
	grep -q "LEGACY=$TMP/sd/.userdata/shared/N64-mupen64plus/data/mupen64plus/save" "$TMP/minarch_env" || fail "$PLAT: legacy save dir"
	grep -q "CORENP=UNSET" "$TMP/minarch_env" || fail "$PLAT: no core netplay on a plain launch"
	grep -q "ROLE=UNSET" "$TMP/minarch_env" || fail "$PLAT: no lockstep session on a plain launch"
	[ ! -e "$TMP/wizard_args" ] || fail "$PLAT: no wizard on a plain launch"
	# hi-res packs stay where the standalone kept them
	grep -qx "TX=$TMP/sd/Roms/Nintendo 64 (N64)/.hires_texture" "$TMP/minarch_env" || fail "$PLAT: hi-res texture dir"
	grep -qx "TXC=$TMP/sd/Roms/Nintendo 64 (N64)/.cache" "$TMP/minarch_env" || fail "$PLAT: hi-res cache dir"
	grep -q 'swapon "$SWAPFILE"' "$PAK/launch.sh" && grep -q 'swapoff "$SWAPFILE"' "$PAK/launch.sh" || fail "$PLAT: hi-res swap file on/off"
	for f in nx_paths.sh nx_netplay_map.awk nx_n64_netplay.sh m64p-server.elf; do [ ! -e "$PAK/$f" ] || fail "$PLAT: stale $f"; done
	[ -f "$PAK/netplay" ] || fail "$PLAT: netplay marker"
	[ ! -e "$TMP/udisk/n64_swap" ] || fail "$PLAT: no hi-res packs: no swap file"
	mkdir -p "$TMP/sd/Roms/Nintendo 64 (N64)/.hires_texture/MK64"
	PATH="$TMP/bin:$PATH" SDCARD_PATH="$TMP/sd" SAVES_PATH="$TMP/sd/Saves" LOGS_PATH="$TMP/logs" \
		USERDATA_PATH="$TMP/sd/.userdata/$PLAT" SHARED_USERDATA_PATH="$TMP/sd/.userdata/shared" \
		SHARED_SYSTEM_PATH="$PWD/skeleton/SYSTEM/shared" NETPLAY_LAUNCH_FLAG="$TMP/no_flag" NX_N64_SYSFS_ROOT="$TMP/sys" NX_UDISK="$TMP/udisk" NX_N64_SWAP_MB=1 \
		sh "$PWD/$PAK/launch.sh" "$TMP/sd/Roms/Nintendo 64 (N64)/Mario Kart 64.z64" >/dev/null 2>&1 || true
	[ -f "$TMP/udisk/n64_swap" ] || fail "$PLAT: hi-res packs present: swap file created"
	rm -rf "$TMP/sd/Roms/Nintendo 64 (N64)/.hires_texture" "$TMP/udisk/n64_swap"
	# Launch with Netplay: the shared lockstep prelaunch (two players, save sync), never the core's own netplay
	rm -f "$TMP/minarch_env" "$TMP/wizard_args" "$TMP/session"
	: > "$TMP/flag"
	PATH="$TMP/bin:$PATH" SDCARD_PATH="$TMP/sd" SAVES_PATH="$TMP/sd/Saves" LOGS_PATH="$TMP/logs" \
		USERDATA_PATH="$TMP/sd/.userdata/$PLAT" SHARED_USERDATA_PATH="$TMP/sd/.userdata/shared" \
		SHARED_SYSTEM_PATH="$PWD/skeleton/SYSTEM/shared" NETPLAY_LAUNCH_FLAG="$TMP/flag" NETPLAY_SESSION_FILE="$TMP/session" \
		NETPLAY_SAVE_STAGE_DIR="$TMP/stage" NETPLAY_SAVE_DATA_DIR="$TMP/fetch" NX_N64_SYSFS_ROOT="$TMP/sys" NX_UDISK="$TMP/udisk" \
		sh "$PWD/$PAK/launch.sh" "$TMP/sd/Roms/Nintendo 64 (N64)/Mario Kart 64.z64" >/dev/null 2>&1 || true
	[ -f "$TMP/minarch_env" ] || { fail "$PLAT: netplay: minarch not launched"; continue; }
	grep -q "ROLE=host" "$TMP/minarch_env" || fail "$PLAT: netplay: lockstep role exported"
	grep -q "CORENP=UNSET" "$TMP/minarch_env" || fail "$PLAT: netplay: no core netplay"
	grep -q -- "--caps core=mupen64plus_next,tag=N64" "$TMP/wizard_args" || fail "$PLAT: netplay: core check caps"
	grep -q -- "--serve-dir" "$TMP/wizard_args" || fail "$PLAT: netplay: save sync"
	! grep -q -- "--max-players" "$TMP/wizard_args" || fail "$PLAT: netplay: two players"
	[ ! -e "$TMP/flag" ] || fail "$PLAT: netplay: launch flag consumed"
done
grep -q '^mupen64plus-rdp-plugin = rice$' skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/default-brick.cfg || fail "brick rice"
grep -q '^mupen64plus-rdp-plugin = rice$' skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/default.cfg || fail "smart pro rice"
grep -q '^mupen64plus-rdp-plugin = gliden64$' skeleton/SYSTEM/tg5050/paks/Emus/N64.pak/default.cfg || fail "sps gliden64"
for c in skeleton/SYSTEM/tg5040/paks/Emus/N64.pak/default*.cfg skeleton/SYSTEM/tg5050/paks/Emus/N64.pak/default.cfg; do
	grep -q '^mupen64plus-MaxHiResTxVramLimit = 200$' "$c" || fail "$c: hi-res VRAM limit 200"
	grep -q '^mupen64plus-txHiresEnable = False$' "$c" || fail "$c: hi-res textures off by default"
	grep -q '^mupen64plus-CorrectTexrectCoords = Auto$' "$c" || fail "$c: 2D black-line fix Auto"
	grep -q '^mupen64plus-EnableNativeResTexrects = Optimized$' "$c" || fail "$c: native-res 2D Optimized"
done
[ "$FAIL" = 0 ] && echo "ALL PASS" || exit 1
