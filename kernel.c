/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003-2009 NVIDIA Corporation
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

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <limits.h>
#include <fts.h>
#include <dlfcn.h>
#include <libkmod.h>

#include "nvidia-installer.h"
#include "kernel.h"
#include "user-interface.h"
#include "files.h"
#include "misc.h"
#include "precompiled.h"
#include "crc.h"
#include "conflicting-kernel-modules.h"

/* local prototypes */

static char *default_kernel_module_installation_path(Options *op);
static char *default_kernel_source_path(Options *op);
static char *find_module_substring(char *string, const char *substring);
static int check_for_loaded_kernel_module(Options *op, const char *);
static void check_for_warning_messages(Options *op);
static int sanity_check(Options *op, const char *dir,
                        const char *sanity_check_name,
                        const char *conftest_name);

static PrecompiledInfo *scan_dir(Options *op, Package *p,
                                 const char *directory_name,
                                 const char *proc_version_string,
                                 char *const *search_filelist);

static char *build_distro_precompiled_kernel_interface_dir(Options *op);
static char *convert_include_path_to_source_path(const char *inc);
static char *get_machine_arch(Options *op);
static int init_libkmod(void);
static void close_libkmod(void);
static int run_conftest(Options *op, const char *dir, const char *args,
                        char **result);
static int run_make(Options *op, Package *p, const char *dir, const char *target,
                    char **vars, const char *status, int lines);
static void load_kernel_module_quiet(Options *op, const char *module_name);
static void modprobe_remove_kernel_module_quiet(Options *op, const char *name);
static int kernel_configuration_conflict(Options *op, Package *p,
                                         int target_system_checks);

/* libkmod handle and function pointers */
static void *libkmod = NULL;
static struct kmod_ctx* (*lkmod_new)(const char*, const char* const*) = NULL;
static struct kmod_ctx* (*lkmod_unref)(struct kmod_ctx*) = NULL;
static struct kmod_module* (*lkmod_module_unref)(struct kmod_module *) = NULL;
static int (*lkmod_module_new_from_path)(struct kmod_ctx*, const char*,
             struct kmod_module**) = NULL;
static int (*lkmod_module_insert_module)(struct kmod_module*, unsigned int,
             const char*) = NULL;

/*
 * Message text that is used by several error messages.
 */

static const char install_your_kernel_source[] = 
"Please make sure you have installed the kernel source files for "
"your kernel and that they are properly configured; on Red Hat "
"Linux systems, for example, be sure you have the 'kernel-source' "
"or 'kernel-devel' RPM installed.  If you know the correct kernel "
"source files are installed, you may specify the kernel source "
"path with the '--kernel-source-path' command line option.";

 


/*
 * determine_kernel_module_installation_path() - get the installation
 * path for the kernel module.  The order is:
 *
 * - if op->kernel_module_installation_path is non-NULL, then it must
 *   have been initialized by the commandline parser, and therefore we
 *   should obey that (so just return).
 *
 * - get the default installation path
 *
 * - if in expert mode, ask the user, and use what they gave, if
 *   non-NULL
 */

int determine_kernel_module_installation_path(Options *op)
{
    char *result;
    int count = 0;
    
    if (op->kernel_module_installation_path) return TRUE;
    
    op->kernel_module_installation_path =
        default_kernel_module_installation_path(op);
    
    if (!op->kernel_module_installation_path) return FALSE;

    if (op->expert) {

    ask_for_kernel_install_path:

        result = ui_get_input(op, op->kernel_module_installation_path,
                              "Kernel module installation path");
        if (result && result[0]) {
            free(op->kernel_module_installation_path);
            op->kernel_module_installation_path = result;
            if (!confirm_path(op, op->kernel_module_installation_path)) {
                return FALSE;
            }
        } else {
            if (result) free(result);
            
            if (++count < NUM_TIMES_QUESTIONS_ASKED) {
                ui_warn(op, "Invalid kernel module installation path.");
                goto ask_for_kernel_install_path;
            } else {
                ui_error(op, "Unable to determine kernel module "
                         "installation path.");

                return FALSE;
            }
        }
    }

    if (!mkdir_with_log(op, op->kernel_module_installation_path, 0755))
        return FALSE;

    ui_expert(op, "Kernel module installation path: %s",
              op->kernel_module_installation_path);

    return TRUE;
    
} /* determine_kernel_module_installation_path() */



/*
 * run_conftest() - run conftest.sh with the given additional arguments; pass
 * the result back to the caller. Returns TRUE on success, or FALSE on failure.
 */

static int run_conftest(Options *op, const char *dir, const char *args,
                        char **result)
{
    char *cmd, *arch;
    int ret;

    if (result) {
        *result = NULL;
    }

    arch = get_machine_arch(op);
    if (!arch) {
        return FALSE;
    }

    cmd = nvstrcat("sh \"", dir, "/conftest.sh\" \"",
                   op->utils[CC], "\" \"",
                   op->utils[CC], "\" \"",
                   arch, "\" \"",
                   op->kernel_source_path, "\" \"",
                   op->kernel_output_path, "\" ",
                   args, NULL);

    ret = run_command(op, cmd, result, FALSE, 0, TRUE);
    nvfree(cmd);

    return ret == 0;
} /* run_conftest() */



/*
 * determine_kernel_source_path() - find the qualified path to the
 * kernel source tree.  This is called from install_from_cwd() if we
 * need to compile the kernel interface files.  Assigns
 * op->kernel_source_path and returns TRUE if successful.  Returns
 * FALSE if no kernel source tree was found.
 */

int determine_kernel_source_path(Options *op, Package *p)
{
    char *result;
    char *source_files[2], *source_path;
    int ret, count = 0;
    
    /* determine the kernel source path */
    
    op->kernel_source_path = default_kernel_source_path(op);
    
    if (op->expert) {
        
    ask_for_kernel_source_path:
        
        result = ui_get_input(op, op->kernel_source_path,
                              "Kernel source path");
        if (result && result[0]) {
            if (!directory_exists(result)) {
                ui_warn(op, "Kernel source path '%s' does not exist.",
                        result);
                free(result);
                
                if (++count < NUM_TIMES_QUESTIONS_ASKED) {
                    goto ask_for_kernel_source_path;
                } else {
                    op->kernel_source_path = NULL;
                }
            } else {
                op->kernel_source_path = result;
            }
        } else {
            ui_warn(op, "Invalid kernel source path.");
            if (result) free(result);
            
            if (++count < NUM_TIMES_QUESTIONS_ASKED) {
                goto ask_for_kernel_source_path;
            } else {
                op->kernel_source_path = NULL;
            }
        }
    }

    /* if we STILL don't have a kernel source path, give up */
    
    if (!op->kernel_source_path) {
        ui_error(op, "Unable to find the kernel source tree for the "
                 "currently running kernel.  %s", install_your_kernel_source);
        
        /*
         * I suppose we could ask them here for the kernel source
         * path, but we've already given users multiple methods of
         * specifying their kernel source tree.
         */
        
        return FALSE;
    }

    /* reject /usr as an invalid kernel source path */

    if (!strcmp(op->kernel_source_path, "/usr") ||
            !strcmp(op->kernel_source_path, "/usr/")) {
        ui_error (op, "The kernel source path '%s' is invalid.  %s",
                  op->kernel_source_path, install_your_kernel_source);
        op->kernel_source_path = NULL;
        return FALSE;
    }

    /* check that the kernel source path exists */

    if (!directory_exists(op->kernel_source_path)) {
        ui_error (op, "The kernel source path '%s' does not exist.  %s",
                  op->kernel_source_path, install_your_kernel_source);
        op->kernel_source_path = NULL;
        return FALSE;
    }

    /* check that <path>/include/linux/kernel.h exists */

    result = nvstrcat(op->kernel_source_path, "/include/linux/kernel.h", NULL);
    if (access(result, F_OK) == -1) {
        ui_error(op, "The kernel header file '%s' does not exist.  "
                 "The most likely reason for this is that the kernel source "
                 "path '%s' is incorrect.  %s", result,
                 op->kernel_source_path, install_your_kernel_source);
        free(result);
        return FALSE;
    }
    free(result);

    if (!determine_kernel_output_path(op)) return FALSE;

    ret = run_conftest(op, p->kernel_module_build_directory, "get_uname",
                       &result);

    if (!ret) {
        ui_error(op, "Unable to determine the version of the kernel "
                 "sources located in '%s'.  %s",
                 op->kernel_source_path, install_your_kernel_source);
        free(result);
        return FALSE;
    }

    if (strncmp(result, "2.4", 3) == 0) {
        source_files[0] = nvstrcat(op->kernel_source_path,
                                   "/include/linux/version.h", NULL);
        source_files[1] = NULL;
        source_path = op->kernel_source_path;
    } else {
        source_files[0] = nvstrcat(op->kernel_output_path,
                                   "/include/linux/version.h", NULL);
        source_files[1] = nvstrcat(op->kernel_output_path,
                                   "/include/generated/uapi/linux/version.h",
                                   NULL);
        source_path = op->kernel_output_path;
    }
    free(result);

    if (access(source_files[0], F_OK) != 0) {
        if (!source_files[1]) {
            ui_error(op, "The kernel header file '%s' does not exist.  "
                     "The most likely reason for this is that the kernel "
                     "source files in '%s' have not been configured.",
                     source_files[0], source_path);
            return FALSE;
        } else if (access(source_files[1], F_OK) != 0) {
            ui_error(op, "Neither the '%s' nor the '%s' kernel header "
                     "file exists.  The most likely reason for this "
                     "is that the kernel source files in '%s' have not been "
                     "configured.",
                     source_files[0], source_files[1], source_path);
            return FALSE;
        }
    }

    /* OK, we seem to have a path to a configured kernel source tree */
    
    ui_log(op, "Kernel source path: '%s'\n", op->kernel_source_path);
    ui_log(op, "Kernel output path: '%s'\n", op->kernel_output_path);
    
    return TRUE;
    
} /* determine_kernel_source_path() */


