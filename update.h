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
 * update.h
 */

#ifndef __NVIDIA_INSTALLER_UPDATE_H__
#define __NVIDIA_INSTALLER_UPDATE_H__

#include "nvgetopt.h"
#include "nvidia-installer.h"

int update(Options *);
int report_latest_driver_version(Options *);
char *append_update_arguments(char *s, int c, const char *arg,
                              const NVGetoptOption *options);

#endif /* __NVIDIA_INSTALLER_UPDATE_H__ */
