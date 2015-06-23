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
#include "snarf.h"
#include "crc.h"

/* local prototypes */

static char *default_kernel_module_installation_path(Options *op);
static char *default_kernel_source_path(Options *op);
static char *find_module_substring(char *string, const char *substring);
static int check_for_loaded_kernel_module(Options *op, const char *);
static void check_for_warning_messages(Options *op);
static PrecompiledInfo *download_updated_kernel_interface(Options*, Package*,
                                                          const char*, 
                                                          char* const*);
static int fbdev_check(Options *op, Package *p);
static int xen_check(Options *op, Package *p);
static int preempt_rt_check(Options *op, Package *p);

static PrecompiledInfo *scan_dir(Options *op, Package *p,
                                 const char *directory_name,
                                 const char *proc_version_string,
                                 char *const *search_filelist);

static char *build_distro_precompiled_kernel_interface_dir(Options *op);
static char *convert_include_path_to_source_path(const char *inc);
static char *get_machine_arch(Options *op);
static int init_libkmod(void);
static void close_libkmod(void);
static int run_conftest(Options *op, Package *p, const char *args,
                        char **result);
static void replace_zero(char* filename, int i);
static void load_kernel_module_quiet(Options *op, const char *module_name);
static void modprobe_remove_kernel_module_quiet(Options *op, const char *name);

/* libkmod handle and function pointers */
static void *libkmod = NULL;
static struct kmod_ctx* (*lkmod_new)(const char*, const char* const*) = NULL;
static struct kmod_ctx* (*lkmod_unref)(struct kmod_ctx*) = NULL;
static struct kmod_module* (*lkmod_module_unref)(struct kmod_module *) = NULL;
static int (*lkmod_module_new_from_path)(struct kmod_ctx*, const char*,
             struct kmod_module**) = NULL;
static int (*lkmod_module_insert_module)(struct kmod_module*, unsigned int,
             const char*) = NULL;
static void free_search_filelist(char **);

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