/*
 * determine_kernel_output_path() - determine the kernel output
 * path; unless specified, the kernel output path is assumed to be
 * the same as the kernel source path.
 */

int determine_kernel_output_path(Options *op)
{
    char *str, *tmp;
    int len;

    /* check --kernel-output-path */

    if (op->kernel_output_path) {
        ui_log(op, "Using the kernel output path '%s' as specified by the "
               "'--kernel-output-path' commandline option.",
               op->kernel_output_path);

        if (!directory_exists(op->kernel_output_path)) {
            ui_error(op, "The kernel output path '%s' does not exist.",
                     op->kernel_output_path);
            op->kernel_output_path = NULL;
            return FALSE;
        }

        return TRUE;
    }

    /* check SYSOUT */

    str = getenv("SYSOUT");
    if (str) {
        ui_log(op, "Using the kernel output path '%s', as specified by the "
               "SYSOUT environment variable.", str);
        op->kernel_output_path = str;

        if (!directory_exists(op->kernel_output_path)) {
            ui_error(op, "The kernel output path '%s' does not exist.",
                     op->kernel_output_path);
            op->kernel_output_path = NULL;
            return FALSE;
        }

        return TRUE;
    }

    /* check /lib/modules/`uname -r`/{source,build} */

    tmp = get_kernel_name(op);

    if (tmp) {
        str = nvstrcat("/lib/modules/", tmp, "/source", NULL);
        len = strlen(str);

        if (!strncmp(op->kernel_source_path, str, len)) {
            nvfree(str);
            str = nvstrcat("/lib/modules/", tmp, "/build", NULL);

            if (directory_exists(str)) {
                op->kernel_output_path = str;
                return TRUE;
            }
        }

        nvfree(str);
    }

    op->kernel_output_path = op->kernel_source_path;
    return TRUE;
}


/*
 * attach_signature() - If we have a detached signature, verify the checksum of
 * the linked module and append the signature.
 */
static int attach_signature(Options *op, Package *p,
                            const PrecompiledFileInfo *fileInfo,
                            const char *module_name) {
    uint32 actual_crc;
    char *module_path;
    int ret = FALSE, command_ret;

    const char *choices[2] = {
        "Install unsigned kernel module",
        "Abort installation"
    };

    ui_log(op, "Attaching module signature to linked kernel module.");

    module_path = nvstrcat(p->kernel_module_build_directory, "/",
                           fileInfo->target_directory, "/", module_name, NULL);

    command_ret = verify_crc(op, module_path, fileInfo->linked_module_crc,
                             &actual_crc);

    if (command_ret) {
        FILE *module_file;

        module_file = fopen(module_path, "a+");

        if (module_file && fileInfo->signature_size) {
            command_ret = fwrite(fileInfo->signature, 1,
                                 fileInfo->signature_size, module_file);
            if (command_ret == fileInfo->signature_size) {
                op->kernel_module_signed = ret = !ferror(module_file);
            }
        } else {
            ret = (ui_multiple_choice(op, choices, 2, 1,
                                      "A detached signature was included with "
                                      "the precompiled interface, but opening "
                                      "the linked kernel module and/or the "
                                      "signature file failed.\n\nThe detached "
                                      "signature will not be added; would you "
                                      "still like to install the unsigned "
                                      "kernel module?") == 0);
        }

        if (module_file) {
            fclose(module_file);
        }
    } else {
        ret = (ui_multiple_choice(op, choices, 2, 1,
                                  "A detached signature was included with the "
                                  "precompiled interface, but the checksum of "
                                  "the linked kernel module (%d) did not match "
                                  "the checksum of the the kernel module for "
                                  "which the detached signature was generated "
                                  "(%d).\n\nThis can happen if the linker on "
                                  "the installation target system is not the "
                                  "same as the linker on the system that built "
                                  "the precompiled interface.\n\nThe detached "
                                  "signature will not be added; would you "
                                  "still like to install the unsigned kernel "
                                  "module?", actual_crc,
                                  fileInfo->linked_module_crc) == 0);
    }

    if (ret) {
        if(op->kernel_module_signed) {
            ui_log(op, "Signature attached successfully.");
        } else {
            ui_log(op, "Signature not attached.");
        }
    } else {
        ui_error(op, "Failed to attach signature.");
    }

    nvfree(module_path);
    return ret;
} /* attach_signature() */


/*
 * unpack_kernel_modules() - unpack the precompiled file bundle, and link any
 * prebuilt kernel interface against their respective binary-only core object
 * files. This results in a complete kernel module, ready for installation.
 *
 * e.g.: ld -r -o nvidia.ko nv-linux.o nvidia/nv-kernel.o_binary
 *
 * If the precompiled file is a complete kernel module instead of an interface
 * file, no additional action is needed after unpacking.
 */

int unpack_kernel_modules(Options *op, Package *p, const char *build_directory,
                          const PrecompiledFileInfo *fileInfo)
{
    char *cmd, *result;
    int ret;
    uint32 attrmask;

    if (fileInfo->type != PRECOMPILED_FILE_TYPE_INTERFACE &&
        fileInfo->type != PRECOMPILED_FILE_TYPE_MODULE) {
        ui_error(op, "The file does not appear to be a valid precompiled "
                 "kernel interface or module.");
        return FALSE;
    }

    ret = precompiled_file_unpack(op, fileInfo, build_directory);
    if (!ret) {
        ui_error(op, "Failed to unpack the precompiled file.");
        return FALSE;
    } else if (fileInfo->type == PRECOMPILED_FILE_TYPE_MODULE) {
        ui_log(op, "Kernel module unpacked successfully.");
        return TRUE;
    }

    cmd = nvstrcat("cd ", build_directory,
                   "; ", op->utils[LD], " ", LD_OPTIONS, " -o ",
                   fileInfo->linked_module_name, " ",
                   fileInfo->target_directory, "/", fileInfo->name, " ",
                   fileInfo->target_directory, "/", fileInfo->core_object_name,
                   NULL);

    ret = run_command(op, cmd, &result, TRUE, 0, TRUE);

    free(cmd);

    if (ret != 0) {
        ui_error(op, "Unable to link kernel module.");
        return FALSE;
    }

    ui_log(op, "Kernel module linked successfully.");

    attrmask = PRECOMPILED_ATTR(DETACHED_SIGNATURE) |
               PRECOMPILED_ATTR(LINKED_MODULE_CRC);

    if ((fileInfo->attributes & attrmask) == attrmask) {
        return attach_signature(op, p, fileInfo,
                                fileInfo->linked_module_name);
    }

    return TRUE;
   
}


static int check_file(Options *op, Package *p, const char *dir,
                      const char *modname)
{
    int ret;
    char *path;

    path = nvstrcat(dir, "/", modname, ".ko", NULL);
    ret = access(path, F_OK);

    if (ret == -1) {
        char *single_module_list = nvstrcat("NV_KERNEL_MODULES=\"", modname,
                                            "\"", NULL);
        char *rebuild_msg = nvstrcat("Checking to see whether the ", modname,
                                     " kernel module was successfully built",
                                     NULL);
        /* Attempt to rebuild the individual module, in case the failure
         * is module-specific and due to a different module */
        run_make(op, p, dir, single_module_list, NULL, rebuild_msg, 25);
        nvfree(single_module_list);
        nvfree(rebuild_msg);

        /* Check the file again */
        ret = access(path, F_OK);
    }

    nvfree(path);

    if (ret == -1) {
        ui_error(op, "The %s kernel module was not created.", modname);
    }

    return ret != -1;
}



/*
 * Build the kernel modules that part of this package.
 */

int build_kernel_modules(Options *op, Package *p)
{
    return build_kernel_interfaces(op, p, NULL);
}


/*
 * Run the module_signing_script with three or four arguments.
 * Return TRUE on success.
 */
static int try_sign_file(Options *op, const char *file, int num_args)
{
    char *cmd;
    const char *arg_1;
    int ret;

    switch (num_args) {
        case 3: arg_1 = ""; break;
        case 4: arg_1 = op->module_signing_hash; break;
        default: return FALSE;
    }

    cmd = nvstrcat("\"", op->module_signing_script, "\" ",
                   arg_1, " \"",
                   op->module_signing_secret_key, "\" \"",
                   op->module_signing_public_key, "\" \"",
                   file, "\"", NULL);
    ret = run_command(op, cmd, NULL, TRUE, 1, TRUE);
    nvfree(cmd);

    return ret == 0;
}

/*
 * sign_kernel_module() - sign a kernel module. The caller is responsible
 * for ensuring that the kernel module is already built successfully and that
 * op->module_signing_{secret,public}_key are set.
 */
