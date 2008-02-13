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
 */

#ifndef __NVIDIA_INSTALLER_COMMAND_LIST_H__
#define __NVIDIA_INSTALLER_COMMAND_LIST_H__


/*
 * Command and CommandList structures - data types for describing what
 * operations to perform to do an install.  The semantics of the s0,
 * s1, and mode fields vary, depending upon the value of the cmd field
 * (see the constants below).
 */

typedef struct {
    int cmd;
    char *s0;
    char *s1;
    mode_t mode;
} Command;

typedef struct {
    int num;
    Command *cmds;
} CommandList;


/*
 * structure for storing a list of filenames.
 */

typedef struct {
    int num;
    char **filename;
} FileList;



/*
 * commands:
 *
 * INSTALL - install the file named is s0, giving it the name in s1;
 * assign s1 the permissions specified by mode
 *
 * BACKUP - move the file named in s0, storing it in the backup
 * directory and recording the data as appropriate.
 *
 * RUN - execute the string in s0
 *
 * SYMLINK - create a symbolic link named s0, pointing at the filename
 * specified in s1.
 */

#define INSTALL_CMD 1
#define BACKUP_CMD  2
#define RUN_CMD     3
#define SYMLINK_CMD 4
#define DELETE_CMD  5


CommandList *build_command_list(Options*, Package *);
void free_command_list(Options*, CommandList*);
int execute_command_list(Options*, CommandList*, const char*, const char*);

void find_conflicting_xfree86_libraries(const char*, FileList*);
void find_conflicting_opengl_libraries(const char*, FileList*);
void condense_file_list(FileList *l);

#endif /* __NVIDIA_INSTALLER_COMMAND_LIST_H__ */