static int run_conftest(Options *op, Package *p, const char *args, char **result)
{
    char *cmd, *arch;
    int ret;

    if (result)
        *result = NULL;

    arch = get_machine_arch(op);
    if (!arch)
        return FALSE;

    cmd = nvstrcat("sh \"", p->kernel_module_build_directory,
                   "/conftest.sh\" \"", op->utils[CC], "\" \"", op->utils[CC],
                   "\" \"", arch, "\" \"", op->kernel_source_path, "\" \"",
                   op->kernel_output_path, "\" ", args, NULL);

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

    ret = run_conftest(op, p, "get_uname", &result);

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
 * Look for the presence of char '0' in filename and replace
 * it with the integer value passed as input. This is used
 * especially for multiple kernel module builds.
 */

static void replace_zero(char *filename, int i)
{
    char *name;

    if (i < 0 || i > 9) return;

    name = strrchr(filename, '0');
    if (name) *name = *name + i;
}


/*
 * link_kernel_module() - link the prebuilt kernel interface against
 * the binary-only core of the kernel module.  This results in a
 * complete kernel module, ready for installation.
 *
 *
 * ld -r -o nvidia.o nv-linux.o nv-kernel.o
 */

int link_kernel_module(Options *op, Package *p, const char *build_directory,
                       const PrecompiledFileInfo *fileInfo)
{
    char *cmd, *result;
    int ret;
    uint32 attrmask;

    if (fileInfo->type != PRECOMPILED_FILE_TYPE_INTERFACE) {
        ui_error(op, "The file does not appear to be a valid precompiled "
                 "kernel interface.");
        return FALSE;
    }

    ret = precompiled_file_unpack(op, fileInfo, build_directory);
    if (!ret) {
        ui_error(op, "Failed to unpack the precompiled interface.");
        return FALSE;
    }

    cmd = nvstrcat("cd ", build_directory, "/", fileInfo->target_directory,
                   "; ", op->utils[LD], " ", LD_OPTIONS, " -o ",
                   fileInfo->linked_module_name, " ",
                   fileInfo->name, " ", fileInfo->core_object_name, NULL);

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
   
} /* link_kernel_module() */



static int build_kernel_module_helper(Options *op, const char *dir,
                                      const char *module, int num_instances)
{
    int ret;
    char *instances = NULL, *cmd, *tmp, *concurrency;

    tmp = op->multiple_kernel_modules && num_instances ?
              nvasprintf("%d", num_instances) : NULL;
    instances = nvstrcat(" NV_BUILD_MODULE_INSTANCES=", tmp, NULL);
    nvfree(tmp);

    concurrency = nvasprintf(" -j%d ", op->concurrency_level);

    tmp = nvasprintf("Building %s kernel module:", module);
    ui_status_begin(op, tmp, "Building");
    nvfree(tmp);

    cmd = nvstrcat("cd ", dir, "; ", op->utils[MAKE], " module",
                   " SYSSRC=", op->kernel_source_path,
                   " SYSOUT=", op->kernel_output_path, concurrency,
                   instances, NULL);
    nvfree(instances);
    nvfree(concurrency);

    ret = run_command(op, cmd, NULL, TRUE, 25, TRUE);

    nvfree(cmd);

    if (ret != 0) {
        ui_status_end(op, "Error.");
        ui_error(op, "Unable to build the %s kernel module.", module);
        /* XXX need more descriptive error message */

        return FALSE;
    }

    ui_status_end(op, "done.");

    return TRUE;
}


static int check_file(Options *op, const char *dir, const char *filename,
                      const char *modname)
{
    int ret;
    char *path;

    path = nvstrcat(dir, "/", filename, NULL);
    ret = access(path, F_OK);
    nvfree(path);

    if (ret == -1) {
        ui_error(op, "The NVIDIA %s module was not created.", modname);
    }

    return ret != -1;
}


/*
 * build_kernel_module() - determine the kernel include directory,
 * copy the kernel module source files into a temporary directory, and
 * compile nvidia.o.
 */

int build_kernel_module(Options *op, Package *p)
{
    char *result, *cmd;
    int ret;

    /*
     * touch all the files in the build directory to avoid make time
     * skew messages
     */
    
    touch_directory(op, p->kernel_module_build_directory);
    
    /*
     * Check if conftest.sh can determine the Makefile, there's
     * no hope for the make rules if this fails.
     */
    ret = run_conftest(op, p, "select_makefile just_msg", &result);

    if (!ret) {
        if (result)
            ui_error(op, "%s", result); /* display conftest.sh's error message */
        nvfree(result);
        return FALSE;
    }

    if (!fbdev_check(op, p)) return FALSE;
    if (!xen_check(op, p)) return FALSE;
    if (!preempt_rt_check(op, p)) return FALSE;

    ui_log(op, "Cleaning kernel module build directory.");
    
    cmd = nvstrcat("cd ", p->kernel_module_build_directory, "; ",
                   op->utils[MAKE], " clean", NULL);

    ret = run_command(op, cmd, &result, TRUE, 0, TRUE);
    free(result);
    free(cmd);

    ret = build_kernel_module_helper(op, p->kernel_module_build_directory,
                                     "NVIDIA", op->num_kernel_modules);

    if (!ret) {
        return FALSE;
    }
    
    /* check that the frontend file actually exists */
    if (op->multiple_kernel_modules) {
        if (!check_file(op, p->kernel_module_build_directory,
                        p->kernel_frontend_module_filename, "frontend")) {
            return FALSE;
        }
    }

    /* check that the file actually exists */
    if (!check_file(op, p->kernel_module_build_directory,
                    p->kernel_module_filename, "kernel")) {
        return FALSE;
    }

    /*
     * Build the UVM kernel module. The build directory from the previous kernel
     * module build must not be cleaned first, as the UVM kernel module will
     * depend on the Module.symvers file produced by that build.
     */

    if (op->install_uvm) {
        ret = build_kernel_module_helper(op, p->uvm_module_build_directory,
                                         "Unified Memory", 0);

        ret = ret && check_file(op, p->uvm_module_build_directory,
                                p->uvm_kernel_module_filename, "Unified Memory");

        if (!ret) {
            ui_error(op, "The Unified Memory kernel module failed to build. "
                     "This kernel module is required for the proper operation "
                     "of CUDA. If you do not need to use CUDA, you can try to "
                     "install this driver package again with the "
                     "'--no-unified-memory' option.");
            return FALSE;
        }
    }

    ui_log(op, "Kernel module compilation complete.");

    return TRUE;
    
} /* build_kernel_module() */



/*
 * sign_kernel_module() - sign the kernel module. The caller is responsible
 * for ensuring that the kernel module is already built successfully and that
 * op->module_signing_{secret,public}_key are set.
 */
int sign_kernel_module(Options *op, const char *build_directory, 
                       const char *module_suffix, int status) {
    char *cmd, *mod_sign_cmd, *mod_sign_hash;
    int ret, success;
    char *build_module_instances_parameter, *concurrency;

    /* if module_signing_script isn't set, then set mod_sign_cmd below to end
     * the nvstrcat() that builds cmd early. */
    mod_sign_cmd = op->module_signing_script ?
                   nvstrcat(" mod_sign_cmd=", op->module_signing_script, NULL) :
                   NULL;

    mod_sign_hash = op->module_signing_hash ?
                    nvstrcat(" CONFIG_MODULE_SIG_HASH=",
                             op->module_signing_hash, NULL) :
                    NULL;

    if (status) {
        ui_status_begin(op, "Signing kernel module:", "Signing");
    }

    if (op->multiple_kernel_modules) {
        build_module_instances_parameter = 
                                 nvasprintf(" NV_BUILD_MODULE_INSTANCES=%d",
                                            NV_MAX_MODULE_INSTANCES);
    }
    else {
        build_module_instances_parameter = nvstrdup("");
    }

    concurrency = nvasprintf(" -j%d ", op->concurrency_level);
    cmd = nvstrcat("cd ", build_directory, "; ", op->utils[MAKE], " module-sign"
                   " SYSSRC=", op->kernel_source_path,
                   " SYSOUT=", op->kernel_output_path,
                   " MODSECKEY=", op->module_signing_secret_key,
                   " MODPUBKEY=", op->module_signing_public_key,
                   " BUILD_MODULES_LIST=\"nvidia", module_suffix, "\" ",
                   build_module_instances_parameter,
                   mod_sign_cmd ? mod_sign_cmd : "",
                   mod_sign_hash ? mod_sign_hash : "", concurrency, NULL);
    nvfree(concurrency);

    ret = run_command(op, cmd, NULL, TRUE, 20 /* XXX */, TRUE);
    success = ret == 0;

    nvfree(mod_sign_hash);
    nvfree(mod_sign_cmd);
    nvfree(cmd);
    nvfree(build_module_instances_parameter);

    if (status) {
        ui_status_end(op, success ? "done." : "Failed to sign kernel module.");
    } else {
        ui_log(op, success ? "Signed kernel module." : "Module signing failed");
    }
    op->kernel_module_signed = success;
    return success;
} /* sign_kernel_module */



/*
 * create_detached_signature() - Link a precompiled interface into a module,
 * sign the resulting linked module, and store a CRC for the linked, unsigned
 * module and the detached signature in the provided PrecompiledFileInfo record.
 */
static int create_detached_signature(Options *op, Package *p,
                                     const char *build_dir,
                                     PrecompiledFileInfo *fileInfo,
                                     const char *module_suffix,
                                     const char *module_filename)
{
    int ret, command_ret;
    struct stat st;
    char *module_path = NULL, *error = NULL, *target_dir = NULL;

    ui_status_begin(op, "Creating a detached signature for the linked "
                    "kernel module:", "Linking module");

    ret = link_kernel_module(op, p, build_dir, fileInfo);

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

    ret = sign_kernel_module(op, target_dir, module_suffix, FALSE);

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
} /* create_detached_signature() */


/*
 * build_kernel_interface_file() - build the kernel interface(s).
 * Returns true if the build was successful, or false on error.
 *
 * This is done by copying the sources to a temporary working
 * directory and building the kernel interface in that directory.
 */

static int build_kernel_interface_file(Options *op, const char *tmpdir,
                                       PrecompiledFileInfo *fileInfo,
                                       const char *kernel_interface_filename)
{
    char *cmd;
    char *kernel_interface, *build_module_instances_parameter = NULL;
    char *concurrency;
    int ret;

    if (op->multiple_kernel_modules) {
        build_module_instances_parameter =
            nvasprintf(" NV_BUILD_MODULE_INSTANCES=%d", NV_MAX_MODULE_INSTANCES);
    }

    concurrency = nvasprintf(" -j%d ", op->concurrency_level);

    cmd = nvstrcat("cd ", tmpdir, "; ", op->utils[MAKE], " ",
                   kernel_interface_filename,
                   " SYSSRC=", op->kernel_source_path,
                   " SYSOUT=", op->kernel_output_path, concurrency,
                   build_module_instances_parameter, NULL);

    nvfree(build_module_instances_parameter);
    nvfree(concurrency);

    ret = run_command(op, cmd, NULL, TRUE, 25 /* XXX */, TRUE);

    free(cmd);

    if (ret != 0) {
        ui_status_end(op, "Error.");
        ui_error(op, "Unable to build the NVIDIA kernel module interface.");
        /* XXX need more descriptive error message */
        return FALSE;
    }

    /* check that the file exists */

    kernel_interface = nvstrcat(tmpdir, "/",
                                kernel_interface_filename, NULL);
    ret = access(kernel_interface, F_OK);
    nvfree(kernel_interface);

    if (ret == -1) {
        ui_status_end(op, "Error.");
        ui_error(op, "The NVIDIA kernel module interface was not created.");
        return FALSE;
    }

    return TRUE;
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
                                 const char *module_suffix,
                                 const char *kernel_interface,
                                 const char *module_filename,
                                 const char *core_file,
                                 const char *target_directory)
{
    int command_ret;

    command_ret = precompiled_read_interface(fileInfo, kernel_interface,
                                             module_filename,
                                             core_file, target_directory);

    if (command_ret) {
        if (op->module_signing_secret_key && op->module_signing_public_key) {
            if (!create_detached_signature(op, p, build_dir, fileInfo,
                                           module_suffix,
                                           module_filename)) {
                return FALSE;
            }
        }
        return TRUE;
    }

    return FALSE;
}



static int build_and_pack_interface(Options *op, Package *p, const char *tmpdir,
                                    const char *subdir,
                                    PrecompiledFileInfo *fileInfo,
                                    const char *interface, const char *module,
                                    const char *suffix, const char *core)
{
    int ret;
    char *filename = NULL;
    char *dir = NULL;

    ui_status_begin(op, "Building kernel module interface: ", "Building %s",
                    interface);

    dir = nvstrcat(tmpdir, "/", subdir, NULL);
    ret = build_kernel_interface_file(op, dir, fileInfo, interface);

    if (!ret) {
        goto done;
    }

    ui_status_end(op, "done.");

    ui_log(op, "Kernel module interface compilation complete.");

    filename = nvstrcat(dir, "/", interface, NULL);

    /* add the kernel interface to the list of files to be packaged */
    ret = pack_kernel_interface(op, p, tmpdir, fileInfo, suffix, filename,
                                module, core, subdir);

done:
    nvfree(dir);
    nvfree(filename);

    return ret;
}



/*
 * build_kernel_interface() - build the kernel interface(s), and store any
 * built interfaces in a newly allocated PrecompiledFileInfo array. Return
 * the number of packaged interface files, or 0 on error.
 *
 * The tmpdir created to build the kernel interface is removed before exit.
 *
 * For multi-RM, the frontend module interface should be compiled and
 * packaged too.
 *
 * XXX this and build_kernel_module() should be merged.
 */

int build_kernel_interface(Options *op, Package *p,
                           PrecompiledFileInfo ** fileInfos)
{
    char *tmpdir = NULL;
    int files_packaged = 0, i;
    int num_files = 1, ret = FALSE;
    char *uvmdir = NULL;

    *fileInfos = NULL;

    /* create a temporary directory */

    tmpdir = make_tmpdir(op);

    if (!tmpdir) {
        ui_error(op, "Unable to create a temporary build directory.");
        return FALSE;
    }
    
    /* copy the kernel module sources to it */
    
    ui_log(op, "Copying kernel module sources to temporary directory.");

    if (!copy_directory_contents
        (op, p->kernel_module_build_directory, tmpdir)) {
        ui_error(op, "Unable to copy the kernel module sources to temporary "
                 "directory '%s'.", tmpdir);
        goto failed;
    }

    uvmdir = nvstrcat(tmpdir, "/" UVM_SUBDIR, NULL);

    if (op->install_uvm) {
        if (!mkdir_recursive(op, uvmdir, 0655, FALSE)) {
            ui_error(op, "Unable to create a temporary subdirectory for the "
                     "Unified Memory kernel module build.");
            goto failed;
        }

        if (!copy_directory_contents(op,
                                     p->uvm_module_build_directory, uvmdir)) {
            ui_error(op, "Unable to copy the Unified Memory kernel module "
                     "sources to temporary directory '%s'.", uvmdir);
            goto failed;
        }
    }
    
    /*
     * touch the contents of the build directory, to avoid make time
     * skew error messages
     */

    touch_directory(op, p->kernel_module_build_directory);

    if (op->multiple_kernel_modules) {
        num_files = op->num_kernel_modules;
    }

    *fileInfos = nvalloc(sizeof(PrecompiledFileInfo) * (num_files + 2));

    for (i = 0; i < num_files; i++) {
        char *kernel_module_filename;
        char *kernel_interface_filename;
        char *module_instance_str = NULL;

        if (op->multiple_kernel_modules) {
            module_instance_str = nvasprintf("%d", i);
        } else {
            module_instance_str = nvstrdup("");
        }

        kernel_interface_filename = nvstrdup(p->kernel_interface_filename);
        replace_zero(kernel_interface_filename, i);

        kernel_module_filename = nvstrdup(p->kernel_module_filename);
        replace_zero(kernel_module_filename, i);

        ret = build_and_pack_interface(op, p, tmpdir, "", *fileInfos + i,
                                       kernel_interface_filename,
                                       kernel_module_filename,
                                       module_instance_str, "nv-kernel.o");

        if (!ret) {
            goto interface_done;
        }

interface_done:

        nvfree(kernel_interface_filename);
        nvfree(kernel_module_filename);
        nvfree(module_instance_str);

        if (!ret)
            goto failed;

        files_packaged++;
    }

    if (op->multiple_kernel_modules) {
        ret = build_and_pack_interface(op, p, tmpdir, "", *fileInfos + num_files,
                                       p->kernel_frontend_interface_filename,
                                       p->kernel_frontend_module_filename,
                                       "-frontend", "");
         if (!ret) {
            goto failed;
         }

        files_packaged++;
    }

    if (op->install_uvm) {
        ret = build_and_pack_interface(op, p, tmpdir, UVM_SUBDIR,
                                       *fileInfos + files_packaged,
                                       p->uvm_interface_filename,
                                       p->uvm_kernel_module_filename, "-uvm", "");

        if (!ret) {
            goto failed;
        }

        files_packaged++;
    }

failed:
    nvfree(uvmdir);

    if (files_packaged == 0) {
        nvfree(*fileInfos);
        *fileInfos = NULL;
    }

    if (tmpdir) {
        remove_directory(op, tmpdir);
        nvfree(tmpdir);
    }

    return files_packaged;

} /* build_kernel_interface() */




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
    int ret = 0, libkmod_failed = FALSE, old_loglevel, loglevel_set;

    *data = NULL;

    /*
     * Temporarily disable most console messages to keep the curses
     * interface from being clobbered when the module is loaded.
     * Save the original console loglevel to allow restoring it once
     * we're done.
     */

    loglevel_set = set_loglevel(PRINTK_LOGLEVEL_KERN_ALERT, &old_loglevel);

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
 * test_kernel_module() - attempt to insmod the kernel modules and then rmmod
 * them.  Return TRUE if the insmod succeeded, or FALSE otherwise.
 */

int test_kernel_module(Options *op, Package *p)
{
    char *cmd = NULL, *data = NULL, *module_path;
    int ret, i;
    const char *depmods[] = { "i2c-core", "drm" };


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

    if (op->multiple_kernel_modules) {
        /* Load nvidia-frontend.ko */
        
        module_path = nvstrcat(p->kernel_module_build_directory, "/",
                               p->kernel_frontend_module_filename, NULL);
        ret = do_insmod(op, module_path, &data, "");
        nvfree(module_path);

        if (ret != 0) {
            ret = ignore_load_error(op, p, p->kernel_frontend_module_filename, 
                                    data, ret);
            goto test_exit;
        }

        nvfree(data);
    }

    /* 
     * Load nvidia0.ko while building multiple kernel modules or
     * load nvidia.ko for non-multiple-kernel-module/simple builds.
     */

    module_path = nvstrcat(p->kernel_module_build_directory, "/",
                           p->kernel_module_filename, NULL);
    ret = do_insmod(op, module_path, &data,
                    "NVreg_DeviceFileUID=0 NVreg_DeviceFileGID=0 "
                    "NVreg_DeviceFileMode=0 NVreg_ModifyDeviceFiles=0");

    nvfree(module_path);

    if (ret != 0) {
        ret = ignore_load_error(op, p, p->kernel_module_filename, data, ret);
        goto test_exit;
    }
    if (op->install_uvm) {
        module_path = nvstrcat(p->uvm_module_build_directory, "/",
                               p->uvm_kernel_module_filename, NULL);
        ret = do_insmod(op, module_path, &data, "");
        nvfree(module_path);

        if (ret != 0) {
            ret = ignore_load_error(op, p, p->uvm_kernel_module_filename, data,
                                    ret);
            if (ret) {
                ui_warn(op, "The NVIDIA Unified Memory module failed to load, "
                        "and the load failure was ignored. This module is "
                        "required in order for the CUDA driver to function; if "
                        "the load failure cannot be resolved, then this system "
                        "will be unable to run CUDA applications.");
            }
            goto test_exit;
        }
    }

    /*
     * check if the kernel module detected problems with this
     * system's kernel and display any warning messages it may
     * have prepared for us.
     */

    check_for_warning_messages(op);

    /*
     * attempt to unload the kernel modules, but don't abort if this fails:
     * the kernel may not have been configured with support for module unloading
     * (Linux 2.6).
     */

    if (op->install_uvm) {
        rmmod_kernel_module(op, p->uvm_kernel_module_name);
    }

    rmmod_kernel_module(op, p->kernel_module_name);

    if (op->multiple_kernel_modules) {
        rmmod_kernel_module(op, p->kernel_frontend_module_name);
    }

    ret = TRUE;

test_exit:
    nvfree(data);
    
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
    
} /* test_kernel_module() */



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
    char *cmd, *data;
    int ret, old_loglevel, loglevel_set;

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

    if (!quiet && ret != 0) {
        if (op->expert) {
            ui_error(op, "Unable to %s the kernel module: '%s'",
                     unload ? "unload" : "load",
                     data);
        } else {
            ui_error(op, "Unable to %s the kernel module.",
                     unload ? "unload" : "load");
        }
        ret = FALSE;
    } else {
        ret = TRUE;
    }

    nvfree(cmd);
    nvfree(data);
    
    return ret;

} /* load_kernel_module() */