int sign_kernel_module(Options *op, const char *build_directory, 
                       const char *module_filename, int status) {
    char *file;
    int success;
    static int num_args = 3;

    /* Lazily set the default value for module_signing_script. */

    if (!op->module_signing_script) {
        op->module_signing_script = nvstrcat(op->kernel_source_path,
                                             "/scripts/sign-file", NULL);
    }

    if (status) {
        ui_status_begin(op, "Signing kernel module:", "Signing");
    }

    file = nvstrcat(build_directory, "/", module_filename, NULL);

  try_sign:

    success = try_sign_file(op, file, num_args);

    /* If sign-file failed to run with three arguments, try running it with
     * four arguments on all subsequent signing attempts. */

    if (num_args == 3 && !success) {
        num_args = 4;

        /* The four-arg version of sign-file needs a hash. */

        if (!op->module_signing_hash) {
            op->module_signing_hash = guess_module_signing_hash(op,
                                          build_directory);
        }

        if (op->module_signing_hash) {
            goto try_sign;
        } else {
            ui_error(op, "The installer was unable to sign %s without "
                     "specifying a hash algorithm on the command line to %s, "
                     "and was also unable to automatically detect the hash. "
                     "If you need to sign the NVIDIA kernel modules, please "
                     "try again and set the '--module-signing-hash' option on "
                     "the installer's command line.",
                     file, op->module_signing_script);
        }
    }

    nvfree(file);

    if (status) {
        ui_status_end(op, success ? "done." : "Failed to sign kernel module.");
    } else {
        ui_log(op, success ? "Signed kernel module." : "Module signing failed");
    }

    op->kernel_module_signed = success;
    return success;
}



/*
 * create_detached_signature() - Link a precompiled interface into a module,
 * sign the resulting linked module, and store a CRC for the linked, unsigned
 * module and the detached signature in the provided PrecompiledFileInfo record.
 */
static int create_detached_signature(Options *op, Package *p,
                                     const char *build_dir,
                                     PrecompiledFileInfo *fileInfo,
                                     const char *module_filename)
{
    int ret, command_ret;
    struct stat st;
    char *module_path = NULL, *error = NULL, *target_dir = NULL;

    ui_status_begin(op, "Creating a detached signature for the linked "
                    "kernel module:", "Linking module");

    ret = unpack_kernel_modules(op, p, build_dir, fileInfo);

    if (!ret) {
        ui_error(op, "Failed to link a kernel module for signing.");
        goto done;
    }

    target_dir = nvstrcat(build_dir, "/", fileInfo->target_directory, NULL);
    module_path = nvstrcat(target_dir, "/", module_filename, NULL);
    command_ret = stat(module_path, &st);

    if (command_ret != 0) {
        ret = FALSE;
        error = "Unable to determine size of linked module.";
        goto done;
    }

    ui_status_update(op, .25, "Generating module checksum");

    fileInfo->linked_module_crc = compute_crc(op, module_path);
    fileInfo->attributes |= PRECOMPILED_ATTR(LINKED_MODULE_CRC);

    ui_status_update(op, .50, "Signing linked module");

    ret = sign_kernel_module(op, target_dir, module_filename, FALSE);

    if (!ret) {
        error = "Failed to sign the linked kernel module.";
        goto done;
    }

    ui_status_update(op, .75, "Detaching module signature");

    fileInfo->signature_size = byte_tail(module_path, st.st_size,
                                         &(fileInfo->signature));

    if (!(fileInfo->signature) || fileInfo->signature_size == 0) {
        error = "Failed to detach the module signature";
        goto done;
    }

    fileInfo->attributes |= PRECOMPILED_ATTR(DETACHED_SIGNATURE);

done:
    if (ret) {
        ui_status_end(op, "done.");
    } else {
        ui_status_end(op, "Error.");
        if (error) {
            ui_error(op, "%s", error);
        }
    }

    nvfree(module_path);
    nvfree(target_dir);
    return ret;
}


/*
 * pack_kernel_interface() - Store the input built interfaces in the
 * PrecompiledFileInfo array.
 *
 * Returns true if the packing was successful, or false on error.
 */
static int pack_kernel_interface(Options *op, Package *p,
                                 const char *build_dir,
                                 PrecompiledFileInfo *fileInfo,
                                 const char *kernel_interface,
                                 const char *module_filename,
                                 const char *core_file)
{
    int command_ret;
    char *file_path = nvstrcat(build_dir, "/", kernel_interface, NULL);

    command_ret = precompiled_read_interface(fileInfo, file_path,
                                             module_filename,
                                             core_file, ".");

    nvfree(file_path);

    if (command_ret) {
        if (op->module_signing_secret_key && op->module_signing_public_key) {
            if (!create_detached_signature(op, p, build_dir, fileInfo,
                                           module_filename)) {
                return FALSE;
            }
        }
        return TRUE;
    }

    return FALSE;
}


static void handle_optional_module_failure(Options *op,
                                           KernelModuleInfo module,
                                           const char *action) {
    if (module.is_optional) {
        ui_error(op, "The %s kernel module failed to %s. This kernel module "
                 "is required for the proper operation of %s. If you do not "
                 "need to use %s, you can try to install this driver package "
                 "again with the '--%s' option.",
                 module.module_name, action, module.optional_module_dependee,
                 module.optional_module_dependee, module.disable_option);
    }
}


static int pack_kernel_module(Options *op,
                              const char *build_dir,
                              PrecompiledFileInfo *fileInfo,
                              const char *module_filename)
{
    int command_ret;
    char *file_path = nvstrcat(build_dir, "/", module_filename, NULL);

    if (op->module_signing_secret_key && op->module_signing_public_key) {
        if (sign_kernel_module(op, build_dir, module_filename, FALSE)) {
            fileInfo->attributes |= PRECOMPILED_ATTR(EMBEDDED_SIGNATURE);
        } else {
            ui_error(op, "Failed to sign precompiled kernel module %s!",
                     module_filename);
            return FALSE;
        }
    }

    command_ret = precompiled_read_module(fileInfo, file_path, "");

    nvfree(file_path);

    if (command_ret) {
        return TRUE;
    }

    return FALSE;
}




/*
 * build_kernel_interfaces() - build the kernel modules and interfaces, and
 * store any precompiled files in a newly allocated PrecompiledFileInfo array.
 * Return the number of packaged files, or 0 on error.
 *
 * If fileInfos is NULL, stop after building the kernel modules and do not
 * build the interfaces.
 *
 * When building interfaces, copy everything into a temporary directory and
 * work out of the tmpdir. For kernel module only builds, operate within
 * p->kernel_module_build_directory, as later packaging steps will look for
 * the built files there. If a tmpdir is created, it is removed before exit.
 */

int build_kernel_interfaces(Options *op, Package *p,
                            PrecompiledFileInfo ** fileInfos)
{
    char *tmpdir = NULL, *builddir;
    int ret, files_packaged = 0, i;

    struct {
        const char *sanity_check_name;
        const char *conftest_name;
    } sanity_checks[] = {
        { "Compiler", "cc_sanity_check" },
        { "Dom0", "dom0_sanity_check" },
        { "Xen", "xen_sanity_check" },
        { "PREEMPT_RT", "preempt_rt_sanity_check" },
        { "vgpu_kvm", "vgpu_kvm_sanity_check" },
    };

    /* do not build if there is a kernel configuration conflict (and don't
     * perform target system checks if we're only building interfaces) */

    if (kernel_configuration_conflict(op, p, fileInfos == NULL)) {
        return 0;
    }

    if (fileInfos) {
        *fileInfos = NULL;
    }

    /* create a temporary directory if we will be packing interfaces */

    if (fileInfos) {
        tmpdir = make_tmpdir(op);
        builddir = tmpdir;

        if (!tmpdir) {
            ui_error(op, "Unable to create a temporary build directory.");
            goto done;
        }

        /* copy the kernel module sources to it */

        ui_log(op, "Copying kernel module sources to temporary directory.");

        if (!copy_directory_contents
            (op, p->kernel_module_build_directory, tmpdir)) {
            ui_error(op, "Unable to copy the kernel module sources to temporary "
                     "directory '%s'.", tmpdir);
            goto done;
        }
    } else {
        builddir = p->kernel_module_build_directory;
    }
    
    /*
     * touch the contents of the build directory, to avoid make time
     * skew error messages
     */

    /* run sanity checks */
    for (i = 0; i < ARRAY_LEN(sanity_checks); i++) {
        if (!sanity_check(op, builddir,
                          sanity_checks[i].sanity_check_name,
                          sanity_checks[i].conftest_name)) {
            return FALSE;
        }
    }

    ui_log(op, "Cleaning kernel module build directory.");
    run_make(op, p, builddir, "clean", NULL, NULL, 0);
    ret = run_make(op, p, builddir, "", NULL, "Building kernel modules", 25);

    /* Test to make sure that all kernel modules were built. */
    for (i = 0; i < p->num_kernel_modules; i++) {
        if (!check_file(op, p, builddir, p->kernel_modules[i].module_name)) {
            handle_optional_module_failure(op, p->kernel_modules[i], "build");
            goto done;
        }
    }

    /*
     * Now check the status of the overall build: it may have failed, despite
     * having produced all expected kernel modules.
     */
    if (!ret) {
        goto done;
    }

    ui_log(op, "Kernel module compilation complete.");

    /*
     * If we're not building interfaces, return the number of built modules
     * instead of the number of packaged interfaces.
     */
    if (fileInfos == NULL) {
        /* If we've made it this far, all the modules were built. */
        files_packaged = p->num_kernel_modules;
        goto done;
    }

    *fileInfos = nvalloc(sizeof(PrecompiledFileInfo) *
        (p->num_kernel_modules + 1));

    for (files_packaged = 0;
         files_packaged < p->num_kernel_modules;
         files_packaged++) {
        PrecompiledFileInfo *fileInfo = *fileInfos + files_packaged;
        KernelModuleInfo *module = p->kernel_modules + files_packaged;

        if (module->has_separate_interface_file) {
            if (!(run_make(op, p, tmpdir, module->interface_filename,
                           NULL, NULL, 0) &&
                pack_kernel_interface(op, p, tmpdir, fileInfo,
                                      module->interface_filename,
                                      module->module_filename,
                                      module->core_object_name))) {
                goto done;
            }
        } else if (!pack_kernel_module(op, tmpdir, fileInfo,
                                       module->module_filename)) {
                goto done;
        }
    }

done:

    if (files_packaged == 0 && fileInfos) {
        nvfree(*fileInfos);
        *fileInfos = NULL;
    }

    if (tmpdir) {
        remove_directory(op, tmpdir);
        nvfree(tmpdir);
    }

    return files_packaged;
}




