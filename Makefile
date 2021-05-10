# -*- Mode: makefile-gmake -*-
#
# LIBGLIBUTIL_PATH can be defined to point to libglibutil root directory
# for side-by-side build.
#

.PHONY: clean all debug release test
.PHONY: print_debug_so print_release_so
.PHONY: print_debug_lib print_release_lib print_coverage_lib
.PHONY: print_debug_link print_release_link
.PHONY: print_debug_path print_release_path

#
# Library version
#

VERSION_MAJOR = 1
VERSION_MINOR = 1
VERSION_RELEASE = 10

# Version for pkg-config
PCVERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Default target
#

all: debug release pkgconfig

#
# Required packages
#

PKGS = glib-2.0 gobject-2.0

ifeq ($(LIBGLIBUTIL_PATH),)

# Assume that libglibutil devel package is installed
PKGS += libglibutil

else

# Side-by-side build
INCLUDES += -I$(LIBGLIBUTIL_PATH)/include
DEBUG_LIBS = -L$(LIBGLIBUTIL_PATH)/build/debug -lglibutil
RELEASE_LIBS = -L$(LIBGLIBUTIL_PATH)/build/release -lglibutil
DEBUG_DEPS = libglibutil_debug
RELEASE_DEPS = libglibutil_release

.PHONY: libglibutil_debug libglibutil_release

libglibutil_debug:
	make -C $(LIBGLIBUTIL_PATH) debug

libglibutil_release:
	make -C $(LIBGLIBUTIL_PATH) release

endif

#
# Library name
#

NAME = gbinder
LIB_NAME = lib$(NAME)
LIB_DEV_SYMLINK = $(LIB_NAME).so
LIB_SYMLINK1 = $(LIB_DEV_SYMLINK).$(VERSION_MAJOR)
LIB_SYMLINK2 = $(LIB_SYMLINK1).$(VERSION_MINOR)
LIB_SONAME = $(LIB_SYMLINK1)
LIB_SO = $(LIB_SONAME).$(VERSION_MINOR).$(VERSION_RELEASE)
LIB = $(LIB_NAME).a

#
# Sources
#

SRC = \
  gbinder_bridge.c \
  gbinder_buffer.c \
  gbinder_cleanup.c \
  gbinder_client.c \
  gbinder_config.c \
  gbinder_driver.c \
  gbinder_eventloop.c \
  gbinder_io_32.c \
  gbinder_io_64.c \
  gbinder_ipc.c \
  gbinder_local_object.c \
  gbinder_local_reply.c \
  gbinder_local_request.c \
  gbinder_log.c \
  gbinder_proxy_object.c \
  gbinder_reader.c \
  gbinder_remote_object.c \
  gbinder_remote_reply.c \
  gbinder_remote_request.c \
  gbinder_rpc_protocol.c \
  gbinder_servicename.c \
  gbinder_servicepoll.c \
  gbinder_writer.c

SRC += \
  gbinder_servicemanager.c \
  gbinder_servicemanager_aidl.c \
  gbinder_servicemanager_aidl2.c \
  gbinder_servicemanager_hidl.c

SRC += \
  gbinder_system.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release
COVERAGE_BUILD_DIR = $(BUILD_DIR)/coverage

#
# Tools and flags
#

CC ?= $(CROSS_COMPILE)gcc
STRIP ?= strip
LD = $(CC)
WARNINGS = -Wall -Wstrict-aliasing -Wunused-result
INCLUDES += -I$(INCLUDE_DIR)
BASE_FLAGS = -fPIC
FULL_CFLAGS = $(BASE_FLAGS) $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS) -shared -Wl,-soname,$(LIB_SONAME) \
  $(shell pkg-config --libs $(PKGS)) -lpthread
DEBUG_FLAGS = -g
RELEASE_FLAGS =
COVERAGE_FLAGS = -g

KEEP_SYMBOLS ?= 0
ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_LDFLAGS = $(FULL_LDFLAGS) $(DEBUG_LIBS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(FULL_LDFLAGS) $(RELEASE_LIBS) $(RELEASE_FLAGS)
DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
COVERAGE_CFLAGS = $(FULL_CFLAGS) $(COVERAGE_FLAGS) --coverage

#
# Files
#

PKGCONFIG = $(BUILD_DIR)/$(LIB_NAME).pc
DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
COVERAGE_OBJS = $(SRC:%.c=$(COVERAGE_BUILD_DIR)/%.o)

DEBUG_SO = $(DEBUG_BUILD_DIR)/$(LIB_SO)
RELEASE_SO = $(RELEASE_BUILD_DIR)/$(LIB_SO)
DEBUG_LINK = $(DEBUG_BUILD_DIR)/$(LIB_SYMLINK1)
RELEASE_LINK = $(RELEASE_BUILD_DIR)/$(LIB_SYMLINK1)
DEBUG_DEV_LINK = $(DEBUG_BUILD_DIR)/$(LIB_DEV_SYMLINK)
RELEASE_DEV_LINK = $(RELEASE_BUILD_DIR)/$(LIB_DEV_SYMLINK)
DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)
COVERAGE_LIB = $(COVERAGE_BUILD_DIR)/$(LIB)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(PKGCONFIG): | $(BUILD_DIR)
$(DEBUG_OBJS) $(DEBUG_SO): | $(DEBUG_BUILD_DIR) $(DEBUG_DEPS)
$(RELEASE_OBJS) $(RELEASE_SO): | $(RELEASE_BUILD_DIR) $(RELEASE_DEPS)
$(COVERAGE_OBJS) $(COVERAGE_LIB): | $(COVERAGE_BUILD_DIR)

