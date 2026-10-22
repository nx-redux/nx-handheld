#!/bin/sh
# Runs every host-side unit test: each workspace/all/<component>/tests/run_tests.sh,
# the n64 netplay server suite and the host-only scripts/tests wrappers. No device,
# no cross toolchain. Exits non-zero if any suite fails; a summary is printed last.
set -u
cd "$(dirname "$0")/.."

LOG_DIR="${TMPDIR:-/tmp}/nx-host-tests"
rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"

passed=0
failed=""

run() { # name, command...
	name="$1"
	shift
	if "$@" >"$LOG_DIR/$name.log" 2>&1; then
		passed=$((passed + 1))
		echo "ok    $name"
	else
		failed="$failed $name"
		echo "FAIL  $name (log: $LOG_DIR/$name.log)"
	fi
}

for runner in workspace/all/*/tests/run_tests.sh; do
	component=$(basename "$(dirname "$(dirname "$runner")")")
	# the n64 server's runner expects the client built by its Makefile
	[ "$component" = "n64-netplay-server" ] && continue
	run "$component" sh "$runner"
done
run n64-netplay-server make -C workspace/all/n64-netplay-server test

# Host-only wrappers; the *-e2e and device scripts need adb and are run by hand.
for t in archive-cache cheat-match cheat-mem cheatdb-data rewind-budget apply-patches \
	scraper-variants media-tools-unit menu-animations turbo-shaper; do
	[ -f "scripts/tests/test-$t.sh" ] && run "script-$t" bash "scripts/tests/test-$t.sh"
done

echo
if [ -n "$failed" ]; then
	echo "$passed passed, failed:$failed"
	exit 1
fi
echo "$passed passed"
