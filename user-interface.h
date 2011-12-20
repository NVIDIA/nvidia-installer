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
 * user_interface.h
 */

#ifndef __NVIDIA_INSTALLER_USER_INTERFACE_H__
#define __NVIDIA_INSTALLER_USER_INTERFACE_H__

#include "nvidia-installer.h"
#include "command-list.h"

int   ui_init                (Options*);
void  ui_set_title           (Options*, const char*, ...);
char *ui_get_input           (Options*, const char*, const char*, ...);
int   ui_display_license     (Options*, const char*);
void  ui_error               (Options*, const char*, ...);
void  ui_warn                (Options*, const char*, ...);
void  ui_message             (Options*, const char*, ...);
void  ui_log                 (Options*, const char*, ...);
void  ui_expert              (Options*, const char*, ...);
void  ui_command_output      (Options*, const char*, ...);
int   ui_approve_command_list(Options*, CommandList*,const char*, ...);
int   ui_yes_no              (Options*, const int, const char*, ...);
void  ui_status_begin        (Options*, const char*, const char*, ...);
void  ui_status_update       (Options*, const float, const char*, ...);
void  ui_status_end          (Options*, const char*, ...);
void  ui_close               (Options*);

#endif /* __NVIDIA_INSTALLER_USER_INTERFACE_H__ */
