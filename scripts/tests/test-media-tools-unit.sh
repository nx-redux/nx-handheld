#!/usr/bin/env bash
# Host unit tests for the music player and the media tools: runs each
# component's tests/run_tests.sh (host compiler only, nothing on a device).
set -euo pipefail
cd "$(dirname "$0")/../.."

for dir in musicplayer mediaplayer imageviewer audiomon taskset osdmusic libgametimedb show2; do
	echo "== workspace/all/$dir/tests"
	sh "workspace/all/$dir/tests/run_tests.sh"
done
echo "media tools unit tests passed"
