MODULE := engines/dosgames

MODULE_OBJS = \
	cpu8086.o \
	dosgames.o \
	dospc.o \
	metaengine.o

# This module can be built as a plugin
ifeq ($(ENABLE_DOSGAMES), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
