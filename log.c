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
 * log.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "nvidia-installer.h"
#include "misc.h"
#include "format.h"

/* global stream for log output */

static FILE *log_file_stream;


/* convenience macro for logging boolean values */

#define BOOLSTR(b) ((b) ? "true" : "false")

/* convenience macro for catching NULL strings */

#define STRSTR(x) ((x) ? (x) : "(not specified)")

#define TLSSTR(x) ({ \
    const char *__tls_str = NULL; \
    switch (x) { \
        case FORCE_CLASSIC_TLS: __tls_str = "classic"; break; \
        case FORCE_NEW_TLS: __tls_str = "elf-tls"; break; \
        default: __tls_str = "(not specified)"; \
    } \
    __tls_str; \
})

#define SELINUXSTR(x) ({ \
    const char *__selinux_str = NULL; \
    switch (x) { \
        case SELINUX_FORCE_YES: __selinux_str = "yes"; break; \
        case SELINUX_FORCE_NO: __selinux_str = "no"; break; \
        case SELINUX_DEFAULT: __selinux_str = "default"; break; \
        default: __selinux_str = "(not specified)"; \
    } \
    __selinux_str; \
})

/*
 * log_init() - if logging is enabled, initialize the log file; if
 * initializing the log file fails, print an error to stderr and
 * disable loggging.  If initialization succeeds, write a header line
 * and the state of all noteworthy options.
 */

