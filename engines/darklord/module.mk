MODULE := engines/darklord

MODULE_OBJS = \
	cpu6502.o \
	darklord.o \
	machine.o \
	metaengine.o

# This module can be built as a plugin
ifeq ($(ENABLE_DARKLORD), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
