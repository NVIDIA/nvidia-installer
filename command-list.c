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
#include <utime.h>

#include "nvidia-installer.h"
#include "command-list.h"
#include "user-interface.h"
#include "backup.h"
#include "misc.h"
#include "files.h"
#include "kernel.h"
#include "manifest.h"
#include "conflicting-kernel-modules.h"


static void free_file_list(FileList* l);

static void find_conflicting_kernel_modules(Options *op, FileList *l);

static void find_existing_files(Package *p, FileList *l,
                                PackageEntryFileTypeList *file_type_list);

static void condense_file_list(Package *p, FileList *l);

static void add_command (CommandList *c, int cmd, ...);

static void add_file_to_list(const char*, const char*, FileList*);

static void append_to_rpm_file_list(Options *op, Command *c);

static ConflictingFileInfo *build_conflicting_file_list(Options *op, Package *p);
static void get_conflicting_file_info(const char *file, ConflictingFileInfo *cfi);

/*
 * find_conflicting_files() optionally takes an array of NoRecursionDirectory
 * entries to indicate which directories should not be recursively searched.
 */

typedef struct {
    int level;   /* max search depth: set to negative for unrestricted depth */
    char *name;  /* name to find: NULL to end the list */
} NoRecursionDirectory;

static void find_conflicting_files(Options *op,
                                   char *path,
                                   ConflictingFileInfo *files,
                                   FileList *l,
                                   const NoRecursionDirectory *skipdirs);


/*
 * Check if a path already exists in the path list, or is a subdirectory of
 * a path that exists in the path list, or is a symlink to or symlink target
 * of a directory that exists in the path list.
 */
