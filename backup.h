/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *      Free Software Foundation, Inc.
 *      59 Temple Place - Suite 330
 *      Boston, MA 02111-1307, USA
 */

#ifndef __NVIDIA_INSTALLER_BACKUP_H__
#define __NVIDIA_INSTALLER_BACKUP_H__

#include "nvidia-installer.h"

#define INSTALLED_SYMLINK  0
#define INSTALLED_FILE     1
#define BACKED_UP_SYMLINK  2
#define BACKED_UP_FILE_NUM 100

int init_backup                 (Options*, Package*);
int do_backup                   (Options*, const char*);
int log_install_file            (Options*, const char*);
int log_create_symlink          (Options*, const char*, const char*);
int get_installed_driver_version(Options*, int*, int*, int*);
int check_for_existing_driver   (Options*, Package*);
int uninstall_existing_driver   (Options*, const int);
int report_driver_information   (Options*);

char *get_installed_driver_version_and_descr(Options *, int *, int *, int *);
int test_installed_files(Options *op);
int find_installed_file(Options *op, char *filename);

#endif /* __NVIDIA_INSTALLER_BACKUP_H__ */