/*
 * check_for_warning_messages() - check if the kernel module detected
 * problems with the target system and registered warning messages
 * for us with the Linux /proc interface. If yes, show these messages
 * to the user.
 */

void check_for_warning_messages(Options *op)
{
    char *paths[2] = { "/proc/driver/nvidia/warnings", NULL };
    FTS *fts;
    FTSENT *ent;
    char *buf = NULL;

    fts = fts_open(paths, FTS_LOGICAL, NULL);
    if (!fts) return;

    while ((ent = fts_read(fts)) != NULL) {
        switch (ent->fts_info) {
            case FTS_F:
                if ((strlen(ent->fts_name) == 6) &&
                    !strncmp("README", ent->fts_name, 6))
                    break;
                if (read_text_file(ent->fts_path, &buf)) {
                    ui_warn(op, "%s", buf);
                    nvfree(buf);
                }
                break;
            default:
                /* ignore this file entry */
                break;
        }
    }

    fts_close(fts);

} /* check_for_warning_messages() */



/*
 * init_libkmod() - Attempt to dlopen() libkmod and the function symbols we
 * need from it. Set the global libkmod handle and function pointers on
 * success. Return TRUE if loading all symbols succeeded; FALSE otherwise.
 */
static int init_libkmod(void)
{
    if (!libkmod) {
        libkmod = dlopen("libkmod.so.2", RTLD_LAZY);
        if(!libkmod) {
            return FALSE;
        }

        lkmod_new = dlsym(libkmod, "kmod_new");
        lkmod_unref = dlsym(libkmod, "kmod_unref");
        lkmod_module_unref = dlsym(libkmod, "kmod_module_unref");
        lkmod_module_new_from_path = dlsym(libkmod, "kmod_module_new_from_path");
        lkmod_module_insert_module = dlsym(libkmod, "kmod_module_insert_module");
    }

    if (libkmod) {
        /* libkmod was already open, or was just successfully dlopen()ed:
         * check to make sure all of the symbols are set */
        if (lkmod_new && lkmod_unref && lkmod_module_unref &&
            lkmod_module_new_from_path && lkmod_module_insert_module) {
            return TRUE;
        } else {
            /* One or more symbols missing; abort */
            close_libkmod();
            return FALSE;
        }
    }
    return FALSE;
} /* init_libkmod() */



/*
 * close_libkmod() - clear all libkmod function pointers and dlclose() libkmod
 */
static void close_libkmod(void)
{
    if (libkmod) {
        dlclose(libkmod);
    }

    libkmod = NULL;
    lkmod_new = NULL;
    lkmod_unref = NULL;
    lkmod_module_unref = NULL;
    lkmod_module_new_from_path = NULL;
    lkmod_module_insert_module = NULL;
} /* close_libkmod() */



#define PRINTK_LOGLEVEL_KERN_ALERT 1

/*
 * Attempt to set the printk loglevel, first using the /proc/sys interface,
 * and falling back to the deprecated sysctl if that fails. Pass the previous
 * loglevel back to the caller and return TRUE on success, or FALSE on failure.
 */
static int set_loglevel(int level, int *old_level)
{
    FILE *fp;
    int loglevel_set = FALSE;

    fp = fopen("/proc/sys/kernel/printk", "r+");
    if (fp) {
        if (!old_level || fscanf(fp, "%d ", old_level) == 1) {
            char *strlevel = nvasprintf("%d", level);

            fseek(fp, 0, SEEK_SET);
            if (fwrite(strlevel, strlen(strlevel), 1, fp) == 1) {
                loglevel_set = TRUE;
            }

            nvfree(strlevel);
        }
        fclose(fp);
    }

    if (!loglevel_set) {
        /*
         * Explicitly initialize the value of len, even though it looks like the
         * syscall should do that, since in practice it doesn't always actually
         * set the value of the pointed-to length parameter.
         */
        size_t len = sizeof(int);
        int name[] = { CTL_KERN, KERN_PRINTK };

        if (!old_level ||
            sysctl(name, ARRAY_LEN(name), old_level, &len, NULL, 0) == 0) {
            if (sysctl(name, ARRAY_LEN(name), NULL, 0, &level, len) == 0) {
                loglevel_set = TRUE;
            }
        }
    }

    return loglevel_set;
}


/*
 * do_insmod() - load the kernel module using libkmod if available; fall back
 * to insmod otherwise. Returns the result of kmod_module_insert_module() if
 * available, or of insmod otherwise. Pass the result of module loading up
 * through the data argument, regardless of whether we used libkmod or insmod.
 */
static int do_insmod(Options *op, const char *module, char **data,
                     const char *module_opts)
{
    int ret = 0, libkmod_failed = FALSE, old_loglevel, loglevel_set, i;

    /* SELinux type labels to add to kernel modules */
    static const char *selinux_kmod_types[] = {
        "modules_object_t",
        NULL
    };

    *data = NULL;

    /*
     * Temporarily disable most console messages to keep the curses
     * interface from being clobbered when the module is loaded.
     * Save the original console loglevel to allow restoring it once
     * we're done.
     */

    loglevel_set = set_loglevel(PRINTK_LOGLEVEL_KERN_ALERT, &old_loglevel);

    /* Some SELinux policies require specific file type labels for inserting
     * kernel modules. Attempt to set known types until one succeeds. If all
     * types fail, hopefully it's just because no type is needed. */
    for (i = 0; selinux_kmod_types[i]; i++) {
        if (set_security_context(op, module, selinux_kmod_types[i])) {
            break;
        }
    }

    if (init_libkmod()) {
        struct kmod_ctx *ctx = NULL;
        struct kmod_module *mod = NULL;
        const char *config_paths = NULL;

        ctx = lkmod_new(NULL, &config_paths);
        if (!ctx) {
            libkmod_failed = TRUE;
            goto kmod_done;
        }

        ret = lkmod_module_new_from_path(ctx, module, &mod);
        if (ret < 0) {
            libkmod_failed = TRUE;
            goto kmod_done;
        }

        ret = lkmod_module_insert_module(mod, 0, module_opts);
        if (ret < 0) {
            /* insmod ignores > 0 return codes of kmod_module_insert_module(),
             * so we should do it too. On failure, strdup() the error string to
             * *data to ensure that it can be freed later. */
            *data = nvstrdup(strerror(-ret));
        }

kmod_done:
        if (mod) {
            lkmod_module_unref(mod);
        }
        if (ctx) {
            lkmod_unref(ctx);
        }
    } else {
        if (op->expert) {
            ui_log(op, "Unable to load module with libkmod; "
                   "falling back to insmod.");
        }
        libkmod_failed = TRUE;
    }

    if (!libkmod || libkmod_failed) {
        char *cmd;

        /* Fall back to insmod */

        cmd = nvstrcat(op->utils[INSMOD], " ", module, " ", module_opts, NULL);

        /* only output the result of the test if in expert mode */

        ret = run_command(op, cmd, data, op->expert, 0, TRUE);
        nvfree(cmd);
    }

    close_libkmod();

    if (loglevel_set) {
        set_loglevel(old_loglevel, NULL);
    }

    return ret;
} /* do_insmod() */


/*
 * Determine if the load error be ignored or not. Also print detailed 
 * error messages corresponding to the return status from do_install(). 
 *
 * Returns true if user chooses to ignore the load error, else, false.
 */

