#
# nvidia-installer: A tool for installing NVIDIA software packages on
# Unix and Linux systems.
#
# Copyright (C) 2003 NVIDIA Corporation
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the:
#
#      Free Software Foundation, Inc.
#      59 Temple Place - Suite 330
#      Boston, MA 02111-1307, USA
#
#
# Makefile
#

# default definitions; can be overwridden by users
ifndef CC
  CC = gcc
endif

ifndef LD
  LD = ld
endif

ifndef HOST_CC
  HOST_CC = $(CC)
endif

ifndef HOST_LD
  HOST_LD = $(LD)
endif


ifndef CFLAGS
  CFLAGS = -g -O -Wall
endif

SHELL = /bin/sh
INSTALL = install -m 755

ifeq ($(NVDEBUG),1)
  STRIP = true
else
  ifndef STRIP
    STRIP = strip
  endif
endif


# default prefix
ifdef ROOT
  prefix = $(ROOT)/usr
else
  prefix = /usr/local
endif

exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin

# Can be overwitten by users for cross-compiling
# get the os and architecture
ifndef INSTALLER_OS
  INSTALLER_OS := $(shell uname)
endif

ifndef INSTALLER_ARCH
  INSTALLER_ARCH := $(shell uname -m)
endif

# cook the architecture
INSTALLER_ARCH := $(subst i386,x86,$(INSTALLER_ARCH))
INSTALLER_ARCH := $(subst i486,x86,$(INSTALLER_ARCH))
INSTALLER_ARCH := $(subst i586,x86,$(INSTALLER_ARCH))
INSTALLER_ARCH := $(subst i686,x86,$(INSTALLER_ARCH))


NVIDIA_INSTALLER = nvidia-installer
MKPRECOMPILED = mkprecompiled

NVIDIA_INSTALLER_PROGRAM_NAME = "nvidia-installer"
NVIDIA_INSTALLER_VERSION = "1.0.7"

NCURSES_UI = nvidia-installer-ncurses-ui.so
NCURSES_UI_C = g_$(NCURSES_UI:.so=.c)

TLS_TEST_C = g_tls_test.c
TLS_TEST_DSO_C = g_tls_test_dso.c
TLS_TEST = tls_test_$(INSTALLER_OS)-$(INSTALLER_ARCH)
TLS_TEST_DSO_SO = tls_test_dso_$(INSTALLER_OS)-$(INSTALLER_ARCH).so

TLS_TEST_32_C = g_tls_test_32.c
TLS_TEST_DSO_32_C = g_tls_test_dso_32.c
TLS_TEST_32 = tls_test_Linux-x86
TLS_TEST_DSO_SO_32 = tls_test_dso_Linux-x86.so

GEN_UI_ARRAY = ./gen-ui-array
CONFIG_H = config.h
STAMP_C = g_stamp.c

# Setup some architecture specific build options
ifeq ($(INSTALLER_OS)-$(INSTALLER_ARCH), Linux-x86_64)
TLS_MODEL=initial-exec
PIC=-fPIC
CFLAGS += -DNV_X86_64
# Only Linux-x86_64 needs the tls_test_32 files
COMPAT_32_SRC = $(TLS_TEST_32_C) $(TLS_TEST_DSO_32_C)
else
# So far all other platforms use local-exec
TLS_MODEL=local-exec
PIC=
# Non-Linux-x86_64 platforms do not include the tls_test_32 files
COMPAT_32_SRC = 
endif

SRC =	backup.c           \
	command-list.c     \
	crc.c              \
	files.c            \
	format.c           \
	install-from-cwd.c \
	kernel.c           \
	log.c              \
	misc.c             \
	nvidia-installer.c \
	precompiled.c      \
	snarf-ftp.c        \
	snarf-http.c       \
	snarf.c            \
	stream-ui.c        \
	update.c           \
	user-interface.c   \
	sanity.c

ALL_SRC = $(SRC) $(NCURSES_UI_C) $(TLS_TEST_C) $(TLS_TEST_DSO_C) \
	$(COMPAT_32_SRC) $(STAMP_C)

OBJS = $(ALL_SRC:.c=.o)

ALL_CFLAGS = -I. $(CFLAGS) -imacros $(CONFIG_H)
ALL_LDFLAGS = -ldl $(LDFLAGS)

MKPRECOMPILED_SRC = crc.c mkprecompiled.c
MKPRECOMPILED_OBJS = $(MKPRECOMPILED_SRC:.c=.o)

# and now, the build rules:

default: all

all: $(NVIDIA_INSTALLER) $(MKPRECOMPILED)

install: NVIDIA_INSTALLER_install MKPRECOMPILED_install

NVIDIA_INSTALLER_install: $(NVIDIA_INSTALLER)
	$(STRIP) $<
	$(INSTALL) $< $(bindir)/$<

