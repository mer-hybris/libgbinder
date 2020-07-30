# -*- Mode: makefile-gmake -*-

.PHONY: all debug release clean cleaner
.PHONY: libgbinder-release libgbinder-debug

#
# Required packages
#

PKGS = glib-2.0 gio-2.0 gio-unix-2.0 libglibutil

#
# Default target
#

all: debug release

#
# Executable
#

EXE = binder-list

#
# Sources
#

SRC = $(EXE).c

#
# Directories
#

SRC_DIR = .
BUILD_DIR = build
LIB_DIR = ../..
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Tools and flags
#

CC ?= $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall
INCLUDES = -I$(LIB_DIR)/include
BASE_FLAGS = -fPIC
CFLAGS = $(BASE_FLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(BASE_FLAGS) $(shell pkg-config --libs $(PKGS))
QUIET_MAKE = make --no-print-directory
DEBUG_FLAGS = -g
RELEASE_FLAGS =

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
SUBMAKE_OPTS += KEEP_SYMBOLS=1
endif

DEBUG_LDFLAGS = $(LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(LDFLAGS) $(RELEASE_FLAGS)
DEBUG_CFLAGS = $(CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(CFLAGS) $(RELEASE_FLAGS) -O2

#
# Files
#

DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
DEBUG_SO_FILE := $(shell $(QUIET_MAKE) -C $(LIB_DIR) print_debug_so)
RELEASE_SO_FILE := $(shell $(QUIET_MAKE) -C $(LIB_DIR) print_release_so)
DEBUG_LINK_FILE := $(shell $(QUIET_MAKE) -C $(LIB_DIR) print_debug_link)
RELEASE_LINK_FILE := $(shell $(QUIET_MAKE) -C $(LIB_DIR) print_release_link)
DEBUG_SO = $(LIB_DIR)/$(DEBUG_SO_FILE)
RELEASE_SO = $(LIB_DIR)/$(RELEASE_SO_FILE)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

debug: libgbinder-debug $(DEBUG_EXE)

release: libgbinder-release $(RELEASE_EXE)

clean:
	rm -f *~
	rm -fr $(BUILD_DIR)

cleaner: clean
	@make -C $(LIB_DIR) clean

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_SO) $(DEBUG_BUILD_DIR) $(DEBUG_OBJS)
	$(LD) $(DEBUG_OBJS) $(DEBUG_LDFLAGS) $< -o $@

$(RELEASE_EXE): $(RELEASE_SO) $(RELEASE_BUILD_DIR) $(RELEASE_OBJS)
	$(LD) $(RELEASE_OBJS) $(RELEASE_LDFLAGS) $< -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

libgbinder-debug:
	@make $(SUBMAKE_OPTS) -C $(LIB_DIR) $(DEBUG_SO_FILE) $(DEBUG_LINK_FILE)

libgbinder-release:
	@make $(SUBMAKE_OPTS) -C $(LIB_DIR) $(RELEASE_SO_FILE) $(RELEASE_LINK_FILE)

#
# Install
#

INSTALL = install

INSTALL_BIN_DIR = $(DESTDIR)/usr/bin

install: release $(INSTALL_BIN_DIR)
	$(INSTALL) -m 755 $(RELEASE_EXE) $(INSTALL_BIN_DIR)

$(INSTALL_BIN_DIR):
	$(INSTALL) -d $@