int load_kernel_module(Options *op, Package *p)
{
    return modprobe_helper(op, p->kernel_module_name, FALSE, FALSE);
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
 * check_for_unloaded_kernel_module() - test if any of the "bad"
 * kernel modules are loaded; if they are, then try to unload it.  If
 * we can't unload it, then report an error and return FALSE;
 */

int check_for_unloaded_kernel_module(Options *op, Package *p)
{
    int n = 0;
    int loaded = FALSE;
    unsigned int bits = 0;
    
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

    while (p->bad_modules[n]) {
        if (check_for_loaded_kernel_module(op, p->bad_modules[n])) {
            loaded = TRUE;
            bits |= (1 << n);
        }
        n++;
    }

    if (!loaded) return TRUE;
    
    /* one or more kernel modules is loaded... try to unload them */

    n = 0;
    while (p->bad_modules[n]) {
        if (!(bits & (1 << n))) {
            n++;
            continue;
        }
        
        rmmod_kernel_module(op, p->bad_modules[n]);

        /* check again */
        
        if (check_for_loaded_kernel_module(op, p->bad_modules[n])) {
            ui_error(op,  "An NVIDIA kernel module '%s' appears to already "
                     "be loaded in your kernel.  This may be because it is "
                     "in use (for example, by the X server), but may also "
                     "happen if your kernel was configured without support "
                     "for module unloading.  Please be sure you have exited "
                     "X before attempting to upgrade your driver.  If you "
                     "have exited X, know that your kernel supports module "
                     "unloading, and still receive this message, then an "
                     "error may have occured that has corrupted the NVIDIA "
                     "kernel module's usage count; the simplest remedy is "
                     "to reboot your computer.",
                     p->bad_modules[n]);
    
            return FALSE;
        }
        n++;
    }

    return TRUE;

} /* check_for_unloaded_kernel_module() */


/*
 * add_file_to_search_filelist() - Add file to build a list
 * of files expected to be unpacked.
 */

static void add_file_to_search_filelist(char **search_filelist, char *filename)
{
    int index = 0;

    while(search_filelist[index]) {
        index++;
    }

    if (index == SEARCH_FILELIST_MAX_ENTRIES)
        return;
 
    search_filelist[index] = nvstrdup(filename);

} /* add_file_to_search_filelist() */


/* 
 * free_search_filelist() - frees the list of files expected
 * to unpack
 */

static void free_search_filelist(char **search_filelist)
{
    int index = 0;

    while (search_filelist[index]) {
        free(search_filelist[index]);
        index++;
    }

} /*free_search_filelist() */


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
    char *search_filelist[SEARCH_FILELIST_MAX_ENTRIES+1];
    int index;
  
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

    memset(search_filelist, 0, sizeof(search_filelist));

    if (op->multiple_kernel_modules) {
        add_file_to_search_filelist(search_filelist, 
                                    p->kernel_frontend_interface_filename);
    }

    for (index = 0; index < op->num_kernel_modules; index++) {
        char *tmp;
        tmp = nvstrdup(p->kernel_interface_filename);
        replace_zero(tmp, index);
        add_file_to_search_filelist(search_filelist, tmp);
        free(tmp);
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
    
    /*
     * If we didn't find a matching precompiled kernel interface, ask
     * if we should try to download one.
     */

    if (!info && !op->no_network && op->precompiled_kernel_interfaces_url) {
        info = download_updated_kernel_interface(op, p,
                                                 proc_version_string,
                                                 search_filelist);
        if (!info) {
            ui_message(op, "No matching precompiled kernel interface was "
                       "found at '%s'; this means that the installer will need "
                       "to compile a kernel interface for your kernel.",
                       op->precompiled_kernel_interfaces_url);
            free_search_filelist(search_filelist);
            goto done;
        }
    }

    free_search_filelist(search_filelist);

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
 * the user specified via the --kernel-name option, or `name -r`.
 */

char __kernel_name[256];

char *get_kernel_name(Options *op)
{
    struct utsname uname_buf;

    if (op->kernel_name) {
        return op->kernel_name;
    } else {
        if (uname(&uname_buf) == -1) {
            ui_warn(op, "Unable to determine kernel version (%s).",
                    strerror(errno));
            return NULL;
        } else {
            strncpy(__kernel_name, uname_buf.release, 256);
            return __kernel_name;
        }
    }
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
        ret = run_conftest(op, p, conftest_cmd, NULL);
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

char *guess_module_signing_hash(Options *op, Package *p)
{
    char *ret;

    if (run_conftest(op, p, "guess_module_signing_hash", &ret)) {
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
 * get_updated_kernel_interfaces() - 
 */

static PrecompiledInfo *
download_updated_kernel_interface(Options *op, Package *p,
                                  const char *proc_version_string,
                                  char *const *search_filelist)
{
    int fd = -1;
    int dst_fd = -1;
    int length = 0, i;
    char *url = NULL;
    char *tmpfile = NULL;
    char *dstfile = NULL;
    char *buf = NULL;
    char *str = MAP_FAILED;
    char *ptr, *s;
    struct stat stat_buf;
    PrecompiledInfo *info = NULL;
    
    /* initialize the tmpfile and url strings */
    
    tmpfile = nvstrcat(op->tmpdir, "/nv-updates-XXXXXX", NULL);
    url = nvstrcat(op->precompiled_kernel_interfaces_url, "/", INSTALLER_OS,
                   "-", INSTALLER_ARCH, "/", p->version, "/updates/updates.txt",
                   NULL);

    /*
     * create a temporary file in which to write the list of available
     * updates
     */

    if ((fd = mkstemp(tmpfile)) == -1) {
        ui_error(op, "Unable to create temporary file (%s)", strerror(errno));
        goto done;
    }

    /* download the updates list */

    if (!snarf(op, url, fd, SNARF_FLAGS_DOWNLOAD_SILENT)) goto done;
    
    /* get the length of the file */

    if (fstat(fd, &stat_buf) == -1) goto done;

    length = stat_buf.st_size;
    
    /* map the file into memory for easier reading */
    
    str = mmap(0, length, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
    if (str == MAP_FAILED) goto done;
    
    /*
     * loop over each line of the updates file: each line should be of
     * the format: "[filename]:::[proc version string]"
     */

    ptr = str;

    while (TRUE) {
        buf = get_next_line(ptr, &ptr, str, length);
        if ((!buf) || (buf[0] == '\0')) goto done;

        s = strstr(buf, ":::");
        if (!s) {
            ui_error(op, "Invalid updates.txt list.");
            goto done;
        }
        
        s += 3; /* skip past the ":::" separator */
        
        if (strcmp(proc_version_string, s) == 0) {

            /* proc versions strings match */
            
            /*
             * terminate the string at the start of the ":::"
             * separator so that buf is the filename
             */
            
            s -= 3;
            *s = '\0';

            /* build the new url and dstfile strings */

            nvfree(url);
            url = nvstrcat(op->precompiled_kernel_interfaces_url, "/",
                           INSTALLER_OS, "-", INSTALLER_ARCH, "/", p->version,
                           "/updates/", buf, NULL);

            dstfile = nvstrcat(p->precompiled_kernel_interface_directory,
                               "/", buf, NULL);
            
            /* create dstfile */
            
            dst_fd = creat(dstfile, S_IRUSR | S_IWUSR);
            if (dst_fd == -1) {
                ui_error(op, "Unable to create file '%s' (%s).",
                         dstfile, strerror(errno));
                goto done;
            }
            
            /* download the file */
            
            if (!snarf(op, url, dst_fd, SNARF_FLAGS_STATUS_BAR)) goto done;
            
            close(dst_fd);
            dst_fd = -1;
            
            /* XXX once we have gpg setup, should check the file here */

            info = get_precompiled_info(op, dstfile, proc_version_string,
                                        p->version, search_filelist);

            if (!info) {
                ui_error(op, "The format of the downloaded precompiled package "
                         "is invalid!");
                free_precompiled(info);
                info = NULL;
            }
            
            /* compare checksums */

            for (i = 0; info && i < info->num_files; i++) {
                uint32 crc = compute_crc_from_buffer(info->files[i].data,
                                                     info->files[i].size);
                if (info->files[i].crc != crc) {
                    ui_error(op, "The embedded checksum of the file %s in the "
                             "downloaded precompiled pacakge '%s' (%" PRIu32
                             ") does not match the computed checksum (%"
                             PRIu32 "); not using.", info->files[i].name,
                             buf, info->files[i].crc, crc);
                    free_precompiled(info);
                    info = NULL;
                }
            }

            goto done;

        }

        nvfree(buf);
    }
    

 done:

    nvfree(dstfile);
    nvfree(buf);
    if (str != MAP_FAILED) munmap(str, length);
    if (dst_fd > 0) close(dst_fd);
    if (fd > 0) close(fd);

    unlink(tmpfile);
    nvfree(tmpfile);
    nvfree(url);

    return info;
}



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

    ret = run_conftest(&dummyop, p, "cc_version_check just_msg", &result);

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
 * fbdev_check() - run the rivafb_sanity_check and the nvidiafb_sanity_check
 * conftests; if either test fails, print the error message from the test
 * and abort the driver installation.
 */

static int fbdev_check(Options *op, Package *p)
{
    char *result;
    int ret;
    
    ui_log(op, "Performing rivafb check.");
    
    ret = run_conftest(op, p,"rivafb_sanity_check just_msg", &result);
    
    if (!ret) {
        if (result)
            ui_error(op, "%s", result);
        nvfree(result);

        return FALSE;
    }

    ui_log(op, "Performing nvidiafb check.");
    
    ret = run_conftest(op, p,"nvidiafb_sanity_check just_msg", &result);
    
    if (!ret) {
        if (result)
            ui_error(op, "%s", result);
        nvfree(result);

        return FALSE;
    }

    return TRUE;
    
} /* fbdev_check() */



/*
 * xen_check() - run the xen_sanity_check conftest; if this test fails, print
 * the test's error message and abort the driver installation.
 */

static int xen_check(Options *op, Package *p)
{
    char *result;
    int ret;
    
    ui_log(op, "Performing Xen check.");
    
    ret = run_conftest(op, p,"xen_sanity_check just_msg", &result);
    
    if (!ret) {
        if (result)
            ui_error(op, "%s", result);
        nvfree(result);

        return FALSE;
    }

    return TRUE;
    
} /* xen_check() */



/*
 * preempt_rt_check() - run the preempt_rt_sanity_check conftest; if this
 * test fails, print the test's error message and abort the driver
 * installation.
 */

static int preempt_rt_check(Options *op, Package *p)
{
    char *result;
    int ret;

    ui_log(op, "Performing PREEMPT_RT check.");

    ret = run_conftest(op, p, "preempt_rt_sanity_check just_msg", &result);

    if (!ret) {
        if (result)
            ui_error(op, "%s", result);
        nvfree(result);

        return FALSE;
    }

    return TRUE;

} /* preempt_rt_check() */



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
