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
 * string-utils.h
 */

#ifndef __STRING_UTILS_H__
#define __STRING_UTILS_H__

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

char *nvstrdup(const char *s);
char *nvstrtolower(char *s);
char *nvstrcat(const char *str, ...);

#endif /* __STRING_UTILS_H__ */
