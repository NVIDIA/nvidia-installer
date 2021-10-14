#
# nvidia-installer: A tool for installing NVIDIA software packages on
# Unix and Linux systems.
#
# Copyright (C) 2008 NVIDIA Corporation
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses>.
#
#
# Makefile
#


##############################################################################
# include common variables and functions
##############################################################################

include utils.mk


##############################################################################
# The calling Makefile may export any of the following variables; we
# assign default values if they are not exported by the caller
##############################################################################


NCURSES_CFLAGS        ?=
NCURSES_LDFLAGS       ?=
NCURSES6_CFLAGS       ?=
NCURSES6_LDFLAGS      ?=
NCURSESW6_CFLAGS      ?=
NCURSESW6_LDFLAGS     ?=
PCIACCESS_CFLAGS      ?=
PCIACCESS_LDFLAGS     ?=

BUILD_NCURSES6 = $(if $(NCURSES6_CFLAGS)$(NCURSES6_LDFLAGS),1,)
BUILD_NCURSESW6 = $(if $(NCURSESW6_CFLAGS)$(NCURSESW6_LDFLAGS),1,)

##############################################################################
# assign variables
##############################################################################

NVIDIA_INSTALLER = $(OUTPUTDIR)/nvidia-installer
MKPRECOMPILED = $(OUTPUTDIR)/mkprecompiled
MAKESELF_HELP_SCRIPT = $(OUTPUTDIR)/makeself-help-script
MAKESELF_HELP_SCRIPT_SH = $(OUTPUTDIR)/makeself-help-script.sh

NVIDIA_INSTALLER_PROGRAM_NAME = "nvidia-installer"

NVIDIA_INSTALLER_VERSION := $(NVIDIA_VERSION)

NCURSES_UI_C       = ncurses-ui.c
NCURSES_UI_O       = $(call BUILD_OBJECT_LIST,$(NCURSES_UI_C))
NCURSES_UI_SO      = $(OUTPUTDIR)/nvidia-installer-ncurses-ui.so
NCURSES_UI_SO_C    = $(OUTPUTDIR)/g_$(notdir $(NCURSES_UI_SO:.so=.c))
NCURSES6_UI_O      = $(OUTPUTDIR)/ncurses6-ui.o
NCURSES6_UI_SO     = $(OUTPUTDIR)/nvidia-installer-ncurses6-ui.so
NCURSES6_UI_SO_C   = $(OUTPUTDIR)/g_$(notdir $(NCURSES6_UI_SO:.so=.c))
NCURSESW6_UI_O     = $(OUTPUTDIR)/ncursesw6-ui.o
NCURSESW6_UI_SO    = $(OUTPUTDIR)/nvidia-installer-ncursesw6-ui.so
NCURSESW6_UI_SO_C  = $(OUTPUTDIR)/g_$(notdir $(NCURSESW6_UI_SO:.so=.c))

RTLD_TEST_C        = $(OUTPUTDIR)/g_rtld_test.c
RTLD_TEST          = $(OUTPUTDIR)/rtld_test

# Use a precompiled rtld_test-Linux-x86 binary to simplify the Linux-x86_64
# build. If a fresh rtld_test-Linux-x86 binary is needed, it can be copied
# from a Linux-x86 build of nvidia-settings.

RTLD_TEST_32_C     = $(OUTPUTDIR)/g_rtld_test_32.c
RTLD_TEST_32       = rtld_test_$(TARGET_OS)-x86

GEN_UI_ARRAY       = $(OUTPUTDIR)/gen-ui-array
CONFIG_H           = $(OUTPUTDIR)/config.h

MANPAGE            = $(OUTPUTDIR)/nvidia-installer.1.gz
GEN_MANPAGE_OPTS   = $(OUTPUTDIR_ABSOLUTE)/gen-manpage-opts
OPTIONS_1_INC      = $(OUTPUTDIR)/options.1.inc

