/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2013 NVIDIA Corporation
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
 */

#ifndef __NVIDIA_INSTALLER_MANIFEST_H__
#define __NVIDIA_INSTALLER_MANIFEST_H__

#include "nvidia-installer.h"

PackageEntryFileCapabilities get_file_type_capabilities(
    PackageEntryFileType type);

PackageEntryFileType parse_manifest_file_type(
    const char *str,
    PackageEntryFileCapabilities *caps);

void get_installable_file_type_list(
    Options *op,
    PackageEntryFileTypeList *installable_file_types);

void add_symlinks_to_file_type_list(
    PackageEntryFileTypeList *file_type_list);

void remove_file_type_from_file_type_list(
    PackageEntryFileTypeList *list, PackageEntryFileType type);
#endif /* __NVIDIA_INSTALLER_MANIFEST_H__ */