static int path_already_exists(char ***paths, int count, const char *path)
{
    int i;

    for (i = 0; i < count; i++) {
        int is_subdir = FALSE;

        is_subdirectory((*paths)[i], path, &is_subdir);

        if (is_subdir) {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * Add a new path to the list of paths to search, provided that it exists
 * and is not redundant.
 * XXX we only check to see if the new directory is a subdirectory of any
 * existing directory, and not the other way around.
 */
static void add_search_path(char ***paths, int *count, const char *path)
{
    if (directory_exists(path) && !path_already_exists(paths, *count, path)) {
        *paths = nvrealloc(*paths, sizeof(char *) * (*count + 1));
        (*paths)[*count] = nvstrdup(path);
        (*count)++;
    }
}

/*
 * Given a path, add the subdirectories "/lib", "/lib32", and "/lib64" to the
 * list of paths to search.
 */
static void add_search_paths(char ***paths, int *count, const char *pathbase)
{
    int i;
    const char *subdirs[] = {
        "/lib",
        "/lib32",
        "/lib64",
    };

    for (i = 0; i < ARRAY_LEN(subdirs); i++) {
        char *path = nvstrcat(pathbase, subdirs[i], NULL);
        add_search_path(paths, count, path);
        nvfree(path);
    }
}

/*
 * Build the list of paths under which to search for conflicting files.
 * Returns the number of paths added to the search list.
 */
static int get_conflicting_search_paths(const Options *op, char ***paths)
{
    int ret = 0;

    *paths = NULL;

    add_search_paths(paths, &ret, DEFAULT_X_PREFIX);
    add_search_paths(paths, &ret, XORG7_DEFAULT_X_PREFIX);
    add_search_paths(paths, &ret, op->x_prefix);
    add_search_paths(paths, &ret, DEFAULT_OPENGL_PREFIX);
    add_search_paths(paths, &ret, op->opengl_prefix);
    add_search_path(paths, &ret, op->x_module_path);
    add_search_path(paths, &ret, op->x_library_path);

#if defined(NV_X86_64)
    if (op->compat32_chroot != NULL) {
        int i;
        char *subdirs[] = {
            DEFAULT_X_PREFIX,
            op->x_prefix,
            DEFAULT_OPENGL_PREFIX,
            op->opengl_prefix,
            op->compat32_prefix,
        };

        for (i = 0; i < ARRAY_LEN(subdirs); i++) {
            char *path = nvstrcat(op->compat32_chroot, "/", subdirs[i], NULL);
            add_search_paths(paths, &ret, path);
            nvfree(path);
        }
    }
#endif

    return ret;
}


/*
 * build_command_list() - construct a list of all the things to do
 * during the installation.  This consists of:
 *
 *   - backing and removing up conflicting files
 *   - installing all the installable files
 *   - create any needed symbolic links
 *   - executing any necessary commands
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
    PackageEntryFileTypeList installable_files;
    PackageEntryFileTypeList tmp_installable_files;
    char *tmp;

    get_installable_file_type_list(op, &installable_files);

    l = (FileList *) nvalloc(sizeof(FileList));
    c = (CommandList *) nvalloc(sizeof(CommandList));

    /* find any possibly conflicting modules and/or libraries */

    if (!op->no_kernel_modules) {
        find_conflicting_kernel_modules(op, l);
    }

    /* check the conflicting file list for any installed kernel modules */

    if (op->kernel_modules_only) {
        const char *kernel = get_kernel_name(op);

        if (dkms_module_installed(op, p->version, kernel)) {
            ui_error(op, "DKMS kernel modules with version %s are already "
                     "installed for the %s kernel.", p->version, kernel);
            free_file_list(l);
            free_command_list(op,c);
            return NULL;
        }

        for (i = 0; i < l->num; i++) {
            if (find_installed_file(op, l->filename[i])) {
                ui_error(op, "The file '%s' already exists as part of this "
                         "driver installation.", l->filename[i]);
                free_file_list(l);
                free_command_list(op, c);
                return NULL;
            }
        }

        /* XXX: If installing with --kernel-modules-only on a system that has
         * the kernel module sources already installed, but does NOT have built
         * kernel modules or DKMS modules, duplicate entries for the source
         * files will be added to the backup log, leading to error messages when
         * uninstalling the driver later leads to redundant attempts to delete
         * the files. */

    }
    
    if (!op->kernel_modules_only) {
        char **paths;
        int numpaths, i;
        ConflictingFileInfo *conflicting_files;

        /*
         * stop recursing into any "nvidia-cg-toolkit"
         * directory to prevent libGL.so.1 from being deleted
         * (see bug 843595).
         *
         * Also, do not recurse into "source" or "build" directories, pretty
         * much ever. This is because distros (for example, Fedora Core 21)
         * have started putting links to kernel source and build trees in
         * other locations, including /usr/lib/modules/`uname -r`/kernel.
         * (See bug 1646361).
         */
        static const NoRecursionDirectory skipdirs[] = {
            { -1, "source" },
            { -1, "build" },
            { -1, "nvidia-cg-toolkit" },
            {  0, NULL }
        };

        numpaths = get_conflicting_search_paths(op, &paths);

        ui_status_begin(op, "Searching for conflicting files:", "Searching");

        conflicting_files = build_conflicting_file_list(op, p);
        for (i = 0; i < numpaths; i++) {
            ui_status_update(op, (i + 1.0f) / numpaths, "Searching: %s", paths[i]);
            find_conflicting_files(op, paths[i], conflicting_files, l,
                                   skipdirs);
        }
        nvfree(conflicting_files);

        ui_status_end(op, "done.");
    }
    
    /*
     * find any existing files that clash with what we're going to
     * install
     */

    tmp_installable_files = installable_files;
    add_symlinks_to_file_type_list(&tmp_installable_files);

    find_existing_files(p, l, &tmp_installable_files);
    
    /* condense the file list */

    condense_file_list(p, l);
    
    /*
     * all of the files in the conflicting file list should be backed
     * up or deleted
     */

    cmd = op->no_backup ? DELETE_CMD : BACKUP_CMD;
    
    for (i = 0; i < l->num; i++)
        add_command(c, cmd, l->filename[i], NULL, 0);
    
    /* Add all the installable files to the list */
    
    for (i = 0; i < p->num_entries; i++) {
        /*
         * Install first, then run execstack. This sets the selinux context on
         * the installed file in the target filesystem, which is essentially
         * guaranteed to support selinux attributes if selinux is enabled.
         * However, the temporary filesystem containing the uninstalled file
         * may be on a filesystem that doesn't support selinux attributes,
         * such as NFS.
         *
         * Since execstack also changes the file on disk, we need to run it
         * after the file is installed but before we compute and log its CRC in
         * the backup log. To achieve that, we tell INTALL_CMD to run execstack
         * as a post-install step.
         *
         * See bugs 530083 and 611327
         */
        if (op->selinux_enabled &&
            (op->utils[EXECSTACK] != NULL) &&
            (p->entries[i].caps.is_shared_lib)) {
            tmp = nvstrcat(op->utils[EXECSTACK], " -c ",
                           p->entries[i].dst, NULL);
        } else {
            tmp = NULL;
        }

        if (installable_files.types[p->entries[i].type]) {
            add_command(c, INSTALL_CMD,
                        p->entries[i].file,
                        p->entries[i].dst,
                        tmp,
                        p->entries[i].mode);
        }

        nvfree(tmp);

        /*
         * For icons, update the mtime on the top-level directory of the icon
         * theme. This triggers desktop environments to invalidate their icon
         * cache.
         *
         * See https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html#implementation_notes
         */
        if (p->entries[i].type == FILE_TYPE_ICON) {
            add_command(c, TOUCH_CMD, op->icon_dir);
        }

        /*
         * delete any temporary generated files
         */

        if (p->entries[i].caps.is_temporary) {
            add_command(c, DELETE_CMD,
                        p->entries[i].file);
        }

        if (op->selinux_enabled &&
            (p->entries[i].caps.is_shared_lib)) {
            tmp = nvstrcat(op->utils[CHCON], " -t ", op->selinux_chcon_type,
                           " ", p->entries[i].dst, NULL);
            add_command(c, RUN_CMD, tmp);
            nvfree(tmp);
        }
    }


    /* create any needed symbolic links */
    
    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].caps.is_symlink) {
            add_command(c, SYMLINK_CMD, p->entries[i].dst,
                        p->entries[i].target);
        }
    }
    
    /*
     * if "--no-abi-note" was requested, scan for any OpenGL
     * libraries, and run the following command on them:
     *
     * objcopy --remove-section=.note.ABI-tag <filename> 2> /dev/null ; true
     */
    
    if (op->no_abi_note) {

        if (op->utils[OBJCOPY]) {
            for (i = 0; i < p->num_entries; i++) {
                if (p->entries[i].type == FILE_TYPE_OPENGL_LIB) {
                    tmp = nvstrcat(op->utils[OBJCOPY],
                            " --remove-section=.note.ABI-tag ",
                            p->entries[i].dst,
                            " 2> /dev/null ; true", NULL);
                    add_command(c, RUN_CMD, tmp);
                    nvfree(tmp);
                }
            }
        } else {
            ui_warn(op, "--no-abi-note option was specified but the system "
                    "utility `objcopy` (package 'binutils') was not found; this "
                    "operation will be skipped.");
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
    
    if (!op->no_kernel_modules && !op->skip_depmod) {
        tmp = nvstrcat(op->utils[DEPMOD], " -a ", op->kernel_name, NULL);
        add_command(c, RUN_CMD, tmp);
        nvfree(tmp);
    }

    /*
     * If systemd files were installed, run `systemctl daemon-reload`.
     */
    if (op->use_systemd == NV_OPTIONAL_BOOL_TRUE) {
        tmp = nvstrcat(op->utils[SYSTEMCTL], " daemon-reload", NULL);
        add_command(c, RUN_CMD, tmp);
        nvfree(tmp);
    }

    /* free the FileList */
    free_file_list(l);

    return c;

} /* build_command_list() */



/*
 * free_file_list() - free the file list
 */

static void free_file_list(FileList* l)
{
    int i;

    if (!l) return;

    for (i = 0; i < l->num; i++) {
        nvfree(l->filename[i]);
    }

    nvfree((char *) l->filename);
    nvfree((char *) l);
    
} /* free_file_list() */



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
        if (c->s2) free(c->s2);
    }

    if (cl->cmds) free(cl->cmds);
    
    free(cl);

} /* free_command_list() */