static int ignore_load_error(Options *op, Package *p, 
                             const char *module_filename,
                             const char* data, int insmod_status)
{
    int ignore_error = FALSE, secureboot, module_sig_force, enokey;
    const char *probable_reason, *signature_related;

    enokey = (-insmod_status == ENOKEY);
    secureboot = (secure_boot_enabled() == 1);
    module_sig_force =
        (test_kernel_config_option(op, p, "CONFIG_MODULE_SIG_FORCE") ==
         KERNEL_CONFIG_OPTION_DEFINED);

    if (enokey) {
        probable_reason = ",";
        signature_related = "";
    } else if (module_sig_force) {
        probable_reason = ". CONFIG_MODULE_SIG_FORCE is set on the target "
                          "kernel, so this is likely";
    } else if (secureboot) {
        probable_reason = ". Secure boot is enabled on this system, so "
                          "this is likely";
    } else {
        probable_reason = ", possibly";
    }

    if (!enokey) {
        signature_related = "if this module loading failure is due to the "
                            "lack of a trusted signature, ";
    }

    if (enokey || secureboot || module_sig_force || op->expert) {
        if (op->kernel_module_signed) {

            const char *choices[2] = {
                "Install signed kernel module",
                "Abort installation"
            };

            ignore_error = (ui_multiple_choice(op, choices, 2, 0,
                                               "The signed kernel module failed "
                                               "to load%s because the kernel "
                                               "does not trust any key which is "
                                               "capable of verifying the module "
                                               "signature. Would you like to "
                                               "install the signed kernel module "
                                               "anyway?\n\nNote that %syou "
                                               "will not be able to load the "
                                               "installed module until after a "
                                               "key that can verify the module "
                                               "signature is added to a key "
                                               "database that is trusted by the "
                                               "kernel. This will likely "
                                               "require rebooting your computer.",
                                               probable_reason,
                                               signature_related) == 0);
        } else {
            const char *secureboot_message, *dkms_message;

            secureboot_message = secureboot == 1 ?
                                     "and sign the kernel module when "
                                     "prompted to do so." :
                                     "and set the --module-signing-secret-"
                                     "key and --module-signing-public-key "
                                     "options on the command line, or run "
                                     "the installer in expert mode to "
                                     "enable the interactive module "
                                     "signing prompts.";

            dkms_message = op->dkms ? " Module signing is incompatible "
                                      "with DKMS, so please select the "
                                      "non-DKMS option when building the "
                                      "kernel module to be signed." : "";
            ui_error(op, "The kernel module failed to load%s because it "
                     "was not signed by a key that is trusted by the "
                     "kernel. Please try installing the driver again, %s%s",
                     probable_reason, secureboot_message, dkms_message);
        }
    }

    if (ignore_error) {
        ui_log(op, "An error was encountered when loading the kernel "
               "module, but that error was ignored, and the kernel module "
               "will be installed, anyway. The error was: %s", data);
    } else {
        ui_error(op, "Unable to load the kernel module '%s'.  This "
                 "happens most frequently when this kernel module was "
                 "built against the wrong or improperly configured "
                 "kernel sources, with a version of gcc that differs "
                 "from the one used to build the target kernel, or "
                 "if a driver such as rivafb, nvidiafb, or nouveau is "
                 "present and prevents the NVIDIA kernel module from "
                 "obtaining ownership of the NVIDIA graphics device(s), "
                 "or no NVIDIA GPU installed in this system is supported "
                 "by this NVIDIA Linux graphics driver release.\n\n"
                 "Please see the log entries 'Kernel module load "
                 "error' and 'Kernel messages' at the end of the file "
                 "'%s' for more information.",
                 module_filename, op->log_file_name);

        /*
         * if in expert mode, run_command() would have caused this to
         * be written to the log file; so if not in expert mode, print
         * the output now.
         */

        if (!op->expert) ui_log(op, "Kernel module load error: %s", data);
    }

    return ignore_error; 
} /* ignore_load_error() */


/*
 * attempt to unload all kernel modules, but don't abort if this fails: the
 * kernel may not have been configured with support for module unloading
 * (Linux 2.6).
 */

static void unload_kernel_modules(Options *op, Package *p) {
    int i;

    for (i = p->num_kernel_modules - 1; i >= 0; i--) {
        rmmod_kernel_module(op, p->kernel_modules[i].module_name);
    }
}


/*
 * test_kernel_module() - attempt to insmod the kernel modules and then rmmod
 * them.  Return TRUE if the insmod succeeded, or FALSE otherwise.
 */

int test_kernel_modules(Options *op, Package *p)
{
    char *cmd = NULL, *data = NULL;
    int ret, i;
    const char *depmods[] = { "i2c-core", "drm", "drm-kms-helper", "vfio_mdev" };

    /* 
     * If we're building/installing for a different kernel, then we
     * can't test the module now.
     */

    if (op->kernel_name) return TRUE;

    /*
     * Attempt to load modules that nvidia.ko might depend on.  Silently ignore
     * failures: if nvidia.ko doesn't depend on the module that failed, the test
     * load below will succeed and it doesn't matter that the load here failed.
     */
    for (i = 0; i < ARRAY_LEN(depmods); i++) {
        load_kernel_module_quiet(op, depmods[i]);
    }

    /*
     * Attempt to load each kernel module one at a time. The order of the list
     * in the package manifest is set such that loading modules in that order
     * should satisfy any dependencies that exist between modules.
     */

    for (i = 0; i < p->num_kernel_modules; i++) {
        int module_success = FALSE;
        char *cmd_output;
        char *module_path = nvstrcat(p->kernel_module_build_directory, "/",
                                     p->kernel_modules[i].module_filename,
                                     NULL);
        const char *module_opts = "";

        if (strcmp(p->kernel_modules[i].module_name, "nvidia") == 0) {
            module_opts = "NVreg_DeviceFileUID=0 NVreg_DeviceFileGID=0 "
                          "NVreg_DeviceFileMode=0 NVreg_ModifyDeviceFiles=0";
        }

        ret = do_insmod(op, module_path, &cmd_output, module_opts);
        nvfree(module_path);

        if (ret == 0) {
            module_success = TRUE;
        } else {
            ret = ignore_load_error(op, p, p->kernel_modules[i].module_filename,
                                    cmd_output, ret);
            if (ret) {
                op->skip_module_load = TRUE;
                ui_log(op, "Ignoring failure to load %s.",
                       p->kernel_modules[i].module_filename);
            } else {
                handle_optional_module_failure(op, p->kernel_modules[i],
                                               "load");
            }
        }

        nvfree(cmd_output);

        if (!module_success) {
            goto test_exit;
        }
    }

    /*
     * check if the kernel module detected problems with this
     * system's kernel and display any warning messages it may
     * have prepared for us.
     */

    check_for_warning_messages(op);

    unload_kernel_modules(op, p);

    ret = TRUE;

test_exit:
    /*
     * display/log the last few lines of the kernel ring buffer
     * to provide further details in case of a load failure or
     * to capture NVRM warning messages, if any.
     */
    cmd = nvstrcat(op->utils[DMESG], " | ",
                   op->utils[TAIL], " -n 25", NULL);

    if (!run_command(op, cmd, &data, FALSE, 0, TRUE))
        ui_log(op, "Kernel messages:\n%s", data);

    nvfree(cmd);
    nvfree(data);

    /*
     * Unload dependencies that might have been loaded earlier.
     */

    for (i = 0; i < ARRAY_LEN(depmods); i++) {
        modprobe_remove_kernel_module_quiet(op, depmods[i]);
    }

    return ret;
}



/*
 * modprobe_helper() - run modprobe; used internally by other functions.
 *
 * module_name: the name of the kernel module to modprobe
 * quiet:       load/unload the kernel module silently if TRUE
 * unload:      remove a kernel module instead of loading it if TRUE
 *              (Note: unlike `rmmod`, `modprobe -r` handles dependencies.
 */

static int modprobe_helper(Options *op, const char *module_name,
                           int quiet, int unload)
{
    int ret = 0, old_loglevel, loglevel_set;
    char *cmd, *data;

    if (op->skip_module_load) {
        return TRUE;
    }

    cmd = nvstrcat(op->utils[MODPROBE],
                   quiet ? " -q" : "",
                   unload ? " -r" : "",
                   " ", module_name,
                   NULL);

    loglevel_set = set_loglevel(PRINTK_LOGLEVEL_KERN_ALERT, &old_loglevel);

    ret = run_command(op, cmd, &data, FALSE, 0, TRUE);

    if (loglevel_set) {
        set_loglevel(old_loglevel, NULL);
    }

    nvfree(cmd);

    if (!quiet && ret != 0) {
        char *expert_detail = nvstrcat(": '", data, "'", NULL);
        ui_error(op, "Unable to %s the '%s' kernel module%s",
                 unload ? "unload" : "load", module_name,
                 op->expert? expert_detail : ".");
        nvfree(expert_detail);
    }

    nvfree(data);

    return ret == 0;
}

int load_kernel_module(Options *op, const char *module_name)
{
    return modprobe_helper(op, module_name, FALSE, FALSE);
}

static void load_kernel_module_quiet(Options *op, const char *module_name)
{
    modprobe_helper(op, module_name, TRUE, FALSE);
}

static void modprobe_remove_kernel_module_quiet(Options *op, const char *name)
{
    modprobe_helper(op, name, TRUE, TRUE);
}

/*
 * Attempt to load all kernel modules that are part of the Package. Returns
 * the number of successfully loaded kernel modules.
 */
int load_kernel_modules(Options *op, Package *p)
{
    int i;

    for (i = 0; i < p->num_kernel_modules; i++) {
        if (!load_kernel_module(op, p->kernel_modules[i].module_name)) {
            break;
        }
    }

    return i;
}



/*
 * check_for_unloaded_kernel_module() - test if any of the "bad"
 * kernel modules are loaded; if they are, then try to unload it.  If
 * we can't unload it, then report an error and return FALSE;
 */

