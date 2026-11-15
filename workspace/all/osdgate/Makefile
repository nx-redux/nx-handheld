###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

include ../../$(PLATFORM)/platform/Makefile.env

###########################################################

# LD_PRELOAD shim that gates SDL2 input events behind /tmp/trimui_osd/osdd_show_up
# for standalone SDL2 apps (ffplay, NextCommander) that do not use PAD_poll().
TARGET = osdgate
PRODUCT = build/$(PLATFORM)/lib$(TARGET).so

CC = $(CROSS_COMPILE)gcc

all:
	mkdir -p build/$(PLATFORM)
	$(CC) -shared -fpic -Os -Wall -Werror -std=gnu99 $(TARGET).c -o $(PRODUCT) -ldl -s

clean:
	rm -f $(PRODUCT)
