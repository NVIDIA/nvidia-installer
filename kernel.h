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
 */

#ifndef __NVIDIA_INSTALLER_KERNEL_H__
#define __NVIDIA_INSTALLER_KERNEL_H__

#include "nvidia-installer.h"
#include "precompiled.h"

typedef enum {
    KERNEL_CONFIG_OPTION_NOT_DEFINED = 0,
    KERNEL_CONFIG_OPTION_DEFINED,
    KERNEL_CONFIG_OPTION_UNKNOWN
} KernelConfigOptionStatus;

int determine_kernel_module_installation_path      (Options*);
int determine_kernel_source_path                   (Options*, Package*);
int determine_kernel_output_path                   (Options*);
int link_kernel_module                             (Options*, Package*,
                                                    const char *,
                                                    const PrecompiledFileInfo *);
int check_cc_version                               (Options*, Package*);
int build_kernel_module                            (Options*, Package*);
int build_kernel_interface                         (Options*, Package*,
                                                    PrecompiledFileInfo **);
int test_kernel_module                             (Options*, Package*);
int load_kernel_module                             (Options*, Package*);
int check_for_unloaded_kernel_module               (Options*);
PrecompiledInfo *find_precompiled_kernel_interface (Options*, Package*);
char *get_kernel_name                              (Options*);
KernelConfigOptionStatus test_kernel_config_option (Options*, Package*,
                                                    const char*);
int sign_kernel_module                             (Options*, const char*, 
                                                    const char*, int);
char *guess_module_signing_hash                    (Options*, Package*);
int rmmod_kernel_module                            (Options*, const char*);

#define SEARCH_FILELIST_MAX_ENTRIES       32

#ifndef ENOKEY
#define	ENOKEY		126	/* Required key not available */
#endif

#endif /* __NVIDIA_INSTALLER_KERNEL_H__ */
