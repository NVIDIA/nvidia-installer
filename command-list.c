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
 *
 *
 * command_list.c - this source file contains functions for building
 * and executing a commandlist (the list of operations to perform to
 * actually do an install).
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "nvidia-installer.h"
#include "command-list.h"
#include "user-interface.h"
#include "backup.h"
#include "misc.h"
#include "files.h"
#include "kernel.h"



static void find_existing_files(Package *p, FileList *l, unsigned int);
static void find_conflicting_kernel_modules(Options *op,
                                            Package *p, FileList *l);

static void add_command (CommandList *c, int cmd, ...);

static void find_matches (const char*, const char*, FileList*, const int);
static void add_file_to_list(const char*, const char*, FileList*);

static void append_to_rpm_file_list(Options *op, Command *c);


/*
 * build_command_list() - construct a list of all the things to do
 * during the installation.  This consists of:
 *
 *   - backing and removing up conflicting files
 *   - installing all the installable files
 *   - create any needed symbolic links
 *   - executing any nessary commands
 *   - running `ldconfig` and `depmod -aq`
 *
 * If we are only installing the kernel module, then we trim back all
 * the stuff that we don't need to do.
 */

CommandList *build_command_list(Options *op, Package *p)
{
    FileList *l;
    CommandList *c;
    int i, cmd;
    unsigned int installable_files;
    char *tmp;

    installable_files = get_installable_file_mask(op);
    
    l = (FileList *) nvalloc(sizeof(FileList));
    c = (CommandList *) nvalloc(sizeof(CommandList));

    /* find any possible conflicting libraries */
    
    if (!op->kernel_module_only) {

        find_conflicting_xfree86_libraries
            (DEFAULT_XFREE86_INSTALLATION_PREFIX,l);
    
        if (strcmp(DEFAULT_XFREE86_INSTALLATION_PREFIX,
                   op->xfree86_prefix) != 0)
            find_conflicting_xfree86_libraries(op->xfree86_prefix, l);
    
        find_conflicting_opengl_libraries
            (DEFAULT_OPENGL_INSTALLATION_PREFIX,l);
    
        if (strcmp(DEFAULT_OPENGL_INSTALLATION_PREFIX, op->opengl_prefix) != 0)
            find_conflicting_opengl_libraries(op->opengl_prefix, l);
    }
    
    find_conflicting_kernel_modules(op, p, l);
    
    /*
     * find any existing files that clash with what we're going to
     * install
     */
    
    find_existing_files(p, l, installable_files | FILE_TYPE_SYMLINK);
    
    /* condense the file list */

    condense_file_list(l);
    
    /* check the conflicting file list for any installed files */

    if (op->kernel_module_only) {
        for (i = 0; i < l->num; i++) {
            if (find_installed_file(op, l->filename[i])) {
                ui_error(op, "The file '%s' already exists as part of this "
                         "driver installation.", l->filename[i]);
                return NULL;
            }
        }
    }
    
    /*
     * all of the files in the conflicting file list should be backed
     * up or deleted
     */

    cmd = op->no_backup ? DELETE_CMD : BACKUP_CMD;
    
    for (i = 0; i < l->num; i++)
        add_command(c, cmd, l->filename[i], NULL, 0);
    
    /* Add all the installable files to the list */
    
    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].flags & installable_files) {
            add_command(c, INSTALL_CMD,
                        p->entries[i].file,
                        p->entries[i].dst,
                        p->entries[i].mode);
        }
    }


    /* create any needed symbolic links */
    
    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].flags & FILE_TYPE_SYMLINK) {
            add_command(c, SYMLINK_CMD, p->entries[i].dst,
                        p->entries[i].target);
        }
    }
    
    /* find any commands we should run */

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].flags & FILE_TYPE_KERNEL_MODULE_CMD) {
            add_command(c, RUN_CMD, p->entries[i].file, NULL, 0);
        }
    }

    /*
     * if "--no-abi-note" was requested, scan for any OpenGL
     * libraries, and run the following command on them:
     *
     * objcopy --remove-section=.note.ABI-tag <filename> 2> /dev/null ; true
     */
    
    if (op->no_abi_note) {
        for (i = 0; i < p->num_entries; i++) {
            if (p->entries[i].flags & FILE_TYPE_OPENGL_LIB) {
                tmp = nvstrcat(op->utils[OBJCOPY],
                               " --remove-section=.note.ABI-tag ",
                               p->entries[i].dst,
                               " 2> /dev/null ; true", NULL);
                add_command(c, RUN_CMD, tmp);
                nvfree(tmp);
            }
        }
    }
    
    /* finally, run ldconfig and depmod */

    add_command(c, RUN_CMD, op->utils[LDCONFIG]);

    /*
     * If we are building for a non-running kernel, specify that
     * kernel name on the depmod commandline line (see depmod(8)
     * manpage for details).  Patch provided by Nigel Spowage
     * <Nigel.Spowage@energis.com>
     */
    
    if ( op->kernel_name && op->kernel_name[0] ) {
    	tmp = nvstrcat(op->utils[DEPMOD], " -aq ", op->kernel_name, NULL);
    } else {
    	tmp = nvstrcat(op->utils[DEPMOD], " -aq", NULL);
    }
    add_command(c, RUN_CMD, tmp);
    nvfree(tmp);
    
    /*
     * if on SuSE or United Linux, also do `/usr/bin/chrc.config
     * SCRIPT_3D no`
     */

    if (((op->distro == SUSE) || (op->distro == UNITED_LINUX)) &&
        (access("/usr/bin/chrc.config", X_OK) == 0)) {
        add_command(c, RUN_CMD, "/usr/bin/chrc.config SCRIPT_3D no");
    }

    /* free the FileList */

    for (i = 0; i < l->num; i++) free(l->filename[i]);
    free(l->filename);
    free(l);

    return c;

} /* build_command_list() */