# Setup some architecture specific build options
ifeq ($(TARGET_OS)-$(TARGET_ARCH), Linux-x86_64)
  TLS_MODEL = initial-exec
  PIC = -fPIC
  # Only Linux-x86_64 needs the rtld_test_32 file
  COMPAT_32_SRC = $(RTLD_TEST_32_C)
else
  # So far all other platforms use local-exec
  TLS_MODEL = local-exec
  PIC =
  # Non-Linux-x86_64 platforms do not include the rtld_test_32 file
  COMPAT_32_SRC =
endif

BULLSEYE_BUILD ?= 0

##############################################################################
# The common-utils directory may be in one of two places: either
# elsewhere in the driver source tree when building nvidia-installer
# as part of the NVIDIA driver build (in which case, COMMON_UTILS_DIR
# should be defined by the calling makefile), or directly in the
# source directory when building from the nvidia-installer source
# tarball (in which case, the below conditional assignments should be
# used)
##############################################################################

COMMON_UTILS_DIR          ?= common-utils

# include the list of source files; defines SRC
include dist-files.mk

include $(COMMON_UTILS_DIR)/src.mk
SRC += $(addprefix $(COMMON_UTILS_DIR)/,$(COMMON_UTILS_SRC))

NCURSES_UI_SO_SRC = $(NCURSES_UI_SO_C)

NCURSES_UI_SO_SRC += $(if $(BUILD_NCURSES6),$(NCURSES6_UI_SO_C),)
NCURSES_UI_SO_SRC += $(if $(BUILD_NCURSESW6),$(NCURSESW6_UI_SO_C),)
CFLAGS += $(if $(BUILD_NCURSES6),-DNV_INSTALLER_NCURSES6,)
CFLAGS += $(if $(BUILD_NCURSESW6),-DNV_INSTALLER_NCURSESW6,)

INSTALLER_SRC = $(SRC) $(NCURSES_UI_SO_SRC) $(RTLD_TEST_C) $(COMPAT_32_SRC)

INSTALLER_OBJS = $(call BUILD_OBJECT_LIST,$(INSTALLER_SRC))

common_cflags  = -I.
common_cflags += -imacros $(CONFIG_H)
common_cflags += -I $(OUTPUTDIR)
common_cflags += -I $(COMMON_UTILS_DIR)

common_cflags += $(if $(BULLSEYE_BUILD),-DBULLSEYE_BUILD,)

CFLAGS += $(common_cflags)

HOST_CFLAGS += $(common_cflags)

LDFLAGS += -L.
LIBS += -ldl

MKPRECOMPILED_SRC = crc.c mkprecompiled.c $(COMMON_UTILS_DIR)/common-utils.c \
                    precompiled.c $(COMMON_UTILS_DIR)/nvgetopt.c
MKPRECOMPILED_OBJS = $(call BUILD_OBJECT_LIST,$(MKPRECOMPILED_SRC))

MAKESELF_HELP_SCRIPT_SRC  = makeself-help-script.c
MAKESELF_HELP_SCRIPT_SRC += $(COMMON_UTILS_DIR)/common-utils.c
MAKESELF_HELP_SCRIPT_SRC += $(COMMON_UTILS_DIR)/nvgetopt.c
MAKESELF_HELP_SCRIPT_SRC += $(COMMON_UTILS_DIR)/msg.c

BUILD_MAKESELF_OBJECT_LIST = \
  $(patsubst %.o,%.makeself.o,$(call BUILD_OBJECT_LIST,$(1)))

MAKESELF_HELP_SCRIPT_OBJS = \
  $(call BUILD_MAKESELF_OBJECT_LIST,$(MAKESELF_HELP_SCRIPT_SRC))

ALL_SRC = $(sort $(INSTALLER_SRC) $(NCURSES_UI_C) $(MKPRECOMPILED_SRC))

# define a quiet rule for GEN-UI-ARRAY
quiet_GEN_UI_ARRAY = GEN-UI-ARRAY $@


##############################################################################
# build rules
##############################################################################

.PNONY: all install NVIDIA_INSTALLER_install MKPRECOMPILED_install \
  MANPAGE_install MAKESELF_HELP_SCRIPT_install clean clobber

