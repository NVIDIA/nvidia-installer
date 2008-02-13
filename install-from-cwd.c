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
 * install_from_cwd.c -
 */


#include <stdio.h>
#include <stdlib.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "kernel.h"
#include "command-list.h"
#include "backup.h"
#include "files.h"
#include "misc.h"
#include "sanity.h"

/* local prototypes */


static Package *parse_manifest(Options *op);
static int install_kernel_module(Options *op,  Package *p);


/*
 * install_from_cwd() - perform an installation from the current
 * working directory; we first ensure that we have a .manifest file in
 * the cwd, and the files listed in the manifest exist and have
 * correct checksums (to ensure the package hasn't been corrupted, not
 * for anything security related).
 *
 * Second, make sure the user accepts the license.
 *
 * Then, optionally override the OpenGL and XFree86 installation
 * prefixes.
 *
 * Determine the currently installed NVIDIA driver version (if any).
 *
 */

int install_from_cwd(Options *op)
{
    Package *p;
    CommandList *c;
    const char *msg;
    int ret;

    static const char edit_your_xf86config[] =
        "Please update your XF86Config or xorg.conf file as "
        "appropriate; see the file /usr/share/doc/"
        "NVIDIA_GLX-1.0/README.txt for details.";

    static const char suse_edit_your_xf86config[] =
        "On SuSE Linux/United Linux please use SaX2 now to enable "
        "the NVIDIA driver.";

    /*
     * validate the manifest file in the cwd, and process it, building
     * a Package struct
     */
    
    if ((p = parse_manifest(op)) == NULL) goto failed;
    
    ui_set_title(op, "%s (%d.%d-%d)", p->description,
                 p->major, p->minor, p->patch);

    /* 
     * warn the user if "legacy" GPUs are installed in this system
     * and if no supported GPU is found, at all.
     */

    check_for_nvidia_graphics_devices(op, p);

    /* check that we are not running any X server */

    if (!check_for_running_x(op)) goto failed;

    /* make sure the kernel module is unloaded */
    
    if (!check_for_unloaded_kernel_module(op, p)) goto failed;
    
    /* ask the user to accept the license */
    
    if (!get_license_acceptance(op)) return FALSE;
    
    /*
     * determine the current NVIDIA version (if any); ask the user if
     * they really want to overwrite the existing installation
     */

    if (!check_for_existing_driver(op, p)) return FALSE;

    /* attempt to build a kernel module for the target kernel */

    if (!op->no_kernel_module) {
        if (!install_kernel_module(op, p)) goto failed;
    } else {
        ui_warn(op, "You specified the '--no-kernel-module' command line "
                "option, nvidia-installer will not install a kernel "
                "module as part of this driver installation, and it will "
                "not remove existing NVIDIA kernel modules not part of "
                "an earlier NVIDIA driver installation.  Please ensure "
                "that an NVIDIA kernel module matching this driver version "
                "is installed seperately.");
    }
    
    /*
     * if we are only installing the kernel module, then remove
     * everything else from the package; otherwise do some
     * OpenGL-specific stuff
     */

    if (op->kernel_module_only) {
        remove_non_kernel_module_files_from_package(op, p);
    } else {

        /* ask for the XFree86 and OpenGL installation prefixes. */
    
        if (!get_prefixes(op)) goto failed;

        /* ask if we should install the OpenGL header files */

        should_install_opengl_headers(op, p);

        /*
         * select the appropriate TLS class, modifying the package as
         * necessary.
         */
    
        select_tls_class(op, p);

        /*
         * if the package contains any libGL.la or .desktop files,
         * process them (perform some search and replacing so
         * that they reflect the correct installation path, etc)
         * and add them to the package list (files to be installed).
         */
        
        process_libGL_la_files(op, p);
        process_dot_desktop_files(op, p);

#if defined(NV_X86_64)
        /*
         * ask if we should install the 32bit compatibility files on
         * this machine.
         */

        should_install_compat32_files(op, p);
#endif /* NV_X86_64 */
    }

    /*
     * now that we have the installation prefixes, build the
     * destination for each file to be installed
     */
    
    if (!set_destinations(op, p)) goto failed;
    
    /*
     * uninstall the existing driver; this needs to be done before
     * building the command list.
     *
     * XXX if we uninstall now, then build the command list, and
     * then ask the user if they really want to execute the
     * command list, if the user decides not to execute the
     * command list, they'll be left with no driver installed.
     */

    if (!op->kernel_module_only) {
        if (!uninstall_existing_driver(op, FALSE)) goto failed;
    }

    /* build a list of operations to execute to do the install */
    
    if ((c = build_command_list(op, p)) == NULL) goto failed;

    /* call the ui to get approval for the list of commands */
    
    if (!ui_approve_command_list(op, c, "%s", p->description)) return FALSE;
    
    /* initialize the backup log file */

    if (!op->kernel_module_only) {
        if (!init_backup(op, p)) goto failed;
    }

    /* execute the command list */

    if (!do_install(op, p, c)) goto failed;
  
    /*
     * check that everything is installed properly (post-install
     * sanity check)
     */

    check_installed_files_from_package(op, p);

    if (!check_sysvipc(op)) goto failed;
    if (!check_runtime_configuration(op, p)) goto failed;
    
    /* done */

    if (op->kernel_module_only) {
        ui_message(op, "Installation of the kernel module for the %s "
                   "(version %s) is now complete.",
                   p->description, p->version_string);
    } else {
        
        /* ask the user if they would like to run nvidia-xconfig */
        
        ret = ui_yes_no(op, op->run_nvidia_xconfig,
                        "Would you like to run the nvidia-xconfig utility "
                        "to automatically update your X configuration file "
                        "so that the NVIDIA X driver will be used when you "
                        "restart X?  Any pre-existing X configuration "
                        "file will be backed up.");
        
        if (ret) {
            ret = run_nvidia_xconfig(op);
        }
        
        if (ret) {
            ui_message(op, "Your X configuration file has been successfully "
                       "updated.  Installation of the %s (version: %s) is now "
                       "complete.", p->description, p->version_string);
        } else {
            
            if ((op->distro == SUSE) || (op->distro == UNITED_LINUX)) {
                msg = suse_edit_your_xf86config;
            } else {
                msg = edit_your_xf86config;
            }
            
            ui_message(op, "Installation of the %s (version: %s) is now "
                       "complete.  %s", p->description,
                       p->version_string, msg);
        }
    }
    
    return TRUE;
    
 failed:

    if (op->logging) {
        ui_error(op, "Installation has failed.  Please see the file '%s' "
                 "for details.  You may find suggestions on fixing "
                 "installation problems in the README available on the "
                 "Linux driver download page at www.nvidia.com.",
                 op->log_file_name);
    } else {
        ui_error(op, "Installation has failed.  You may find suggestions "
                 "on fixing installation problems in the README available "
                 "on the Linux driver download page at www.nvidia.com.");
    }
    
    return FALSE;

} /* install_from_cwd() */



