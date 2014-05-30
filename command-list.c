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

#include "nvidia-installer.h"
#include "command-list.h"
#include "user-interface.h"
#include "backup.h"
#include "misc.h"
#include "files.h"
#include "kernel.h"
#include "manifest.h"


static void free_file_list(FileList* l);

static void find_conflicting_xfree86_libraries(Options *,
                                               const char *,
                                               FileList *);

static void find_conflicting_xfree86_libraries_fullpath(Options *op,
                                                        char *,
                                                        FileList *l);

static void find_conflicting_opengl_libraries(Options *,
                                              const char *,
                                              FileList *);

static void find_conflicting_kernel_modules(Options *op,
                                            Package *p,
                                            FileList *l);

static void find_existing_files(Package *p, FileList *l,
                                PackageEntryFileTypeList *file_type_list);

static void condense_file_list(Package *p, FileList *l);

static void add_command (CommandList *c, int cmd, ...);

static void add_file_to_list(const char*, const char*, FileList*);

static void append_to_rpm_file_list(Options *op, Command *c);

/*
 * find_conflicting_files() optionally takes an array of NoRecursionDirectory
 * entries to indicate which directories should not be recursively searched.
 */

typedef struct {
    int level;   /* max search depth: set to negative for unrestricted depth */
    char *name;  /* name to find: NULL to end the list */
} NoRecursionDirectory;


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
    PackageEntryFileTypeList installable_files;
    PackageEntryFileTypeList tmp_installable_files;
    char *tmp;

    get_installable_file_type_list(op, &installable_files);

    l = (FileList *) nvalloc(sizeof(FileList));
    c = (CommandList *) nvalloc(sizeof(CommandList));

    /* find any possibly conflicting modules and/or libraries */

    if (!op->no_kernel_module || op->dkms)
        find_conflicting_kernel_modules(op, p, l);

    /* check the conflicting file list for any installed kernel modules */

    if (op->kernel_module_only) {
        if (dkms_module_installed(op, p->version)) {
            ui_error(op, "A DKMS kernel module with version %s is already "
                     "installed.", p->version);
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

        /* XXX: If installing with --kernel-module-only on a system that has
         * the kernel module sources already installed, but does NOT have a
         * built kernel module or DKMS module, duplicate entries for the source
         * files will be added to the backup log, leading to error messages
         * when uninstalling the driver later leads to redundant attempts to
         * delete the files. */

    }
    
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
        ui_status_update(op, 0.48f, "%s", op->x_prefix);
        find_conflicting_xfree86_libraries(op, op->x_prefix, l);

        ui_status_update(op, 0.64f, "%s", op->x_module_path);
        find_conflicting_xfree86_libraries_fullpath(op, op->x_module_path, l);
        ui_status_update(op, 0.80f, "%s", op->x_library_path);
        find_conflicting_xfree86_libraries_fullpath(op, op->x_library_path, l);

        ui_status_end(op, "done.");

        ui_status_begin(op, "Searching for conflicting OpenGL files:", "Searching");

        ui_status_update(op, 0.20f, DEFAULT_X_PREFIX);
        find_conflicting_opengl_libraries(op, DEFAULT_X_PREFIX, l);
        ui_status_update(op, 0.40f, "%s", op->x_prefix);
        find_conflicting_opengl_libraries(op, op->x_prefix, l);
        ui_status_update(op, 0.60f, DEFAULT_OPENGL_PREFIX);
        find_conflicting_opengl_libraries(op, DEFAULT_OPENGL_PREFIX, l);
        ui_status_update(op, 0.80f, "%s", op->opengl_prefix);
        find_conflicting_opengl_libraries(op, op->opengl_prefix, l);

        ui_status_end(op, "done.");

#if defined(NV_X86_64)
        if (op->compat32_chroot != NULL) {
            char *prefix;

            ui_status_begin(op, "Searching for conflicting compat32 files:", "Searching");

            prefix = nvstrcat(op->compat32_chroot, DEFAULT_X_PREFIX, NULL);
            ui_status_update(op, 0.20f, "%s", prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            prefix = nvstrcat(op->compat32_chroot, op->x_prefix, NULL);
            ui_status_update(op, 0.40f, "%s", prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            prefix = nvstrcat(op->compat32_chroot, DEFAULT_OPENGL_PREFIX, NULL);
            ui_status_update(op, 0.60f, "%s", prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            prefix = nvstrcat(op->compat32_chroot, op->compat32_prefix, NULL);
            ui_status_update(op, 0.80f, "%s", prefix);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);

            ui_status_end(op, "done.");
        }
#endif /* NV_X86_64 */
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
            /* if it's a NEWSYM and the file already exists, don't add a command
             * for it */
            if (p->entries[i].type == FILE_TYPE_XMODULE_NEWSYM) {
                struct stat buf;
                if(!stat(p->entries[i].dst, &buf) || errno != ENOENT) {
                    ui_expert(op, "Not creating a symlink from %s to %s "
                                  "because a file already exists at that path "
                                  "or the path is inaccessible.",
                                  p->entries[i].dst, p->entries[i].target);
                    continue;
                }
            }

            add_command(c, SYMLINK_CMD, p->entries[i].dst,
                        p->entries[i].target);
        }
    }
    
    /* find any commands we should run */

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].type == FILE_TYPE_KERNEL_MODULE_CMD) {
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
    
    if (!op->no_kernel_module) {
        tmp = nvstrcat(op->utils[DEPMOD], " -aq ", op->kernel_name, NULL);
        add_command(c, RUN_CMD, tmp);
        nvfree(tmp);
    }
    
    /*
     * if on SuSE or United Linux, also do `/usr/bin/chrc.config
     * SCRIPT_3D no`
     */

    if (((op->distro == SUSE) || (op->distro == UNITED_LINUX)) &&
        (access("/usr/bin/chrc.config", X_OK) == 0)) {
        add_command(c, RUN_CMD, "/usr/bin/chrc.config SCRIPT_3D no");
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
    ret = run_command(op, cmd, &data, TRUE, 0, TRUE);
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
 * CONFLICT_ARCH_ALL: file always conflicts, regardless of arch
 * CONFLICT_ARCH_32: file only conflicts if its arch is 32 bit
 * CONFLICT_ARCH_64: file only conflicts if its arch is 64 bit
 */

typedef enum {
    CONFLICT_ARCH_ALL,
    CONFLICT_ARCH_32,
    CONFLICT_ARCH_64,
} ConflictArch;


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

    ConflictArch conflictArch;
} ConflictingFileInfo;

static void find_conflicting_files(Options *op,
                                   char *path,
                                   ConflictingFileInfo *files,
                                   FileList *l,
                                   const NoRecursionDirectory *skipdirs);

static void find_conflicting_libraries(Options *op,
                                       const char *prefix,
                                       ConflictingFileInfo *libs,
                                       FileList *l);

static ConflictingFileInfo __xfree86_opengl_libs[] = {

    /* Conflicting OpenGL libraries */

    { "libnvidia-glcore.",   17, /* strlen("libnvidia-glcore.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libGL.",              6,  /* strlen("libGL.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libGLwrapper.",       13, /* strlen("libGLwrapper.") */
                             NULL,            CONFLICT_ARCH_ALL        },

    /* Conflicting X extensions */

    { "libglx.",             7,  /* strlen("libglx.") */
                             "glxModuleData", CONFLICT_ARCH_ALL        },
    { "libglamoregl.",       13, /* strlen("libglamoregl.") */
                             NULL,            CONFLICT_ARCH_ALL        },

    /* Conflicting EGL libraries: */

    { "libEGL.",             7,  /* strlen("libEGL.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libGLESv1_CM.",       13, /* strlen("libGLESv1_CM." */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libGLESv2.",          10, /* strlen("libGLESv2." */
                             NULL,            CONFLICT_ARCH_ALL        },
    { NULL,                  0,     NULL,     CONFLICT_ARCH_ALL        }
};

static ConflictingFileInfo __xfree86_non_opengl_libs[] = {
    { "nvidia_drv.",         11, /* strlen("nvidia_drv.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libvdpau_nvidia.",    16, /* strlen("libvdpau_nvidia.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-cfg.",      14, /* strlen("libnvidia-cfg.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libcuda.",            8,  /* strlen("libcuda.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-compiler.", 19, /* strlen("libnvidia-compiler.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvcuvid.",         11, /* strlen("libnvcuvid.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-ml.",       13, /* strlen("libnvidia-ml.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-encode.",   17, /* strlen("libnvidia-encode.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-vgx.",      14, /* strlen("libnvidia-vgx.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-ifr.",      14, /* strlen("libnvidia-ifr.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-vgxcfg.",   17, /* strlen("libnvidia-vgxcfg.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { NULL,                  0,     NULL,     CONFLICT_ARCH_ALL        }
};

static ConflictingFileInfo __xfree86_vdpau_wrapper_libs[] = {
    { "libvdpau.",           9,  /* strlen("libvdpau.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libvdpau_trace.",     15, /* strlen("libvdpau_trace.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { NULL,                  0,     NULL,     CONFLICT_ARCH_ALL        }
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
    if (!op->no_opengl_files) {
        find_conflicting_libraries(op, xprefix, __xfree86_opengl_libs, l);
    }
    find_conflicting_libraries(op, xprefix, __xfree86_non_opengl_libs, l);
    if (op->install_vdpau_wrapper == NV_OPTIONAL_BOOL_TRUE) {
        find_conflicting_libraries(op, xprefix, __xfree86_vdpau_wrapper_libs, l);
    }

} /* find_conflicting_xfree86_libraries() */



/*
 * find_conflicting_xfree86_libraries_fullpath() - same as
 * find_conflicting_xfree86_libraries, but bypasses the
 * find_conflicting_libraries step, which appends "lib", "lib64", and
 * "lib32" to the path name.  Use this when you have the fullpath that
 * you want searched.
 */

static void find_conflicting_xfree86_libraries_fullpath(Options *op,
                                                        char *path,
                                                        FileList *l)
{
    if (!op->no_opengl_files) {
        find_conflicting_files(op, path, __xfree86_opengl_libs, l, NULL);
    }
    find_conflicting_files(op, path, __xfree86_non_opengl_libs, l, NULL);
    if (op->install_vdpau_wrapper == NV_OPTIONAL_BOOL_TRUE) {
        find_conflicting_files(op, path, __xfree86_vdpau_wrapper_libs, l, NULL);
    }

} /* find_conflicting_xfree86_libraries_fullpath() */



static ConflictingFileInfo __opengl_libs[] = {
    { "libnvidia-glcore.",   17, /* strlen("libnvidia-glcore.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libGL.",              6,  /* strlen("libGL.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-tls.",      14, /* strlen("libnvidia-tls.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libGLwrapper.",       13, /* strlen("libGLwrapper.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { NULL,                  0,     NULL,     CONFLICT_ARCH_ALL        }
};

static ConflictingFileInfo __non_opengl_libs[] = {
    { "libnvidia-cfg.",      14, /* strlen("libnvidia-cfg.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libcuda.",            8,  /* strlen("libcuda.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-compiler.", 19, /* strlen("libnvidia-compiler.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { "libnvidia-ml.",       13, /* strlen("libnvidia-ml.") */
                             NULL,            CONFLICT_ARCH_ALL        },
    { NULL,                  0,     NULL,     CONFLICT_ARCH_ALL        }
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
    if (!op->no_opengl_files) {
        find_conflicting_libraries(op, glprefix, __opengl_libs, l);
    }
    find_conflicting_libraries(op, glprefix, __non_opengl_libs, l);

} /* find_conflicting_opengl_libraries() */



/*
 * find_conflicting_kernel_modules() - search for conflicting kernel
 * modules under the kernel module installation prefix.
 */

static void find_conflicting_kernel_modules(Options *op,
                                            Package *p, FileList *l)
{
    int i = 0, n = 0;
    ConflictingFileInfo files[2];
    char *paths[3];
    char *tmp = get_kernel_name(op);

    /* Don't descend into the "build" or "source" directories; these won't
     * contain modules, and may be symlinks back to an actual source tree. */
    static const NoRecursionDirectory skipdirs[] = {
        { 1, "build" },
        { 1, "source" },
        { 0, NULL }
    };

    memset(files, 0, sizeof(files));
    files[1].name = NULL;
    files[1].len = 0;
    if (op->kernel_module_installation_path) {
        paths[i++] = op->kernel_module_installation_path;
    }

    if (tmp) {
        paths[i++] = nvstrcat("/lib/modules/", tmp, NULL);
    }

    paths[i] = NULL;
    
    for (i = 0; paths[i]; i++) {
        for (n = 0; p->bad_module_filenames[n]; n++) {
            /*
             * Recursively search for this conflicting kernel module
             * relative to the current prefix.
             */
            files[0].name = p->bad_module_filenames[n];
            files[0].len = strlen(files[0].name);

            find_conflicting_files(op, paths[i], files, l, skipdirs);
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
    int i, len;

    /* check if the file only conflicts on certain architectures */

    if (info.conflictArch != CONFLICT_ARCH_ALL) {
        ElfFileType elftype = get_elf_architecture(filename);

        switch (elftype) {
            case ELF_ARCHITECTURE_32:
                ret = info.conflictArch != CONFLICT_ARCH_32;
                break;
            case ELF_ARCHITECTURE_64:
                ret = info.conflictArch != CONFLICT_ARCH_64;
                break;
            default:
                /*
                 * XXX ignore symlinks with indeterminate architectures: their
                 * targets may have already been deleted, and they'll be reused
                 * or replaced as part of the installation, anyway.
                 */
                if (lstat(filename, &stat_buf) == -1) {
                    ui_warn(op, "Unable to stat '%s'.", filename);
                } else if ((stat_buf.st_mode & S_IFLNK) == S_IFLNK) {
                    ret = TRUE;
                } else {
                    ui_warn(op, "Unable to determine the architecture of the "
                            "file '%s', which has an architecture-specific "
                            "conflict.", filename);
                }
                break;
        }
    }

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

    if (!stat_buf.st_size) {
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

    len = strlen(info.requiredString);

    for (i = 0; (i + len) <= stat_buf.st_size; i++) {
        if ((strncmp(&file[i], info.requiredString, len) == 0) &&
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

    /*
     * stop recursing into any "nvidia-cg-toolkit"
     * directory to prevent libGL.so.1 from being deleted
     * (see bug 843595).
     */
    static const NoRecursionDirectory skipdirs[] = {
        { -1, "nvidia-cg-toolkit" },
        { 0, NULL }
    };

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

        if (paths[i]) find_conflicting_files(op, paths[i], files, l, skipdirs);
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