/*
 * free_command_list() - free the specified commandlist
 */

void free_command_list(Options *op, CommandList *cl)
{
    int i;
    Command *c;

    if (!cl) return;

    for (i = 0; i < cl->num; i++) {
        c = &cl->cmds[i];
        if (c->s0) free(c->s0);
        if (c->s1) free(c->s1);
    }

    if (cl->cmds) free(cl->cmds);
    
    free(cl);

} /* free_command_list() */



/*
 * execute_command_list() - execute the commands in the command list.
 *
 * If any failure occurs, ask the user if they would like to continue.
 */

int execute_command_list(Options *op, CommandList *c,
                         const char *title, const char *msg)
{
    int i, ret;
    char *data;
    float percent;

    ui_status_begin(op, title, msg);

    for (i = 0; i < c->num; i++) {

        percent = (float) i / (float) c->num;

        switch (c->cmds[i].cmd) {
                
        case INSTALL_CMD:
            ui_expert(op, "Installing: %s --> %s",
                      c->cmds[i].s0, c->cmds[i].s1);
            ui_status_update(op, percent, "Installing: %s", c->cmds[i].s1);
            
            ret = install_file(op, c->cmds[i].s0, c->cmds[i].s1,
                               c->cmds[i].mode);
            if (!ret) {
                ret = continue_after_error(op, "Cannot install %s",
                                           c->cmds[i].s1);
                if (!ret) return FALSE;
            } else {
                log_install_file(op, c->cmds[i].s1);
                append_to_rpm_file_list(op, &c->cmds[i]);
            }
            break;
            
        case RUN_CMD:
            ui_expert(op, "Executing: %s", c->cmds[i].s0);
            ui_status_update(op, percent, "Executing: `%s` "
                             "(this may take a moment...)", c->cmds[i].s0);
            ret = run_command(op, c->cmds[i].s0, &data, TRUE, 0, TRUE);
            if (ret != 0) {
                ui_error(op, "Failed to execute `%s`: %s",
                         c->cmds[i].s0, data);
                ret = continue_after_error(op, "Failed to execute `%s`",
                                           c->cmds[i].s0);
                if (!ret) return FALSE;
            }
            if (data) free(data);
            break;

        case SYMLINK_CMD:
            ui_expert(op, "Creating symlink: %s -> %s",
                      c->cmds[i].s0, c->cmds[i].s1);
            ui_status_update(op, percent, "Creating symlink: %s",
                             c->cmds[i].s1);

            ret = symlink(c->cmds[i].s1, c->cmds[i].s0);
            if (ret == -1) {
                ret = continue_after_error(op, "Cannot create symlink %s (%s)",
                                           c->cmds[i].s0, strerror(errno));
                if (!ret) return FALSE;
            } else {
                log_create_symlink(op, c->cmds[i].s0, c->cmds[i].s1);
            }
            break;

        case BACKUP_CMD:
            ui_expert(op, "Backing up: %s", c->cmds[i].s0);
            ui_status_update(op, percent, "Backing up: %s", c->cmds[i].s0);

            ret = do_backup(op, c->cmds[i].s0);
            if (!ret) {
                ret = continue_after_error(op, "Cannot backup %s",
                                           c->cmds[i].s0);
                if (!ret) return FALSE;
            }
            break;

        case DELETE_CMD:
            ui_expert(op, "Deleting: %s", c->cmds[i].s0);
            ret = unlink(c->cmds[i].s0);
            if (ret == -1) {
                ret = continue_after_error(op, "Cannot delete %s",
                                           c->cmds[i].s0);
                if (!ret) return FALSE;
            }
            break;

        default:
            /* XXX should never get here */
            return FALSE;
            break;
        }
    }

    ui_status_end(op, "done.");

    return TRUE;
    
} /* execute_command_list() */