/*
 * install_kernel_module() - attempt to build and install a kernel
 * module for the running kernel; we first check if a prebuilt kernel
 * interface file exists. If yes, we try to link it into the final
 * kernel module, else we try to build one from source.
 *
 * If we succeed in building a kernel module, we attempt to load it
 * into the host kernel and add it to the list of files to install if
 * the load attempt succeeds.
 */

static int install_kernel_module(Options *op,  Package *p)
{
    /* determine where to install the kernel module */
    
    if (!determine_kernel_module_installation_path(op)) return FALSE;

    /* check '/proc/sys/kernel/modprobe' */

    if (!check_proc_modprobe_path(op)) return FALSE;

    /*
     * do nvchooser-style logic to decide if we have a prebuilt kernel
     * module for their kernel
     *
     * XXX One could make the argument that we should not actually do
     * the building/linking now, but just add this to the list of
     * operations and do it when we execute the operation list.  I
     * think it's better to make sure we have a kernel module early on
     * -- a common problem for users will be not having a prebuilt
     * kernel interface for their kernel, and not having the kernel
     * headers installed, so it's better to catch that earlier on.
     */
    
    if (find_precompiled_kernel_interface(op, p)) {

        /*
         * we have a prebuild kernel interface, so now link the kernel
         * interface with the binary portion of the kernel module.
         *
         * XXX if linking fails, maybe we should fall through and
         * attempt to build the kernel module?  No, if linking fails,
         * then there is something pretty seriously wrong... better to
         * abort.
         */
        
        if (!link_kernel_module(op, p)) return FALSE;

    } else {
        /*
         * make sure the required development tools are present on
         * this system before attempting to verify the compiler and
         * trying to build a custom kernel interface.
         */
        if (!check_development_tools(op, p)) return FALSE;

        /*
         * make sure that the selected or default system compiler
         * is compatible with the target kernel; the user may choose
         * to override the check.
         */
        if (!check_cc_version(op, p)) return FALSE;

        /*
         * we do not have a prebuilt kernel interface; thus we'll need
         * to compile the kernel interface, so determine where the
         * kernel source files are.
         */
        
        if (!determine_kernel_source_path(op, p)) return FALSE;
    
        /* and now, build the kernel interface */
        
        if (!build_kernel_module(op, p)) return FALSE;
    }

    /*
     * if we got this far, we have a complete kernel module; test it
     * to be sure it's OK
     */
    
    if (!test_kernel_module(op, p)) return FALSE;
    
    /* add the kernel module to the list of things to install */
    
    if (!add_kernel_module_to_package(op, p)) return FALSE;
    
    return TRUE;
}


