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
 * precompiled.h
 */

#ifndef __NVIDIA_INSTALLER_PRECOMPILED_H__
#define __NVIDIA_INSTALLER_PRECOMPILED_H__

#define OUTPUT_FILENAME "nv-linux.o"

typedef struct {
    
    uint32 crc;
    char *version;
    char *proc_version_string;
    char *description;

} PrecompiledInfo;


char *read_proc_version(Options *op);

PrecompiledInfo *precompiled_unpack(Options *op,
                                    const char *filename,
                                    const char *output_filename,
                                    const char *real_proc_version_string,
                                    const char *package_version);


#endif /* __NVIDIA_INSTALLER_PRECOMPILED_H__ */
