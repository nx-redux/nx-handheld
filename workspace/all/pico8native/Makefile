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
SHIM_SRC = shim/shim.c shim/frame.c shim/paths.c shim/clock.c shim/audio.c p8ipc.c
SHIM_CFLAGS = -std=gnu99 -shared -fPIC -fvisibility=hidden -O2 -Wall -Wextra -Werror -U_FORTIFY_SOURCE
SHIM = $(BUILD)/pico8shim.so

all: shim

shim: $(SHIM)

$(SHIM): $(SHIM_SRC) $(wildcard shim/*.h) p8ipc.h
	mkdir -p $(BUILD)
	$(CC) $(SHIM_CFLAGS) -o $@ $(SHIM_SRC) -ldl -lpthread

# Device-side harness for the shim spike (tests/spike_host.c); not shipped
spike: $(BUILD)/spike_host

$(BUILD)/spike_host: tests/spike_host.c p8ipc.c p8ipc.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -o $@ tests/spike_host.c p8ipc.c -lpthread

tests:
	./tests/run_tests.sh

clean:
	rm -rf build

.PHONY: all shim spike tests clean
