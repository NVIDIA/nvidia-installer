/*
 * nvidia-installer: A tool for installing/un-installing the
 * NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2004-2010 NVIDIA Corporation
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
 * nvgetopt.h
 */

#ifndef __NVGETOPT_H__
#define __NVGETOPT_H__

#define NVGETOPT_FALSE 0
#define NVGETOPT_TRUE 1

/*
 * indicates that the option is a boolean value; the presence of the
 * option will be interpretted as a TRUE value; if the option is
 * prepended with '--no-', the option will be interpretted as a FALSE
 * value.  On success, nvgetopt will return the parsed boolean value
 * through 'boolval'.
 */

#define NVGETOPT_IS_BOOLEAN       0x1


/*
 * indicates that the option takes an argument to be interpretted as a
 * string; on success, nvgetopt will return the parsed string argument
 * through 'strval'.
 */

#define NVGETOPT_STRING_ARGUMENT  0x2


#define NVGETOPT_HAS_ARGUMENT (NVGETOPT_STRING_ARGUMENT)

#define NVGETOPT_HELP_ALWAYS 0x20

typedef struct {
    const char *name;
    int val;
    unsigned int flags;
    char *description; /* not used by nvgetopt() */
} NVGetoptOption;


int nvgetopt(int argc, char *argv[], const NVGetoptOption *options,
             char **strval);

#endif /* __NVGETOPT_H__ */
