#
# nvidia-installer: A tool for installing NVIDIA software packages on
# Unix and Linux systems.
#
# Copyright (C) 2008 NVIDIA Corporation.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of Version 2 of the GNU General Public
# License as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See Version 2
# of the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the:
#
#           Free Software Foundation, Inc.
#           59 Temple Place - Suite 330
#           Boston, MA 02111-1307, USA
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
SRC += format.c
SRC += install-from-cwd.c
SRC += kernel.c
SRC += log.c
SRC += misc.c
SRC += nvidia-installer.c
SRC += precompiled.c
SRC += snarf-ftp.c
SRC += snarf-http.c
SRC += snarf.c
SRC += stream-ui.c
SRC += update.c
SRC += user-interface.c
SRC += sanity.c

DIST_FILES := $(SRC)

DIST_FILES += backup.h
DIST_FILES += command-list.h
DIST_FILES += crc.h
DIST_FILES += files.h
DIST_FILES += format.h
DIST_FILES += kernel.h
DIST_FILES += misc.h
DIST_FILES += nvidia-installer-ui.h
DIST_FILES += nvidia-installer.h
DIST_FILES += option_table.h
DIST_FILES += precompiled.h
DIST_FILES += sanity.h
DIST_FILES += snarf-internal.h
DIST_FILES += snarf.h
DIST_FILES += update.h
DIST_FILES += user-interface.h

DIST_FILES += COPYING
DIST_FILES += README
DIST_FILES += dist-files.mk

DIST_FILES += rtld_test_Linux-x86
DIST_FILES += rtld_test_Linux-x86_64

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

DIST_FILES += gen-ui-array.c
DIST_FILES += ncurses-ui.c
DIST_FILES += mkprecompiled.c