/*
 ***************************************************************************
 * local static routines
 ***************************************************************************
 */

static const char *__libdirs[] = { "lib", "lib64", NULL };

/*
 * find_conflicting_xfree86_libraries() - search for conflicting
 * libraries under the XFree86 installation prefix, for all possible
 * libdirs.
 */

void find_conflicting_xfree86_libraries(const char *xprefix, FileList *l)
{
    char *s;
    const char *libdir;
    int i;

    for (i = 0; __libdirs[i]; i++) {
        
        libdir = __libdirs[i];

        /*
         * [xprefix]/[libdir]/libGL.*
         * [xprefix]/[libdir]/libGLcore.*
         * [xprefix]/[libdir]/libXvMCNVIDIA*
         * [xprefix]/[libdir]/libGLwrapper.*
         */

        s = nvstrcat(xprefix, "/", libdir, NULL);
        find_matches(s, "libGL.", l, FALSE);
        find_matches(s, "libGLcore.", l, FALSE);
        find_matches(s, "libXvMCNVIDIA", l, FALSE);
        find_matches(s, "libGLwrapper.", l, FALSE);
        free(s);
    
        /*
         * [xprefix]/[libdir]/tls/libGL.*
         * [xprefix]/[libdir]/tls/libGLcore.*
         * [xprefix]/[libdir]/tls/libXvMCNVIDIA*
         * [xprefix]/[libdir]/tls/libGLwrapper.*
         */
        
        s = nvstrcat(xprefix, "/", libdir, "/tls", NULL);
        find_matches(s, "libGL.", l, FALSE);
        find_matches(s, "libGLcore.", l, FALSE);
        find_matches(s, "libXvMCNVIDIA", l, FALSE);
        find_matches(s, "libGLwrapper.", l, FALSE);
        free(s);
    
        /*
         * [xprefix]/[libdir]/modules/extensions/libGLcore.*
         * [xprefix]/[libdir]/modules/extensions/libglx.*
         * [xprefix]/[libdir]/modules/extensions/libGLwrapper.*
         */
        
        s = nvstrcat(xprefix, "/", libdir, "/modules/extensions", NULL);
        find_matches(s, "libglx.", l, FALSE);
        find_matches(s, "libGLcore.", l, FALSE);
        find_matches(s, "libGLwrapper.", l, FALSE);
        free(s);
    }
    
} /* find_conflicting_xfree86_libraries() */