$(DEBUG_LINK): | $(DEBUG_LIB)
$(RELEASE_LINK): | $(RELEASE_LIB)
$(DEBUG_DEV_LINK): | $(DEBUG_LINK)
$(RELEASE_DEV_LINK): | $(RELEASE_LINK)

#
# Rules
#

debug: $(DEBUG_SO) $(DEBUG_LINK) $(DEBUG_DEV_LINK)

release: $(RELEASE_SO) $(RELEASE_LINK) $(RELEASE_DEV_LINK)

debug_lib: $(DEBUG_LIB)

release_lib: $(RELEASE_LIB)

coverage_lib: $(COVERAGE_LIB)

pkgconfig: $(PKGCONFIG)

print_debug_so:
	@echo $(DEBUG_SO)

print_release_so:
	@echo $(RELEASE_SO)

print_debug_lib:
	@echo $(DEBUG_LIB)

print_release_lib:
	@echo $(RELEASE_LIB)

print_coverage_lib:
	@echo $(COVERAGE_LIB)

print_debug_link:
	@echo $(DEBUG_LINK)

print_release_link:
	@echo $(RELEASE_LINK)

print_debug_path:
	@echo $(DEBUG_BUILD_DIR)

print_release_path:
	@echo $(RELEASE_BUILD_DIR)

clean:
	make -C test clean
	make -C unit clean
	rm -fr test/coverage/results test/coverage/*.gcov
	rm -f *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~
	rm -fr $(BUILD_DIR) RPMS installroot
	rm -fr debian/tmp debian/libgbinder debian/libgbinder-dev
	rm -f documentation.list debian/files debian/*.substvars
	rm -f debian/*.debhelper.log debian/*.debhelper debian/*~
	rm -f debian/libgbinder.install debian/libgbinder-dev.install

test:
	make -C unit test

$(BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(COVERAGE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(COVERAGE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(COVERAGE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_SO): $(DEBUG_OBJS)
	$(LD) $(DEBUG_OBJS) $(DEBUG_LDFLAGS) -o $@

$(RELEASE_SO): $(RELEASE_OBJS)
	$(LD) $(RELEASE_OBJS) $(RELEASE_LDFLAGS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	$(STRIP) $@
endif

$(DEBUG_LIB): $(DEBUG_OBJS)
	$(AR) rc $@ $?
	ranlib $@

$(RELEASE_LIB): $(RELEASE_OBJS)
	$(AR) rc $@ $?
	ranlib $@

$(DEBUG_LINK):
	ln -sf $(LIB_SO) $@

$(RELEASE_LINK):
	ln -sf $(LIB_SO) $@

$(DEBUG_DEV_LINK):
	ln -sf $(LIB_SYMLINK1) $@

$(RELEASE_DEV_LINK):
	ln -sf $(LIB_SYMLINK1) $@

$(COVERAGE_LIB): $(COVERAGE_OBJS)
	$(AR) rc $@ $?
	ranlib $@

#
# LIBDIR usually gets substituted with arch specific dir.
# It's relative in deb build and can be whatever in rpm build.
#

LIBDIR ?= usr/lib
ABS_LIBDIR := $(shell echo /$(LIBDIR) | sed -r 's|/+|/|g')

$(PKGCONFIG): $(LIB_NAME).pc.in Makefile
	sed -e 's|@version@|$(PCVERSION)|g' -e 's|@libdir@|$(ABS_LIBDIR)|g' $< > $@

debian/%.install: debian/%.install.in
	sed 's|@LIBDIR@|$(LIBDIR)|g' $< > $@

#
# Install
#

INSTALL_PERM  = 644

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d
INSTALL_FILES = $(INSTALL) -m $(INSTALL_PERM)

INSTALL_LIB_DIR = $(DESTDIR)$(ABS_LIBDIR)
INSTALL_INCLUDE_DIR = $(DESTDIR)/usr/include/$(NAME)
INSTALL_PKGCONFIG_DIR = $(DESTDIR)$(ABS_LIBDIR)/pkgconfig

install: $(INSTALL_LIB_DIR)
	$(INSTALL) -m 755 $(RELEASE_SO) $(INSTALL_LIB_DIR)
	ln -sf $(LIB_SO) $(INSTALL_LIB_DIR)/$(LIB_SYMLINK2)
	ln -sf $(LIB_SYMLINK2) $(INSTALL_LIB_DIR)/$(LIB_SYMLINK1)

install-dev: install $(INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR)
	$(INSTALL_FILES) $(INCLUDE_DIR)/*.h $(INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)
	ln -sf $(LIB_SYMLINK1) $(INSTALL_LIB_DIR)/$(LIB_DEV_SYMLINK)

$(INSTALL_LIB_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PKGCONFIG_DIR):
	$(INSTALL_DIRS) $@
