#!/bin/sh
# Host-side unit tests for emu-options (options.elf backends) and the minarch options.json
# serializer it reads. No SDL, no cross toolchain. Run from anywhere.
set -eu
cd "$(dirname "$0")"
mkdir -p build
CF="-std=gnu99 -Wall -Werror -Wno-deprecated-declarations -fsanitize=address -g -I.. -I../../common"
OVL="../../common/emu_overlay_cfg.c ../../common/cjson/cJSON.c"
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc $CF -o build/test_emu_ovl_enum test_emu_ovl_enum.c $OVL
./build/test_emu_ovl_enum
cc $CF -o build/test_launch_schema test_launch_schema.c ../opts_override.c $OVL
./build/test_launch_schema
cc $CF -o build/test_opts_launch test_opts_launch.c ../opts_launch.c $OVL
./build/test_opts_launch
# The -D limits must match workspace/all/emu-options/Makefile.
cc $CF -DEMU_OVL_MAX_SECTIONS=32 -DEMU_OVL_MAX_ITEMS=64 -DEMU_OVL_MAX_DESC=512 \
	-I../../../tg5040/platform -DPLATFORM=\"tg5040\" \
	-o build/test_opts_minarch test_opts_minarch.c ../opts_minarch.c ../opts_override.c ../../common/utils.c $OVL
./build/test_opts_minarch
cc $CF -o build/test_opts_override test_opts_override.c ../opts_override.c $OVL
./build/test_opts_override
# libretro-common is cloned by minarch's build and is not tracked; without it, use the stub
# that carries only the option structs ma_opts_schema.c reads.
LR=../../minarch/libretro-common/include
[ -f "$LR/libretro.h" ] || LR=stub
cc $CF -I../../minarch -I"$LR" -o build/test_opts_schema test_opts_schema.c ../../minarch/ma_opts_schema.c $OVL
./build/test_opts_schema
echo "emu-options: all tests passed"
