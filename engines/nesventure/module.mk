MODULE := engines/nesventure

MODULE_OBJS = \
	cpu6502.o \
	nesui.o \
	metaengine.o \
	nes.o \
	nes_apu.o \
	nesventure.o

# This module can be built as a plugin
ifeq ($(ENABLE_NESVENTURE), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
