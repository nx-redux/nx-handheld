CORES= a2600
CORES+= a5200
CORES+= a7800
CORES+= handy
CORES+= c64
CORES+= c128
CORES+= fake-08
CORES+= fbneo
CORES+= fceumm
CORES+= gambatte
CORES+= gpsp
CORES+= libretro-cap32
CORES+= libretro-uae
CORES+= mednafen_pce_fast
CORES+= mednafen_supafaust
CORES+= mednafen_vb
CORES+= mednafen_wswan
CORES+= mgba
CORES+= pcsx_rearmed
CORES+= swanstation
CORES+= pet
CORES+= picodrive
CORES+= genesis_plus_gx
CORES+= plus4
CORES+= pokemini
CORES+= race
CORES+= snes9x
CORES+= vic
CORES+= prboom
CORES+= bluemsx
CORES+= gearcoleco
CORES+= flycast
CORES+= flycast_legacy
CORES+= ppsspp
CORES+= mupen64plus_next

###############################

# optional core vars
# *_REPO=
# *_HASH=
# *_CORE=
# *_FLAGS=
# *_MAKEFILE=
# *_BUILD_PATH=
# *_SUBMODULES=
# *_LICENSE=
# *_LICENSE_NOTICE=

a2600_REPO = https://github.com/libretro/stella2014-libretro
a2600_CORE = stella2014_libretro.so
a2600_HASH = 7d1361e407e63f29e52892655069e5fb4096e691
a2600_LICENSE = stella/license.txt

a5200_CORE = a5200_libretro.so
a5200_HASH = 40c6f2f1ad4a3145b328d5baaf010fae6c7e752b

a7800_CORE = prosystem_libretro.so
a7800_REPO = https://github.com/libretro/prosystem-libretro
a7800_HASH = 8a88014287c7a01cd568067e5a557d0a2b2a051f

handy_REPO = https://github.com/libretro/libretro-handy
handy_HASH = bc55d462f0b2d6b073ea93dc552ebd73cec60fd1
handy_LICENSE = lynx/license.txt

fbneo_REPO = https://github.com/libretro/FBNeo
fbneo_CORE = fbneo_libretro.so
fbneo_BUILD_PATH = fbneo/src/burner/libretro
fbneo_MAKE = make
fbneo_HASH = d15071abf14191eebb4a5d7c7147398edcf237c8
# the root LICENSE.md only links to this
fbneo_LICENSE = src/license.txt

mednafen_pce_fast_REPO = https://github.com/libretro/beetle-pce-fast-libretro
mednafen_pce_fast_HASH = 2de1ce1c3f237b161c126c54ba407990bb86d1e9

mednafen_vb_REPO = https://github.com/libretro/beetle-vb-libretro
mednafen_vb_HASH = d5ae54c8e23b573a3b34e888b2d48a588027e92c

# WonderSwan / WonderSwan Color (WSC.pak). RetroAchievements via the WSC/WS
# tags (see all/minarch/ra_consoles.h).
mednafen_wswan_REPO = https://github.com/libretro/beetle-wswan-libretro
mednafen_wswan_HASH = 4b01295838ea89e3f1355bbe4cb5cf98aa6108cd

fake-08_REPO = https://github.com/jtothebell/fake-08
fake-08_CORE = fake08_libretro.so
fake-08_BUILD_PATH = fake-08/platform/libretro
fake-08_HASH = 814991a2571ad3970e386cef48f3b148aa1c27b9

fceumm_REPO = https://github.com/libretro/libretro-fceumm
fceumm_HASH = e0f3086a8104f8fbbe49a2615b10401504a9533f

gambatte_REPO = https://github.com/libretro/gambatte-libretro
gambatte_HASH = 77f2567d718b8973d02caa2daa8dce4953faf811