/*
 * add_this_kernel() - build a precompiled kernel interface for the
 * running kernel, and repackage the .run file to include the new
 * precompiled kernel interface.
 */

int add_this_kernel(Options *op)
{
    Package *p;
    
    /* parse the manifest */

    if ((p = parse_manifest(op)) == NULL) goto failed;

    /* find the kernel header files */

    if (!determine_kernel_source_path(op, p)) goto failed;

    /* build the precompiled kernel interface */

    if (!build_kernel_interface(op, p)) goto failed;
    
    /* pack the precompiled kernel interface */

    if (!pack_precompiled_kernel_interface(op, p)) goto failed;
    
    return TRUE;

 failed:

    ui_error(op, "Unable to add a precompiled kernel interface for the "
             "running kernel.");
    
    return FALSE;
    
} /* add_this_kernel() */



/*
 * parse_manifest() - open and read the .manifest file in the current
 * directory.
 *
 * The first nine lines of the .manifest file are:
 *
 *   - a description string
 *   - a version string of the form "major.minor-patch"
 *   - the kernel module file name
 *   - the kernel interface file name
 *   - the kernel module name (what `rmmod` and `modprobe` should use)
 *   - a whitespace-separated list of module names that should be
 *     removed before installing a new kernel module
 *   - a whitespace-separated list of kernel module filenames that
 *     should be uninstalled before installing a new kernel module
 *   - kernel module build directory
 *   - directory containing precompiled kernel interfaces
 *
 * The rest of the manifest file is file entries.  A file entry is a
 * whitespace-separated list containing:
 *
 *   - a filename (relative to the cwd)
 *   - an octal value describing the permissions
 *   - a flag describing the file type
 *   - certain file types have an architecture
 *   - certain file types have a second flag
 *   - certain file types will have a path
 *   - symbolic links will name the target of the link
 */

