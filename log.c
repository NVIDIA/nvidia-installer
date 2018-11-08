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
 * log.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "nvidia-installer.h"
#include "misc.h"

/* global stream for log output */

static FILE *log_file_stream;


/* convenience macro for logging boolean values */

#define BOOLSTR(b) ((b) ? "true" : "false")

/* convenience macro for catching NULL strings */

#define STRSTR(x) ((x) ? (x) : "(not specified)")

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

void log_init(Options *op, int argc, char * const argv[])
{
    time_t now;
    char *path;
    int i;

    if (!op->logging) return;
    
    log_file_stream = fopen(op->log_file_name, "w");
    
    if (!log_file_stream) {
        fprintf(stderr, "%s: Error opening log file '%s' for "
                "writing (%s); disabling logging.\n",
                PROGRAM_NAME, op->log_file_name, strerror(errno));
        op->logging = FALSE;
        return;
    }
    
    log_printf(op, NULL, "%s log file '%s'",
               PROGRAM_NAME, op->log_file_name);

    now = time(NULL);
    log_printf(op, NULL, "creation time: %s", ctime(&now));
    log_printf(op, NULL, "installer version: %s",
               NVIDIA_INSTALLER_VERSION);
    log_printf(op, NULL, "");

    path = getenv("PATH");
    log_printf(op, NULL, "PATH: %s", STRSTR(path));
    log_printf(op, NULL, "");

    log_printf(op, NULL, "nvidia-installer command line:");
    for (i = 0; i < argc; i++) {
        log_printf(op, "    ", "%s", argv[i]);
    }

    log_printf(op, NULL, "");
    
} /* log_init() */



/*
 * log_printf() - if the logging option is set, this function writes
 * the given printf-style input to the log_file_stream; if the logging
 * option is not set, then nothing is done here.
 */

void log_printf(Options *op, const char *prefix, const char *fmt, ...)
{
    char *buf;
    int append_newline = TRUE;

    if (!op->logging) return;

    NV_VSNPRINTF(buf, fmt);

    /*
     * do not append a newline to the end of the string if the caller
     * already did
     */

    if (buf && buf[0] && (buf[strlen(buf) - 1] == '\n')) {
        append_newline = FALSE;
    }

    if (prefix) {
        fprintf(log_file_stream, "%s", prefix);
    }
    fprintf(log_file_stream, "%s%s", buf, append_newline ? "\n" : "");

    nvfree(buf);
    
    /* flush, just to be safe */

    fflush(log_file_stream);
    
} /* log_printf() */