int check_for_unloaded_kernel_module(Options *op)
{
    int n;
    int loaded = FALSE;
    unsigned long long int bits = 0;

    /*
     * We can skip this check if we are installing for a non-running
     * kernel and only installing a kernel module.
     */

    if (op->kernel_module_only && op->kernel_name) {
        ui_log(op, "Only installing a kernel module for a non-running "
               "kernel; skipping the \"is an NVIDIA kernel module loaded?\" "
               "test.");
        return TRUE;
    }

    /*
     * We can also skip this check if we aren't installing a kernel
     * module at all.
     */

    if (op->no_kernel_module) {
        ui_log(op, "Not installing a kernel module; skipping the \"is an "
               "NVIDIA kernel module loaded?\" test.");
        return TRUE;
    }

    for (n = 0; n < num_conflicting_kernel_modules; n++) {
        if (check_for_loaded_kernel_module(op, conflicting_kernel_modules[n])) {
            loaded = TRUE;
            bits |= (1 << n);
        }
    }

    if (!loaded) return TRUE;

    /* one or more kernel modules is loaded... try to unload them */

    for (n = 0; n < num_conflicting_kernel_modules; n++) {
        if (!(bits & (1 << n))) {
            continue;
        }

        rmmod_kernel_module(op, conflicting_kernel_modules[n]);

        /* check again */

        if (check_for_loaded_kernel_module(op, conflicting_kernel_modules[n])) {
            ui_error(op,  "An NVIDIA kernel module '%s' appears to already "
                     "be loaded in your kernel.  This may be because it is "
                     "in use (for example, by an X server, a CUDA program, "
                     "or the NVIDIA Persistence Daemon), but this may also "
                     "happen if your kernel was configured without support "
                     "for module unloading.  Please be sure to exit any "
                     "programs that may be using the GPU(s) before attempting "
                     "to upgrade your driver.  If no GPU-based programs are "
                     "running, you know that your kernel supports module "
                     "unloading, and you still receive this message, then an "
                     "error may have occured that has corrupted an NVIDIA "
                     "kernel module's usage count, for which the simplest "
                     "remedy is to reboot your computer.",
                     conflicting_kernel_modules[n]);

            return FALSE;
        }
    }

    return TRUE;

}


/*
 * find_precompiled_kernel_interface() - do assorted black magic to
 * determine if the given package contains a precompiled kernel interface
 * for the kernel on this system.
 *
 * XXX it would be nice to extend this so that a kernel module could
 * be installed for a kernel other than the currently running one.
 */

PrecompiledInfo *find_precompiled_kernel_interface(Options *op, Package *p)
{
    char *proc_version_string, *tmp;
    PrecompiledInfo *info = NULL;
    char **search_filelist = NULL;
    int i;
  
    /* allow the user to completely skip this search */
    
    if (op->no_precompiled_interface) {
        ui_log(op, "Not probing for precompiled kernel interfaces.");
        return NULL;
    }
    
    /* retrieve the proc version string for the running kernel */

    proc_version_string = read_proc_version(op, op->proc_mount_point);
    
    if (!proc_version_string) goto done;
    
    /* make sure the target directory exists */
    
    if (!mkdir_recursive(op, p->kernel_module_build_directory, 0755, FALSE))
        goto done;

    search_filelist = nvalloc((p->num_kernel_modules + 1) * sizeof(char*));

    for (i = 0; i < p->num_kernel_modules; i++) {
        if (p->kernel_modules[i].has_separate_interface_file) {
            search_filelist[i] = p->kernel_modules[i].interface_filename;
        } else {
            search_filelist[i] = p->kernel_modules[i].module_filename;
        }
    }

    /*
     * if the --precompiled-kernel-interfaces-path option was
     * specified, search that directory, first
     */
    
    if (op->precompiled_kernel_interfaces_path) {
        info = scan_dir(op, p, op->precompiled_kernel_interfaces_path,
                        proc_version_string, search_filelist);
    }
    
    /*
     * If we didn't find a match, search for distro-provided
     * precompiled kernel interfaces
     */

    if (!info) {
        tmp = build_distro_precompiled_kernel_interface_dir(op);
        if (tmp) {
            info = scan_dir(op, p, tmp, proc_version_string, search_filelist);
            nvfree(tmp);
        }
    }

    /*
     * if we still haven't found a match, search in
     * p->precompiled_kernel_interface_directory (the directory
     * containing the precompiled kernel interfaces shipped with the
     * package)
     */

    if (!info) {
        info = scan_dir(op, p, p->precompiled_kernel_interface_directory,
                        proc_version_string, search_filelist);
    }
    
    /* If we found one, ask expert users if they really want to use it */

    if (info && op->expert) {
        const char *choices[2] = {
            "Use the precompiled interface",
            "Compile the interface"
        };

        if (ui_multiple_choice(op, choices, 2, 0, "A precompiled kernel "
                               "interface for the kernel '%s' has been found.  "
                               "Would you like use the precompiled interface, "
                               "or would you like to compile the interface "
                               "instead?", info->description) == 1) {
            free_precompiled(info);
            info = NULL;
        }
    }

 done:

    nvfree(search_filelist);

    nvfree(proc_version_string);

    if (!info && op->expert) {
        ui_message(op, "No precompiled kernel interface was found to match "
                   "your kernel; this means that the installer will need to "
                   "compile a new kernel interface.");
    }

    return info;
}



/*
 * get_kernel_name() - get the kernel name: this is either what
 * the user specified via the --kernel-name option, or `uname -r`.
 */

char __kernel_name[256];

char *get_kernel_name(Options *op)
{
    struct utsname uname_buf;

    __kernel_name[0] = '\0';

    if (uname(&uname_buf) == -1) {
        ui_warn(op, "Unable to determine the version of the running kernel "
                "(%s).", strerror(errno));
    } else {
        strncpy(__kernel_name, uname_buf.release, sizeof(__kernel_name));
        __kernel_name[sizeof(__kernel_name) - 1] = '\0';
    }

    if (op->kernel_name) {
        if (strcmp(op->kernel_name, __kernel_name) != 0) {
            /* Don't load kernel modules built against a non-running kernel */
            op->skip_module_load = TRUE;
        }
        return op->kernel_name;
    }

    if (__kernel_name[0]) {
        return __kernel_name;
    }

    return NULL;
} /* get_kernel_name() */



/*
 * test_kernel_config_option() - test to see if the given option is defined
 * in the target kernel's configuration.
 */

KernelConfigOptionStatus test_kernel_config_option(Options* op, Package *p,
                                                   const char *option)
{
    if (op->kernel_source_path && op->kernel_output_path) {
        int ret;
        char *conftest_cmd;

        conftest_cmd = nvstrcat("test_configuration_option ", option, NULL);
        ret = run_conftest(op, p->kernel_module_build_directory, conftest_cmd,
                           NULL);
        nvfree(conftest_cmd);

        return ret ? KERNEL_CONFIG_OPTION_DEFINED :
                     KERNEL_CONFIG_OPTION_NOT_DEFINED;
    }

    return KERNEL_CONFIG_OPTION_UNKNOWN;
}



/*
 * guess_module_signing_hash() - return the hash algorithm used for
 * signing kernel modules, or NULL if it can't be determined.
 */

char *guess_module_signing_hash(Options *op, const char *build_directory)
{
    char *ret;

    if (run_conftest(op, build_directory,
                     "guess_module_signing_hash", &ret)) {
        return ret;
    }

    return NULL;
}

/*
 ***************************************************************************
 * local static routines
 ***************************************************************************
 */



/*
 * default_kernel_module_installation_path() - do the equivalent of:
 *
 * SYSSRC = /lib/modules/$(shell uname -r)
 *
 * ifeq ($(shell if test -d $(SYSSRC)/kernel; then echo yes; fi),yes)
 *   INSTALLDIR = $(SYSSRC)/kernel/drivers/video
 * else
 *   INSTALLDIR = $(SYSSRC)/video
 * endif
 */

static char *default_kernel_module_installation_path(Options *op)
{
    char *str, *tmp;
    
    tmp = get_kernel_name(op);
    if (!tmp) return NULL;

    str = nvstrcat("/lib/modules/", tmp, "/kernel", NULL);
    
    if (directory_exists(str)) {
        free(str);
        str = nvstrcat("/lib/modules/", tmp, "/kernel/drivers/video", NULL);
        return str;
    }

    free(str);
    
    str = nvstrcat("/lib/modules/", tmp, "/video", NULL);

    return str;

} /* default_kernel_module_installation_path() */



/*
 * default_kernel_source_path() - determine the default kernel
 * source path, if possible.  Return NULL if no default kernel path
 * is found.
 *
 * Here is the logic:
 *
 * if --kernel-source-path was set, use that
 * 
 * if --kernel-include-path was set, use that (converting it to the
 * source path); also print a warning that --kernel-include-path is
 * deprecated.
 *
 * else if SYSSRC is set, use that
 *
 * else if /lib/modules/`uname -r`/build exists use that
 *
 * else if /usr/src/linux exists use that
 *
 * else return NULL
 *
 * One thing to note is that for the first two methods
 * (--kernel-source-path and $SYSSRC) we don't check for directory
 * existence before returning.  This is intentional: if the user set
 * one of these, then they're trying to set a particular path.  If
 * that directory doesn't exist, then better to abort installation with
 * an appropriate error message in determine_kernel_source_path().
 * Whereas, for the later two (/lib/modules/`uname -r`/build
 * and /usr/src/linux), these are not explicitly requested by
 * the user, so it makes sense to only use them if they exist.
 */ 