static Package *parse_manifest (Options *op)
{
    char *buf, *c, *flag , *tmpstr;
    int done, n, line;
    int fd, len = 0;
    struct stat stat_buf;
    Package *p;
    char *manifest = MAP_FAILED, *ptr;
    
    p = (Package *) nvalloc(sizeof (Package));
    
    /* open the manifest file */

    if ((fd = open(".manifest", O_RDONLY)) == -1) {
        ui_error(op, "No package found for installation.  Please run "
                 "this utility with the '--help' option for usage "
                 "information.");
        goto fail;
    }
    
    if (fstat(fd, &stat_buf) == -1) goto cannot_open;
    
    len = stat_buf.st_size;

    manifest = mmap(0, len, PROT_READ, MAP_FILE|MAP_SHARED, fd, 0);
    if (manifest == MAP_FAILED) goto cannot_open;
    
    /* the first line is the description */

    line = 1;
    p->description = get_next_line(manifest, &ptr, manifest, len);
    if (!p->description) goto invalid_manifest_file;
    
    /* the second line is the version */
    
    line++;
    p->version_string = get_next_line(ptr, &ptr, manifest, len);
    if (!p->version_string) goto invalid_manifest_file;
    if (!nvid_version(p->version_string, &p->major, &p->minor, &p->patch))
        goto invalid_manifest_file;
    
    /* new third line is the kernel interface filename */

    line++;
    p->kernel_interface_filename = get_next_line(ptr, &ptr, manifest, len);
    if (!p->kernel_interface_filename) goto invalid_manifest_file;

    /* the fourth line is the kernel module name */

    line++;
    p->kernel_module_name = get_next_line(ptr, &ptr, manifest, len);
    if (!p->kernel_module_name) goto invalid_manifest_file;

    /*
     * the fifth line is a whitespace-separated list of kernel modules
     * to be unloaded before installing the new kernel module
     */

    line++;
    tmpstr = get_next_line(ptr, &ptr, manifest, len);
    if (!tmpstr) goto invalid_manifest_file;

    p->bad_modules = NULL;
    c = tmpstr;
    n = 0;
    
    do {
        n++;
        p->bad_modules = (char **)
            nvrealloc(p->bad_modules, n * sizeof(char *));
        p->bad_modules[n-1] = read_next_word(c, &c);
    } while (p->bad_modules[n-1]);
    
    /*
     * the sixth line is a whitespace-separated list of kernel module
     * filenames to be uninstalled before installing the new kernel
     * module
     */

    line++;
    tmpstr = get_next_line(ptr, &ptr, manifest, len);
    if (!tmpstr) goto invalid_manifest_file;

    p->bad_module_filenames = NULL;
    c = tmpstr;
    n = 0;
    
    do {
        n++;
        p->bad_module_filenames = (char **)
            nvrealloc(p->bad_module_filenames, n * sizeof(char *));
        p->bad_module_filenames[n-1] = read_next_word(c, &c);
    } while (p->bad_module_filenames[n-1]);
    
    /* the seventh line is the kernel module build directory */

    line++;
    p->kernel_module_build_directory = get_next_line(ptr, &ptr, manifest, len);
    if (!p->kernel_module_build_directory) goto invalid_manifest_file;
    remove_trailing_slashes(p->kernel_module_build_directory);

    /*
     * the eigth line is the directory containing precompiled kernel
     * interfaces
     */

    line++;
    p->precompiled_kernel_interface_directory =
        get_next_line(ptr, &ptr, manifest, len);
    if (!p->precompiled_kernel_interface_directory)
        goto invalid_manifest_file;
    remove_trailing_slashes(p->precompiled_kernel_interface_directory);

    /* the rest of the file is file entries */

    done = FALSE;
    line++;
    
    do {
        buf = get_next_line(ptr, &ptr, manifest, len);
        if ((!buf) || (buf[0] == '\0')) {
            done = TRUE;
        } else {
            
            p->num_entries++;
            n = p->num_entries - 1;
            
            /* extend the PackageEntry array */

            if ((p->entries = (PackageEntry *) nvrealloc
                 (p->entries, sizeof(PackageEntry) *
                  p->num_entries)) == NULL) {
                ui_error(op, "Memory allocation failure.");
                goto fail;
            }
            
            /* read the file name and permissions */

            c = buf;

            p->entries[n].file = read_next_word(buf, &c);
            tmpstr = read_next_word(c, &c);
            
            /* if any of them were NULL, fail */

            if (!p->entries[n].file || !tmpstr) goto invalid_manifest_file;
            
            /* translate the mode string into an octal mode */
            
            if (!mode_string_to_mode(op, tmpstr, &p->entries[n].mode)) {
                goto invalid_manifest_file;
            }
            free(tmpstr);
            
            /* every file has a type field */

            p->entries[n].flags = 0x0;

            flag = read_next_word(c, &c);
            if (!flag) goto invalid_manifest_file;

            if (strcmp(flag, "KERNEL_MODULE_SRC") == 0)
                p->entries[n].flags |= FILE_TYPE_KERNEL_MODULE_SRC;
            else if (strcmp(flag, "KERNEL_MODULE_CMD") == 0)
                p->entries[n].flags |= FILE_TYPE_KERNEL_MODULE_CMD;
            else if (strcmp(flag, "OPENGL_HEADER") == 0)
                p->entries[n].flags |= FILE_TYPE_OPENGL_HEADER;
            else if (strcmp(flag, "OPENGL_LIB") == 0)
                p->entries[n].flags |= FILE_TYPE_OPENGL_LIB;
            else if (strcmp(flag, "LIBGL_LA") == 0)
                p->entries[n].flags |= FILE_TYPE_LIBGL_LA;
            else if (strcmp(flag, "XLIB_STATIC_LIB") == 0)
                p->entries[n].flags |= FILE_TYPE_XLIB_STATIC_LIB;
            else if (strcmp(flag, "XLIB_SHARED_LIB") == 0)
                p->entries[n].flags |= FILE_TYPE_XLIB_SHARED_LIB;
            else if (strcmp(flag, "TLS_LIB") == 0)
                p->entries[n].flags |= FILE_TYPE_TLS_LIB;
            else if (strcmp(flag, "UTILITY_LIB") == 0)
                p->entries[n].flags |= FILE_TYPE_UTILITY_LIB;
            else if (strcmp(flag, "DOCUMENTATION") == 0)
                p->entries[n].flags |= FILE_TYPE_DOCUMENTATION;
            else if (strcmp(flag, "MANPAGE") == 0)
                p->entries[n].flags |= FILE_TYPE_MANPAGE;
            else if (strcmp(flag, "OPENGL_SYMLINK") == 0)
                p->entries[n].flags |= FILE_TYPE_OPENGL_SYMLINK;
            else if (strcmp(flag, "XLIB_SYMLINK") == 0)
                p->entries[n].flags |= FILE_TYPE_XLIB_SYMLINK;
            else if (strcmp(flag, "TLS_SYMLINK") == 0)
                p->entries[n].flags |= FILE_TYPE_TLS_SYMLINK;
            else if (strcmp(flag, "UTILITY_SYMLINK") == 0)
                p->entries[n].flags |= FILE_TYPE_UTILITY_SYMLINK;
            else if (strcmp(flag, "INSTALLER_BINARY") == 0)
                p->entries[n].flags |= FILE_TYPE_INSTALLER_BINARY;
            else if (strcmp(flag, "UTILITY_BINARY") == 0)
                p->entries[n].flags |= FILE_TYPE_UTILITY_BINARY;
            else if (strcmp(flag, "DOT_DESKTOP") == 0)
                p->entries[n].flags |= FILE_TYPE_DOT_DESKTOP;
            else if (strcmp(flag, "XMODULE_SHARED_LIB") == 0)
                p->entries[n].flags |= FILE_TYPE_XMODULE_SHARED_LIB;
            else if (strcmp(flag, "XMODULE_SYMLINK") == 0)
                p->entries[n].flags |= FILE_TYPE_XMODULE_SYMLINK;
            else if (strcmp(flag, "XMODULE_NEWSYM") == 0)
                p->entries[n].flags |= FILE_TYPE_XMODULE_NEWSYM;
            else {
                nvfree(flag);
                goto invalid_manifest_file;
            }

            nvfree(flag);

            /* some libs/symlinks have an arch field */

            if (p->entries[n].flags & FILE_TYPE_HAVE_ARCH) {
                flag = read_next_word(c, &c);
                if (!flag) goto invalid_manifest_file;

                if (strcmp(flag, "COMPAT32") == 0)
                    p->entries[n].flags |= FILE_CLASS_COMPAT32;
                else if (strcmp(flag, "NATIVE") == 0)
                    p->entries[n].flags |= FILE_CLASS_NATIVE;
                else {
                    nvfree(flag);
                    goto invalid_manifest_file;
                }

                nvfree(flag);
            }

            /* some libs/symlinks have a class field */

            if (p->entries[n].flags & FILE_TYPE_HAVE_CLASS) {
                flag = read_next_word(c, &c);
                if (!flag) goto invalid_manifest_file;

                if (strcmp(flag, "CLASSIC") == 0)
                    p->entries[n].flags |= FILE_CLASS_CLASSIC_TLS;
                else if (strcmp(flag, "NEW") == 0)
                    p->entries[n].flags |= FILE_CLASS_NEW_TLS;
                else {
                    nvfree(flag);
                    goto invalid_manifest_file;
                }

                nvfree(flag);
            }

            /* libs and documentation have a path field */

            if (p->entries[n].flags & FILE_TYPE_HAVE_PATH) {
                p->entries[n].path = read_next_word(c, &c);
                if (!p->entries[n].path) goto invalid_manifest_file;
            } else {
                p->entries[n].path = NULL;
            }
            
            /* symlinks and newsyms have a target */

            if (p->entries[n].flags & FILE_TYPE_HAVE_TARGET) {
                p->entries[n].target = read_next_word(c, &c);
                if (!p->entries[n].target) goto invalid_manifest_file;
            } else {
                p->entries[n].target = NULL;
            }

            /*
             * as a convenience for later, set the 'name' pointer to
             * the basename contained in 'file' (ie the portion of
             * 'file' without any leading directory components
             */

            p->entries[n].name = strrchr(p->entries[n].file, '/');
            if (p->entries[n].name) p->entries[n].name++;
            
            if (!p->entries[n].name) p->entries[n].name = p->entries[n].file;
            
            /* free the line */
            
            free(buf);
        }
        
        line++;

    } while (!done);
    
    munmap(manifest, len);
    if (fd != -1) close(fd);

    return p;
    
 cannot_open:
    ui_error(op, "Failure opening package's .manifest file (%s).",
             strerror(errno));
    goto fail;

 invalid_manifest_file:

    ui_error(op, "Invalid .manifest file; error on line %d.", line);
    goto fail;

 fail:
    if (p && p->entries) free(p->entries);
    if (p) free(p);
    if (manifest != MAP_FAILED) munmap(manifest, len);
    if (fd != -1) close(fd);
    return NULL;
       
} /* parse_manifest() */