MKPRECOMPILED_install: $(MKPRECOMPILED)
	$(INSTALL) $< $(bindir)/$<

$(MKPRECOMPILED): $(CONFIG_H) $(MKPRECOMPILED_OBJS)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) $(MKPRECOMPILED_OBJS) -o $@

$(NVIDIA_INSTALLER): $(CONFIG_H) $(OBJS)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) $(OBJS) -o $@

$(NCURSES_UI_C): $(GEN_UI_ARRAY) $(NCURSES_UI)
	$(GEN_UI_ARRAY) $(NCURSES_UI) ncurses_ui_array > $@

$(GEN_UI_ARRAY): gen-ui-array.c $(CONFIG_H)
	$(HOST_CC) $(ALL_CFLAGS) $< -o $@

$(NCURSES_UI): ncurses-ui.o
	$(CC) -o $@ -shared ncurses-ui.o -lncurses

$(TLS_TEST_C): $(GEN_UI_ARRAY) $(TLS_TEST)
	$(GEN_UI_ARRAY) $(TLS_TEST) tls_test_array > $@

$(TLS_TEST_DSO_C): $(GEN_UI_ARRAY) $(TLS_TEST_DSO_SO)
	$(GEN_UI_ARRAY) $(TLS_TEST_DSO_SO) tls_test_dso_array > $@

$(TLS_TEST_32_C): $(GEN_UI_ARRAY) $(TLS_TEST_32)
	$(GEN_UI_ARRAY) $(TLS_TEST_32) tls_test_array_32 > $@

$(TLS_TEST_DSO_32_C): $(GEN_UI_ARRAY) $(TLS_TEST_DSO_SO_32)
	$(GEN_UI_ARRAY) $(TLS_TEST_DSO_SO_32) tls_test_dso_array_32 > $@

ncurses-ui.o: ncurses-ui.c $(CONFIG_H)
	$(CC) -c $(ALL_CFLAGS) $< -fPIC -o $@

%.o: %.c $(CONFIG_H)
	$(CC) -c $(ALL_CFLAGS) $< -o $@

%.d: %.c
	@set -e; $(CC) -MM $(CPPFLAGS) $< \
		| sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
		[ -s $@ ] || rm -f $@

$(CONFIG_H):
	@ rm -f $@
	@ echo    "#define INSTALLER_OS \"$(INSTALLER_OS)\"" >> $@
	@ echo    "#define INSTALLER_ARCH \"$(INSTALLER_ARCH)\"" >> $@
	@ echo -n "#define NVIDIA_INSTALLER_VERSION " >> $@
	@ echo    "\"$(NVIDIA_INSTALLER_VERSION)\"" >> $@
	@ echo -n "#define PROGRAM_NAME " >> $@
	@ echo    "\"$(NVIDIA_INSTALLER_PROGRAM_NAME)\"" >> $@

$(STAMP_C): $(filter-out $(STAMP_C:.c=.o), $(OBJS))
	@ rm -f $@
	@ echo -n "const char NV_ID[] = \"nvidia id: " >> $@
	@ echo -n "$(NVIDIA_INSTALLER_PROGRAM_NAME):  " >> $@
	@ echo -n "version $(NVIDIA_INSTALLER_VERSION)  " >> $@
	@ echo -n "($(shell whoami)@$(shell hostname))  " >> $@
	@ echo    "$(shell date)\";" >> $@
	@ echo    "const char *pNV_ID = NV_ID + 11;" >> $@

clean clobber:
	rm -rf $(NVIDIA_INSTALLER) $(MKPRECOMPILED) \
		$(NCURSES_UI) $(NCURSES_UI_C) \
		$(TLS_TEST_C) $(TLS_TEST_DSO_C) $(COMPAT_32_SRC) \
		$(GEN_UI_ARRAY) $(CONFIG_H) $(STAMP_C) *.o *~ *.d

# rule to rebuild tls_test and tls_test_dso; a precompiled tls_test
# and tls_test_dso is distributed with nvidia_installer because they
# require a recent toolchain to build.

rebuild_tls_test: tls_test.c
	gcc -Wall -O2 -fomit-frame-pointer -o $(TLS_TEST) -ldl $<
	strip $(TLS_TEST)

rebuild_tls_test_dso: tls_test_dso.c
	gcc -Wall -O2 $(PIC) -fomit-frame-pointer -c $< -ftls-model=$(TLS_MODEL)
	gcc -o $(TLS_TEST_DSO_SO) -shared tls_test_dso.o
	strip $(TLS_TEST_DSO_SO)

# dummy rule to override implicit rule that builds tls_test from
# tls_test.c

tls_test: tls_test.c
	touch $@

print_version:
	@ echo $(NVIDIA_INSTALLER_VERSION)

-include $(SRC:.c=.d)
