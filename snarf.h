/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 *
 * snarf.h
 */

#ifndef __NVIDIA_INSTALLER_SNARF_H__
#define __NVIDIA_INSTALLER_SNARF_H__

#include "nvidia-installer.h"

/*
 * SNARF_FLAGS_STATUS_BAR: when this flag is set, a status bar is
 * displayed, showing the progress of the download.
 */
#define SNARF_FLAGS_STATUS_BAR 0x1

/*
 * SNARF_FLAGS_DOWNLOAD_SILENT: when this flag is set, then snarf will
 * not print error messages when the download fails
 */
#define SNARF_FLAGS_DOWNLOAD_SILENT 0x2

int snarf(Options *op, const char *url, int out_fd, uint32 flags);

#endif /* __NVIDIA_INSTALLER_SNARF_H__ */
