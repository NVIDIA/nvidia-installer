/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2015 NVIDIA Corporation
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

#include "common-utils.h"

/*
 * A list of kernel modules that will conflict with this driver installation.
 * The list should be maintained in reverse dependency order; i.e., it should
 * be possible to unload kernel modules one at a time, in the order that they
 * appear in this list.
 */

const char * const conflicting_kernel_modules[] = {
    "nvidia-vgpu-vfio",
    "nvidia-uvm",
    "nvidia-drm",
    "nvidia-modeset",
    "nvidia",
    "nvidia0", "nvidia1", "nvidia2", "nvidia3",
    "nvidia4", "nvidia5", "nvidia6", "nvidia7",
    "nvidia-frontend",
};

const int num_conflicting_kernel_modules = ARRAY_LEN(conflicting_kernel_modules);