static char *default_kernel_source_path(Options *op)
{
    char *str, *tmp;
    
    str = tmp = NULL;
    
    /* check --kernel-source-path */

    if (op->kernel_source_path) {
        ui_log(op, "Using the kernel source path '%s' as specified by the "
               "'--kernel-source-path' commandline option.",
               op->kernel_source_path);
        return op->kernel_source_path;
    }

    /* check --kernel-include-path */

    if (op->kernel_include_path) {
        ui_warn(op, "The \"--kernel-include-path\" option is deprecated "
                "(as part of reorganization to support Linux 2.6); please use "
                "\"--kernel-source-path\" instead.");
        str = convert_include_path_to_source_path(op->kernel_include_path);
        ui_log(op, "Using the kernel source path '%s' (inferred from the "
               "'--kernel-include-path' commandline option '%s').",
               str, op->kernel_include_path);
        return str;
    }

    /* check SYSSRC */
    
    str = getenv("SYSSRC");
    if (str) {
        ui_log(op, "Using the kernel source path '%s', as specified by the "
               "SYSSRC environment variable.", str);
        return str;
    }
    
    /* check /lib/modules/`uname -r`/build and /usr/src/linux-`uname -r` */
    
    tmp = get_kernel_name(op);

    if (tmp) {
        str = nvstrcat("/lib/modules/", tmp, "/source", NULL);

        if (directory_exists(str)) {
            return str;
        }

        nvfree(str);

        str = nvstrcat("/lib/modules/", tmp, "/build", NULL);
    
        if (directory_exists(str)) {
            return str;
        }

        nvfree(str);

        /*
         * check "/usr/src/linux-`uname -r`", too; patch suggested by
         * Peter Berg Larsen <pebl@math.ku.dk>
         */

        str = nvstrcat("/usr/src/linux-", tmp, NULL);
        if (directory_exists(str)) {
            return str;
        }

        free(str);
    }

    /* finally, try /usr/src/linux */

    if (directory_exists("/usr/src/linux")) {
        return "/usr/src/linux";
    }
    
    return NULL;
    
} /* default_kernel_source_path() */


/*
 * find_module_substring() - find substring in a given string where differences
 * between hyphens and underscores are ignored. Returns a pointer to the
 * beginning of the substring, or NULL if the string/substring is NULL, or if
 * length of substring is greater than length of string, or substring is not
 * found.
 */

static char *find_module_substring(char *string, const char *substring)
{
    int string_len, substring_len, len;
    char *tstr;
    const char *tsubstr;

    if ((string == NULL) || (substring == NULL))
        return NULL;

    string_len = strlen(string);
    substring_len = strlen(substring);

    for (len = 0; len <= string_len - substring_len; len++, string++) {
        if (*string != *substring) {
            continue;
        }

        for (tstr = string, tsubstr = substring;
             *tsubstr != '\0';
             tstr++, tsubstr++) {
            if (*tstr != *tsubstr) {
                if (((*tstr == '-') || (*tstr == '_')) &&
                    ((*tsubstr == '-') || (*tsubstr == '_')))
                    continue;
                break;
            }
        }

        if (*tsubstr == '\0')
            return string;
    }

    return NULL;
} /* find_module_substring */


/*
 * substring_is_isolated() - given the string 'substring' with length 'len',
 * which points to a location inside the string 'string', check to see if
 * 'substring' is surrounded by either whitespace or the start/end of 'string'
 * on both ends.
 */

static int substring_is_isolated(const char *substring, const char *string,
                                 int len)
{
    if (substring != string) {
        if (!isspace(substring[-1])) {
            return FALSE;
        }
    }

    if (substring[len] && !isspace(substring[len])) {
        return FALSE;
    }

    return TRUE;
}


/*
 * check_for_loaded_kernel_module() - check if the specified kernel
 * module is currently loaded using `lsmod`.  Returns TRUE if the
 * kernel module is loaded; FALSE if it is not.
 *
 * Be sure to check that the character following the kernel module
 * name is a space (to avoid getting false positivies when the given
 * kernel module name is contained within another kernel module name.
 */

static int check_for_loaded_kernel_module(Options *op, const char *module_name)
{
    char *result = NULL;
    int ret, found = FALSE;

    ret = run_command(op, op->utils[LSMOD], &result, FALSE, 0, TRUE);
    
    if ((ret == 0) && (result) && (result[0] != '\0')) {
        char *ptr;
        int len = strlen(module_name);

        for (ptr = result;
             (ptr = find_module_substring(ptr, module_name));
             ptr += len) {
            if (substring_is_isolated(ptr, result, len)) {
                found = TRUE;
                break;
            }
        }
    }
    
    if (result) free(result);
    
    return found;
    
} /* check_for_loaded_kernel_module() */


/*
 * rmmod_kernel_module() - run `rmmod $module_name`
 */

int rmmod_kernel_module(Options *op, const char *module_name)
{
    int ret, old_loglevel, loglevel_set;
    char *cmd;
    
    cmd = nvstrcat(op->utils[RMMOD], " ", module_name, NULL);

    loglevel_set = set_loglevel(PRINTK_LOGLEVEL_KERN_ALERT, &old_loglevel);
    
    ret = run_command(op, cmd, NULL, FALSE, 0, TRUE);

    if (loglevel_set) {
        set_loglevel(old_loglevel, NULL);
    }

    free(cmd);
    
    return ret ? FALSE : TRUE;
    
} /* rmmod_kernel_module() */




/*
 * check_cc_version() - check if the selected or default system
 * compiler is compatible with the one that was used to build the
 * currently running kernel.
 */

int check_cc_version(Options *op, Package *p)
{
    char *result;
    int ret;
    Options dummyop;

    const char *choices[2] = {
        "Ignore CC version check",
        "Abort installation"
    };

    /* 
     * If we're building/installing for a different kernel, then we
     * can't do the gcc version check (we don't have a /proc/version
     * string from which to get the kernel's gcc version).
     * If the user passes the option no-cc-version-check, then we also
     * shouldn't perform the cc version check.
     */

    if (op->ignore_cc_version_check) {
        setenv("IGNORE_CC_MISMATCH", "1", 1);
        return TRUE;
    }

    /* Kernel source/output paths may not be set yet; we don't need them
     * for this test, anyway. */
    dummyop = *op;
    dummyop.kernel_source_path = "DUMMY_SOURCE_PATH";
    dummyop.kernel_output_path = "DUMMY_OUTPUT_PATH";

    ret = run_conftest(&dummyop, p->kernel_module_build_directory,
                       "cc_version_check just_msg", &result);

    if (ret) return TRUE;

    ret = (ui_multiple_choice(op, choices, 2, 1, "The CC version check failed:"
                              "\n\n%s\n\nIf you know what you are doing you "
                              "can either ignore the CC version check and "
                              "continue installation, or abort installation, "
                              "set the CC environment variable to the name of "
                              "the compiler used to compile your kernel, and "
                              "restart installation.", result) == 1);
    nvfree(result);
    
    if (!ret) setenv("IGNORE_CC_MISMATCH", "1", 1);
    
    return !ret;
             
} /* check_cc_version() */


/*
 * sanity_check() - run the given sanity check conftest; if the test fails,
 * print the error message from the test. Return the status from the test.
 */

static int sanity_check(Options *op, const char *dir,
                        const char *sanity_check_name,
                        const char *conftest_name)
{
    char *result, *conftest_args;
    int ret;

    ui_log(op, "Performing %s check.", sanity_check_name);

    conftest_args = nvstrcat(conftest_name, " just_msg", NULL);
    ret = run_conftest(op, dir, conftest_args, &result);
    nvfree(conftest_args);

    if (!ret && result) {
        ui_error(op, "%s", result);
    }

    nvfree(result);
    return ret;
}



/*
 * scan_dir() - scan through the specified directory for a matching
 * precompiled kernel interface.
 */

static PrecompiledInfo *scan_dir(Options *op, Package *p,
                                 const char *directory_name,
                                 const char *proc_version_string,
                                 char *const *search_filelist)
{
    DIR *dir;
    struct dirent *ent;
    PrecompiledInfo *info = NULL;
    char *filename;
    
    if (!directory_name) return NULL;
    
    dir = opendir(directory_name);
    if (!dir) return NULL;

    /*
     * loop over all contents of the directory, looking for a
     * precompiled kernel interface that matches the running kernel
     */

    while ((ent = readdir(dir)) != NULL) {

        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;
            
        filename = nvstrcat(directory_name, "/", ent->d_name, NULL);
        
        info = get_precompiled_info(op, filename, proc_version_string,
                                    p->version, search_filelist);

        free(filename);

        if (info) break;
    }
        
    if (closedir(dir) != 0) {
        ui_error(op, "Failure while closing directory '%s' (%s).",
                 directory_name,
                 strerror(errno));
    }
    
    return info;
    
} /* scan_dir() */



/*
 * build_distro_precompiled_kernel_interface_dir() - construct this
 * path:
 *
 *  /lib/modules/precompiled/`uname -r`/nvidia/gfx/
 */

static char *build_distro_precompiled_kernel_interface_dir(Options *op)
{
    struct utsname uname_buf;
    char *str;
    
    if (uname(&uname_buf) == -1) {
        ui_error(op, "Unable to determine kernel version (%s)",
                 strerror(errno));
        return NULL;
    }
    
    str = nvstrcat("/lib/modules/precompiled/", uname_buf.release,
                   "/nvidia/gfx/", NULL);
    
    return str;

} /* build_distro_precompiled_kernel_interface_dir() */



/*
 * convert_include_path_to_source_path() - given input to
 * "--kernel-include-path", convert it to "--kernel-source-path" by
 * scanning from the end to the previous "/".
 */

