/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003-2009 NVIDIA Corporation
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
 * alloc-utils.c: this file contains heap management helper functions.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc-utils.h"

/*
 * nvalloc() - malloc wrapper that checks for errors, and zeros
 * out the memory; if an error occurs, an error is printed
 * to stderr and exit is called.  This function will only return
 * on success.
 */

void *nvalloc(size_t size)
{
    void *m = malloc(size);

    if (!m) {
        fprintf(stderr, "%s: memory allocation failure (%s)! \n",
                PROGRAM_NAME, strerror(errno));
        exit(1);
    }
    memset((char *) m, 0, size);
    return m;

} /* nvalloc() */



/*
 * nvrealloc() - realloc wrapper that checks for errors; if an
 * error occurs, an error is printed to stderr and exit
 * is called.  This function will only return on success.
 */

void *nvrealloc(void *ptr, size_t size)
{
    void *m;

    if (ptr == NULL) return nvalloc(size);

    m = realloc(ptr, size);
    if (!m) {
        fprintf(stderr, "%s: memory re-allocation failure (%s)! \n",
                PROGRAM_NAME, strerror(errno));
        exit(1);
    }
    return m;

} /* nvrealloc() */



/*
 * nvfree() - frees memory allocated with nvalloc(), provided
 * a non-NULL pointer is provided.
 */
void nvfree(char *s)
{
    if (s) free(s);

} /* nvfree() */
