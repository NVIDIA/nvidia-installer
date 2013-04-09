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
#include "manifest.h"

/* default names for generated signing keys */

#define SECKEY_NAME "tmp.key"
#define PUBKEY_NAME "tmp.der"

/* local prototypes */


static Package *parse_manifest(Options *op);
static int install_kernel_module(Options *op,  Package *p);
static void free_package(Package *p);
static int assisted_module_signing(Options *op, Package *p);


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
    int ran_pre_install_hook = FALSE;

    static const char edit_your_xf86config[] =
        "Please update your XF86Config or xorg.conf file as "
        "appropriate; see the file /usr/share/doc/"
        "NVIDIA_GLX-1.0/README.txt for details.";

    /*
     * validate the manifest file in the cwd, and process it, building
     * a Package struct
     */
    
    if ((p = parse_manifest(op)) == NULL) goto failed;
    
    ui_set_title(op, "%s (%s)", p->description, p->version);
    
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
    
    if (!get_license_acceptance(op)) goto exit_install;
    
    ui_log(op, "Installing NVIDIA driver version %s.", p->version);

    /*
     * determine the current NVIDIA version (if any); ask the user if
     * they really want to overwrite the existing installation
     */

    if (!check_for_existing_driver(op, p)) goto exit_install;

    /* run the distro preinstall hook */

    if (!run_distro_hook(op, "pre-install")) {
        if (!ui_yes_no(op, TRUE,
                       "The distribution-provided pre-install script failed!  "
                       "Continue installation anyway?")) {
            goto failed;
        }
    }
    ran_pre_install_hook = TRUE;

    /* fail if the nouveau driver is currently in use */

    if (!check_for_nouveau(op)) goto failed;

    /* attempt to build a kernel module for the target kernel */

    if (!op->no_kernel_module) {

        /* Offer the DKMS option if DKMS exists and the kernel module sources 
         * will be installed somewhere. Don't offer DKMS as an option if module
         * signing was requested. */

        if (find_system_util("dkms") && !op->no_kernel_module_source &&
            !(op->module_signing_secret_key && op->module_signing_public_key)) {
            op->dkms = ui_yes_no(op, op->dkms,
                                "Would you like to register the kernel module "
                                "sources with DKMS? This will allow DKMS to "
                                "automatically build a new module, if you "
                                "install a different kernel later.");
        }

        /* Only do the normal kernel module install if not using DKMS */

        if (op->dkms) {
            op->no_kernel_module = TRUE;
        } else if (!install_kernel_module(op, p)) {
            goto failed;
        }
    } else {
        ui_warn(op, "You specified the '--no-kernel-module' command line "
                "option, nvidia-installer will not install a kernel "
                "module as part of this driver installation, and it will "
                "not remove existing NVIDIA kernel modules not part of "
                "an earlier NVIDIA driver installation.  Please ensure "
                "that an NVIDIA kernel module matching this driver version "
                "is installed seperately.");

        /* no_kernel_module should imply no DKMS */

        if (op->dkms) {
            ui_warn(op, "You have specified both the '--no-kernel-module' "
                    "and the '--dkms' command line options. The '--dkms' "
                    "option will be ignored.");
            op->dkms = FALSE;
        }
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

    if (op->no_opengl_files) {
        remove_opengl_files_from_package(op, p);
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
    
    if (!ui_approve_command_list(op, c, "%s", p->description)) {
        goto exit_install;
    }
    
    /* initialize the backup log file */

    if (!op->kernel_module_only) {
        if (!init_backup(op, p)) goto failed;
    }

    /* execute the command list */

    if (!do_install(op, p, c)) goto failed;

    /* Register, build, and install the module with DKMS, if requested */

    if (op->dkms && !dkms_install_module(op, p->version, get_kernel_name(op)))
        goto failed;

    /* run the distro postinstall script */

    run_distro_hook(op, "post-install");

    /*
     * check that everything is installed properly (post-install
     * sanity check)
     */

    check_installed_files_from_package(op, p);

    if (!check_sysvipc(op)) goto failed;
    if (!check_runtime_configuration(op, p)) goto failed;
    
    /* done */

    if (op->kernel_module_only || op->no_nvidia_xconfig_question) {

        ui_message(op, "Installation of the kernel module for the %s "
                   "(version %s) is now complete.",
                   p->description, p->version);
    } else {
        
        /* ask the user if they would like to run nvidia-xconfig */
        
        ret = ui_yes_no(op, op->run_nvidia_xconfig,
                        "Would you like to run the nvidia-xconfig utility "
                        "to automatically update your X configuration file "
                        "so that the NVIDIA X driver will be used when you "
                        "restart X?  Any pre-existing X configuration "
                        "file will be backed up.");
        
        if (ret) {
            ret = run_nvidia_xconfig(op, FALSE);
        }
        
        if (ret) {
            ui_message(op, "Your X configuration file has been successfully "
                       "updated.  Installation of the %s (version: %s) is now "
                       "complete.", p->description, p->version);
        } else {
            
            msg = edit_your_xf86config;
            
            ui_message(op, "Installation of the %s (version: %s) is now "
                       "complete.  %s", p->description,
                       p->version, msg);
        }
    }
    
    free_package(p);

    return TRUE;
    
 failed:

    /*
     * something bad happened during installation; print an error
     * message and return FALSE
     */
    
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

    if (ran_pre_install_hook)
        run_distro_hook(op, "failed-install");

    /* fall through into exit_install... */

 exit_install:

    /*
     * we are exiting installation; this can happen for reasons that
     * do not merit the error message (e.g., the user declined the
     * license agreement)
     */
    
    free_package(p);
    
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
    PrecompiledInfo *precompiled_info;

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
    
    if ((precompiled_info = find_precompiled_kernel_interface(op, p))) {

        /*
         * we have a prebuilt kernel interface, so now link the kernel
         * interface with the binary portion of the kernel module.
         *
         * XXX if linking fails, maybe we should fall through and
         * attempt to build the kernel module?  No, if linking fails,
         * then there is something pretty seriously wrong... better to
         * abort.
         */
        
        if (!link_kernel_module(op, p, p->kernel_module_build_directory,
                                precompiled_info)) return FALSE;

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

    /* Optionally sign the kernel module */
    if (!assisted_module_signing(op, p)) return FALSE;

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
    
    free_package(p);

    return TRUE;

 failed:

    ui_error(op, "Unable to add a precompiled kernel interface for the "
             "running kernel.");
    
    free_package(p);

    return FALSE;
    
} /* add_this_kernel() */



/*
 * parse_manifest() - open and read the .manifest file in the current
 * directory.
 *
 * The first nine lines of the .manifest file are:
 *
 *   - a description string
 *   - a version string
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
    int fd, ret, len = 0;
    struct stat stat_buf, entry_stat_buf;
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
    p->version = get_next_line(ptr, &ptr, manifest, len);
    if (!p->version) goto invalid_manifest_file;
    
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
        if (!buf) {
            done = TRUE;
        } else if (buf[0] == '\0') {
            free(buf);
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
            
            /* initialize the new entry */

            memset(&p->entries[n], 0, sizeof(PackageEntry));

            /* read the file name and permissions */

            c = buf;

            p->entries[n].file = read_next_word(buf, &c);

            if (!p->entries[n].file) goto invalid_manifest_file;

            tmpstr = read_next_word(c, &c);
            
            if (!tmpstr) goto invalid_manifest_file;
            
            /* translate the mode string into an octal mode */
            
            ret = mode_string_to_mode(op, tmpstr, &p->entries[n].mode);

            free(tmpstr);

            if (!ret) goto invalid_manifest_file;
            
            /* every file has a type field */

            p->entries[n].type = FILE_TYPE_NONE;

            flag = read_next_word(c, &c);
            if (!flag) goto invalid_manifest_file;

            p->entries[n].type = parse_manifest_file_type(flag,
                                                          &p->entries[n].caps);

            if (p->entries[n].type == FILE_TYPE_NONE) {
                nvfree(flag);
                goto invalid_manifest_file;
            }

            nvfree(flag);

            /* some libs/symlinks have an arch field */

            p->entries[n].compat_arch = FILE_COMPAT_ARCH_NONE;

            if (p->entries[n].caps.has_arch) {
                flag = read_next_word(c, &c);
                if (!flag) goto invalid_manifest_file;

                if (strcmp(flag, "COMPAT32") == 0)
                    p->entries[n].compat_arch = FILE_COMPAT_ARCH_COMPAT32;
                else if (strcmp(flag, "NATIVE") == 0)
                    p->entries[n].compat_arch = FILE_COMPAT_ARCH_NATIVE;
                else {
                    nvfree(flag);
                    goto invalid_manifest_file;
                }

                nvfree(flag);
            }

            /* some libs/symlinks have a class field */

            p->entries[n].tls_class = FILE_TLS_CLASS_NONE;

            if (p->entries[n].caps.has_tls_class) {
                flag = read_next_word(c, &c);
                if (!flag) goto invalid_manifest_file;

                if (strcmp(flag, "CLASSIC") == 0)
                    p->entries[n].tls_class = FILE_TLS_CLASS_CLASSIC;
                else if (strcmp(flag, "NEW") == 0)
                    p->entries[n].tls_class = FILE_TLS_CLASS_NEW;
                else {
                    nvfree(flag);
                    goto invalid_manifest_file;
                }

                nvfree(flag);
            }

            /* libs and documentation have a path field */

            if (p->entries[n].caps.has_path) {
                p->entries[n].path = read_next_word(c, &c);
                if (!p->entries[n].path) goto invalid_manifest_file;
            } else {
                p->entries[n].path = NULL;
            }
            
            /* symlinks have a target */

            if (p->entries[n].caps.is_symlink) {
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

            /*
             * store the inode and device information, so that we can
             * later recognize it, to avoid accidentally moving it as
             * part of the 'find_conflicting_files' path
             */

            if (stat(p->entries[n].file, &entry_stat_buf) != -1) {
                p->entries[n].inode = entry_stat_buf.st_ino;
                p->entries[n].device = entry_stat_buf.st_dev;
            } else {
                p->entries[n].inode = 0;
                p->entries[n].device = 0;
            }

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
    free_package(p);
    if (manifest != MAP_FAILED) munmap(manifest, len);
    if (fd != -1) close(fd);
    return NULL;
       
} /* parse_manifest() */



/*
 * add_package_entry() - add a PackageEntry to the package's entries
 * array.
 */

void add_package_entry(Package *p,
                       char *file,
                       char *path,
                       char *name,
                       char *target,
                       char *dst,
                       PackageEntryFileType type,
                       PackageEntryFileTlsClass tls_class,
                       mode_t mode)
{
    int n;
    struct stat stat_buf;

    n = p->num_entries;

    p->entries =
        (PackageEntry *) nvrealloc(p->entries, (n + 1) * sizeof(PackageEntry));

    memset(&p->entries[n], 0, sizeof(PackageEntry));

    p->entries[n].file      = file;
    p->entries[n].path      = path;
    p->entries[n].name      = name;
    p->entries[n].target    = target;
    p->entries[n].dst       = dst;
    p->entries[n].type      = type;
    p->entries[n].tls_class = tls_class;
    p->entries[n].mode      = mode;
    p->entries[n].caps      = get_file_type_capabilities(type);

    if (stat(p->entries[n].file, &stat_buf) != -1) {
        p->entries[n].inode = stat_buf.st_ino;
        p->entries[n].device = stat_buf.st_dev;
    } else {
        p->entries[n].inode = 0;
        p->entries[n].device = 0;
    }

    p->num_entries++;

} /* add_package_entry() */



/*
 * free_package() - free the Package data structure
 */

static void free_package(Package *p)
{
    int i;

    if (!p) return;
    
    nvfree(p->description);
    nvfree(p->version);
    nvfree(p->kernel_module_filename);
    nvfree(p->kernel_interface_filename);
    nvfree(p->kernel_module_name);
    
    if (p->bad_modules) {
        for (i = 0; p->bad_modules[i]; i++) {
            nvfree(p->bad_modules[i]);
        }
        nvfree((char *) p->bad_modules);
    }

    if (p->bad_module_filenames) {
        for (i = 0; p->bad_module_filenames[i]; i++) {
            nvfree(p->bad_module_filenames[i]);
        }
        nvfree((char *) p->bad_module_filenames);
    }
    
    nvfree(p->kernel_module_build_directory);
    
    nvfree(p->precompiled_kernel_interface_directory);
    
    for (i = 0; i < p->num_entries; i++) {
        nvfree(p->entries[i].file);
        nvfree(p->entries[i].path);
        nvfree(p->entries[i].target);
        nvfree(p->entries[i].dst);

        /*
         * Note: p->entries[i].name just points into
         * p->entries[i].file, so don't free p->entries[i].name
         */
    }

    nvfree((char *) p->entries);

    nvfree((char *) p);
    
} /* free_package() */



/*
 * assisted_module_signing() - Guide the user through the module signing process
 */

static int assisted_module_signing(Options *op, Package *p)
{
    int generate_keys = FALSE, do_sign = FALSE, secureboot;

    secureboot = secure_boot_enabled();

    if (secureboot < 0) {
        ui_log(op, "Unable to determine if Secure Boot is enabled: %s",
               strerror(-secureboot));
    }

    if (op->kernel_module_signed) {
        /* The kernel module is already signed, e.g. from linking a precompiled
         * interface + appending a detached signature */
        return TRUE;
    }

    if (test_kernel_config_option(op, p, "CONFIG_DUMMY_OPTION") ==
        KERNEL_CONFIG_OPTION_UNKNOWN) {
        /* Unable to test kernel configuration options, possibly due to
         * missing kernel headers. Since we might be installing on a
         * system that doesn't have the headers, bail out. */
        return TRUE;
    }

    if (op->module_signing_secret_key && op->module_signing_public_key) {
        /* If the user supplied signing keys, sign the module, regardless of
         * whether or not we actually need to. */
        do_sign = TRUE;
    } else if (test_kernel_config_option(op, p, "CONFIG_MODULE_SIG_FORCE") ==
               KERNEL_CONFIG_OPTION_DEFINED) {
        /* If CONFIG_MODULE_SIG_FORCE is set, we must sign. */
        ui_message(op, "The target kernel has CONFIG_MODULE_SIG_FORCE set, "
                   "which means that it requires that kernel modules be "
                   "cryptographically signed by a trusted key.");
        do_sign = TRUE;
    } else if (secureboot != 1 && !op->expert) {
        /* If this is a non-UEFI system, or a UEFI system with secure boot
         * disabled, or we are unable to determine whether the system has
         * secure boot enabled, bail out unless in expert mode. */
        return TRUE;
    } else if (test_kernel_config_option(op, p, "CONFIG_MODULE_SIG") ==
               KERNEL_CONFIG_OPTION_DEFINED){
        /* The kernel may or may not enforce module signatures; ask the user
         * whether to sign the module. */

        const char* sb_message = (secureboot == 1) ?
                                     "This system also has UEFI Secure Boot "
                                     "enabled; many distributions enforce "
                                     "module signature verification on UEFI "
                                     "systems when Secure Boot is enabled. " :
                                     "";

        do_sign = ui_yes_no(op, FALSE, "The target kernel has "
                            "CONFIG_MODULE_SIG set, which means that it "
                            "supports cryptographic signatures on kernel "
                            "modules. On some systems, the kernel may "
                            "refuse to load modules without a valid "
                            "signature from a trusted key. %sWould you like "
                            "to sign the NVIDIA kernel module?", sb_message);
    }

    if (!do_sign) {
        /* The user explicitly opted out of module signing, or the kernel does
         * not support module signatures, and no signing keys were provided;
         * there is nothing for us to do here. */
        return TRUE;
    }

    /* If we're missing either key, we need to get both from the user. */
    if (!op->module_signing_secret_key || !op->module_signing_public_key) {
        generate_keys = !ui_yes_no(op, FALSE, "Do you already have a key pair "
                                   "which can be used to sign the NVIDIA "
                                   "kernel module? Answer 'Yes' to use an "
                                   "existing key pair, or 'No' to generate a "
                                   "new key pair.");
        if (generate_keys) {
            char *cmdline;
            int ret;

            if (!op->utils[OPENSSL]) {
                ui_error(op, "Unable to generate key pair: openssl not "
                         "found!");
                return FALSE;
            }

            log_printf(op, NULL, "Generating key pair for module signing...");

            /* Generate a key pair using openssl.
             * XXX We assume that sign-file requires the X.509 certificate
             * in DER format; if this changes in the future we will need
             * to be able to accommodate the actual required format. */

            cmdline = nvstrcat("cd ", p->kernel_module_build_directory, "; ",
                               op->utils[OPENSSL], " req -new -x509 -newkey "
                               "rsa:2048 -days 7300 -nodes -sha256 -subj "
                               "\"/CN=nvidia-installer generated signing key/\""
                               " -keyout " SECKEY_NAME " -outform DER -out "
                               PUBKEY_NAME, NULL);

            ret = run_command(op, cmdline, NULL, TRUE, 8, TRUE);

            nvfree(cmdline);

            if (ret != 0) {
                ui_error(op, "Failed to generate key pair!");
                return FALSE;
            }

            log_printf(op, NULL, "Signing keys generated successfully.");

            /* Set the signing keys to the newly generated pair. The paths
             * are relative to p->kernel_module_build_directory, since we
             * cd to it before signing the module. */
            op->module_signing_secret_key = SECKEY_NAME;
            op->module_signing_public_key = PUBKEY_NAME;
        } else {
            /* The user already has keys; prompt for their locations. */
            op->module_signing_secret_key =
                get_filename(op, op->module_signing_secret_key,
                             "Please provide the path to the private key");
            op->module_signing_public_key =
                get_filename(op, op->module_signing_public_key,
                             "Please provide the path to the public key");
        }
    }

    /* Now that we have keys (user-supplied or installer-generated),
     * sign the kernel module which we built earlier. */
    if (!sign_kernel_module(op, p->kernel_module_build_directory, TRUE)) {
        return FALSE;
    }

    if (generate_keys) {

        /* If keys were generated, we should install the verification cert
         * so that the user can make the kernel trust it, and either delete
         * or install the private signing key. */
        char *file, *name, *result = NULL, *fingerprint, *cmdline;
        char short_fingerprint[9];
        int ret, delete_secret_key;

        delete_secret_key = ui_yes_no(op, TRUE, "The NVIDIA kernel module was "
                                      "successfully signed with a newly "
                                      "generated key pair. Would you like to "
                                      "delete the private signing key?");

        /* Get the fingerprint of the X.509 certificate. We already used 
           openssl to create a keypair at this point, so we know we have it;
           otherwise, we would have already returned by now. */
        cmdline = nvstrcat(op->utils[OPENSSL], " x509 -noout -fingerprint ",
                           "-inform DER -in ", p->kernel_module_build_directory,
                           "/"PUBKEY_NAME, NULL);
        ret = run_command(op, cmdline, &result, FALSE, 0, FALSE);
        nvfree(cmdline);

        /* Format: "SHA1 Fingerprint=00:00:00:00:..." */
        fingerprint = strchr(result, '=') + 1;

        if (ret != 0 || !fingerprint || strlen(fingerprint) < 40) {
            char *sha1sum = find_system_util("sha1sum");

            if (sha1sum) {
                /* the openssl command failed, or we parsed its output
                 * incorrectly; try to get a sha1sum of the DER certificate */
                cmdline = nvstrcat(sha1sum, p->kernel_module_build_directory,
                                   "/"PUBKEY_NAME, NULL);
                ret = run_command(op, cmdline, &result, FALSE, 0, FALSE);
                nvfree(sha1sum);
                nvfree(cmdline);

                fingerprint = result;
            }

            if (!sha1sum || ret != 0 || !fingerprint ||
                strlen(fingerprint) < 40) {
                /* Unable to determine fingerprint */
                fingerprint = "UNKNOWN";
            } else {
                char *end = strchr(fingerprint, ' ');
                *end = '\0';
            }
        } else {
            /* Remove any ':' characters from fingerprint and truncate */
            char *tmp = nv_strreplace(fingerprint, ":", "");
            strncpy(short_fingerprint, tmp, sizeof(fingerprint));
            nvfree(tmp);
        }
        short_fingerprint[sizeof(short_fingerprint) - 1] = '\0';

        /* Add the public key to the package */
        file = nvstrcat(p->kernel_module_build_directory, "/"PUBKEY_NAME, NULL);

        /* XXX name will be leaked when freeing package */
        name = nvstrcat("nvidia-modsign-crt-", short_fingerprint, ".der", NULL);

        add_package_entry(p,
                          file,
                          NULL, /* path */
                          name,
                          NULL, /* target */
                          NULL, /* dst */
                          FILE_TYPE_MODULE_SIGNING_KEY,
                          FILE_TLS_CLASS_NONE,
                          0444);

        ui_message(op, "An X.509 certificate containing the public signing "
                    "key will be installed to %s/%s. The SHA1 fingerprint of "
                    "this certificate is: %s.\n\nThis certificate must be "
                    "added to a key database which is trusted by your kernel "
                    "in order for the kernel to be able to verify the module "
                    "signature.", op->module_signing_key_path, name,
                    fingerprint);

        nvfree(result);

        /* Delete or install the private key */
        file = nvstrcat(p->kernel_module_build_directory, "/"SECKEY_NAME, NULL);

        if (delete_secret_key) {
            secure_delete(op, file);
        } else {

            /* Add the private key to the package */

            name = nvstrcat("nvidia-modsign-key-", short_fingerprint, ".key",
                            NULL);

            add_package_entry(p,
                              file,
                              NULL, /* path */
                              name,
                              NULL, /* target */
                              NULL, /* dst */
                              FILE_TYPE_MODULE_SIGNING_KEY,
                              FILE_TLS_CLASS_NONE,
                              0400);

            ui_message(op, "The private signing key will be installed to %s/%s. "
                       "After the public key is added to a key database which "
                       "is trusted by your kernel, you may reuse the saved "
                       "public/private key pair to sign additional kernel "
                       "modules, without needing to re-enroll the public key. "
                       "Please take some reasonable precautions to secure the "
                       "private key: see the README for suggestions.",
                       op->module_signing_key_path, name);
        }
    } /* if (generate_keys) */

    return TRUE;
} /* assisted_module_signing() */
