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
 * format.h
 */

#ifndef __NVIDIA_INSTALLER_FORMAT_H__
#define __NVIDIA_INSTALLER_FORMAT_H__

#include <stdio.h>
#include <stdarg.h>

void reset_current_terminal_width(unsigned short new_val);

void fmtout(const char *fmt, ...);
void fmtoutp(const char *prefix, const char *fmt, ...);
void fmterr(const char *fmt, ...);
void fmterrp(const char *prefix, const char *fmt, ...);
void format(FILE *stream, const char *prefix, const char *fmt, ...);

TextRows *nv_format_text_rows(const char *prefix, const char *buf,
                              int width, int word_boundary);
void nv_free_text_rows(TextRows *t);

#endif /* __NVIDIA_INSTALLER_FORMAT_H__ */
