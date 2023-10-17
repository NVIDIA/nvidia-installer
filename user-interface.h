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

enum {
    CONTINUE_CHOICE = 0,
    ABORT_CHOICE,
    NUM_CONTINUE_ABORT_CHOICES /* Must be the last one */
};
extern const char * const CONTINUE_ABORT_CHOICES[];

int   ui_init                (Options*);
void  ui_set_title           (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
char *ui_get_input           (Options*, const char*, const char*, ...) NV_ATTRIBUTE_PRINTF(3, 4);
void  ui_error               (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_warn                (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_message             (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_log                 (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_expert              (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_command_output      (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
int   ui_approve_command_list(Options*, CommandList*,const char*, ...) NV_ATTRIBUTE_PRINTF(3, 4);
int   ui_yes_no              (Options*, const int, const char*, ...)   NV_ATTRIBUTE_PRINTF(3, 4);
int   ui_multiple_choice     (Options *, const char * const*, int, int,
                              const char *, ...)                       NV_ATTRIBUTE_PRINTF(5, 6);
int   ui_paged_prompt        (Options *, const char *, const char *,
                              const char *, const char * const *, int, int);
void  ui_status_begin        (Options*, const char*, const char*, ...) NV_ATTRIBUTE_PRINTF(3, 4);
void  ui_status_update       (Options*, const float, const char*, ...) NV_ATTRIBUTE_PRINTF(3, 4);
void  ui_indeterminate_begin (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_indeterminate_end   (Options*);
void  ui_status_end          (Options*, const char*, ...)              NV_ATTRIBUTE_PRINTF(2, 3);
void  ui_close               (Options*);

/* Useful when different message types may be suitable in different contexts */
typedef void ui_message_func (Options*, const char*, ...) NV_ATTRIBUTE_PRINTF(2, 3);

#endif /* __NVIDIA_INSTALLER_USER_INTERFACE_H__ */
