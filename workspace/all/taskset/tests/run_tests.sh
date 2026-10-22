#!/bin/sh
# Host-side unit tests for taskset/. No cross toolchain. The "taskset:" lines
# on stderr are the argument errors the test provokes.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
# scripts/clangd/include supplies glibc's cpu_set_t API on macOS (a no-op on Linux)
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I../../../../scripts/clangd/include -o /tmp/nx_test_taskset test_taskset.c
/tmp/nx_test_taskset