/*
 * find_conflicting_opengl_libraries() - search for conflicting
 * libraries under the OpenGL installation prefix, for all possible
 * libdirs.
 */

void find_conflicting_opengl_libraries(const char *glprefix, FileList *l)
{
    char *s;
    const char *libdir;
    int i;

    for (i = 0; __libdirs[i]; i++) {
        
        libdir = __libdirs[i];

        /*
         * [glprefix]/[libdir]/libGL.*
         * [glprefix]/[libdir]/libGLcore.*
         * [glprefix]/[libdir]/libGLwrapper.*
         * [glprefix]/[libdir]/tls/libGL.*
         * [glprefix]/[libdir]/tls/libGLcore.*
         * [glprefix]/[libdir]/tls/libGLwrapper.*
         */
    
        s = nvstrcat(glprefix, "/", libdir, NULL);
        find_matches(s, "libGL.", l, FALSE);
        find_matches(s, "libGLcore.", l, FALSE);
        find_matches(s, "libGLwrapper.", l, FALSE);
        find_matches(s, "libnvidia-tls.", l, FALSE);
        free(s);

        s = nvstrcat(glprefix, "/", libdir, "/tls", NULL);
        find_matches(s, "libGL.", l, FALSE);
        find_matches(s, "libGLcore.", l, FALSE);
        find_matches(s, "libGLwrapper.", l, FALSE);
        find_matches(s, "libnvidia-tls.", l, FALSE);
        free(s);
    }
    
} /* find_conflicting_opengl_libraries() */



/*
 * find_conflicting_kernel_modules() - search for conflicting kernel
 * modules under the kernel module installation prefix.
 *
 * XXX rather than use a fixed list of prefixes, maybe we should scan for the
 * kernel module name anywhere under /lib/modules/`uname -r`/ ?
 */

static void find_conflicting_kernel_modules(Options *op,
                                            Package *p, FileList *l)
{
    int i, n = 0;
    char *prefixes[5];
    char *tmp = get_kernel_name(op);

    prefixes[0] = op->kernel_module_installation_path;

    if (tmp) {
        prefixes[1] = nvstrcat("/lib/modules/", tmp, "/kernel/drivers/video", NULL);
        prefixes[2] = nvstrcat("/lib/modules/", tmp, "/kernel/drivers/char", NULL);
        prefixes[3] = nvstrcat("/lib/modules/", tmp, "/video", NULL);
        prefixes[4] = NULL;
    } else {
        prefixes[1] = NULL;
    }
    
    /* look for all the conflicting module names in all the possible prefixes */
    
    for (i = 0; prefixes[i]; i++) {
        for (n = 0; p->bad_module_filenames[n]; n++) {
            find_matches(prefixes[i], p->bad_module_filenames[n], l, TRUE);
        }
    }

    /* free any prefixes we nvstrcat()ed above  */

    for (i = 1; prefixes[i]; i++) {
        nvfree(prefixes[i]);
    }

} /* find_conflicting_kernel_modules() */



/*
 * find_existing_files() - given a Package description, search for any
 * of the package files which already exist, and add them to the
 * FileList.
 */

static void find_existing_files(Package *p, FileList *l, unsigned int flag)
{
    int i;
    struct stat stat_buf;

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].flags & flag) {
            if (lstat(p->entries[i].dst, &stat_buf) == 0) {
                add_file_to_list(NULL, p->entries[i].dst, l);
            }
        }
    }
} /* find_existing_files() */



/*
 * condense_file_list() - Take a FileList stucture and delete any
 * duplicate entries in the list.  This is a pretty brain dead
 * brute-force algorithm.
 */

void condense_file_list(FileList *l)
{
    char **s = NULL;
    int n = 0, i, j, match;

    for (i = 0; i < l->num; i++) {
        match = FALSE;
        for (j = 0; j < n; j++) {
            if (strcmp(l->filename[i], s[j]) == 0) {
                match = TRUE;
                break;
            }
        }
        
        if (!match) {
            s = (char **) nvrealloc(s, sizeof(char *) * (n + 1));
            s[n] = nvstrdup(l->filename[i]);
            n++;
        }
    }
    
    for (i = 0; i < l->num; i++) free(l->filename[i]);
    free(l->filename);

    l->filename = s;
    l->num = n;

} /* condense_file_list() */



