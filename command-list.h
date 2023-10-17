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
 */

#ifndef __NVIDIA_INSTALLER_COMMAND_LIST_H__
#define __NVIDIA_INSTALLER_COMMAND_LIST_H__

/*
 * List of commands to process. Each command has a description which can
 * be accessed by command list clients; the actual command structure which
 * gets processed is opaque.
 */

typedef struct {
    int num;
    char **descriptions;
    struct __command *cmds;
} CommandList;


/*
 * structure for storing a list of filenames.
 */

typedef struct {
    int num;
    char **filename;
} FileList;


CommandList *build_command_list(Options*, Package *);
void free_command_list(Options*, CommandList*);
int execute_command_list(Options*, CommandList*, const char*, const char*);

#endif /* __NVIDIA_INSTALLER_COMMAND_LIST_H__ */
