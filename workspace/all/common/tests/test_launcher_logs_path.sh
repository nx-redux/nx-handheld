#!/bin/sh
# Host test for the nx_update_logs_path helper in MinUI.pak/launch.sh:
# debugLogging=1 -> LOGS_PATH is the persistent .userdata logs dir;
# debugLogging=0 (or missing key) -> LOGS_PATH is a freshly wiped tmpfs dir.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# The helper reads debugLogging from minuisettings.txt itself; each case
# writes the file it needs.
export USERDATA_PATH="$tmp/userdata"
export SHARED_USERDATA_PATH="$tmp/shared"
mkdir -p "$SHARED_USERDATA_PATH"
settings() { # settings <debugLogging value, or "" for no key>
	{
		echo "fontSize=1"
		[ -n "$1" ] && echo "debugLogging=$1"
		echo "haptics=1"
	} > "$SHARED_USERDATA_PATH/minuisettings.txt"
}

fail() { echo "FAIL: $*" >&2; exit 1; }

for plat in tg5040 tg5050; do
	launcher="$root/skeleton/SYSTEM/$plat/paks/MinUI.pak/launch.sh"
	sh -n "$launcher" || fail "$plat launch.sh does not parse"
	# Extract just the helper function so the test never runs the real boot script.
	sed -n '/^nx_update_logs_path() {/,/^}/p' "$launcher" > "$tmp/helper.sh"
	[ -s "$tmp/helper.sh" ] || fail "$plat: helper not found"
	# Redirect the tmpfs target under $tmp so the host /tmp is untouched.
	sed 's|/tmp/nx-logs|'"$tmp"'/nx-logs|g' "$tmp/helper.sh" > "$tmp/helper_t.sh"

	# 1. setting on -> persistent path, created, flag exported, stale tmpfs dir removed
	mkdir -p "$tmp/nx-logs"; echo stale > "$tmp/nx-logs/old.txt"
	settings 1
	out=$(sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$NX_DEBUG_LOGGING $LOGS_PATH"')
	[ "$out" = "1 $USERDATA_PATH/logs" ] || fail "$plat on: got '$out'"
	[ -d "$USERDATA_PATH/logs" ] || fail "$plat on: logs dir not created"
	[ ! -e "$tmp/nx-logs" ] || fail "$plat on: stale tmpfs dir not removed"

	# 2. setting off -> tmpfs path, pre-existing file wiped
	mkdir -p "$tmp/nx-logs"; echo stale > "$tmp/nx-logs/old.txt"
	settings 0
	out=$(sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$NX_DEBUG_LOGGING $LOGS_PATH"')
	[ "$out" = "0 $tmp/nx-logs" ] || fail "$plat off: got '$out'"
	[ -d "$tmp/nx-logs" ] || fail "$plat off: tmpfs dir not created"
	[ ! -e "$tmp/nx-logs/old.txt" ] || fail "$plat off: stale log not wiped"

	# 3. key absent (old settings file) -> behaves as off
	settings ""
	out=$(sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$LOGS_PATH"')
	[ "$out" = "$tmp/nx-logs" ] || fail "$plat missing key: got '$out'"

	# 4. no settings file yet (first boot) -> behaves as off
	rm -f "$SHARED_USERDATA_PATH/minuisettings.txt"
	out=$(sh -c '. "'"$tmp"'/helper_t.sh"; nx_update_logs_path; echo "$LOGS_PATH"')
	[ "$out" = "$tmp/nx-logs" ] || fail "$plat no settings file: got '$out'"
	rm -rf "$USERDATA_PATH" "$tmp/nx-logs"
done
echo "test_launcher_logs_path: OK"
