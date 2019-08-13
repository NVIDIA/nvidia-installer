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
int unpack_kernel_modules                          (Options*, Package*,
                                                    const char *,
                                                    const PrecompiledFileInfo *);
int build_kernel_modules                           (Options*, Package*);
int build_kernel_interfaces                        (Options*, Package*,
                                                    PrecompiledFileInfo **);
int test_kernel_modules                            (Options*, Package*);
int load_kernel_module                             (Options*, const char*);
int check_for_unloaded_kernel_module               (Options*);
PrecompiledInfo *find_precompiled_kernel_interface (Options*, Package*);
char *get_kernel_name                              (Options*);
KernelConfigOptionStatus test_kernel_config_option (Options*, Package*,
                                                    const char*);
int sign_kernel_module                             (Options*, const char*, 
                                                    const char*, int);
char *guess_module_signing_hash                    (Options*, const char*);
int remove_kernel_module_from_package              (Package*, const char*);
void free_kernel_module_info                       (KernelModuleInfo);
int package_includes_kernel_module                 (const Package*,
                                                    const char *);
int rmmod_kernel_module                            (Options*, const char *);
int conftest_sanity_check                          (Options*, const char *,
                                                    const char *, const char *);

#ifndef ENOKEY
#define	ENOKEY		126	/* Required key not available */
#endif

#endif /* __NVIDIA_INSTALLER_KERNEL_H__ */
