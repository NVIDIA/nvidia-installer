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
#include <stddef.h>

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

/* local prototypes */


static Package *parse_manifest(Options *op);
static int install_kernel_modules(Options *op,  Package *p);
static void free_package(Package *p);
static int assisted_module_signing(Options *op, Package *p);

static const KernelModuleInfo optional_modules[] = {
    {
         .module_name = "nvidia-uvm",
         .optional_module_dependee = "CUDA",
         .disable_option = "no-unified-memory",
         .option_offset = offsetof(Options, install_uvm),
    },
    {
         .module_name = "nvidia-drm",
         .optional_module_dependee = "DRM-KMS",
         .disable_option = "no-drm",
         .option_offset = offsetof(Options, install_drm),
    },
    {
        .module_name = "nvidia-peermem",
        .optional_module_dependee = "GPUDirect RDMA p2p memory sharing",
        .disable_option = "no-peermem",
        .option_offset = offsetof(Options, install_peermem),
    },
};

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
    int ret;
    int ran_pre_install_hook = FALSE;
    HookScriptStatus res;

    static const char* edit_your_xf86config =
        "Please update your xorg.conf file as "
        "appropriate; see the file /usr/share/doc/"
        "NVIDIA_GLX-1.0/README.txt for details.";

    /*
     * validate the manifest file in the cwd, and process it, building
     * a Package struct
     */
    
    if ((p = parse_manifest(op)) == NULL) goto failed;

    if (!op->x_files_packaged) {
        edit_your_xf86config = "";
    }

    ui_set_title(op, "%s (%s)", p->description, p->version);
    
    /* 
     * warn the user if "legacy" GPUs are installed in this system
     * and if no supported GPU is found, at all.
     */

    check_for_nvidia_graphics_devices(op, p);

    /* check that we are not running any X server */

    if (!check_for_running_x(op)) goto failed;

    /* run the distro pre unload hook */
    res = run_distro_hook(op, "pre-unload");
    if (res == HOOK_SCRIPT_FAIL) {
        ui_error(op, "Pre-unload hook script failed");
        goto failed;
    }

    /* make sure the kernel module is unloaded */
    
    if (!check_for_unloaded_kernel_module(op)) goto failed;
    
    ui_log(op, "Installing NVIDIA driver version %s.", p->version);

    /*
     * determine the current NVIDIA version (if any); ask the user if
     * they really want to overwrite the existing installation
     */

    if (!check_for_existing_driver(op, p)) goto exit_install;

    /*
     * check to see if an alternate method of installation is already installed
     * or is available, but not installed; ask the user if they really want to
     * install anyway despite the presence/availability of an alternate install.
     */

    if (!check_for_alternate_install(op)) goto exit_install;

    /* run the distro preinstall hook */

    res = run_distro_hook(op, "pre-install");
    if (res == HOOK_SCRIPT_FAIL) {
        if (ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
                               NUM_CONTINUE_ABORT_CHOICES,
                               CONTINUE_CHOICE, /* Default choice */
                               "The distribution-provided pre-install "
                               "script failed!  Are you sure you want "
                               "to continue?") == ABORT_CHOICE) {
            goto failed;
        }
    } else if (res == HOOK_SCRIPT_SUCCESS) {
        if (ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
                               NUM_CONTINUE_ABORT_CHOICES,
                               CONTINUE_CHOICE, /* Default choice */
                               "The distribution-provided pre-install script "
                               "completed successfully. If this is the first "
                               "time you have run the installer, this script "
                               "may have helped disable Nouveau, but a reboot "
                               "may be required first.  "
                               "Would you like to continue, or would you "
                               "prefer to abort installation to reboot the "
                               "system?") == ABORT_CHOICE) {
            goto exit_install;
        }
        ran_pre_install_hook = TRUE;
    }

    /* fail if the nouveau driver is currently in use */

    if (!check_for_nouveau(op)) goto failed;

    /* ask if we should install the optional kernel modules */

    should_install_optional_modules(op, p, optional_modules,
                                    ARRAY_LEN(optional_modules));

    /* attempt to build the kernel modules for the target kernel */

    if (!op->no_kernel_module) {
        if (!install_kernel_modules(op, p)) {
            goto failed;
        }
    } else {
        ui_warn(op, "You specified the '--no-kernel-module' command line "
                "option, nvidia-installer will not install a kernel "
                "module as part of this driver installation, and it will "
                "not remove existing NVIDIA kernel modules not part of "
                "an earlier NVIDIA driver installation.  Please ensure "
                "that an NVIDIA kernel module matching this driver version "
                "is installed separately.");

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
        remove_non_kernel_module_files_from_package(p);
    } else {

        /* ask for the XFree86 and OpenGL installation prefixes. */
    
        if (!get_prefixes(op)) goto failed;

        /*
         * if the package contains any .desktop files,
         * process them (perform some search and replacing so
         * that they reflect the correct installation path, etc)
         * and add them to the package list (files to be installed).
         */

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
        remove_opengl_files_from_package(p);
    }

    if (op->no_wine_files) {
        remove_wine_files_from_package(p);
    }

    /*
     * determine whether systemd files should be installed
     */
    if (op->use_systemd != NV_OPTIONAL_BOOL_TRUE) {
        remove_systemd_files_from_package(p);
    }

    /*
     * Remove any kernel module source files that won't be installed.
     */
    remove_non_installed_kernel_module_source_files_from_package(p);

    /*
     * now that we have the installation prefixes, build the
     * destination for each file to be installed
     */
    
    if (!set_destinations(op, p)) goto failed;

    /*
     * if we are installing OpenGL libraries, ensure that a symlink gets
     * installed to /usr/lib/libGL.so.1. add_libgl_abi_symlink() sets its own
     * destination, so it must be called after set_destinations().
     */
    if (!op->kernel_module_only && !op->no_opengl_files) {
        add_libgl_abi_symlink(op, p);
    }
    
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
        if (!run_existing_uninstaller(op)) goto failed;
    }

    if (!check_libglvnd_files(op, p)) {
        goto failed;
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

    /*
     * Leave nvidia-drm loaded in case an X server with OutputClass-based driver
     * matching is being used.
     */

    if (!op->no_kernel_module || op->dkms) {
        if (package_includes_kernel_module(p, "nvidia-drm")) {
            if (!load_kernel_module(op, "nvidia-drm")) {
                goto failed;
            }
        }

        if (package_includes_kernel_module(p, "nvidia-vgpu-vfio")) {
            if (!load_kernel_module(op, "nvidia-vgpu-vfio")) {
                goto failed;
            }
        }
    }

    /* run the distro postinstall script */

    run_distro_hook(op, "post-install");

    /*
     * check that everything is installed properly (post-install
     * sanity check)
     */

    check_installed_files_from_package(op, p);

    /* done */

    if (op->kernel_module_only || op->no_nvidia_xconfig_question) {

        ui_message(op, "Installation of the kernel module for the %s "
                   "(version %s) is now complete.",
                   p->description, p->version);
    } else {
        
        /* ask the user if they would like to run nvidia-xconfig */
        
        const char *msg = "Would you like to run the nvidia-xconfig utility "
                          "to automatically update your X configuration file "
                          "so that the NVIDIA X driver will be used when you "
                          "restart X?  Any pre-existing X configuration "
                          "file will be backed up.";
        
        ret = run_nvidia_xconfig(op, FALSE, msg, op->run_nvidia_xconfig);
        
        if (ret) {
            ui_message(op, "Your X configuration file has been successfully "
                       "updated.  Installation of the %s (version: %s) is now "
                       "complete.", p->description, p->version);
        } else {
            ui_message(op, "Installation of the %s (version: %s) is now "
                       "complete.  %s", p->description,
                       p->version, edit_your_xf86config);
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
 * Attempt to build and install the appropriate kernel modules for the
 * running kernel; we first check if prebuilt kernel interfaces exist.
 * If yes, we try to link those into the final kernel module, else we
 * try to build from source.
 *
 * If we succeed in building the kernel modules, we attempt to load
 * them into the host kernel and add them to the list of files to
 * install if the load attempt succeeds.
 */

static int install_kernel_modules(Options *op,  Package *p)
{
    PrecompiledInfo *precompiled_info;

    process_dkms_conf(op,p);

    /* Offer the DKMS option if DKMS exists and the kernel module sources 
     * will be installed somewhere. Don't offer DKMS as an option if module
     * signing was requested. */

    if (op->utils[DKMS] && !op->no_kernel_module_source &&
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
        return TRUE;
    }

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

        int i, precompiled_success = TRUE;

        /*
         * make sure the required development tools are present on
         * this system before trying to link the kernel interface.
         */
        if (!check_precompiled_kernel_interface_tools(op)) {
            precompiled_success = FALSE;
            goto precompiled_done;
        }

        /*
         * we have a prebuilt kernel interface package, so now link the
         * kernel interface files to produce the kernel module.
         *
         * XXX if linking fails, maybe we should fall through and
         * attempt to build the kernel module?  No, if linking fails,
         * then there is something pretty seriously wrong... better to
         * abort.
         */

        for (i = 0; i < precompiled_info->num_files; i++) {
            if (!unpack_kernel_modules(op, p, p->kernel_module_build_directory,
                                       &(precompiled_info->files[i]))) {
                precompiled_success = FALSE;
                goto precompiled_done;
            }
        }
precompiled_done:
        free_precompiled(precompiled_info);
        if (!precompiled_success) {
            return FALSE;
        }
    } else {
        /*
         * make sure the required development tools are present on
         * this system before attempting to verify the compiler and
         * trying to build a custom kernel interface.
         */
        if (!check_development_tools(op, p)) return FALSE;

        /*
         * we do not have a prebuilt kernel interface; thus we'll need
         * to compile the kernel interface, so determine where the
         * kernel source files are.
         */
        
        if (!determine_kernel_source_path(op, p)) return FALSE;
    
        /* and now, build the kernel interface */
        
        if (!build_kernel_modules(op, p)) return FALSE;
    }

    /* Optionally sign the kernel module */
    if (!assisted_module_signing(op, p)) return FALSE;

    /*
     * if we got this far, we have a complete kernel module; test it
     * to be sure it's OK
     */
    
    if (!test_kernel_modules(op, p)) return FALSE;
    
    /* add the kernel modules to the list of things to install */
    
    add_kernel_modules_to_package(op, p);
    
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
    PrecompiledFileInfo *fileInfos;
    
    /* parse the manifest */

    if ((p = parse_manifest(op)) == NULL) goto failed;

    /* make sure we have the development tools */

    if (!check_development_tools(op, p)) goto failed;

    /* find the kernel header files */

    if (!determine_kernel_source_path(op, p)) goto failed;

    /* build the precompiled files */

    if (p->num_kernel_modules != build_kernel_interfaces(op, p, &fileInfos))
        goto failed;
    
    /* pack the precompiled files */

    if (!pack_precompiled_files(op, p, p->num_kernel_modules, fileInfos))
        goto failed;
    
    free_package(p);

    return TRUE;

 failed:

    ui_error(op, "Unable to add a precompiled kernel interface for the "
             "running kernel.");
    
    free_package(p);

    return FALSE;
    
} /* add_this_kernel() */



/*
 * Returns TRUE if the given module has a separate interface, FALSE otherwise.
 */
static int has_separate_interface_file(char *name) {
    int i;

    static const char* no_interface_modules[] = {
        "nvidia-vgpu-vfio",
        "nvidia-uvm",
        "nvidia-drm",
        "nvidia-peermem",
    };

    for (i = 0; i < ARRAY_LEN(no_interface_modules); i++) {
        if (strcmp(no_interface_modules[i],name) == 0) {
            return FALSE;
        }
    }

    return TRUE;
};

/*
 * Populate the module info records for optional records with information
 * that can be used in e.g. error messages.
 */
static void populate_optional_module_info(KernelModuleInfo *module)
{
    int i;

    for (i = 0; i < ARRAY_LEN(optional_modules); i++) {
        if (strcmp(optional_modules[i].module_name, module->module_name) == 0) {
            module->is_optional = TRUE;
            module->optional_module_dependee =
                optional_modules[i].optional_module_dependee;
            module->disable_option = optional_modules[i].disable_option;
            module->option_offset = optional_modules[i].option_offset;
            return;
        }
    }
}


/*
 * Return a string with 'suffix' appended to the original source string, after
 * replacing "nvidia" with "nv" at the beginning of the original source string.
 */
static char *nvidia_to_nv(const char *name, const char *suffix) {
    if (strncmp("nvidia", name, strlen("nvidia")) != 0) {
        return NULL;
    }

    return nvstrcat("nv", name + strlen("nvidia"), suffix, NULL);
}


/*
 * Iterate over the list of kernel modules from the manifest file; generate
 * and store module information records for each module in the Package.
 */
static int parse_kernel_modules_list(Package *p, char *list) {
    char *name;

    p->num_kernel_modules = 0; /* in case this gets called more than once */

    for (name = strtok(list, " "); name; name = strtok(NULL, " ")) {
        KernelModuleInfo *module;
        p->kernel_modules = nvrealloc(p->kernel_modules,
                                      (p->num_kernel_modules + 1) *
                                      sizeof(p->kernel_modules[0]));
        module = p->kernel_modules + p->num_kernel_modules;
        memset(module, 0, sizeof(*module));

        module->module_name = nvstrdup(name);
        module->module_filename = nvstrcat(name, ".ko", NULL);
        module->has_separate_interface_file = has_separate_interface_file(name);
        if (module->has_separate_interface_file) {
            char *core_binary = nvidia_to_nv(name, "-kernel.o_binary");
            module->interface_filename = nvidia_to_nv(name, "-linux.o");
            module->core_object_name = nvstrcat(name, "/", core_binary, NULL);
            nvfree(core_binary);
        }
        populate_optional_module_info(module);

        p->num_kernel_modules++;
    }

    return p->num_kernel_modules;
}


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
 *   - file types which inherit their paths will have a path depth
 *   - symbolic links will name the target of the link
 */

static Package *parse_manifest (Options *op)
{
    char *buf, *c, *tmpstr;
    int line;
    int fd, ret, len = 0;
    struct stat stat_buf;
    Package *p;
    char *manifest = MAP_FAILED, *ptr;
    int opengl_files_packaged = FALSE;

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
    
    /* Ignore the third line */

    line++;
    nvfree(get_next_line(ptr, &ptr, manifest, len));

    /* the fourth line is the list of kernel modules. */

    line++;
    tmpstr = get_next_line(ptr, &ptr, manifest, len);
    if (parse_kernel_modules_list(p, tmpstr) == 0) {
        goto invalid_manifest_file;
    }
    nvfree(tmpstr);

    /*
     * set the default value of excluded_kernel_modules to an empty, heap
     * allocated string so that it can be freed and won't prematurely end
     * an nvstrcat()ed string when unset.
     */

    p->excluded_kernel_modules = nvstrdup("");

    /*
     * ignore the fifth and sixth lines
     */

    line++;
    nvfree(get_next_line(ptr, &ptr, manifest, len));
    line++;
    nvfree(get_next_line(ptr, &ptr, manifest, len));

    /* the seventh line is the kernel module build directory */

    line++;
    p->kernel_module_build_directory = get_next_line(ptr, &ptr, manifest, len);
    if (!p->kernel_module_build_directory) goto invalid_manifest_file;

    /*
     * allow the kernel module build directory to be overridden from the command
     * line
     */

    if (op->kernel_module_build_directory_override) {
        nvfree(p->kernel_module_build_directory);
        p->kernel_module_build_directory =
            nvstrdup(op->kernel_module_build_directory_override);
    }

    remove_trailing_slashes(p->kernel_module_build_directory);

    /*
     * the eighth line is the directory containing precompiled kernel
     * interfaces
     */

    line++;
    p->precompiled_kernel_interface_directory =
        get_next_line(ptr, &ptr, manifest, len);
    if (!p->precompiled_kernel_interface_directory)
        goto invalid_manifest_file;
    remove_trailing_slashes(p->precompiled_kernel_interface_directory);

    /* the rest of the file is file entries */

    line++;
    
    for (; (buf = get_next_line(ptr, &ptr, manifest, len)); line++) {
        char *flag = NULL;
        PackageEntry entry;
        int entry_success = FALSE;

        if (buf[0] == '\0') {
            free(buf);
            break;
        }

        /* initialize the new entry */

        memset(&entry, 0, sizeof(PackageEntry));

        /* read the file name and permissions */

        c = buf;

        entry.file = read_next_word(buf, &c);

        if (!entry.file) goto entry_done;

        tmpstr = read_next_word(c, &c);

        if (!tmpstr) goto entry_done;

        /* translate the mode string into an octal mode */

        ret = mode_string_to_mode(op, tmpstr, &entry.mode);

        free(tmpstr);

        if (!ret) goto entry_done;

        /* every file has a type field */

        entry.type = FILE_TYPE_NONE;

        flag = read_next_word(c, &c);
        if (!flag) goto entry_done;

        entry.type = parse_manifest_file_type(flag, &entry.caps);

        if (entry.type == FILE_TYPE_NONE) {
            goto entry_done;
        }

        /* Track whether certain file types were packaged */

        switch (entry.type) {
            case FILE_TYPE_XMODULE_SHARED_LIB:
                op->x_files_packaged = TRUE;
                break;
            default: break;
        }

        /* set opengl_files_packaged if any OpenGL files were packaged */

        if (entry.caps.is_opengl) {
            opengl_files_packaged = TRUE;
        }

        /* some libs/symlinks have an arch field */

        entry.compat_arch = FILE_COMPAT_ARCH_NONE;

        if (entry.caps.has_arch) {
            nvfree(flag);
            flag = read_next_word(c, &c);
            if (!flag) goto entry_done;

            if (strcmp(flag, "COMPAT32") == 0)
                entry.compat_arch = FILE_COMPAT_ARCH_COMPAT32;
            else if (strcmp(flag, "NATIVE") == 0)
                entry.compat_arch = FILE_COMPAT_ARCH_NATIVE;
            else {
                goto entry_done;
            }
        }

        /* if compat32 files are packaged, set compat32_files_packaged */

        if (entry.compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
            op->compat32_files_packaged = TRUE;
        }

        /* some file types have a path field, or inherit their paths */

        if (entry.caps.has_path) {
            entry.path = read_next_word(c, &c);
            if (!entry.path) goto invalid_manifest_file;
        } else if (entry.caps.inherit_path) {
            int i;
            char *path, *depth, *slash;
            const char * const depth_marker = "INHERIT_PATH_DEPTH:";

            depth = read_next_word(c, &c);
            if (!depth ||
                strncmp(depth, depth_marker, strlen(depth_marker)) != 0) {
                goto invalid_manifest_file;
            }
            entry.inherit_path_depth = atoi(depth + strlen(depth_marker));
            nvfree(depth);

            /* Remove the file component from the packaged filename */
            path = entry.path = nvstrdup(entry.file);
            slash = strrchr(path, '/');
            if (slash == NULL) {
                goto invalid_manifest_file;
            }
            slash[1] = '\0';

            /* Strip leading directory components from the path */
            for (i = 0; i < entry.inherit_path_depth; i++) {
                slash = strchr(entry.path, '/');

                if (slash == NULL) {
                    goto invalid_manifest_file;
                }

                entry.path = slash + 1;
            }

            entry.path = nvstrdup(entry.path);
            nvfree(path);
        } else {
            entry.path = NULL;
        }

        /* symlinks have a target */

        if (entry.caps.is_symlink) {
            entry.target = read_next_word(c, &c);
            if (!entry.target) goto invalid_manifest_file;
        } else {
            entry.target = NULL;
        }

        /*
         * as a convenience for later, set the 'name' pointer to
         * the basename contained in 'file' (ie the portion of
         * 'file' without any leading directory components
         */

        entry.name = strrchr(entry.file, '/');
        if (entry.name) entry.name++;

        if (!entry.name) entry.name = entry.file;

        add_package_entry(p,
                          entry.file,
                          entry.path,
                          entry.name,
                          entry.target,
                          entry.dst,
                          entry.type,
                          entry.compat_arch,
                          entry.mode);

        entry_success = TRUE;

 entry_done:
        /* clean up */

        nvfree(buf);
        nvfree(flag);
        if (!entry_success) {
            goto invalid_manifest_file;
        }
    }

    /* If no OpenGL files were packaged, we can't install them. Set the
     * no_opengl_files flag so that everything we skip when explicitly
     * excluding OpenGL is also skipped when OpenGL is not packaged. */

    if (!opengl_files_packaged) {
        op->no_opengl_files = TRUE;
    }

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
                       PackageEntryFileCompatArch compat_arch,
                       mode_t mode)
{
    int n;
    struct stat stat_buf;

    n = p->num_entries;

    p->entries =
        (PackageEntry *) nvrealloc(p->entries, (n + 1) * sizeof(PackageEntry));

    memset(&p->entries[n], 0, sizeof(PackageEntry));

    p->entries[n].file        = file;
    p->entries[n].path        = path;
    p->entries[n].name        = name;
    p->entries[n].target      = target;
    p->entries[n].dst         = dst;
    p->entries[n].type        = type;
    p->entries[n].mode        = mode;
    p->entries[n].caps        = get_file_type_capabilities(type);
    p->entries[n].compat_arch = compat_arch;

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
    
    nvfree(p->kernel_module_build_directory);

    nvfree(p->precompiled_kernel_interface_directory);

    for (i = 0; i < p->num_kernel_modules; i++) {
        free_kernel_module_info(p->kernel_modules[i]);
    }
    nvfree(p->kernel_modules);

    nvfree(p->excluded_kernel_modules);

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
    int generate_keys = FALSE, do_sign = FALSE, secureboot, i;

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

        const char *choices[2] = {
            "Sign the kernel module",
            "Install without signing"
        };

        const char* sb_message = (secureboot == 1) ?
                                     "This system also has UEFI Secure Boot "
                                     "enabled; many distributions enforce "
                                     "module signature verification on UEFI "
                                     "systems when Secure Boot is enabled. " :
                                     "";

        do_sign = (ui_multiple_choice(op, choices, 2, 1, "The target kernel "
                                      "has CONFIG_MODULE_SIG set, which means "
                                      "that it supports cryptographic "
                                      "signatures on kernel modules. On some "
                                      "systems, the kernel may refuse to load "
                                      "modules without a valid signature from "
                                      "a trusted key. %sWould you like to sign "
                                      "the NVIDIA kernel module?",
                                      sb_message) == 0);
    }

    if (!do_sign) {
        /* The user explicitly opted out of module signing, or the kernel does
         * not support module signatures, and no signing keys were provided;
         * there is nothing for us to do here. */
        return TRUE;
    }

    /* If we're missing either key, we need to get both from the user. */
    if (!op->module_signing_secret_key || !op->module_signing_public_key) {

        const char *choices[2] = {
            "Use an existing key pair",
            "Generate a new key pair"
        };

        generate_keys = (ui_multiple_choice(op, choices, 2, 1, "Would you like "
                                            "to sign the NVIDIA kernel module "
                                            "with an existing key pair, or "
                                            "would you like to generate a new "
                                            "one?") == 1);

        if (generate_keys) {
            char *cmdline, *x509_hash, *private_key_path, *public_key_path;
            int ret, generate_failed = FALSE;

            if (!op->utils[OPENSSL]) {
                ui_error(op, "Unable to generate key pair: openssl not "
                         "found!");
                return FALSE;
            }

            /* Determine what hashing algorithm to use for the generated X.509
             * certificate. XXX The default is to use the same hash that is
             * used for signing modules; the two hashes are actually orthogonal
             * to each other, but by choosing the module signing hash we are
             * guaranteed that the chosen hash will be built into the kernel.
             */
            if (op->module_signing_x509_hash) {
                x509_hash = nvstrdup(op->module_signing_x509_hash);
            } else {
                char *guess, *guess_trimmed, *warn = NULL;

                char *no_guess = "Unable to guess the module signing hash.";
                char *common_warn = "The module signing certificate generated "
                                    "by nvidia-installer will be signed with "
                                    "sha256 as a fallback. If the resulting "
                                    "certificate fails to import into your "
                                    "kernel's trusted keyring, please run the "
                                    "installer again, and either use a pre-"
                                    "generated key pair, or set the "
                                    "--module-signing-x509-hash option if you "
                                    "plan to generate a new key pair with "
                                    "nvidia-installer.";

                guess = guess_module_signing_hash(op,
                                                  p->kernel_module_build_directory);

                if (guess == NULL) {
                    warn = no_guess;
                    goto guess_fail;
                }

                guess_trimmed = nv_trim_space(guess);
                guess_trimmed = nv_trim_char_strict(guess_trimmed, '"');

                if (guess_trimmed) {
                    if (strlen(guess_trimmed) == 0) {
                        warn = no_guess;
                        goto guess_fail;
                    }

                    x509_hash = nvstrdup(guess_trimmed);
                } else {
                    warn = "Error while parsing the detected module signing "
                           "hash.";
                    goto guess_fail;
                }

guess_fail:
                nvfree(guess);

                if (warn) {
                    ui_warn(op, "%s %s", warn, common_warn);
                    x509_hash = nvstrdup("sha256");
                }
            }

            log_printf(op, NULL, "Generating key pair for module signing...");

            /* Generate temporary files for the signing key and certificate */

            private_key_path = write_temp_file(op, 0, NULL, 0600);
            public_key_path = write_temp_file(op, 0, NULL, 0644);

            if (!private_key_path || !public_key_path) {
                ui_error(op, "Failed to create one or more temporary files for "
                         "the module signing keys.");
                generate_failed = TRUE;
                goto generate_done;
            }

            /* Generate a key pair using openssl.
             * XXX We assume that sign-file requires the X.509 certificate
             * in DER format; if this changes in the future we will need
             * to be able to accommodate the actual required format. */

            cmdline = nvstrcat("cd ", p->kernel_module_build_directory, "; ",
                               op->utils[OPENSSL], " req -new -x509 -newkey "
                               "rsa:2048 -days 7300 -nodes -subj "
                               "\"/CN=nvidia-installer generated signing key/\""
                               " -keyout ", private_key_path,
                               " -outform DER -out ", public_key_path,
                               " -", x509_hash, NULL);
            nvfree(x509_hash);

            ret = run_command(op, cmdline, NULL, TRUE, 8, TRUE);

            nvfree(cmdline);

            if (ret != 0) {
                ui_error(op, "Failed to generate key pair!");
                generate_failed = TRUE;
                goto generate_done;
            }

            log_printf(op, NULL, "Signing keys generated successfully.");

            /* Set the signing keys to the newly generated pair. */

            op->module_signing_secret_key = nvstrdup(private_key_path);
            op->module_signing_public_key = nvstrdup(public_key_path);

generate_done:
            nvfree(private_key_path);
            nvfree(public_key_path);

            if (generate_failed) {
                return FALSE;
            }
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
     * sign the kernel module/s which we built earlier. */

    for (i = 0; i < p->num_kernel_modules; i++) {
        if (!sign_kernel_module(op, p->kernel_module_build_directory,
                                p->kernel_modules[i].module_filename, TRUE)) {
            return FALSE;
        }
    }

    if (generate_keys) {

        /* If keys were generated, we should install the verification cert
         * so that the user can make the kernel trust it, and either delete
         * or install the private signing key. */
        char *name, *result = NULL, *fingerprint, *cmdline;
        char short_fingerprint[9];
        int ret, delete_secret_key;

        delete_secret_key = ui_yes_no(op, TRUE, "The NVIDIA kernel module was "
                                      "successfully signed with a newly "
                                      "generated key pair. Would you like to "
                                      "delete the private signing key?");

        /* Get the fingerprint of the X.509 certificate. We already used 
           openssl to create a key pair at this point, so we know we have it;
           otherwise, we would have already returned by now. */
        cmdline = nvstrcat(op->utils[OPENSSL], " x509 -noout -fingerprint ",
                           "-inform DER -in ", op->module_signing_public_key,
                           NULL);
        ret = run_command(op, cmdline, &result, FALSE, 0, FALSE);
        nvfree(cmdline);

        /* Format: "SHA1 Fingerprint=00:00:00:00:..." */
        fingerprint = strchr(result, '=') + 1;

        if (ret != 0 || !fingerprint || strlen(fingerprint) < 40) {
            char *sha1sum = find_system_util("sha1sum");

            if (sha1sum) {
                /* the openssl command failed, or we parsed its output
                 * incorrectly; try to get a sha1sum of the DER certificate */
                cmdline = nvstrcat(sha1sum, " ", op->module_signing_public_key,
                                   NULL);
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
            strncpy(short_fingerprint, tmp, sizeof(short_fingerprint) - 1);
            nvfree(tmp);
        }
        short_fingerprint[sizeof(short_fingerprint) - 1] = '\0';

        /* Add the public key to the package */

        /* XXX name will be leaked when freeing package */
        name = nvstrcat("nvidia-modsign-crt-", short_fingerprint, ".der", NULL);

        add_package_entry(p,
                          nvstrdup(op->module_signing_public_key),
                          NULL, /* path */
                          name,
                          NULL, /* target */
                          NULL, /* dst */
                          FILE_TYPE_MODULE_SIGNING_KEY,
                          FILE_COMPAT_ARCH_NONE,
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

        if (delete_secret_key) {
            secure_delete(op, op->module_signing_secret_key);
        } else {

            /* Add the private key to the package */

            name = nvstrcat("nvidia-modsign-key-", short_fingerprint, ".key",
                            NULL);

            add_package_entry(p,
                              nvstrdup(op->module_signing_secret_key),
                              NULL, /* path */
                              name,
                              NULL, /* target */
                              NULL, /* dst */
                              FILE_TYPE_MODULE_SIGNING_KEY,
                              FILE_COMPAT_ARCH_NONE,
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
