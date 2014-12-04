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
 * misc.h
 */

#ifndef __NVIDIA_INSTALLER_MISC_H__
#define __NVIDIA_INSTALLER_MISC_H__

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "nvidia-installer.h"
#include "command-list.h"

char *read_next_word (char *buf, char **e);

int check_euid(Options *op);
int check_runlevel(Options *op);
int adjust_cwd(Options *op, const char *program_name);
char *get_next_line(char *buf, char **e, char *start, int length);
int run_command(Options *op, const char *cmd, char **data,
                int output, int status, int redirect);
int read_text_file(const char *filename, char **buf);
char *find_system_util(const char *util);
int find_system_utils(Options *op);
int find_module_utils(Options *op);
int check_selinux(Options *op);
int check_proc_modprobe_path(Options *op);
int check_development_tools(Options *op, Package *p);
char *extract_version_string(const char *str);
int continue_after_error(Options *op, const char *fmt, ...);
int do_install(Options *op, Package *p, CommandList *c);
void should_install_opengl_headers(Options *op, Package *p);
void should_install_compat32_files(Options *op, Package *p);
void check_installed_files_from_package(Options *op, Package *p);
uint64_t get_installable_file_mask(Options *op);
int tls_test(Options *op, int compat_32_libs);
int check_runtime_configuration(Options *op, Package *p);
void collapse_multiple_slashes(char *s);
int is_symbolic_link_to(const char *path, const char *dest);
Distribution get_distribution(Options *op);
int check_for_running_x(Options *op);
void query_xorg_version(Options *op);
int check_for_nvidia_graphics_devices(Options *op, Package *p);
int run_nvidia_xconfig(Options *op, int restore);
int run_distro_hook(Options *op, const char *hook);
int check_for_nouveau(Options *op);
int dkms_module_installed(Options *op, const char *version);
int dkms_install_module(Options *op, const char *version, const char *kernel);
int dkms_remove_module(Options *op, const char *version);

#endif /* __NVIDIA_INSTALLER_MISC_H__ */
