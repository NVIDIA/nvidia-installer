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
#include <fcntl.h>
#include <sys/mman.h>
#include <fts.h>
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


static void find_conflicting_xfree86_libraries(Options *,
                                               const char *,
                                               FileList *);

static void find_conflicting_xfree86_libraries_fullpath(Options *op,
                                                        const char *,
                                                        FileList *l);

static void find_conflicting_opengl_libraries(Options *,
                                              const char *,
                                              FileList *);

static void find_conflicting_kernel_modules(Options *op,
                                            Package *p,
                                            FileList *l);

static void find_existing_files(Package *p, FileList *l, uint64_t);

static void condense_file_list(Package *p, FileList *l);

static void add_command (CommandList *c, int cmd, ...);

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
    uint64_t installable_files;
    char *tmp;

    installable_files = get_installable_file_mask(op);
    
    l = (FileList *) nvalloc(sizeof(FileList));
    c = (CommandList *) nvalloc(sizeof(CommandList));

    /* find any possibly conflicting libraries and/or modules */
    
    if (!op->kernel_module_only) {
        /*
         * Note that searching the various paths may produce duplicate
         * entries for conflicting files; this is OK because we will take
         * care of these duplicates in condense_file_list().
         */

        ui_status_begin(op, "Searching for conflicting X files:", "Searching");

        ui_status_update(op, 0.16f, DEFAULT_X_PREFIX);
        find_conflicting_xfree86_libraries(op, DEFAULT_X_PREFIX, l);
        ui_status_update(op, 0.32f, XORG7_DEFAULT_X_PREFIX);
        find_conflicting_xfree86_libraries(op, XORG7_DEFAULT_X_PREFIX, l);
        ui_status_update(op, 0.48f, op->x_prefix);
        find_conflicting_xfree86_libraries(op, op->x_prefix, l);

        ui_status_update(op, 0.64f, op->x_module_path);
        find_conflicting_xfree86_libraries_fullpath(op, op->x_module_path, l);
        ui_status_update(op, 0.80f, op->x_library_path);
        find_conflicting_xfree86_libraries_fullpath(op, op->x_library_path, l);

        ui_status_end(op, "done.");

        ui_status_begin(op, "Searching for conflicting OpenGL files:", "Searching");

        ui_status_update(op, 0.20f, DEFAULT_X_PREFIX);
        find_conflicting_opengl_libraries(op, DEFAULT_X_PREFIX, l);
        ui_status_update(op, 0.40f, op->x_prefix);
        find_conflicting_opengl_libraries(op, op->x_prefix, l);
        ui_status_update(op, 0.60f, DEFAULT_OPENGL_PREFIX);
        find_conflicting_opengl_libraries(op, DEFAULT_OPENGL_PREFIX, l);
        ui_status_update(op, 0.80f, op->opengl_prefix);
        find_conflicting_opengl_libraries(op, op->opengl_prefix, l);

        ui_status_end(op, "done.");

#if defined(NV_X86_64)
        if (op->compat32_chroot != NULL) {
            char *prefix;

            ui_status_begin(op, "Searching for conflicting compat32 files:", "Searching");

            prefix = nvstrcat(op->compat32_chroot, DEFAULT_X_PREFIX, NULL);
            ui_status_update(op, 0.20f, prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            prefix = nvstrcat(op->compat32_chroot, op->x_prefix, NULL);
            ui_status_update(op, 0.40f, prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            prefix = nvstrcat(op->compat32_chroot, DEFAULT_OPENGL_PREFIX, NULL);
            ui_status_update(op, 0.60f, prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            prefix = nvstrcat(op->compat32_chroot, op->compat32_prefix, NULL);
            ui_status_update(op, 0.80f, prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            ui_status_end(op, "done.");
        }
#endif /* NV_X86_64 */
    }
    
    if (!op->no_kernel_module)
        find_conflicting_kernel_modules(op, p, l);
    
    /*
     * find any existing files that clash with what we're going to
     * install
     */
    
    find_existing_files(p, l, installable_files | FILE_TYPE_SYMLINK);
    
    /* condense the file list */

    condense_file_list(p, l);
    
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
        if (op->selinux_enabled &&
            (op->utils[EXECSTACK] != NULL) &&
            ((p->entries[i].flags & FILE_TYPE_SHARED_LIB) ||
             (p->entries[i].flags & FILE_TYPE_XMODULE_SHARED_LIB))) {
            tmp = nvstrcat(op->utils[EXECSTACK], " -c ",
                           p->entries[i].file, NULL);
            add_command(c, RUN_CMD, tmp);
            nvfree(tmp);
        }

        if (p->entries[i].flags & installable_files) {
            add_command(c, INSTALL_CMD,
                        p->entries[i].file,
                        p->entries[i].dst,
                        p->entries[i].mode);
        }

        /*
         * delete the temporary libGL.la and .desktop files generated
         * based on templates earlier.
         */

        if ((p->entries[i].flags & FILE_TYPE_LIBGL_LA) ||
                (p->entries[i].flags & FILE_TYPE_DOT_DESKTOP)) {
            add_command(c, DELETE_CMD,
                        p->entries[i].file);
        }

        if (op->selinux_enabled &&
            ((p->entries[i].flags & FILE_TYPE_SHARED_LIB) ||
             (p->entries[i].flags & FILE_TYPE_XMODULE_SHARED_LIB))) {
            tmp = nvstrcat(op->utils[CHCON], " -t ", op->selinux_chcon_type,
                           " ", p->entries[i].dst, NULL);
            add_command(c, RUN_CMD, tmp);
            nvfree(tmp);
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

typedef struct {
    const char *name;
    int len;

    /*
     * if requiredString is non-NULL, then a file must have this
     * string in order to be considered a conflicting file; we use
     * this to only consider "libglx.*" files conflicts if they have
     * the string "glxModuleData".
     */

    const char *requiredString;
} ConflictingFileInfo;

static void find_conflicting_files(Options *op,
                                   char *path,
                                   ConflictingFileInfo *files,
                                   FileList *l);

static void find_conflicting_libraries(Options *op,
                                       const char *prefix,
                                       ConflictingFileInfo *libs,
                                       FileList *l);

static ConflictingFileInfo __xfree86_libs[] = {
    { "libGLcore.",     10, /* strlen("libGLcore.") */     NULL            },
    { "libGL.",         6,  /* strlen("libGL.") */         NULL            },
    { "libGLwrapper.",  13, /* strlen("libGLwrapper.") */  NULL            },
    { "libglx.",        7,  /* strlen("libglx.") */        "glxModuleData" },
    { "libXvMCNVIDIA",  13, /* strlen("libXvMCNVIDIA") */  NULL            },
    { "libnvidia-cfg.", 14, /* strlen("libnvidia-cfg.") */ NULL            },
    { "nvidia_drv.",    11, /* strlen("nvidia_drv.") */    NULL            },
    { "libcuda.",       8,  /* strlen("libcuda.") */       NULL            },
    { NULL,             0,                                 NULL            }
};

/*
 * find_conflicting_xfree86_libraries() - search for conflicting
 * libraries under the XFree86 installation prefix, for all possible
 * libdirs.
 */

static void find_conflicting_xfree86_libraries(Options *op,
                                               const char *xprefix,
                                               FileList *l)
{
    find_conflicting_libraries(op, xprefix, __xfree86_libs, l);

} /* find_conflicting_xfree86_libraries() */



/*
 * find_conflicting_xfree86_libraries_fullpath() - same as
 * find_conflicting_xfree86_libraries, but bypasses the
 * find_conflicting_libraries step, which appends "lib", "lib64", and
 * "lib32" to the path name.  Use this when you have the fullpath that
 * you want searched.
 */

static void find_conflicting_xfree86_libraries_fullpath(Options *op,
                                                        const char *path,
                                                        FileList *l)
{
    find_conflicting_files(op, (char *) path, __xfree86_libs, l);
    
} /* find_conflicting_xfree86_libraries_fullpath() */



static ConflictingFileInfo __opengl_libs[] = {
    { "libGLcore.",     10, /* strlen("libGLcore.") */     NULL },
    { "libGL.",         6,  /* strlen("libGL.") */         NULL },
    { "libnvidia-tls.", 14, /* strlen("libnvidia-tls.") */ NULL },
    { "libGLwrapper.",  13, /* strlen("libGLwrapper.") */  NULL },
    { "libcuda.",       8,  /* strlen("libcuda.") */       NULL },
    { NULL, 0 }
};

/*
 * find_conflicting_opengl_libraries() - search for conflicting
 * libraries under the OpenGL installation prefix, for all possible
 * libdirs.
 */

static void find_conflicting_opengl_libraries(Options *op,
                                              const char *glprefix,
                                              FileList *l)
{
    find_conflicting_libraries(op, glprefix, __opengl_libs, l);

} /* find_conflicting_opengl_libraries() */



/*
 * find_conflicting_kernel_modules() - search for conflicting kernel
 * modules under the kernel module installation prefix.
 */

static void find_conflicting_kernel_modules(Options *op,
                                            Package *p, FileList *l)
{
    int i, n = 0;
    ConflictingFileInfo files[2];
    char *paths[3];
    char *tmp = get_kernel_name(op);

    files[1].name = NULL;
    files[1].len = 0;
    paths[0] = op->kernel_module_installation_path;

    if (tmp) {
        paths[1] = nvstrcat("/lib/modules/", tmp, NULL);
        paths[2] = NULL;
    } else {
        paths[1] = NULL;
    }
    
    for (i = 0; paths[i]; i++) {
        for (n = 0; p->bad_module_filenames[n]; n++) {
            /*
             * Recursively search for this conflicting kernel module
             * relative to the current prefix.
             */
            files[0].name = p->bad_module_filenames[n];
            files[0].len = strlen(files[0].name);

            find_conflicting_files(op, paths[i], files, l);
        }
    }

    /* free any paths we nvstrcat()'d above  */

    for (i = 1; paths[i]; i++) {
        nvfree(paths[i]);
    }

} /* find_conflicting_kernel_modules() */



/*
 * find_existing_files() - given a Package description, search for any
 * of the package files which already exist, and add them to the
 * FileList.
 */

static void find_existing_files(Package *p, FileList *l, uint64_t flag)
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
 * ignore_conflicting_file() - ignore (i.e., do not put it on the list
 * of files to backup) the conflicting file 'filename' if requiredString
 * is non-NULL and we cannot find the string in 'filename'.
 */

static int ignore_conflicting_file(Options *op,
                                   const char *filename,
                                   const char *requiredString)
{
    int fd = -1;
    struct stat stat_buf;
    char *file = MAP_FAILED;
    int ret = FALSE;
    int i, len;

    /* if no requiredString, do not ignore this conflicting file */

    if (!requiredString) return FALSE;

    if ((fd = open(filename, O_RDONLY)) == -1) {
        ui_error(op, "Unable to open '%s' for reading (%s)",
                 filename, strerror(errno));
        goto cleanup;
    }

    if (fstat(fd, &stat_buf) == -1) {
        ui_error(op, "Unable to determine size of '%s' (%s)",
                 filename, strerror(errno));
        goto cleanup;
    }

    if ((file = mmap(0, stat_buf.st_size, PROT_READ,
                     MAP_FILE | MAP_SHARED, fd, 0)) == MAP_FAILED) {
        ui_error(op, "Unable to map file '%s' for reading (%s)",
                 filename, strerror(errno));
        goto cleanup;
    }

    /*
     * if the requiredString is not found within the mapping of file,
     * ignore the conflicting file; when scanning for the string,
     * ensure that the string is either at the end of the file, or
     * followed by '\0'.
     */

    ret = TRUE;

    len = strlen(requiredString);

    for (i = 0; (i + len) <= stat_buf.st_size; i++) {
        if ((strncmp(&file[i], requiredString, len) == 0) &&
            (((i + len) == stat_buf.st_size) || (file[i+len] == '\0'))) {
            ret = FALSE;
            break;
        }
    }

    /* fall through to cleanup */

 cleanup:

    if (file != MAP_FAILED) {
        munmap(file, stat_buf.st_size);
    }

    if (fd != -1) {
        close(fd);
    }

    return ret;

} /* ignore_conflicting_file() */




/*
 * find_conflicting_files() - search for any conflicting
 * files in all the specified paths within the hierarchy under
 * the given prefix.
 */

static void find_conflicting_files(Options *op,
                                   char *path,
                                   ConflictingFileInfo *files,
                                   FileList *l)
{
    int i;
    char *paths[2];
    FTS *fts;
    FTSENT *ent;

    paths[0] = path; /* search root */
    paths[1] = NULL;

    fts = fts_open(paths, FTS_LOGICAL | FTS_NOSTAT, NULL);
    if (!fts) return;

    while ((ent = fts_read(fts)) != NULL) {
        switch (ent->fts_info) {
        case FTS_F:
        case FTS_SLNONE:
            for (i = 0; files[i].name; i++) {
                if (!strncmp(ent->fts_name, files[i].name, files[i].len) &&
                    !ignore_conflicting_file(op, ent->fts_path,
                                             files[i].requiredString)) {
                    add_file_to_list(NULL, ent->fts_path, l);
                }
            }
            break;

        case FTS_DP:
        case FTS_D:
            if (op->no_recursion)
                fts_set(fts, ent, FTS_SKIP);
            break;

        default:
            /*
             * we only care about regular files, symbolic links
             * and directories; traversing the hierarchy logically
             * to simplify handling of paths with symbolic links
             * to directories, we only need to handle broken links
             * and, if recursion was disabled, directories.
             */
            break;
        }
    }

    fts_close(fts);

} /* find_conflicting_files() */




/*
 * find_conflicting_libraries() - search for any conflicting
 * libraries in all relevant libdirs within the hierarchy under
 * the given prefix.
 */

static void find_conflicting_libraries(Options *op,
                                       const char *prefix,
                                       ConflictingFileInfo *files,
                                       FileList *l)
{
    int i, j;
    char *paths[4];

    paths[0] = nvstrcat(prefix, "/", "lib", NULL);
    paths[1] = nvstrcat(prefix, "/", "lib64", NULL);
    paths[2] = nvstrcat(prefix, "/", "lib32", NULL);
    paths[3] = NULL;

    for (i = 0; paths[i]; i++) {
        for (j = 0; (j < 3) && paths[i]; j++) {
            /*
             * XXX Check if any one of the 'paths' entries really
             * is a symbolic link pointing to one of the other
             * entries. The logic could be made smarter, since it's
             * unlikely that ../lib32 would be a symbolic link to
             * ../lib64 or vice versa.
             */
            if (!paths[j] || (i == j)) continue;

            if (is_symbolic_link_to(paths[i], paths[j])) {
                ui_expert(op, "The conflicting library search path "
                          "'%s' is a symbolic link to the library "
                          "search path '%s'; skipping '%s'.",
                          paths[i], paths[j], paths[i]);
                free(paths[i]); paths[i] = NULL;
            }
        }

        if (paths[i]) find_conflicting_files(op, paths[i], files, l);
    }

    for (i = 0; i < 3; i++)
        nvfree(paths[i]);

} /* find_conflicting_libraries() */


/*
 * condense_file_list() - Take a FileList stucture and delete any
 * duplicate entries in the list.  This is a pretty brain dead
 * brute-force algorithm.
 */

static void condense_file_list(Package *p, FileList *l)
{
    char **s = NULL;
    int n = 0, i, j, keep;

    struct stat stat_buf, *stat_bufs;

    /* allocate enough space in our temporary 'stat' array */

    if (l->num) {
        stat_bufs  = nvalloc(sizeof(struct stat) * l->num);
    } else {
        stat_bufs  = NULL;
    }
    
    /*
     * walk through our original (uncondensed) list of files and move
     * unique files to a new (condensed) list.  For each file in the
     * original list, get the filesystem information for the file, and
     * then compare that to the filesystem information for all the
     * files in the new list.  If the file from the original list does
     * not match any file in the new list, add it to the new list.
     */

    for (i = 0; i < l->num; i++) {
        keep = TRUE;

        if (lstat(l->filename[i], &stat_buf) == -1)
            continue;

        /*
         * check if this file is in the package we're trying to
         * install; we don't want to remove files that are in the
         * package; symlinks may have tricked us into looking for
         * conflicting files inside our unpacked .run file.
         */

        for (j = 0; j < p->num_entries; j++) {
            if ((p->entries[j].device == stat_buf.st_dev) &&
                (p->entries[j].inode == stat_buf.st_ino)) {
                keep = FALSE;
                break;
            }
        }

        for (j = 0; keep && (j < n); j++) {

            /*
             * determine if the two files are the same by comparing
             * device and inode
             */

            if ((stat_buf.st_dev == stat_bufs[j].st_dev) &&
                (stat_buf.st_ino == stat_bufs[j].st_ino)) {
                keep = FALSE;
                break;
            }
        }

        if (keep) {
            s = (char **) nvrealloc(s, sizeof(char *) * (n + 1));
            s[n] = nvstrdup(l->filename[i]);
            stat_bufs[n] = stat_buf;
            n++;
        }
    }
    
    if (stat_bufs) nvfree((void *)stat_bufs);

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