all: $(NVIDIA_INSTALLER) $(MKPRECOMPILED) $(MAKESELF_HELP_SCRIPT) \
  $(MAKESELF_HELP_SCRIPT_SH) $(MANPAGE)

install: NVIDIA_INSTALLER_install MKPRECOMPILED_install MANPAGE_install \
  MAKESELF_HELP_SCRIPT_install

NVIDIA_INSTALLER_install: $(NVIDIA_INSTALLER)
	$(MKDIR) $(BINDIR)
	$(INSTALL) $(INSTALL_BIN_ARGS) $< $(BINDIR)/$(notdir $<)

MKPRECOMPILED_install: $(MKPRECOMPILED)
	$(MKDIR) $(BINDIR)
	$(INSTALL) $(INSTALL_BIN_ARGS) $< $(BINDIR)/$(notdir $<)

MAKESELF_HELP_SCRIPT_install: $(MAKESELF_HELP_SCRIPT)
	$(MKDIR) $(BINDIR)
	$(INSTALL) $(INSTALL_BIN_ARGS) $< $(BINDIR)/$(notdir $<)

MANPAGE_install: $(MANPAGE)
	$(MKDIR) $(MANDIR)
	$(INSTALL) $(INSTALL_DOC_ARGS) $< $(MANDIR)/$(notdir $<)

$(eval $(call DEBUG_INFO_RULES, $(MKPRECOMPILED)))
$(MKPRECOMPILED).unstripped: $(MKPRECOMPILED_OBJS)
	$(call quiet_cmd,LINK) $(CFLAGS) $(LDFLAGS) $(BIN_LDFLAGS) \
	  $(MKPRECOMPILED_OBJS) -o $@ $(LIBS)

$(MAKESELF_HELP_SCRIPT): $(MAKESELF_HELP_SCRIPT_OBJS)
	$(call quiet_cmd,HOST_LINK) $(HOST_CFLAGS) $(HOST_LDFLAGS) \
	  $(HOST_BIN_LDFLAGS) $(MAKESELF_HELP_SCRIPT_OBJS) -o $@

$(eval $(call DEBUG_INFO_RULES, $(NVIDIA_INSTALLER)))
$(NVIDIA_INSTALLER).unstripped: $(INSTALLER_OBJS)
	$(call quiet_cmd,LINK) $(CFLAGS) $(LDFLAGS) $(PCIACCESS_LDFLAGS) \
	  $(BIN_LDFLAGS) $(INSTALLER_OBJS) -o $@ \
	  $(LIBS) -Bstatic -lpciaccess -Bdynamic

$(GEN_UI_ARRAY): gen-ui-array.c $(CONFIG_H)
	$(call quiet_cmd,HOST_CC) $(HOST_CFLAGS) $(HOST_LDFLAGS) \
	  $(HOST_BIN_LDFLAGS) $< -o $@

$(NCURSES_UI_SO): $(NCURSES_UI_O)
	$(call quiet_cmd,LINK) -shared $(NCURSES_LDFLAGS) \
	  $(CFLAGS) $(LDFLAGS) $(BIN_LDFLAGS) $< -o $@ -lncurses $(LIBS)

$(NCURSES6_UI_SO): $(NCURSES6_UI_O)
	$(call quiet_cmd,LINK) -shared $(NCURSES6_LDFLAGS) \
	  $(CFLAGS) $(LDFLAGS) $(BIN_LDFLAGS) $< -o $@ -lncurses $(LIBS)

$(NCURSESW6_UI_SO): $(NCURSESW6_UI_O)
	$(call quiet_cmd,LINK) -shared $(NCURSESW6_LDFLAGS) \
	  $(CFLAGS) $(LDFLAGS) $(BIN_LDFLAGS) $< -o $@ -lncursesw $(LIBS)

$(NCURSES_UI_SO_C): $(GEN_UI_ARRAY) $(NCURSES_UI_SO)
	$(call quiet_cmd,GEN_UI_ARRAY) $(NCURSES_UI_SO) ncurses_ui_array > $@

