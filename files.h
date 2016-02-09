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

#ifndef __NVIDIA_INSTALLER_FILES_H__
#define __NVIDIA_INSTALLER_FILES_H__

#include "nvidia-installer.h"
#include "precompiled.h"

int remove_directory(Options *op, const char *victim);
int touch_directory(Options *op, const char *victim);
int copy_file(Options *op, const char *srcfile,
              const char *dstfile, mode_t mode);
char *write_temp_file(Options *op, const int len,
                      const unsigned char *data, mode_t perm);
void select_tls_class(Options *op, Package *p); /* XXX move? */
int set_destinations(Options *op, Package *p); /* XXX move? */
int get_license_acceptance(Options *op); /* XXX move? */
int get_prefixes(Options *op); /* XXX move? */
void add_kernel_modules_to_package(Options *op, Package *p);
void remove_non_kernel_module_files_from_package(Options *op, Package *p);
void remove_opengl_files_from_package(Options *op, Package *p);
int mode_string_to_mode(Options *op, char *s, mode_t *mode);
char *mode_to_permission_string(mode_t mode);
int confirm_path(Options *op, const char *path);
int mkdir_recursive(Options *op, const char *path, const mode_t mode, int log);
int mkdir_with_log(Options *op, const char *path, const mode_t mode);
char *get_symlink_target(Options *op, const char *filename);
char *get_resolved_symlink_target(Options *op, const char *filename);
int install_file(Options *op, const char *srcfile,
                 const char *dstfile, mode_t mode);
int install_symlink(Options *op, const char *linkname, const char *dstfile);
size_t get_file_size(Options *op, const char *filename);
size_t fget_file_size(Options *op, const int fd);
char *get_tmpdir(Options *op);
char *make_tmpdir(Options *op);
int nvrename(Options *op, const char *src, const char *dst);
int check_for_existing_rpms(Options *op);
int copy_directory_contents(Options *op, const char *src, const char *dst);
int pack_precompiled_files(Options *op, Package *p, int num_files,
                           PrecompiledFileInfo *files);

char *process_template_file(Options *op, PackageEntry *pe,
                            char **tokens, char **replacements);
void process_libGL_la_files(Options *op, Package *p);
void process_dot_desktop_files(Options *op, Package *p);
void process_dkms_conf(Options *op, Package *p);
int set_security_context(Options *op, const char *filename);
void get_default_prefixes_and_paths(Options *op);
void get_compat32_path(Options *op);
char *nv_strreplace(char *src, char *orig, char *replace);
char *get_filename(Options *op, const char *def, const char *msg);
int secure_delete(Options *op, const char *file);
void invalidate_package_entry(PackageEntry *entry);
int is_subdirectory(const char *dir, const char *subdir, int *is_subdir);
void add_libgl_abi_symlink(Options *op, Package *p);

int check_libglvnd_files(Options *op, Package *p);
void select_glvnd(Options *op, Package *p);

#endif /* __NVIDIA_INSTALLER_FILES_H__ */