static char *convert_include_path_to_source_path(const char *inc)
{
    char *c, *str;

    str = nvstrdup(inc);

    /* go to the end of the string */

    for (c = str; *c; c++);
    
    /* move to the last printable character */

    c--;

    /* if the string ends in '/'; backup one more */

    if (*c == '/') c--;

    /* now back up to the next '/' */

    while ((c >= str) && (*c != '/')) c--;

    if (*c == '/') *c = '\0';

    return str;

} /* convert_include_path_to_source_path() */


/*
 * get_machine_arch() - get the machine architecture, substituting
 * i386 for i586 and i686 or arm for arm7l.
 */

static char __machine_arch[16];

char *get_machine_arch(Options *op)
{
    struct utsname uname_buf;

    if (uname(&uname_buf) == -1) {
        ui_warn(op, "Unable to determine machine architecture (%s).",
                strerror(errno));
        return NULL;
    } else {
        if ((strncmp(uname_buf.machine, "i586", 4) == 0) ||
            (strncmp(uname_buf.machine, "i686", 4) == 0)) {
            strcpy(__machine_arch, "i386");
        } else if ((strncmp(uname_buf.machine, "armv", 4) == 0)) {
            strcpy(__machine_arch, "arm");
        } else {
            strncpy(__machine_arch, uname_buf.machine,
                    sizeof(__machine_arch));
        }
        return __machine_arch;
    }

} /* get_machine_arch() */

/*
 * Run `make` with the specified target and make variables. The variables
 * are given in the form of a NULL-terminated array of alternating key
 * and value strings, e.g. { "KEY1", "value1", "KEY2", "value2", NULL }.
 * If a 'status' string is given, then a ui_status progress bar is shown
 * using 'status' as the initial message, expecting 'lines' lines of output
 * from the make command.
 */
static int run_make(Options *op, Package *p, const char *dir, const char *target,
                    char **vars, const char *status, int lines) {
    char *cmd, *concurrency, *data = NULL;
    int i = 0, ret;

    concurrency = nvasprintf(" -j%d ", op->concurrency_level);

    cmd = nvstrcat("cd ", dir, "; ",
                   op->utils[MAKE], " -k", concurrency, target,
                   " NV_EXCLUDE_KERNEL_MODULES=\"",
                   p->excluded_kernel_modules, "\"",
                   " SYSSRC=\"", op->kernel_source_path, "\"",
                   " SYSOUT=\"", op->kernel_output_path, "\"",
                   NULL);
    nvfree(concurrency);

    if (vars) {
        while (vars[i] && vars[i+1]) {
            char *old_cmd = cmd;

            cmd = nvstrcat(old_cmd, " ", vars[i], "=\"", vars[i+1], "\"", NULL);
            nvfree(old_cmd);
            i += 2;
        }
    }

    if (status) {
        ui_status_begin(op, status, "");
    }

    ret = (run_command(op, cmd, &data, TRUE, status ? lines : 0, TRUE) == 0);

    if (status) {
        if (ret) {
            ui_status_end(op, "done.");
        } else {
            ui_status_end(op, "Error.");
        }
    }

    if (!ret) {
        char *status_extra;

        if (status) {
            status_extra = nvasprintf(" while performing the step: \"%s\"",
                                       status);
        } else {
            status_extra = nvstrdup("");
        }

        ui_error(op, "An error occurred%s. See " DEFAULT_LOG_FILE_NAME
                     " for details.", status_extra);
        ui_log(op, "The command `%s` failed with the following output:\n\n%s",
               cmd, data);
        nvfree(status_extra);
    }

    nvfree(cmd);
    nvfree(data);

    return ret;
}


/*
 * Remove any instances of kernel module 'module' from the kernel modules
 * list in the package.
 */
int remove_kernel_module_from_package(Package *p, const char *module)
{
    int i, found = 0;

    for (i = 0; i < p->num_kernel_modules; i++) {
        if (found) {
            p->kernel_modules[i - found] = p->kernel_modules[i];
        }

        if (strcmp(p->kernel_modules[i].module_name, module) == 0) {
            free_kernel_module_info(p->kernel_modules[i]);
            found++;
        }
    }

    if (found) {
        char *old_exclude_list;

        p->num_kernel_modules -= found;
        old_exclude_list = p->excluded_kernel_modules;
        p->excluded_kernel_modules = nvstrcat(module, " ", old_exclude_list,
                                              NULL);
        nvfree(old_exclude_list);
    }

    return found;
}


void free_kernel_module_info(KernelModuleInfo info)
{
    nvfree(info.module_name);
    nvfree(info.module_filename);
    if (info.has_separate_interface_file) {
        nvfree(info.interface_filename);
        nvfree(info.core_object_name);
    }
}


int package_includes_kernel_module(const Package *p, const char *module)
{
    int i;

    for (i = 0; i < p->num_kernel_modules; i++) {
        if (strcmp(p->kernel_modules[i].module_name, module) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

#if defined(NV_PPC64LE)
/*
 * Check the current policy for onlining new memory blocks.
 * Returns NV_OPTIONAL_BOOL_TRUE if the policy is to auto-online new blocks,
 * NV_OPTIONAL_BOOL_FALSE if the policy is to leave new blocks offline, and
 * NV_OPTIONAL_BOOL_DEFAULT if the policy cannot be determined.
 */
static NVOptionalBool auto_online_blocks(void)
{
    static const char *file = "/sys/devices/system/memory/auto_online_blocks";
    int fd;
    NVOptionalBool ret = NV_OPTIONAL_BOOL_DEFAULT;

    fd = open(file, O_RDONLY);
    if (fd >= 0) {
        char auto_online_status[9]; /* strlen("offline\n") + 1 */
        ssize_t bytes_read = read(fd, auto_online_status,
                                  sizeof(auto_online_status) - 1);

        if (bytes_read >= 0) {
            int i = bytes_read;

            /* NUL-terminate and truncate any trailing whitespace */
            do {
                auto_online_status[i] = '\0';
                i--;
            } while (i >= 0 && isspace(auto_online_status[i]));

            if (strcmp(auto_online_status, "online") == 0) {
                ret = NV_OPTIONAL_BOOL_TRUE;
            } else if (strcmp(auto_online_status, "offline") == 0) {
                ret = NV_OPTIONAL_BOOL_FALSE;
            }
        }

        close(fd);
    }

    return ret;
}

/*
 * Get the CPU type from /proc/cpuinfo: this is ppc64le-only for now because
 * it's only used by ppc64le-only code, and because the contents of the cpuinfo
 * file in procfs vary greatly by CPU architecture.
 */
static char *get_cpu_type(const Options *op)
{
    char *proc_cpuinfo = nvstrcat(op->proc_mount_point, "/", "cpuinfo", NULL);
    FILE *fp = fopen(proc_cpuinfo, "r");

    nvfree(proc_cpuinfo);
    if (fp) {
        char *line, *ret = NULL;
        int eof;

        while((line = fget_next_line(fp, &eof))) {
            ret = nvrealloc(ret, strlen(line) + 1);

            if (sscanf(line, "cpu : %s", ret) == 1) {
                return ret;
            }

            if (eof) {
                break;
            }
        }

        nvfree(ret);
    }

    return NULL;
}
#endif

/*
 * Check the kernel configuration for potential conflicts. Return TRUE if a
 * conflict is detected, or FALSE if no conflict is detected, or the user
 * chooses to ignore a conflict. target_system_checks enables additional
 * checks that are only valid when performed on the target system.
 */
static int kernel_configuration_conflict(Options *op, Package *p,
                                         int target_system_checks)
{
/* Auto-onlining is problematic on POWER9 and Volta or later */
#if defined(NV_PPC64LE)
    if (test_kernel_config_option(op, p, "CONFIG_MEMORY_HOTPLUG_DEFAULT_ONLINE")
        == KERNEL_CONFIG_OPTION_DEFINED) {
        NVOptionalBool auto_online = NV_OPTIONAL_BOOL_DEFAULT;

        if (target_system_checks) {
            char *cpu_type = get_cpu_type(op);
            int cpu_is_power8 = strncmp(cpu_type, "POWER8", strlen("POWER8")) == 0;

            nvfree(cpu_type);
            if (cpu_is_power8) {
                /* No conflict on pre-POWER9 systems */
                return FALSE;
            }

            auto_online = auto_online_blocks();
        }

        if (auto_online == NV_OPTIONAL_BOOL_FALSE) {
            ui_log(op, "CONFIG_MEMORY_HOTPLUG_DEFAULT_ONLINE is enabled, but "
                   "current policy is offline new memory blocks; continuing.");
        } else {
            int choice;
            const char *msg = "CONFIG_MEMORY_HOTPLUG_DEFAULT_ONLINE is enabled "
                              "on the target kernel. Some NVIDIA GPUs on some "
                              "system configurations will not work correctly "
                              "with auto-onlined memory; if you are not sure "
                              "whether your system will work, configure your "
                              "bootloader to set memhp_default_state=offline "
                              "on the kernel command line, or build a kernel "
                              "with CONFIG_MEMORY_HOTPLUG_DEFAULT_ONLINE "
                              "disabled in the kernel configuration. If you "
                              "choose to make these changes, you may abort "
                              "the installation now in order to make them.";

            choice = ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
                                        NUM_CONTINUE_ABORT_CHOICES,
                                        ABORT_CHOICE, "%s", msg);

            return choice == ABORT_CHOICE;
        }
    }
#endif

    return FALSE;
}