$(NCURSES6_UI_SO_C): $(GEN_UI_ARRAY) $(NCURSES6_UI_SO)
	$(call quiet_cmd,GEN_UI_ARRAY) $(NCURSES6_UI_SO) ncurses6_ui_array > $@

$(NCURSESW6_UI_SO_C): $(GEN_UI_ARRAY) $(NCURSESW6_UI_SO)
	$(call quiet_cmd,GEN_UI_ARRAY) $(NCURSESW6_UI_SO) ncursesw6_ui_array > $@

$(RTLD_TEST_C): $(GEN_UI_ARRAY) $(RTLD_TEST)
	$(call quiet_cmd,GEN_UI_ARRAY) $(RTLD_TEST) rtld_test_array > $@

$(RTLD_TEST_32_C): $(GEN_UI_ARRAY) $(RTLD_TEST_32)
	$(call quiet_cmd,GEN_UI_ARRAY) $(RTLD_TEST_32) rtld_test_array_32 > $@

# misc.c includes pciaccess.h
$(call BUILD_OBJECT_LIST,misc.c): CFLAGS += $(PCIACCESS_CFLAGS)

# ncurses-ui.c includes ncurses.h
$(NCURSES_UI_O): CFLAGS += $(NCURSES_CFLAGS)
$(NCURSES6_UI_O): CFLAGS += $(NCURSES6_CFLAGS)
$(NCURSESW6_UI_O): CFLAGS += $(NCURSESW6_CFLAGS)

# build the ncurses ui DSO as position-indpendent code
$(NCURSES_UI_O) $(NCURSES6_UI_O) $(NCURSESW6_UI_O): CFLAGS += -fPIC

# define the rule to build each object file
$(foreach src,$(ALL_SRC),$(eval $(call DEFINE_OBJECT_RULE,TARGET,$(src))))
$(eval $(call DEFINE_OBJECT_RULE_WITH_OBJECT_NAME,TARGET,$(NCURSES_UI_C),$(NCURSES6_UI_O)))
$(eval $(call DEFINE_OBJECT_RULE_WITH_OBJECT_NAME,TARGET,$(NCURSES_UI_C),$(NCURSESW6_UI_O)))

# define a rule to build each makeself-help-script object file
$(foreach src,$(MAKESELF_HELP_SCRIPT_SRC),\
  $(eval $(call DEFINE_OBJECT_RULE_WITH_OBJECT_NAME,HOST,$(src),\
    $(call BUILD_MAKESELF_OBJECT_LIST,$(src)))))

$(CONFIG_H): $(VERSION_MK)
	@ $(RM) -f $@
	@ $(MKDIR) $(OUTPUTDIR)
	@ $(ECHO)    "#define INSTALLER_OS \"$(TARGET_OS)\"" >> $@
	@ $(ECHO)    "#define INSTALLER_ARCH \"$(TARGET_ARCH)\"" >> $@
	@ $(ECHO) -n "#define NVIDIA_INSTALLER_VERSION " >> $@
	@ $(ECHO)    "\"$(NVIDIA_INSTALLER_VERSION)\"" >> $@
	@ $(ECHO) -n "#define PROGRAM_NAME " >> $@
	@ $(ECHO)    "\"$(NVIDIA_INSTALLER_PROGRAM_NAME)\"" >> $@

$(call BUILD_OBJECT_LIST,$(ALL_SRC)) $(NCURSES6_UI_O) $(NCURSESW6_UI_O): $(CONFIG_H)
$(call BUILD_MAKESELF_OBJECT_LIST,$(MAKESELF_HELP_SCRIPT_SRC)): $(CONFIG_H)

clean clobber:
	rm -rf $(OUTPUTDIR)


# rule to build a native rtld_test; a precompiled Linux-x86 rtld_test is
# distributed with nvidia-installer to simplify Linux-x86_64 builds.