/*
 * add_command() - grow the commandlist and append the new command,
 * parsing the variable argument list.
 */

static void add_command(CommandList *c, int cmd, ...)
{
    int n = c->num;
    char *s;
    va_list ap;
    
    c->cmds = (Command *) nvrealloc(c->cmds, sizeof(Command) * (n + 1));
 
    c->cmds[n].cmd  = cmd;
    c->cmds[n].s0   = NULL;
    c->cmds[n].s1   = NULL;
    c->cmds[n].mode = 0x0;
    
    va_start(ap, cmd);

    switch (cmd) {
      case INSTALL_CMD:
        s = va_arg(ap, char *);
        c->cmds[n].s0 = nvstrdup(s);
        s = va_arg(ap, char *);
        c->cmds[n].s1 = nvstrdup(s);
        c->cmds[n].mode = va_arg(ap, mode_t);
        break;
      case BACKUP_CMD:
        s = va_arg(ap, char *);
        c->cmds[n].s0 = nvstrdup(s);
        break;
      case RUN_CMD:
        s = va_arg(ap, char *);
        c->cmds[n].s0 = nvstrdup(s);
        break;
      case SYMLINK_CMD:
        s = va_arg(ap, char *);
        c->cmds[n].s0 = nvstrdup(s);
        s = va_arg(ap, char *);
        c->cmds[n].s1 = nvstrdup(s);
        break;
      case DELETE_CMD:
        s = va_arg(ap, char *);
        c->cmds[n].s0 = nvstrdup(s);
        break;
      default:
        break;
    }

    va_end(ap);

    c->num++;

} /* add_command() */



/*
 * find_matches() - given a directory, a filename, and an existing
 * FileList data structure, open the specified directory, and look for
 * any entries that match the filename.
 *
 * If the parameter 'exact' is TRUE, then the filenames must match
 * exactly.  If 'exact' is FALSE, then only the beginning of the
 * directory entry name must match the filename in question.
 *
 * This could alternatively be implemented using glob(3).
 */

static void find_matches(const char *directory, const char *filename,
                         FileList *l, const int exact)
{
    struct stat stat_buf;
    struct dirent *ent;
    int len;
    DIR *dir;
    
    if (lstat(directory, &stat_buf) == -1) return;
    if (S_ISDIR(stat_buf.st_mode) == 0) return;
    if ((dir = opendir(directory)) == NULL) return;

    len = strlen(filename);

    while ((ent = readdir(dir)) != NULL) {
        if (exact) {
            if (strcmp(ent->d_name, filename) == 0) {
                add_file_to_list(directory, ent->d_name, l);
            }
        } else {
            if (strncmp(ent->d_name, filename, len) == 0) {
                add_file_to_list(directory, ent->d_name, l);
            }
        }
    }
    
    closedir (dir); 
    
} /* find_matches() */



/*
 * add_file_to_list() - concatenate the given directory and filename,
 * appending to the FileList structure.  If the 'directory' parameter
 * is NULL, then just append the filename to the FileList.
 */

static void add_file_to_list(const char *directory,
                             const char *filename, FileList *l)
{
    int len, n = l->num;
    
    l->filename = (char **) nvrealloc(l->filename, sizeof(char *) * (n + 1));

    if (directory) {
        len = strlen(filename) + strlen(directory) + 2;
        l->filename[n] = (char *) nvalloc(len);
        snprintf(l->filename[n], len, "%s/%s", directory, filename);
    } else {
        l->filename[n] = nvstrdup(filename);
    }
    l->num++;

} /* add_file_to_list() */


static void append_to_rpm_file_list(Options *op, Command *c)
{
    FILE *file;

    if (!op->rpm_file_list) return;

    file = fopen(op->rpm_file_list, "a");
    fprintf(file, "%%attr (%04o, root, root) %s\n", c->mode, c->s1);
    fclose(file);
}
