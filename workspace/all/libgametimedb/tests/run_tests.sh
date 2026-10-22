#!/bin/sh
# Host-side unit tests for libgametimedb/. Needs the host sqlite3 library, no
# cross toolchain. The /tmp/next command parsing (next_cmd.h) is covered by
# common/tests/test_next_cmd.c.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -Wno-deprecated-declarations -fsanitize=address -g -I.. -I../../common -I../../../tg5040/platform -DPLATFORM=\"tg5040\" -o /tmp/nx_test_gametimedb_paths ../gametimedb.c ../../common/utils.c test_gametimedb_paths.c -lsqlite3
/tmp/nx_test_gametimedb_paths