$(eval $(call DEBUG_INFO_RULES, $(RTLD_TEST)))
$(RTLD_TEST).unstripped: rtld_test.c $(CONFIG_H)
	$(call quiet_cmd,LINK) $(CFLAGS) $(LDFLAGS) $(BIN_LDFLAGS) -o $@ -lGL -lEGL $<


##############################################################################
# rule to build MAKESELF_HELP_SCRIPT_SH; this shell script is packaged
# with the driver so that the script can be run on any platform when
# the driver is later repackaged
##############################################################################

$(MAKESELF_HELP_SCRIPT_SH): $(MAKESELF_HELP_SCRIPT)
	@ $(ECHO) "#!/bin/sh" > $@
	@ $(ECHO) "while [ \"\$$1\" ]; do" >> $@
	@ $(ECHO) "    case \$$1 in" >> $@
	@ $(ECHO) "        \"--advanced-options-args-only\")" >> $@
	@ $(ECHO) "            cat <<- \"ADVANCED_OPTIONS_ARGS_ONLY\"" >> $@
	$(MAKESELF_HELP_SCRIPT) --advanced-options-args-only >> $@
	@ $(ECHO) "ADVANCED_OPTIONS_ARGS_ONLY" >> $@
	@ $(ECHO) "            ;;" >> $@
	@ $(ECHO) "        \"--help-args-only\")" >> $@
	@ $(ECHO) "            cat <<- \"HELP_ARGS_ONLY\"" >> $@
	$(MAKESELF_HELP_SCRIPT) --help-args-only >> $@
	@ $(ECHO) "HELP_ARGS_ONLY" >> $@
	@ $(ECHO) "            ;;" >> $@
	@ $(ECHO) "        *)" >> $@
	@ $(ECHO) "            echo \"unrecognized option '$$1'"\" >> $@
	@ $(ECHO) "            break" >> $@
	@ $(ECHO) "            ;;" >> $@
	@ $(ECHO) "    esac" >> $@
	@ $(ECHO) "    shift" >> $@
	@ $(ECHO) "done" >> $@
	$(CHMOD) u+x $@


##############################################################################
# Documentation
##############################################################################

AUTO_TEXT = ".\\\" WARNING: THIS FILE IS AUTO-GENERATED!  Edit $< instead."

doc: $(MANPAGE)

GEN_MANPAGE_OPTS_SRC  = gen-manpage-opts.c
GEN_MANPAGE_OPTS_SRC += $(COMMON_UTILS_DIR)/gen-manpage-opts-helper.c

GEN_MANPAGE_OPTS_OBJS = $(call BUILD_OBJECT_LIST,$(GEN_MANPAGE_OPTS_SRC))

$(foreach src, $(GEN_MANPAGE_OPTS_SRC), \
    $(eval $(call DEFINE_OBJECT_RULE,HOST,$(src))))

$(GEN_MANPAGE_OPTS_OBJS): $(CONFIG_H)

$(GEN_MANPAGE_OPTS): $(GEN_MANPAGE_OPTS_OBJS)
	$(call quiet_cmd,HOST_LINK) \
	    $(HOST_CFLAGS) $(HOST_LDFLAGS) $(HOST_BIN_LDFLAGS) $^ -o $@

$(OPTIONS_1_INC): $(GEN_MANPAGE_OPTS)
	@$< > $@

$(MANPAGE): nvidia-installer.1.m4 $(OPTIONS_1_INC) $(VERSION_MK)
	$(call quiet_cmd,M4) \
	   -D__HEADER__=$(AUTO_TEXT) \
	   -D__VERSION__=$(NVIDIA_INSTALLER_VERSION) \
	   -D__DATE__="`$(DATE) +%F`" \
	   -D__INSTALLER_OS__="$(TARGET_OS)" \
	   -D__INSTALLER_ARCH__="$(TARGET_ARCH)" \
	   -D__DRIVER_VERSION__="$(NVIDIA_VERSION)" \
	   -D__OUTPUTDIR__=$(OUTPUTDIR) \
	   -I $(OUTPUTDIR) \
	   $< | $(GZIP_CMD) -9nf > $@