/*
 * execute_run_command() - execute a RUN_CMD from the command list.
 */

static inline int execute_run_command(Options *op, float percent, const char *cmd)
{
    int ret;
    char *data;

    ui_expert(op, "Executing: %s", cmd);
    ui_status_update(op, percent, "Executing: `%s` "
                     "(this may take a moment...)", cmd);
    ret = run_command(op, cmd, &data, TRUE, NULL, TRUE);
    if (ret != 0) {
        ui_error(op, "Failed to execute `%s`: %s", cmd, data);
        ret = continue_after_error(op, "Failed to execute `%s`", cmd);
        if (!ret) {
            nvfree(data);
            return FALSE;
        }
    }
    if (data) free(data);
    return TRUE;
} /* execute_run_command() */

/*
 * execute_command_list() - execute the commands in the command list.
 *
 * If any failure occurs, ask the user if they would like to continue.
 */

int execute_command_list(Options *op, CommandList *c,
                         const char *title, const char *msg)
{
    int i, ret;
    float percent;

    ui_status_begin(op, title, "%s", msg);

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
                /*
                 * perform post-install step before logging the backup
                 */
                if (c->cmds[i].s2 &&
                    !execute_run_command(op, percent, c->cmds[i].s2)) {
                    return FALSE;
                }

                log_install_file(op, c->cmds[i].s1);
                append_to_rpm_file_list(op, &c->cmds[i]);
            }
            break;
            
        case RUN_CMD:
            if (!execute_run_command(op, percent, c->cmds[i].s0)) {
                return FALSE;
            }
            break;

        case SYMLINK_CMD:
            ui_expert(op, "Creating symlink: %s -> %s",
                      c->cmds[i].s0, c->cmds[i].s1);
            ui_status_update(op, percent, "Creating symlink: %s",
                             c->cmds[i].s1);

            ret = install_symlink(op, c->cmds[i].s1, c->cmds[i].s0);

            if (!ret) {
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

        case TOUCH_CMD:
            ui_expert(op, "Updating mtime: %s", c->cmds[i].s0);
            ret = utime(c->cmds[i].s0, NULL);
            if (ret == -1) {
                ret = continue_after_error(op, "Cannot touch %s",
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



/*
 * find_conflicting_kernel_modules() - search for conflicting kernel
 * modules under the kernel module installation prefix.
 */

static void find_conflicting_kernel_modules(Options *op, FileList *l)
{
    int i = 0;
    ConflictingFileInfo *files;
    char *paths[3];
    char *tmp = get_kernel_name(op);
    char **filenames;

    /* Don't descend into the "build" or "source" directories; these won't
     * contain modules, and may be symlinks back to an actual source tree. */
    static const NoRecursionDirectory skipdirs[] = {
        { 1, "build" },
        { 1, "source" },
        { 0, NULL }
    };

    if (op->kernel_module_installation_path) {
        paths[i++] = op->kernel_module_installation_path;
    }

    if (tmp) {
        paths[i++] = nvstrcat("/lib/modules/", tmp, NULL);
    }

    paths[i] = NULL;

    /* Build the list of conflicting kernel modules */

    files = nvalloc((num_conflicting_kernel_modules + 1) * sizeof(files[0]));
    filenames = nvalloc(num_conflicting_kernel_modules * sizeof(filenames[0]));

    for (i = 0; i < num_conflicting_kernel_modules; i++) {
        filenames[i] = nvstrcat(conflicting_kernel_modules[i], ".ko", NULL);
        files[i].name = filenames[i];
        files[i].len = strlen(filenames[i]);
    }

    for (i = 0; paths[i]; i++) {
        /*
         * Recursively search for the conflicting kernel modules
         * relative to the current prefix.
         */

        find_conflicting_files(op, paths[i], files, l, skipdirs);
    }

    /* free any paths we nvstrcat()'d above  */

    for (i = 1; paths[i]; i++) {
        nvfree(paths[i]);
    }

    /* free the kernel module names */

    for (i = 0; i < num_conflicting_kernel_modules; i++) {
        nvfree(filenames[i]);
    }
    nvfree(filenames);
    nvfree(files);
}



/*
 * find_existing_files() - given a Package description, search for any
 * of the package files which already exist, and add them to the
 * FileList.
 */

static void find_existing_files(Package *p, FileList *l,
                                PackageEntryFileTypeList *file_type_list)
{
    int i;
    struct stat stat_buf;

    for (i = 0; i < p->num_entries; i++) {
        if (file_type_list->types[p->entries[i].type]) {
            if (lstat(p->entries[i].dst, &stat_buf) == 0) {
                add_file_to_list(NULL, p->entries[i].dst, l);
            }
        }
    }
} /* find_existing_files() */



/*
 * ignore_conflicting_file() - ignore (i.e., do not put it on the list
 * of files to backup) the conflicting file 'filename' if requiredString
 * is non-NULL and we cannot find the string in 'filename', or if the
 * file only conflicts on specific architectures, and the file's
 * architecture does not match.
 */

static int ignore_conflicting_file(Options *op,
                                   const char *filename,
                                   const ConflictingFileInfo info)
{
    int fd = -1;
    struct stat stat_buf;
    char *file = MAP_FAILED;
    int ret = FALSE;
    int i, len, size = 0;

    /* if no requiredString, do not check for the required string */

    if (!info.requiredString) return ret;

    ret = FALSE;

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

    size = stat_buf.st_size;

    if (!size) {
        goto cleanup;
    }

    if ((file = mmap(0, size, PROT_READ,
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

    len = strlen(info.requiredString);

    for (i = 0; (i + len) <= size; i++) {
        if ((strncmp(&file[i], info.requiredString, len) == 0) &&
            (((i + len) == size) || (file[i+len] == '\0'))) {
            ret = FALSE;
            break;
        }
    }

    /* fall through to cleanup */

 cleanup:

    if (file != MAP_FAILED) {
        munmap(file, size);
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
                                   FileList *l,
                                   const NoRecursionDirectory *skipdirs)
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
                /* end compare at len e.g. so "libGL." matches "libGL.so.1" */
                if (!strncmp(ent->fts_name, files[i].name, files[i].len) &&
                    !ignore_conflicting_file(op, ent->fts_path,
                                             files[i])) {
                    add_file_to_list(NULL, ent->fts_path, l);
                }
            }
            break;

        case FTS_DP:
        case FTS_D:
            if (op->no_recursion) {
                fts_set(fts, ent, FTS_SKIP);
            } else if (skipdirs) {
                const NoRecursionDirectory *dir;
                for (dir = skipdirs; dir->name; dir++) {
                    if ((dir->level < 0 || dir->level >= ent->fts_level) &&
                        strcmp(ent->fts_name, dir->name) == 0) {
                        fts_set(fts, ent, FTS_SKIP);
                    }
                }
            }
            break;

        default:
            /*
             * we only care about regular files, symbolic links
             * and directories; traversing the hierarchy logically
             * to simplify handling of paths with symbolic links
             * to directories, we only need to handle broken links
             * and, if recursion was not disabled, directories.
             */
            break;
        }
    }

    fts_close(fts);

} /* find_conflicting_files() */

void get_conflicting_file_info(const char *file, ConflictingFileInfo *cfi)
{
    char *c;

    cfi->name = file;

    /*
     * match the names of DSOs with "libfoo.so*" by stopping any comparisons
     * after ".so".
     */

    c = strstr(file, ".so.");
    if (c) {
        cfi->len = (c - file) + 3;
    } else {
        cfi->len = strlen(file);
    }

    /*
     * XXX avoid conflicting with libglx.so if it doesn't include the string
     * "glxModuleData" to avoid removing the wrong libglx.so (bug 489316)
     */

    if (strncmp(file, "libglx.so", cfi->len) == 0) {
        cfi->requiredString = "glxModuleData";
    } else {
        cfi->requiredString = NULL;
    }
}

ConflictingFileInfo *build_conflicting_file_list(Options *op, Package *p)
{
    ConflictingFileInfo *cfList = NULL;
    int index = 0;
    int i;

    // Allocate enough space for the whole file list, plus two extra files and
    // a NULL at the end.
    cfList = nvalloc((p->num_entries + 3) * sizeof(ConflictingFileInfo));

    for (i = 0; i < p->num_entries; i++) {
        PackageEntry *entry = &p->entries[i];
        if (entry->caps.is_shared_lib && entry->caps.is_conflicting) {
            get_conflicting_file_info(entry->name, &cfList[index++]);
        }
    }

    /* XXX always conflict with these files if OpenGL files will be installed
     * libGLwrapper.so: this library has an SONAME of libGL.so.1 (bug 74761) */
    if (!op->no_opengl_files) {
        get_conflicting_file_info("libGLwrapper.so", &cfList[index++]);
    }

    /* terminate the conflicting files list */
    cfList[index].name = NULL;

    return cfList;
}



/*
 * condense_file_list() - Take a FileList structure and delete any
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
    c->cmds[n].s2   = NULL;
    c->cmds[n].mode = 0x0;
    
    va_start(ap, cmd);

    switch (cmd) {
      case INSTALL_CMD:
        s = va_arg(ap, char *);
        c->cmds[n].s0 = nvstrdup(s);
        s = va_arg(ap, char *);
        c->cmds[n].s1 = nvstrdup(s);
        s = va_arg(ap, char *);
        c->cmds[n].s2 = nvstrdup(s);
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
      case TOUCH_CMD:
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
