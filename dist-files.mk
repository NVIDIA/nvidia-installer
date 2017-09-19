#
# nvidia-installer: A tool for installing NVIDIA software packages on
# Unix and Linux systems.
#
# Copyright (C) 2008 NVIDIA Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

##############################################################################
# define the list of files that should be distributed in the
# nvidia-installer tarball; this is used by the NVIDIA driver build
# when packaging the tarball, and by the nvidia-installer makefile when
# building nvidia-installer.
#
# Defines SRC and DIST_FILES
##############################################################################

SRC := backup.c
SRC += command-list.c
SRC += crc.c
SRC += files.c
SRC += install-from-cwd.c
SRC += kernel.c
SRC += log.c
SRC += misc.c
SRC += nvidia-installer.c
SRC += precompiled.c
SRC += stream-ui.c
SRC += user-interface.c
SRC += sanity.c
SRC += conflicting-kernel-modules.c

DIST_FILES := $(SRC)

DIST_FILES += backup.h
DIST_FILES += command-list.h
DIST_FILES += crc.h
DIST_FILES += files.h
DIST_FILES += kernel.h
DIST_FILES += misc.h
DIST_FILES += nvidia-installer-ui.h
DIST_FILES += nvidia-installer.h
DIST_FILES += option_table.h
DIST_FILES += precompiled.h
DIST_FILES += sanity.h
DIST_FILES += user-interface.h
DIST_FILES += conflicting-kernel-modules.h

DIST_FILES += COPYING
DIST_FILES += README
DIST_FILES += dist-files.mk

DIST_FILES += rtld_test_Linux-x86
DIST_FILES += rtld_test_Linux-x86_64
DIST_FILES += rtld_test_Linux-armv7l-gnueabi
DIST_FILES += rtld_test_Linux-armv7l-gnueabihf

DIST_FILES += tls_test_Linux-ia64
DIST_FILES += tls_test_Linux-x86
DIST_FILES += tls_test_Linux-x86_64

DIST_FILES += tls_test_dso_Linux-ia64.so
DIST_FILES += tls_test_dso_Linux-x86.so
DIST_FILES += tls_test_dso_Linux-x86_64.so

DIST_FILES += tls_test.c
DIST_FILES += tls_test_dso.c
DIST_FILES += rtld_test.c

DIST_FILES += nvidia-installer.1.m4
DIST_FILES += gen-manpage-opts.c
DIST_FILES += makeself-help-script.c

DIST_FILES += gen-ui-array.c
DIST_FILES += ncurses-ui.c
DIST_FILES += mkprecompiled.c
