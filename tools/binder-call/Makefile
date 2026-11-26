# -*- Mode: makefile-gmake -*-

.PHONY: all debug release install clean cleaner
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

EXE = binder-call

#
# Sources
#

SRC = $(EXE).c
GEN_SRC = \
  cmdline.tab.c \
  lex.cmdline.c

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

PKG_CONFIG ?= pkg-config
CC ?= $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall
DEFINES += -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_32 \
  -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_MAX_ALLOWED
INCLUDES = -I$(LIB_DIR)/include -I$(SRC_DIR)
BASE_FLAGS = -fPIC -g
BASE_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS)
BASE_CFLAGS = $(BASE_FLAGS) $(CFLAGS)
FULL_CFLAGS = $(BASE_CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) -MMD -MP \
  $(shell $(PKG_CONFIG) --cflags $(PKGS))
FULL_LDFLAGS = $(BASE_LDFLAGS)
LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))

KEEP_SYMBOLS ?= 0

DEBUG_CFLAGS = $(FULL_CFLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) -O2

DEBUG_LIB = $(LIB_DIR)/build/debug/libgbinder.so
RELEASE_LIB = $(LIB_DIR)/build/release/libgbinder.so

DEBUG_LIBS += $(DEBUG_LIB) $(LIBS)
RELEASE_LIBS += $(RELEASE_LIB) $(LIBS)

DEBUG_LDFLAGS = $(FULL_LDFLAGS)
RELEASE_LDFLAGS = $(FULL_LDFLAGS)

#
# Files
#

DEBUG_OBJS = \
  $(GEN_SRC:%.c=$(DEBUG_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(GEN_SRC:%.c=$(RELEASE_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

DEBUG_GEN_FILES = $(GEN_SRC:%=$(DEBUG_BUILD_DIR)/%)
RELEASE_GEN_FILES = $(GEN_SRC:%=$(RELEASE_BUILD_DIR)/%)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_LIB): | libgbinder-debug
$(RELEASE_LIB): | libgbinder-release

$(DEBUG_GEN_FILES) $(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_GEN_FILES) $(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

debug: $(DEBUG_EXE)

release: $(RELEASE_EXE)

clean:
	rm -f *~
	rm -fr $(BUILD_DIR)

cleaner: clean
	@$(MAKE) -C $(LIB_DIR) clean

$(DEBUG_BUILD_DIR) $(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.tab.c: $(SRC_DIR)/%.y
	bison -pcmdline -bcmdline -d -o $@ $<

$(RELEASE_BUILD_DIR)/%.tab.c: $(SRC_DIR)/%.y
	bison -pcmdline -bcmdline -d -o $@ $<

$(DEBUG_BUILD_DIR)/lex.%.c: $(SRC_DIR)/%.l $(DEBUG_BUILD_DIR)/%.tab.c
	flex -o $@ $<

$(RELEASE_BUILD_DIR)/lex.%.c: $(SRC_DIR)/%.l $(RELEASE_BUILD_DIR)/%.tab.c
	flex -o $@ $<

$(DEBUG_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o: $(DEBUG_BUILD_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(RELEASE_BUILD_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_LIB) $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_LIB) $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

libgbinder-debug:
	@$(MAKE) -C $(LIB_DIR) debug

libgbinder-release:
	@$(MAKE) -C $(LIB_DIR) release

#
# Install
#

INSTALL = install

INSTALL_BIN_DIR = $(DESTDIR)/usr/bin

install: release $(INSTALL_BIN_DIR)
	$(INSTALL) -m 755 $(RELEASE_EXE) $(INSTALL_BIN_DIR)

$(INSTALL_BIN_DIR):
	$(INSTALL) -d $@
