###########################################################
# Supported platforms
SUPPORTED_PLATFORMS = tg5040 tg5050
DEFAULT_PLATFORM = tg5040

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif
ifeq (,$(PLATFORM))
PLATFORM=$(DEFAULT_PLATFORM)
endif
ifeq (,$(filter $(PLATFORM),$(SUPPORTED_PLATFORMS)))
$(error Invalid PLATFORM '$(PLATFORM)'. Supported: $(SUPPORTED_PLATFORMS))
endif

###########################################################

CC = $(CROSS_COMPILE)gcc
BUILD = build/$(PLATFORM)

# pico8shim.so is LD_PRELOAD'ed into pico8_64. It wraps SDL by dlsym and needs no SDL
# headers or libraries; only its wrappers are exported (-fvisibility=hidden). No LTO or
# -Ofast from Makefile.env: the shim is tiny and must not pull in newer libm symbols.
# _FORTIFY_SOURCE would turn open() into an inline wrapper the hook cannot replace.
# shim/state.c builds in the vendored dlmalloc and LZ4 (shim/vendor/) for save states.
SHIM_SRC = shim/shim.c shim/frame.c shim/paths.c shim/clock.c shim/audio.c shim/state.c p8ipc.c p8state.c
SHIM_CFLAGS = -std=gnu99 -shared -fPIC -fvisibility=hidden -O2 -Wall -Wextra -Werror -U_FORTIFY_SOURCE
SHIM = $(BUILD)/pico8shim.so

# pico8native_libretro.so: the libretro core minarch loads; spawns pico8_64 with the shim.
# libretro.h comes from minarch's libretro-common checkout (built before this in workspace/Makefile).
CORE_SRC = core.c core_args.c p8ipc.c p8state.c
CORE_CFLAGS = -std=gnu99 -shared -fPIC -fvisibility=hidden -O2 -Wall -Wextra -Werror -I. -I../minarch/libretro-common/include
CORE = $(BUILD)/pico8native_libretro.so

# The child runs on the toolchain's own SDL2 (it has the offscreen video driver the
# device SDL2 lacks), shipped in P8.pak/lib and put on the child's LD_LIBRARY_PATH only.
SDL_SRC = $(firstword $(wildcard $(PREFIX)/lib/libSDL2-2.0.so.0 $(SYSROOT)/usr/lib/libSDL2-2.0.so.0))
SDL = $(BUILD)/lib/libSDL2-2.0.so.0

all: shim core sdl

shim: $(SHIM)

core: $(CORE)

$(CORE): $(CORE_SRC) core_args.h p8ipc.h p8state.h
	mkdir -p $(BUILD)
	$(CC) $(CORE_CFLAGS) -o $@ $(CORE_SRC) -ldl -lpthread

sdl: $(SDL)

$(SDL):
	@test -n "$(SDL_SRC)" || (echo "no libSDL2-2.0.so.0 in the toolchain sysroot (PREFIX=$(PREFIX))" >&2; exit 1)
	mkdir -p $(BUILD)/lib
	cp -L $(SDL_SRC) $@
	chmod 755 $@

$(SHIM): $(SHIM_SRC) $(wildcard shim/*.h shim/vendor/*) p8ipc.h p8state.h
	mkdir -p $(BUILD)
	$(CC) $(SHIM_CFLAGS) -o $@ $(SHIM_SRC) -ldl -lpthread

# Device-side harness for the shim spike (tests/spike_host.c); not shipped
spike: $(BUILD)/spike_host

$(BUILD)/spike_host: tests/spike_host.c p8ipc.c p8ipc.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -o $@ tests/spike_host.c p8ipc.c -lpthread

# Device-side minimal frontend for the core (tests/core_host.c); not shipped
corehost: $(BUILD)/core_host

$(BUILD)/core_host: tests/core_host.c
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -I../minarch/libretro-common/include -o $@ tests/core_host.c -ldl

tests:
	./tests/run_tests.sh

clean:
	rm -rf build

.PHONY: all shim core sdl spike corehost tests clean