# Pinned: upstream gpsp drifts under the all/cores/patches/gpsp/* patches
# (rfu.c for 001/002, the JIT cache mapping for 003, load_gamepak for 004).
# When bumping this hash, regen them against the new source in the same change.
# Keep nx-mobile's gpsp_HASH on the same commit (it shares 001/002/004).
gpsp_HASH = 5819380c2ffb0900219d700a382ee68c464ebb99
# The lockstep link state machine (shared with mGBA) is compiled in from
# all/cores/sio_lockstep (004 patch); NX_SIOLS_DIR points the patched Makefile at it.
gpsp_FLAGS = NX_SIOLS_DIR=$(CURDIR)/../../all/cores/sio_lockstep
gpsp_DEPS = $(wildcard ../../all/cores/sio_lockstep/*.c ../../all/cores/sio_lockstep/*.h)

mednafen_supafaust_REPO = https://github.com/libretro/supafaust
mednafen_supafaust_HASH = 642d1d1b6684aa7e306a02a89885f3f5456a5157

mgba_HASH = 7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6
# Upstream builds the libretro core with CMake only (8940477), via a build script
# like flycast/ppsspp. The real link cable (all/cores/patches/mgba/001) compiles in
# the same lockstep state machine as gpSP, and the Wi-Fi link (003) gpSP's
# Wireless Adapter and cable protocols from all/cores/gba_wifi_link; NX_SIOLS_DIR
# and NX_GWL_DIR point the patched CMakeLists.txt at them.
mgba_MAKE = NX_SIOLS_DIR=$(CURDIR)/../../all/cores/sio_lockstep NX_GWL_DIR=$(CURDIR)/../../all/cores/gba_wifi_link sh $(CURDIR)/../../all/cores/mgba/build-libretro.sh $(PLATFORM)
mgba_DEPS = ../../all/cores/mgba/build-libretro.sh ../../all/cores/flycast/toolchain-aarch64.cmake $(wildcard ../../all/cores/sio_lockstep/*.c ../../all/cores/sio_lockstep/*.h ../../all/cores/gba_wifi_link/*.c ../../all/cores/gba_wifi_link/*.h ../../all/cores/patches/mgba/*.patch)
# That code is GPL-2.0-or-later, so the shipped license says the combined core is
# distributed under the GPL (MPL-2.0 section 3.3).
mgba_LICENSE_NOTICE = ../../all/cores/mgba/LICENSE-sio_lockstep.txt ../../all/cores/gba_wifi_link/NOTICE.txt

pcsx_rearmed_MAKEFILE = Makefile.libretro
pcsx_rearmed_HASH = 7b0e8f8fadfbce3d170d195b9d9eecaabfa08c8e
# pcsx has a disabled submodule for gnulib, which takes forever to check out and is unused - skip checkout
pcsx_rearmed_SUBMODULES = frontend/libpicofe

# SwanStation (PSX.pak): libretro's DuckStation fork, a more accurate PlayStation
# core than pcsx_rearmed with a GLES hardware renderer. Its Makefile.libretro only
# knows host platforms, so build as unix and name the CPU (the docker host is
# x86_64); the shared patch strips the .so.
swanstation_HASH = 631f0fb31b809e0f889d7c551fe695788e39c1d3
swanstation_MAKE = make -f Makefile.libretro platform=unix CPU_ARCH=aarch64

picodrive_REPO = https://github.com/irixxxx/picodrive
picodrive_MAKEFILE = Makefile.libretro
# No background Audio on Sega CD
# https://github.com/LoveRetro/NextUI/issues/68
picodrive_HASH = 9c298c1fe7347a50b07a494cdb8c91d2f9b40c6a

# Alternative Mega Drive/Genesis (+ SegaCD/SMS/GG/SG-1000) core. Its
# Makefile.libretro doesn't recognise our platform= value and would fall through
# to the Windows .dll branch, so force the generic unix branch: it builds a .so
# with the toolchain's $(CC) (aarch64 cross gcc). RetroAchievements picks it up
# via the GPGX tag (see all/minarch/ra_consoles.h). LDFLAGS=-s strips at link
# time (the unix branch leaves LDFLAGS empty) so the .so matches the other
# cores' stripped size instead of shipping ~13 MB of debug_info.
genesis_plus_gx_REPO = https://github.com/libretro/Genesis-Plus-GX
genesis_plus_gx_MAKE = make -f Makefile.libretro platform=unix LDFLAGS=-s
genesis_plus_gx_HASH = f27655b592ef43f317272d79883bcaa10d3ac8ad

pokemini_REPO = https://github.com/libretro/PokeMini
pokemini_MAKEFILE = Makefile.libretro
# Same pin as nx-mobile: netplay and Device Sync states need the same core on both.
pokemini_HASH = 132111b76343559860532a1ccc094f93f1ed5650

prboom_REPO = https://github.com/libretro/libretro-prboom
prboom_HASH = 4849268e7342494193f2bbf2d0128a751cf58da7

race_HASH = c7810dd7f172827bfa2004813bc000b13786636b

snes9x2005_plus_REPO = https://github.com/libretro/snes9x2005
snes9x2005_plus_FLAGS = USE_BLARGG_APU=1
snes9x2005_plus_LICENSE = copyright

snes9x_REPO = https://github.com/libretro/snes9x
snes9x_CORE = snes9x_libretro.so
snes9x_BUILD_PATH = snes9x/libretro
snes9x_HASH = fae2fea08f74180759ef540ee94259213f503480

libretro-uae_REPO = https://github.com/libretro/libretro-uae
libretro-uae_BRANCH = 2.6.1
libretro-uae_CORE = puae2021_libretro.so
libretro-uae_HASH = 2826e67ac623974f3b8ab15951f2f8407d81fc17

libretro-cap32_REPO = https://github.com/libretro/libretro-cap32
libretro-cap32_CORE = cap32_libretro.so
libretro-cap32_HASH = 54eabbb210a6149496b1d023bebddb3f0120bc33
libretro-cap32_LICENSE = cap32/COPYING.txt

c64_REPO  = https://github.com/libretro/vice-libretro
c64_CORE  = vice_x64_libretro.so
c64_FLAGS = EMUTYPE=x64
c64_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

c128_REPO  = https://github.com/libretro/vice-libretro
c128_CORE  = vice_x128_libretro.so
c128_FLAGS = EMUTYPE=x128
c128_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

vic_REPO  = https://github.com/libretro/vice-libretro
vic_CORE  = vice_xvic_libretro.so
vic_FLAGS = EMUTYPE=xvic
vic_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

pet_REPO  = https://github.com/libretro/vice-libretro
pet_CORE  = vice_xpet_libretro.so
pet_FLAGS = EMUTYPE=xpet
pet_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

plus4_REPO  = https://github.com/libretro/vice-libretro
plus4_CORE  = vice_xplus4_libretro.so
plus4_FLAGS = EMUTYPE=xplus4
plus4_HASH  = 8818c3a39146b9a26df23a1e4880b4420f93a255

bluemsx_REPO = https://github.com/libretro/blueMSX-libretro
bluemsx_CORE = bluemsx_libretro.so
bluemsx_MAKEFILE = Makefile.libretro
bluemsx_HASH = 036376d6679c9e153712dbbb3fdca774afc49706

gearcoleco_REPO = https://github.com/drhelius/Gearcoleco.git
gearcoleco_CORE = gearcoleco_libretro.so
gearcoleco_BUILD_PATH = gearcoleco/platforms/libretro
gearcoleco_HASH = 2805e70fd56c3bb6a0412a4ddaf210eefc8a5d7c

###############################

# flycast (Dreamcast) libretro core, on nx-mobile's pin (past v2.7). CMake, so
# flycast_MAKE runs a build script (toolchain + flags live next to it); shared
# patches are in all/cores/patches/flycast/. Only the submodules the libretro
# build uses are initialised (tinygettext's own submodule is finished by the script).
flycast_REPO = https://github.com/flyinghead/flycast
flycast_HASH = 7adbf3caef251b153e348df3f469a0f35878339d
flycast_SUBMODULES = core/deps/libchdr core/deps/tinygettext core/deps/asio
flycast_MAKE = sh $(CURDIR)/../../all/cores/flycast/build-libretro.sh $(PLATFORM)
flycast_DEPS = ../../all/cores/flycast/build-libretro.sh ../../all/cores/flycast/toolchain-aarch64.cmake

# PPSSPP (PSP) libretro core, on nx-mobile's pin (netplay and Device Sync states
# need the same core on both). CMake via a build script like flycast; shared patches in
# all/cores/patches/ppsspp/. All bundled submodules (ffmpeg, glslang, ...) are
# needed, so the template's default recursive init is used.
ppsspp_REPO = https://github.com/hrydgard/ppsspp
ppsspp_HASH = d4e3c7eb2bf0ec9241621b3752cd3e5406178c78
ppsspp_MAKE = NX_CORES_OUTPUT=$(CURDIR)/$(OUTPUT_ROOT) sh $(CURDIR)/../../all/cores/ppsspp/build-libretro.sh $(PLATFORM)
ppsspp_DEPS = ../../all/cores/ppsspp/build-libretro.sh ../../all/cores/ppsspp/outline_atomics.c ../../all/cores/flycast/toolchain-aarch64.cmake

# flycast_legacy (DCX, "Dreamcast Lite"): libretro's own fork of flycast as of
# 2022-04-06 (4c293f30; diverged from upstream in 2015), the build CrossMix-OS
# ships. On the Brick it runs heavy games at full speed where upstream 2.7 does
# ~80 %. Built with the fork's libretro Makefile (platform=arm64: GLES, arm64
# dynarec); TARGET_NAME keeps it from colliding with DC's flycast_libretro.so.
# No OpenMP (only texture upscaling uses it, which DCX leaves off). CC_AS: the
# arm64 dynarec stub (ngen_arm64.S) needs the C preprocessor, so gcc assembles it.
flycast_legacy_REPO = https://github.com/libretro/flycast
flycast_legacy_HASH = 4c293f306bc16a265c2d768af5d0cea138426054
# tg5050's toolchain has no KHR/khrplatform.h (its GLES2 headers include it):
# the Khronos header is vendored in all/cores/flycast_legacy/include. Its
# libGLESv2 is a stub whose symbols live in libmali, so link that too (the
# fork links with --no-undefined).
flycast_legacy_MAKE = CPATH=$(CURDIR)/../../all/cores/flycast_legacy/include make platform=arm64 ARCH=aarch64 TARGET_NAME=flycast_legacy HAVE_OPENMP=0 CC_AS="$(CC)" GL_LIB="-lGLESv2 -lmali"

# mupen64plus-next (N64.pak): GLES3, GLideN64 + new aarch64 dynarec, core
# netplay (patch 0002, our m64p-server) and the Rice video plugin built in
# (patch 0003 + build-rice.sh) for the PowerVR devices.
mupen64plus_next_REPO = https://github.com/libretro/mupen64plus-libretro-nx
mupen64plus_next_HASH = 15bada85906593376e8fd697f1b0b3af3a20f512
# No real submodules, but a stray gitlink (paraLLEl RSP's lightning/gnulib, no
# .gitmodules) breaks the template's default recursive init; a plain-file
# pathspec makes init/update a no-op.
mupen64plus_next_SUBMODULES = -- README.md
# core licence + the built-in Rice plugin's (written by build-libretro.sh)
mupen64plus_next_LICENSE = NX-LICENSES.txt
mupen64plus_next_MAKE = sh $(CURDIR)/../../all/cores/mupen64plus_next/build-libretro.sh $(PLATFORM)
mupen64plus_next_DEPS = ../../all/cores/mupen64plus_next/build-libretro.sh ../../all/cores/mupen64plus_next/build-rice.sh $(wildcard ../../all/cores/mupen64plus_next/rice/*) $(wildcard ../../all/cores/mupen64plus_next/rice/patches/*.patch) $(wildcard ../../all/cores/mupen64plus_next/nx/*)

include ../../all/cores/Makefile
