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
 * format.c - this source file contains routines for formatting string
 * output.
 */


#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>

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



/*
 * nv_format_text_rows() - this function breaks the given string str
 * into some number of rows, where each row is not longer than the
 * specified width.
 *
 * If prefix is non-NULL, the first line is prepended with the prefix,
 * and subsequent lines are indented to line up with the prefix.
 *
 * If word_boundary is TRUE, then attempt to only break lines on
 * boundaries between words.
 *
 * XXX Note that we don't use nvalloc() or any of the other wrapper
 * functions from here, so that this function doesn't require any
 * non-c library symbols (so that it can be called from dlopen()'ed
 * user interfaces.
 */

TextRows *nv_format_text_rows(const char *prefix, const char *str,
                              int width, int word_boundary)
{
    int len, prefix_len, z, w, i;
    char *line, *buf, *local_prefix, *a, *b, *c;
    TextRows *t;

    /* initialize the TextRows structure */

    t = (TextRows *) malloc(sizeof(TextRows));
    t->t = NULL;
    t->n = 0;
    t->m = 0;

    if (!str) return t;

    buf = strdup(str);

    z = strlen(buf); /* length of entire string */
    a = buf;         /* pointer to the start of the string */

    /* initialize the prefix fields */

    if (prefix) {
        prefix_len = strlen(prefix);
        local_prefix = nvstrdup(prefix);
    } else {
        prefix_len = 0;
        local_prefix = NULL;
    }

    /* adjust the max width for any prefix */

    w = width - prefix_len;

    do {
        /*
         * if the string will fit on one line, point b to the end of the
         * string
         */

        if (z < w) b = a + z;

        /* 
         * if the string won't fit on one line, move b to where the
         * end of the line should be, and then move b back until we
         * find a space; if we don't find a space before we back b all
         * the way up to a, just assign b to where the line should end.
         */

        else {
            b = a + w;

            if (word_boundary) {
                while ((b >= a) && (!isspace(*b))) b--;
                if (b <= a) b = a + w;
            }
        }

        /* look for any newline inbetween a and b, and move b to it */

        for (c = a; c < b; c++) if (*c == '\n') { b = c; break; }

        /*
         * copy the string that starts at a and ends at b, prepending
         * with a prefix, if present
         */

        len = b-a;
        len += prefix_len;
        line = (char *) malloc(len+1);
        if (local_prefix) strncpy(line, local_prefix, prefix_len);
        strncpy(line + prefix_len, a, len - prefix_len);
        line[len] = '\0';

        /* append the new line to the array of text rows */

        t->t = (char **) realloc(t->t, sizeof(char *) * (t->n + 1));
        t->t[t->n] = line;
        t->n++;

        if (t->m < len) t->m = len;

        /*
         * adjust the length of the string and move the pointer to the
         * beginning of the new line
         */

        z -= (b - a + 1);
        a = b + 1;

        /* move to the first non whitespace character (excluding newlines) */

        if (word_boundary && isspace(*b)) {
            while ((z) && (isspace(*a)) && (*a != '\n')) a++, z--;
        } else {
            if (!isspace(*b)) z++, a--;
        }

        if (local_prefix) {
            for (i = 0; i < prefix_len; i++) local_prefix[i] = ' ';
        }

    } while (z > 0);

    if (local_prefix) free(local_prefix);
    free(buf);

    return t;

} /* nv_format_text_rows() */



/*
 * nv_free_text_rows() - free the TextRows data structure allocated by
 * nv_format_text_rows()
 */

void nv_free_text_rows(TextRows *t)
{
    int i;

    if (!t) return;
    for (i = 0; i < t->n; i++) free(t->t[i]);
    if (t->t) free(t->t);
    free(t);

} /* nv_free_text_rows() */
