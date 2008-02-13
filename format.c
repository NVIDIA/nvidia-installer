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
 * format.c - this source file contains routines for formatting string
 * output.
 */


#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>

#include "nvidia-installer.h"
#include "format.h"
#include "misc.h"

static unsigned short __terminal_width = 0;



#define DEFAULT_WIDTH 75

/* 
 * Format and display a printf style string such that it fits within
 * the terminal width 
 */

#define NV_VFORMAT(stream, wb, prefix, fmt)     \
do {                                            \
    char *buf;                                  \
    NV_VSNPRINTF(buf, fmt);                     \
    vformat(stream, wb, prefix, buf);           \
    free (buf);                                 \
} while(0)


static void vformat(FILE *stream, const int wb,
                    const char *prefix, const char *buf);


/*
 * reset_current_terminal_width() - if new_val is zero, then use the
 * TIOCGWINSZ ioctl to get the current width of the terminal, and
 * assign it the value to __terminal_width.  If the ioctl fails, use a
 * hardcoded constant.  If new_val is non-zero, then use new_val.
 */

void reset_current_terminal_width(unsigned short new_val)
{
    struct winsize ws;
    
    if (new_val) {
        __terminal_width = new_val;
        return;
    }

    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        __terminal_width = DEFAULT_WIDTH;
    } else {
        __terminal_width = ws.ws_col - 1;
    }
} /* get_current_terminal_width() */



/*
 * fmtout() - stdout format function: prints the given string to
 * stdout with no prefix.
 */

void fmtout(const char *fmt, ...)
{
    NV_VFORMAT(stdout, TRUE, NULL, fmt);

} /* fmtout() */



/*
 * fmtoutp() - stdout format function with prefix: prints the given
 * string to stdout with the given prefix.
 */

void fmtoutp(const char *prefix, const char *fmt, ...)
{
    NV_VFORMAT(stdout, TRUE, prefix, fmt);

} /* fmtoutp() */



/*
 * fmterr() - stderr format function: prints the given string to
 * stderr with no prefix.
 */

void fmterr(const char *fmt, ...)
{
    NV_VFORMAT(stderr, TRUE, NULL, fmt);

} /* fmterr() */



/*
 * fmterrp() - stderr format function: prints the given string to
 * stderr with the given prefix.
 */

void fmterrp(const char *prefix, const char *fmt, ...)
{
    NV_VFORMAT(stderr, TRUE, prefix, fmt);

} /* fmterrp() */



/*
 * format() & vformat() - these takes a printf-style format string and
 * a variable list of args.  We use NV_VSNPRINTF to generate the
 * desired string, and then call nv_format_text_rows() to format the
 * string so that not more than __terminal_width characters are
 * printed across.
 *
 * The resulting formatted output is written to the specified stream.
 * The output may also include an optional prefix (to be prepended on
 * the first line, and filled with spaces on subsequent lines.
 *
 * The wb argument indicates whether the line wrapping should only
 * break on word boundaries.
 */

void format(FILE *stream, const char *prefix, const char *fmt, ...)
{
    NV_VFORMAT(stream, TRUE, prefix, fmt);

} /* format() */



static void vformat(FILE *stream, const int wb,
                    const char *prefix, const char *buf)
{
    int i;
    TextRows *t;
    
    if (!__terminal_width) reset_current_terminal_width(0);

    t = nv_format_text_rows(prefix, buf, __terminal_width, wb);

    for (i = 0; i < t->n; i++) fprintf(stream, "%s\n", t->t[i]);
    
    nv_free_text_rows(t);
    
} /* vformat() */