void log_init(Options *op)
{
    time_t now;

    if (!op->logging) return;
    
    log_file_stream = fopen(op->log_file_name, "w");
    
    if (!log_file_stream) {
        fprintf(stderr, "%s: Error opening log file '%s' for "
                "writing (%s); disabling logging.\n",
                PROGRAM_NAME, op->log_file_name, strerror(errno));
        op->logging = FALSE;
        return;
    }
    
    log_printf(op, TRUE, NULL, "%s log file '%s'",
               PROGRAM_NAME, op->log_file_name);

    now = time(NULL);
    log_printf(op, TRUE, NULL, "creation time: %s", ctime(&now));
    log_printf(op, TRUE, NULL, "installer version: %s",
               NVIDIA_INSTALLER_VERSION);
    log_printf(op, TRUE, NULL, "");
    
    log_printf(op, TRUE, NULL, "option status:");
    log_printf(op, TRUE, NULL, "  license pre-accepted    : %s",
               BOOLSTR(op->accept_license));
    log_printf(op, TRUE, NULL, "  update                  : %s",
               BOOLSTR(op->update));
    log_printf(op, TRUE, NULL, "  force update            : %s",
               BOOLSTR(op->force_update));
    log_printf(op, TRUE, NULL, "  expert                  : %s",
               BOOLSTR(op->expert));
    log_printf(op, TRUE, NULL, "  uninstall               : %s",
               BOOLSTR(op->uninstall));
    log_printf(op, TRUE, NULL, "  driver info             : %s",
               BOOLSTR(op->driver_info));
    log_printf(op, TRUE, NULL, "  precompiled interfaces  : %s",
               BOOLSTR(!op->no_precompiled_interface));
    log_printf(op, TRUE, NULL, "  no ncurses color        : %s",
               BOOLSTR(op->no_ncurses_color));
    log_printf(op, TRUE, NULL, "  query latest version    : %s",
               BOOLSTR(op->latest));
    log_printf(op, TRUE, NULL, "  OpenGL header files     : %s",
               BOOLSTR(op->opengl_headers));
    log_printf(op, TRUE, NULL, "  no questions            : %s",
               BOOLSTR(op->no_questions));
    log_printf(op, TRUE, NULL, "  silent                  : %s",
               BOOLSTR(op->silent));
    log_printf(op, TRUE, NULL, "  no recursion            : %s",
               BOOLSTR(op->no_recursion));
    log_printf(op, TRUE, NULL, "  no backup               : %s",
               BOOLSTR(op->no_backup));
    log_printf(op, TRUE, NULL, "  kernel module only      : %s",
               BOOLSTR(op->kernel_module_only));
    log_printf(op, TRUE, NULL, "  sanity                  : %s",
               BOOLSTR(op->sanity));
    log_printf(op, TRUE, NULL, "  add this kernel         : %s",
               BOOLSTR(op->add_this_kernel));
    log_printf(op, TRUE, NULL, "  no runlevel check       : %s",
               BOOLSTR(op->no_runlevel_check));
    log_printf(op, TRUE, NULL, "  no network              : %s",
               BOOLSTR(op->no_network));
    log_printf(op, TRUE, NULL, "  no ABI note             : %s",
               BOOLSTR(op->no_abi_note));
    log_printf(op, TRUE, NULL, "  no RPMs                 : %s",
               BOOLSTR(op->no_rpms));
    log_printf(op, TRUE, NULL, "  no kernel module        : %s",
               BOOLSTR(op->no_kernel_module));
    log_printf(op, TRUE, NULL, "  force SELinux           : %s",
               SELINUXSTR(op->selinux_option));
    log_printf(op, TRUE, NULL, "  no X server check       : %s",
               BOOLSTR(op->no_x_check));
    log_printf(op, TRUE, NULL, "  no cc version check     : %s",
               BOOLSTR(op->ignore_cc_version_check));
    log_printf(op, TRUE, NULL, "  force tls               : %s",
               TLSSTR(op->which_tls));
#if defined(NV_X86_64)
    log_printf(op, TRUE, NULL, "  force compat32 tls      : %s",
               TLSSTR(op->which_tls_compat32));
#endif
    log_printf(op, TRUE, NULL, "  X install prefix        : %s",
               STRSTR(op->x_prefix));
    log_printf(op, TRUE, NULL, "  X library install path  : %s",
               STRSTR(op->x_library_path));
    log_printf(op, TRUE, NULL, "  X module install path   : %s",
               STRSTR(op->x_module_path));
    log_printf(op, TRUE, NULL, "  OpenGL install prefix   : %s",
               STRSTR(op->opengl_prefix));
    log_printf(op, TRUE, NULL, "  OpenGL install libdir   : %s",
               STRSTR(op->opengl_libdir));
#if defined(NV_X86_64)
    log_printf(op, TRUE, NULL, "  compat32 install chroot : %s",
               STRSTR(op->compat32_chroot));
    log_printf(op, TRUE, NULL, "  compat32 install prefix : %s",
               STRSTR(op->compat32_prefix));
    log_printf(op, TRUE, NULL, "  compat32 install libdir : %s",
               STRSTR(op->compat32_libdir));
#endif
    log_printf(op, TRUE, NULL, "  utility install prefix  : %s",
               STRSTR(op->utility_prefix));
    log_printf(op, TRUE, NULL, "  utility install libdir  : %s",
               STRSTR(op->utility_libdir));
    log_printf(op, TRUE, NULL, "  doc install prefix      : %s",
               STRSTR(op->documentation_prefix));
    log_printf(op, TRUE, NULL, "  kernel name             : %s",
               STRSTR(op->kernel_name));
    log_printf(op, TRUE, NULL, "  kernel include path     : %s",
               STRSTR(op->kernel_include_path));
    log_printf(op, TRUE, NULL, "  kernel source path      : %s",
               STRSTR(op->kernel_source_path));
    log_printf(op, TRUE, NULL, "  kernel output path      : %s",
               STRSTR(op->kernel_output_path));
    log_printf(op, TRUE, NULL, "  kernel install path     : %s",
               STRSTR(op->kernel_module_installation_path));
    log_printf(op, TRUE, NULL, "  proc mount point        : %s",
               STRSTR(op->proc_mount_point));
    log_printf(op, TRUE, NULL, "  ui                      : %s",
               STRSTR(op->ui_str));
    log_printf(op, TRUE, NULL, "  tmpdir                  : %s",
               STRSTR(op->tmpdir));
    log_printf(op, TRUE, NULL, "  ftp mirror              : %s",
               STRSTR(op->ftp_site));
    log_printf(op, TRUE, NULL, "  RPM file list           : %s",
               STRSTR(op->rpm_file_list));


    log_printf(op, TRUE, NULL, "");
    
} /* log_init() */



/*
 * log_printf() - if the logggin option is set, this function writes
 * the given printf-style input to the log_file_stream; if the logging
 * option is not set, then nothing is done here.
 */

#define LOG_WIDTH 79

void log_printf(Options *op, const int wb,
                const char *prefix, const char *fmt, ...)
{
    char *buf;
    int i;
    TextRows *t;

    if (!op->logging) return;

    NV_VSNPRINTF(buf, fmt);
    t = nv_format_text_rows(prefix, buf, LOG_WIDTH, wb);
    
    for (i = 0; i < t->n; i++) fprintf(log_file_stream, "%s\n", t->t[i]);
    
    nv_free_text_rows(t);
    nvfree(buf);
    
    /* flush, just to be safe */

    fflush(log_file_stream);
    
} /* log_printf() */
