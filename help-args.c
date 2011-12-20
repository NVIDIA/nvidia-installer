/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003-2009 NVIDIA Corporation
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
 * help-args.c: this file contains a utility function that outputs the
 * option table in a human readable format.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "nvidia-installer.h"
#include "format.h"

#include "option_table.h"

/*
 * cook_description() - the description string may contain text
 * within brackets, which is used by the manpage generator
 * to denote text to be italicized.  We want to omit the bracket
 * characters here.
 */

static char *cook_description(const char *description)
{
    int len;
    char *s, *dst;
    const char *src;

    len = strlen(description);
    s = nvalloc(len + 1);

    for (src = description, dst = s; *src; src++) {
        if (*src != '[' && (*src != ']')) {
            *dst = *src;
            dst++;
        }
    }

    *dst = '\0';

    return s;

} /* cook_description() */



void print_help_args_only(int is_uninstall, int args_only, int advanced)
{
    int i, j, len;
    char *msg, *tmp, scratch[64];
    const NVGetoptOption *o;

    /*
     * the args_only parameter is used by makeself.sh to get our
     * argument list and description; in this case we don't
     * want to format to the width of the terminal, so hardcode
     * the width to 65.
     */
    if (args_only) reset_current_terminal_width(65);

    for (i = 0; __options[i].name; i++) {
        o = &__options[i];

        /*
         * if non-advanced help is requested, and the ALWAYS flag is
         * not set, then skip this option
         */

        if (!advanced && !(o->flags & NVGETOPT_HELP_ALWAYS)) continue;

        /* Skip options with no help text */
        if (!o->description) continue;

        /* Skip options that do not apply to nvidia-uninstall if we're
         * invoked as nvidia-uninstall. */
        if (is_uninstall
            && !(o->flags & NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL)) {
            continue;
        }

        if (o->flags & NVGETOPT_IS_BOOLEAN) {
            msg = nvstrcat("--", o->name, "/--no-", o->name, NULL);
        } else if (isalnum(o->val)) {
            sprintf(scratch, "%c", o->val);
            msg = nvstrcat("-", scratch, ", --", o->name, NULL);
        } else {
            msg = nvstrcat("--", o->name, NULL);
        }
        if (o->flags & NVGETOPT_HAS_ARGUMENT) {
            len = strlen(o->name);
            for (j = 0; j < len; j++) scratch[j] = toupper(o->name[j]);
            scratch[len] = '\0';
            tmp = nvstrcat(msg, "=", scratch, NULL);
            nvfree(msg);
            msg = tmp;
        }
        fmtoutp(TAB, msg);
        if (o->description) {
            tmp = cook_description(o->description);
            fmtoutp(BIGTAB, tmp);
            nvfree(tmp);
        }
        fmtout("");
        nvfree(msg);
    }
}
