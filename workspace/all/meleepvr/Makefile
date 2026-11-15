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

# LD_PRELOAD shim that binds Dawn's SwapChainEGL directly to SDL2's window
# EGLSurface when MELEE_FLIP_PRESENT_THREAD=0 on PowerVR Rogue GE8300.
TARGET = meleepvr
PRODUCT = build/$(PLATFORM)/lib$(TARGET).so

CC = $(CROSS_COMPILE)gcc

all:
	mkdir -p build/$(PLATFORM)
	$(CC) -shared -fpic -Os -Wall -Werror -std=gnu99 $(TARGET).c -o $(PRODUCT) -ldl -lpthread -s

clean:
	rm -f $(PRODUCT)
