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

/*
 * NV_VSNPRINTF() - takes a fmt string, and uses vsnprintf to build
 * the resulting string whic it assigns to buf.  The caller of this
 * function is responsible for freeing the returned string.
 */
#define NV_VSNPRINTF(buf, fmt)                                  \
do {                                                            \
    if (!fmt) {                                                 \
        (buf) = NULL;                                           \
    } else {                                                    \
        va_list ap;                                             \
        int len, current_len = NV_LINE_LEN;                     \
                                                                \
        (buf) = malloc(current_len);                            \
                                                                \
        while (1) {                                             \
            va_start(ap, fmt);                                  \
            len = vsnprintf((buf), current_len, (fmt), ap);     \
            va_end(ap);                                         \
                                                                \
            if ((len > -1) && (len < current_len)) {            \
                break;                                          \
            } else if (len > -1) {                              \
                current_len = len + 1;                          \
            } else {                                            \
                current_len += NV_LINE_LEN;                     \
            }                                                   \
                                                                \
            (buf) = realloc(buf, current_len);                  \
        }                                                       \
    }                                                           \
} while (0)

/*
 * NV_STRCAT() - takes a dynamically allocated string followed by a
 * NULL-terminated list of arbitrary strings and concatenates the
 * strings with nvstrcat(); the newly allocated string replaces the
 * original one, which is freed.
 */
#define NV_STRCAT(str, args...)              \
do {                                         \
    char *__tmp_str = (str);                 \
    (str) = nvstrcat(__tmp_str, ##args);     \
    nvfree(__tmp_str);                       \
} while (0)

void *nvalloc(size_t size);
void *nvrealloc(void *ptr, size_t size);
char *nvstrdup(const char *s);
void nvfree(char *s);
char *nvstrtolower(char *s);
char *nvstrcat(const char *str, ...);
char *read_next_word (char *buf, char **e);
char *assemble_string(const char *fmt, va_list ap);

int check_euid(Options *op);
int check_runlevel(Options *op);
int adjust_cwd(Options *op, const char *program_name);
char *fget_next_line(FILE *fp, int *eof);
char *get_next_line(char *buf, char **e, char *start, int length);
int run_command(Options *op, const char *cmd, char **data,
                int output, int status, int redirect);
int read_text_file(const char *filename, char **buf);
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
int check_for_modular_xorg(Options *op);
int run_distro_hook(Options *op, const char *hook);
int check_for_nouveau(Options *op);

TextRows *nv_format_text_rows(const char *prefix, const char *buf,
                              int width, int word_boundary);
void nv_free_text_rows(TextRows *t);

#endif /* __NVIDIA_INSTALLER_MISC_H__ */
