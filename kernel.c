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
static int check_for_loaded_kernel_module(Options *op, const char *);
static void check_for_warning_messages(Options *op);
static int rmmod_kernel_module(Options *op, const char *);
static PrecompiledInfo *download_updated_kernel_interface(Options*, Package*,
                                                          const char*);
static int fbdev_check(Options *op, Package *p);
static int xen_check(Options *op, Package *p);

static PrecompiledInfo *scan_dir(Options *op, Package *p,
                                 const char *directory_name,
                                 const char *output_filename,
                                 const char *proc_version_string);

static char *build_distro_precompiled_kernel_interface_dir(Options *op);
static char *convert_include_path_to_source_path(const char *inc);
static char *guess_kernel_module_filename(Options *op);
static char *get_machine_arch(Options *op);
static int init_libkmod(void);
static void close_libkmod(void);
static int run_conftest(Options *op, Package *p, const char *args,
                        char **result);

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

    if (!mkdir_recursive(op, op->kernel_module_installation_path, 0755))
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
    char *CC, *cmd, *arch;
    int ret;

    arch = get_machine_arch(op);
    if (!arch)
        return FALSE;

    CC = getenv("CC");
    if (!CC) CC = "cc";

    cmd = nvstrcat("sh ", p->kernel_module_build_directory,
                   "/conftest.sh ", CC, " ", CC, " ", arch, " ",
                   op->kernel_source_path, " ",
                   op->kernel_output_path, " ",
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
            if (!directory_exists(op, result)) {
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

    if (!directory_exists(op, op->kernel_source_path)) {
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

        if (!directory_exists(op, op->kernel_output_path)) {
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

        if (!directory_exists(op, op->kernel_output_path)) {
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

            if (directory_exists(op, str)) {
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
                            const PrecompiledInfo *info) {
    uint32 actual_crc;
    char *module_filename;
    int ret = FALSE, command_ret;

    ui_log(op, "Attaching module signature to linked kernel module.");

    module_filename = nvstrcat(p->kernel_module_build_directory, "/",
                               p->kernel_module_filename, NULL);

    command_ret = verify_crc(op, module_filename, info->linked_module_crc,
                     &actual_crc);

    if (command_ret) {
        FILE *module_file, *signature_file;

        module_file = fopen(module_filename, "a+");
        signature_file = fopen(info->detached_signature, "r");

        if (module_file && signature_file) {
            char buf;

            while(fread(&buf, 1, 1, signature_file)) {
                command_ret = fwrite(&buf, 1, 1, module_file);
                if (command_ret != 1) {
                    goto attach_done;
                }
            }

            ret = feof(signature_file) &&
                         !ferror(signature_file) &&
                         !ferror(module_file);

            op->kernel_module_signed = ret;
attach_done:
            fclose(module_file);
            fclose(signature_file);
        } else {
            ret = ui_yes_no(op, FALSE,
                            "A detached signature was included with the "
                            "precompiled interface, but opening the linked "
                            "kernel module and/or the signature file failed."
                            "\n\nThe detached signature will not be added; "
                            "would you still like to install the unsigned "
                            "kernel module?");
        }
    } else {
        ret = ui_yes_no(op, FALSE,
                        "A detached signature was included with the "
                        "precompiled interface, but the checksum of the linked "
                        "kernel module (%d) did not match the checksum of the "
                        "the kernel module for which the detached signature "
                        "was generated (%d).\n\nThis can happen if the linker "
                        "on the installation target system is not the same as "
                        "the linker on the system that built the precompiled "
                        "interface.\n\nThe detached signature will not be "
                        "added; would you still like to install the unsigned "
                        "kernel module?", actual_crc, info->linked_module_crc);
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

    nvfree(module_filename);
    return ret;
} /* attach_signature() */



/*
 * link_kernel_module() - link the prebuilt kernel interface against
 * the binary-only core of the kernel module.  This results in a
 * complete kernel module, ready for installation.
 *
 *
 * ld -r -o nvidia.o nv-linux.o nv-kernel.o
 */

int link_kernel_module(Options *op, Package *p, const char *build_directory,
                       const PrecompiledInfo *info)
{
    char *cmd, *result;
    int ret;
    
    p->kernel_module_filename = guess_kernel_module_filename(op);

    cmd = nvstrcat("cd ", build_directory, "; ", op->utils[LD],
                   " ", LD_OPTIONS,
                   " -o ", p->kernel_module_filename,
                   " ", PRECOMPILED_KERNEL_INTERFACE_FILENAME,
                   " nv-kernel.o", NULL);
    
    ret = run_command(op, cmd, &result, TRUE, 0, TRUE);
    
    free(cmd);

    if (ret != 0) {
        ui_error(op, "Unable to link kernel module.");
        return FALSE;
    }

    ui_log(op, "Kernel module linked successfully.");

    if (info && info->detached_signature) {
        return attach_signature(op, p, info);
    }

    return TRUE;
   
} /* link_kernel_module() */


/*
 * build_kernel_module() - determine the kernel include directory,
 * copy the kernel module source files into a temporary directory, and
 * compile nvidia.o.
 */

int build_kernel_module(Options *op, Package *p)
{
    char *result, *cmd, *tmp;
    int len, ret;

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
        ui_error(op, "%s", result); /* display conftest.sh's error message */
        nvfree(result);
        return FALSE;
    }

    if (!fbdev_check(op, p)) return FALSE;
    if (!xen_check(op, p)) return FALSE;

    cmd = nvstrcat("cd ", p->kernel_module_build_directory,
                   "; make print-module-filename",
                   " SYSSRC=", op->kernel_source_path,
                   " SYSOUT=", op->kernel_output_path, NULL);
    
    ret = run_command(op, cmd, &p->kernel_module_filename, FALSE, 0, FALSE);
    
    free(cmd);

    if (ret != 0) {
        ui_error(op, "Unable to determine the NVIDIA kernel module filename.");
        nvfree(result);
        return FALSE;
    }
    
    ui_log(op, "Cleaning kernel module build directory.");
    
    len = strlen(p->kernel_module_build_directory) + 32;
    cmd = nvalloc(len);
    
    snprintf(cmd, len, "cd %s; make clean", p->kernel_module_build_directory);

    ret = run_command(op, cmd, &result, TRUE, 0, TRUE);
    free(result);
    free(cmd);
    
    ui_status_begin(op, "Building kernel module:", "Building");

    cmd = nvstrcat("cd ", p->kernel_module_build_directory,
                   "; make module",
                   " SYSSRC=", op->kernel_source_path,
                   " SYSOUT=", op->kernel_output_path, NULL);
    
    ret = run_command(op, cmd, &result, TRUE, 25, TRUE);

    free(cmd);

    if (ret != 0) {
        ui_status_end(op, "Error.");
        ui_error(op, "Unable to build the NVIDIA kernel module.");
        /* XXX need more descriptive error message */
        return FALSE;
    }
    
    /* check that the file actually exists */

    tmp = nvstrcat(p->kernel_module_build_directory, "/",
                   p->kernel_module_filename, NULL);
    if (access(tmp, F_OK) == -1) {
        free(tmp);
        ui_status_end(op, "Error.");
        ui_error(op, "The NVIDIA kernel module was not created.");
        return FALSE;
    }
    free(tmp);
    
    ui_status_end(op, "done.");

    ui_log(op, "Kernel module compilation complete.");

    return TRUE;
    
} /* build_kernel_module() */



/*
 * sign_kernel_module() - sign the kernel module. The caller is responsible
 * for ensuring that the kernel module is already built successfully and that
 * op->module_signing_{secret,public}_key are set.
 */
int sign_kernel_module(Options *op, const char *build_directory, int status) {
    char *cmd, *mod_sign_cmd, *mod_sign_hash;
    int ret, success;

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

    cmd = nvstrcat("cd ", build_directory, "; make module-sign"
                   " SYSSRC=", op->kernel_source_path,
                   " SYSOUT=", op->kernel_output_path,
                   " MODSECKEY=", op->module_signing_secret_key,
                   " MODPUBKEY=", op->module_signing_public_key,
                   mod_sign_cmd ? mod_sign_cmd : "",
                   mod_sign_hash ? mod_sign_hash : "", NULL);

    ret = run_command(op, cmd, NULL, TRUE, 20 /* XXX */, TRUE);
    success = ret == 0;

    nvfree(mod_sign_hash);
    nvfree(mod_sign_cmd);
    nvfree(cmd);

    if (status) {
        ui_status_end(op, success ? "done." : "Failed to sign kernel module.");
    } else {
        ui_log(op, success ? "Signed kernel module." : "Module signing failed");
    }
    op->kernel_module_signed = success;
    return success;
} /* sign_kernel_module */



/*
 * byte_tail() - write to outfile from infile, starting at the specified byte
 * offset, and going until the end of infile. This is needed because `tail -c`
 * is unreliable in some implementations.
 */
static int byte_tail(const char *infile, const char *outfile, int start)
{
    FILE *in = NULL, *out = NULL;
    int success = FALSE, ret;
    char buf;

    in = fopen(infile, "r");
    out = fopen(outfile, "w");

    if (!in || !out) {
        goto done;
    }

    ret = fseek(in, start, SEEK_SET);
    if (ret != 0) {
        goto done;
    }

    while(fread(&buf, 1, 1, in)) {
        ret = fwrite(&buf, 1, 1, out);
        if (ret != 1) {
            goto done;
        }
    }

    success = feof(in) && !ferror(in) && !ferror(out);

done:
    fclose(in);
    fclose(out);
    return success;
}



/*
 * create_detached_signature() - Link the precompiled interface with nv-kernel.o,
 * sign the resulting linked nvidia.ko, and split the signature off into a separate
 * file. Copy a checksum and detached signature for the linked module to the kernel
 * module build directory on success.
 */
static int create_detached_signature(Options *op, Package *p,
                                 const char *build_dir)
{
    int ret, command_ret;
    struct stat st;
    FILE *checksum_file;
    char *module_path = NULL, *tmp_path = NULL, *error = NULL, *dstfile = NULL;

    ui_status_begin(op, "Creating a detached signature for the linked "
                    "kernel module:", "Linking module");

    tmp_path = nvstrcat(build_dir, "/", PRECOMPILED_KERNEL_INTERFACE_FILENAME,
                        NULL);
    symlink(p->kernel_interface_filename, tmp_path);

    ret = link_kernel_module(op, p, build_dir, NULL);

    if (!ret) {
        ui_error(op, "Failed to link a kernel module for signing.");
        goto done;
    }

    module_path = nvstrcat(build_dir, "/", p->kernel_module_filename, NULL);
    command_ret = stat(module_path, &st);

    if (command_ret != 0) {
        ret = FALSE;
        error = "Unable to determine size of linked module.";
        goto done;
    }

    ui_status_update(op, .25, "Generating module checksum");

    nvfree(tmp_path);
    tmp_path = nvstrcat(build_dir, "/", KERNEL_MODULE_CHECKSUM_FILENAME, NULL);
    checksum_file = fopen(tmp_path, "w");

    if(checksum_file) {
        uint32 crc = compute_crc(op, module_path);
        command_ret = fwrite(&crc, sizeof(crc), 1, checksum_file);
        fclose(checksum_file);
        if (command_ret != 1) {
            ret = FALSE;
            error = "Failed to write the module checksum.";
            goto done;
        }
    } else {
        ret = FALSE;
        error = "Failed to open the checksum file for writing.";
        goto done;
    }

    dstfile = nvstrcat(p->kernel_module_build_directory, "/",
                       KERNEL_MODULE_CHECKSUM_FILENAME, NULL);

    if (!copy_file(op, tmp_path, dstfile, 0644)) {
        ret = FALSE;
        error = "Failed to copy the kernel module checksum file.";
        goto done;
    }

    ui_status_update(op, .50, "Signing linked module");

    ret = sign_kernel_module(op, build_dir, FALSE);

    if (!ret) {
        error = "Failed to sign the linked kernel module.";
        goto done;
    }

    ui_status_update(op, .75, "Detaching module signature");

    nvfree(tmp_path);
    tmp_path = nvstrcat(build_dir, "/", DETACHED_SIGNATURE_FILENAME, NULL);
    ret = byte_tail(module_path, tmp_path, st.st_size);

    if (!ret) {
        error = "Failed to detach the module signature";
        goto done;
    }

    nvfree(dstfile);
    dstfile = nvstrcat(p->kernel_module_build_directory, "/",
                       DETACHED_SIGNATURE_FILENAME, NULL);

    if (!copy_file(op, tmp_path, dstfile, 0644)) {
        ret = FALSE;
        error = "Failed to copy the detached signature file.";
        goto done;
    }

done:
    if (ret) {
        ui_status_end(op, "done.");
    } else {
        ui_status_end(op, "Error.");
        if (error) {
            ui_error(op, error);
        }
    }

    nvfree(dstfile);
    nvfree(tmp_path);
    nvfree(module_path);
    return ret;
} /* create_detached_signature() */



/*
 * build_kernel_interface() - build the kernel interface, and place it
 * here:
 *
 * "%s/%s", p->kernel_module_build_directory,
 * PRECOMPILED_KERNEL_INTERFACE_FILENAME
 *
 * This is done by copying the sources to a temporary working
 * directory, building, and copying the kernel interface back to the
 * kernel module source directory.  The tmpdir is removed when
 * complete.
 *
 * XXX this and build_kernel_module() should be merged.
 */

int build_kernel_interface(Options *op, Package *p)
{
    char *tmpdir = NULL;
    char *cmd = NULL;
    char *kernel_interface = NULL;
    char *dstfile = NULL;
    int ret = FALSE;
    int command_ret;

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
    
    /*
     * touch the contents of the build directory, to avoid make time
     * skew error messages
     */

    touch_directory(op, p->kernel_module_build_directory);

    /* build the kernel interface */

    ui_status_begin(op, "Building kernel interface:", "Building");
    
    cmd = nvstrcat("cd ", tmpdir, "; make ", p->kernel_interface_filename,
                   " SYSSRC=", op->kernel_source_path, NULL);
    
    command_ret = run_command(op, cmd, NULL, TRUE, 25 /* XXX */, TRUE);

    if (command_ret != 0) {
        ui_status_end(op, "Error.");
        ui_error(op, "Unable to build the NVIDIA kernel module interface.");
        /* XXX need more descriptive error message */
        goto failed;
    }
    
    /* check that the file exists */

    kernel_interface = nvstrcat(tmpdir, "/",
                                p->kernel_interface_filename, NULL);
    
    if (access(kernel_interface, F_OK) == -1) {
        ui_status_end(op, "Error.");
        ui_error(op, "The NVIDIA kernel module interface was not created.");
        goto failed;
    }

    ui_status_end(op, "done.");

    ui_log(op, "Kernel module interface compilation complete.");

    /* copy the kernel interface from the tmpdir back to the srcdir */
    
    dstfile = nvstrcat(p->kernel_module_build_directory, "/",
                       PRECOMPILED_KERNEL_INTERFACE_FILENAME, NULL);

    if (!copy_file(op, kernel_interface, dstfile, 0644)) goto failed;

    if (op->module_signing_secret_key && op->module_signing_public_key) {
        ret = create_detached_signature(op, p, tmpdir);
    } else {
        ret = TRUE;
    }
    
 failed:
    
    remove_directory(op, tmpdir);

    if (tmpdir) nvfree(tmpdir);
    if (cmd) nvfree(cmd);
    if (kernel_interface) nvfree(kernel_interface);
    if (dstfile) nvfree(dstfile);

    return ret;

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



/*
 * do_insmod() - load the kernel module using libkmod if available; fall back
 * to insmod otherwise. Returns the result of kmod_module_insert_module() if
 * available, or of insmod otherwise. Pass the result of module loading up
 * through the data argument, regardless of whether we used libkmod or insmod.
 */
static int do_insmod(Options *op, const char *module, char **data)
{
    int ret = 0, libkmod_failed = FALSE;
    *data = NULL;

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

        ret = lkmod_module_insert_module(mod, 0, "");
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

        cmd = nvstrcat(op->utils[INSMOD], " ", module, NULL);

        /* only output the result of the test if in expert mode */

        ret = run_command(op, cmd, data, op->expert, 0, TRUE);
        nvfree(cmd);
    }

    close_libkmod();

    return ret;
} /* do_insmod() */



/*
 * test_kernel_module() - attempt to insmod the kernel module and then
 * rmmod it.  Return TRUE if the insmod succeeded, or FALSE otherwise.
 */

int test_kernel_module(Options *op, Package *p)
{
    char *cmd = NULL, *data = NULL, *module_path;
    int old_loglevel = 0, new_loglevel = 0;
    int fd, ret, name[] = { CTL_KERN, KERN_PRINTK }, i;
    size_t len = sizeof(int);
    const char *depmods[] = { "i2c-core", "drm" };

    /* 
     * If we're building/installing for a different kernel, then we
     * can't test the module now.
     */

    if (op->kernel_name) return TRUE;

    /*
     * Temporarily disable most console messages to keep the curses
     * interface from being clobbered when the module is loaded.
     * Save the original console loglevel to allow restoring it once
     * we're done.
     */
    fd = open("/proc/sys/kernel/printk", O_RDWR);
    if (fd >= 0) {
        if (read(fd, &old_loglevel, 1) == 1) {
            new_loglevel = '2'; /* KERN_CRIT */
            write(fd, &new_loglevel, 1);
        }
    } else {
        if (!sysctl(name, 2, &old_loglevel, &len, NULL, 0)) {
            new_loglevel = 2; /* KERN_CRIT */
            sysctl(name, 2, NULL, 0, &new_loglevel, len);
        }
    }

    /*
     * Attempt to load modules that nvidia.ko might depend on.  Silently ignore
     * failures: if nvidia.ko doesn't depend on the module that failed, the test
     * load below will succeed and it doesn't matter that the load here failed.
     */
    for (i = 0; i < ARRAY_LEN(depmods); i++) {
        cmd = nvstrcat(op->utils[MODPROBE], " -q ", depmods[i], NULL);
        run_command(op, cmd, NULL, FALSE, 0, TRUE);
        nvfree(cmd);
    }

    /* Load nvidia.ko */
    module_path = nvstrcat(p->kernel_module_build_directory, "/",
                           p->kernel_module_filename, NULL);
    ret = do_insmod(op, module_path, &data);
    nvfree(module_path);

    if (ret != 0) {
        int ignore_error = FALSE, secureboot, module_sig_force, enokey;
        const char *probable_reason, *signature_related;

        enokey = (-ret == ENOKEY);
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

                ignore_error = ui_yes_no(op, TRUE,
                                     "The signed kernel module failed to "
                                     "load%s because the kernel does not "
                                     "trust any key which is capable of "
                                     "verifying the module signature. "
                                     "Would you like to install the signed "
                                     "kernel module anyway?\n\nNote that %s"
                                     "you will not be able to load the "
                                     "installed module until after a key "
                                     "that can verify the module signature "
                                     "is added to a key database that is "
                                     "trusted by the kernel. This will "
                                     "likely require rebooting your "
                                     "computer.", probable_reason,
                                     signature_related);
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
                ignore_error = FALSE;
            }
        }

        if (ignore_error) {
            ui_log(op, "An error was encountered when loading the kernel "
                   "module, but that error was ignored, and the kernel module "
                   "will be installed, anyway. The error was: %s", data);
            ret = TRUE;
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
                     p->kernel_module_filename, op->log_file_name);

            /*
             * if in expert mode, run_command() would have caused this to
             * be written to the log file; so if not in expert mode, print
             * the output now.
             */

            if (!op->expert) ui_log(op, "Kernel module load error: %s", data);
            ret = FALSE;
        }

    } else {

        /*
         * check if the kernel module detected problems with this
         * system's kernel and display any warning messages it may
         * have prepared for us.
         */

        check_for_warning_messages(op);

        /*
         * attempt to unload the kernel module, but don't abort if
         * this fails: the kernel may not have been configured with
         * support for module unloading (Linux 2.6).
         */
        cmd = nvstrcat(op->utils[RMMOD], " ", p->kernel_module_name, NULL);
        run_command(op, cmd, NULL, FALSE, 0, TRUE);
        ret = TRUE;
        nvfree(cmd);
    }

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

    if (fd >= 0) {
        if (new_loglevel != 0)
            write(fd, &old_loglevel, 1);
        close(fd);
    } else {
        if (new_loglevel != 0)
            sysctl(name, 2, NULL, 0, &old_loglevel, len);
    }

    /*
     * Unload dependencies that might have been loaded earlier.
     */
    for (i = 0; i < ARRAY_LEN(depmods); i++) {
        cmd = nvstrcat(op->utils[MODPROBE], " -qr ", depmods[i], NULL);
        run_command(op, cmd, NULL, FALSE, 0, TRUE);
        nvfree(cmd);
    }

    return ret;
    
} /* test_kernel_module() */



/*
 * load_kernel_module() - modprobe the kernel module
 */

int load_kernel_module(Options *op, Package *p)
{
    char *cmd, *data;
    int len, ret;

    len = strlen(op->utils[MODPROBE]) + strlen(p->kernel_module_name) + 2;

    cmd = (char *) nvalloc(len);
    
    snprintf(cmd, len, "%s %s", op->utils[MODPROBE], p->kernel_module_name);
    
    ret = run_command(op, cmd, &data, FALSE, 0, TRUE);

    if (ret != 0) {
        if (op->expert) {
            ui_error(op, "Unable to load the kernel module: '%s'", data);
        } else {
            ui_error(op, "Unable to load the kernel module.");
        }
        ret = FALSE;
    } else {
        ret = TRUE;
    }

    nvfree(cmd);
    nvfree(data);
    
    return ret;

} /* load_kernel_module() */



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
 * find_precompiled_kernel_interface() - do assorted black magic to
 * determine if the given package contains a precompiled kernel interface
 * for the kernel on this system.
 *
 * XXX it would be nice to extend this so that a kernel module could
 * be installed for a kernel other than the currently running one.
 */

PrecompiledInfo *find_precompiled_kernel_interface(Options *op, Package *p)
{
    char *proc_version_string, *output_filename, *tmp;
    PrecompiledInfo *info = NULL;

    /* allow the user to completely skip this search */
    
    if (op->no_precompiled_interface) {
        ui_log(op, "Not probing for precompiled kernel interfaces.");
        return NULL;
    }
    
    /* retrieve the proc version string for the running kernel */

    proc_version_string = read_proc_version(op);
    
    if (!proc_version_string) goto failed;
    
    /* make sure the target directory exists */
    
    if (!mkdir_recursive(op, p->kernel_module_build_directory, 0755))
        goto failed;
    
    /* build the output filename */
    
    output_filename = nvstrcat(p->kernel_module_build_directory, "/",
                               PRECOMPILED_KERNEL_INTERFACE_FILENAME, NULL);
    
    /*
     * if the --precompiled-kernel-interfaces-path option was
     * specified, search that directory, first
     */
    
    if (op->precompiled_kernel_interfaces_path) {
        info = scan_dir(op, p, op->precompiled_kernel_interfaces_path,
                        output_filename, proc_version_string);
    }
    
    /*
     * If we didn't find a match, search for distro-provided
     * precompiled kernel interfaces
     */

    if (!info) {
        tmp = build_distro_precompiled_kernel_interface_dir(op);
        if (tmp) {
            info = scan_dir(op, p, tmp, output_filename, proc_version_string);
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
                        output_filename, proc_version_string);
    }
    
    /*
     * If we didn't find a matching precompiled kernel interface, ask
     * if we should try to download one.
     */

    if (!info && !op->no_network && op->precompiled_kernel_interfaces_url) {
        info = download_updated_kernel_interface(op, p,
                                                 proc_version_string);
        if (!info) {
            ui_message(op, "No matching precompiled kernel interface was "
                       "found at '%s'; this means that the installer will need "
                       "to compile a kernel interface for your kernel.",
                       op->precompiled_kernel_interfaces_url);
            return NULL;
        }
    }

    /* If we found one, ask expert users if they really want to use it */

    if (info && op->expert) {
        if (!ui_yes_no(op, TRUE, "A precompiled kernel interface for the "
                       "kernel '%s' has been found.  Would you like to "
                       "use this? (answering 'no' will require the "
                       "installer to compile the interface)",
                       info->description)) {
            free_precompiled(info);
            info = NULL;
        }
    }

    if (info) {
        return info;
    }

 failed:

    if (op->expert) {
        ui_message(op, "No precompiled kernel interface was found to match "
                   "your kernel; this means that the installer will need to "
                   "compile a new kernel interface.");
    }

    return NULL;
    
} /* find_precompiled_kernel_interface() */



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
    
    if (directory_exists(op, str)) {
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

        if (directory_exists(op, str)) {
            return str;
        }

        nvfree(str);

        str = nvstrcat("/lib/modules/", tmp, "/build", NULL);
    
        if (directory_exists(op, str)) {
            return str;
        }

        nvfree(str);

        /*
         * check "/usr/src/linux-`uname -r`", too; patch suggested by
         * Peter Berg Larsen <pebl@math.ku.dk>
         */

        str = nvstrcat("/usr/src/linux-", tmp, NULL);
        if (directory_exists(op, str)) {
            return str;
        }

        free(str);
    }

    /* finally, try /usr/src/linux */

    if (directory_exists(op, "/usr/src/linux")) {
        return "/usr/src/linux";
    }
    
    return NULL;
    
} /* default_kernel_source_path() */


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
    char *ptr, *result = NULL;
    int ret;

    ret = run_command(op, op->utils[LSMOD], &result, FALSE, 0, TRUE);
    
    if ((ret == 0) && (result) && (result[0] != '\0')) {
        ptr = strstr(result, module_name);
        if (ptr) {
            ptr += strlen(module_name);
            if(!isspace(*ptr)) ret = 1;
        } else {
            ret = 1;
        }
    }
    
    if (result) free(result);
    
    return ret ? FALSE : TRUE;
    
} /* check_for_loaded_kernel_module() */


/*
 * rmmod_kernel_module() - run `rmmod nvidia`
 */

static int rmmod_kernel_module(Options *op, const char *module_name)
{
    int len, ret;
    char *cmd;
    
    len = strlen(op->utils[RMMOD]) + strlen(module_name) + 2;
    
    cmd = (char *) nvalloc(len);
    
    snprintf(cmd, len, "%s %s", op->utils[RMMOD], module_name);
    
    ret = run_command(op, cmd, NULL, FALSE, 0, TRUE);
    
    free(cmd);
    
    return ret ? FALSE : TRUE;
    
} /* rmmod_kernel_module() */



/*
 * get_updated_kernel_interfaces() - 
 */

static PrecompiledInfo *
download_updated_kernel_interface(Options *op, Package *p,
                                  const char *proc_version_string)
{
    int fd = -1;
    int dst_fd = -1;
    int length;
    char *url = NULL;
    char *tmpfile = NULL;
    char *dstfile = NULL;
    char *buf = NULL;
    char *output_filename = NULL;
    char *str = (void *) -1;
    char *ptr, *s;
    struct stat stat_buf;
    PrecompiledInfo *info = NULL;
    uint32 crc;
    
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
    if (str == (void *) -1) goto done;
    
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

            /* build the output filename string */
            
            output_filename = nvstrcat(p->kernel_module_build_directory, "/",
                                       PRECOMPILED_KERNEL_INTERFACE_FILENAME,
                                       NULL);
            
            /* unpack the downloaded file */
            
            info = precompiled_unpack(op, dstfile, output_filename,
                                      proc_version_string,
                                      p->version);
            
            /* compare checksums */
            
            crc = compute_crc(op, output_filename);
            
            if (info && (info->crc != crc)) {
                ui_error(op, "The embedded checksum of the downloaded file "
                         "'%s' (%" PRIu32 ") does not match the computed "
                         "checksum (%" PRIu32 "); not using.", buf, info->crc,
                         crc);
                unlink(dstfile);
                free_precompiled(info);
                info = NULL;
            }
            
            goto done;
        }

        nvfree(buf);
    }
    

 done:

    if (dstfile) nvfree(dstfile);
    if (buf) nvfree(buf);
    if (str != (void *) -1) munmap(str, stat_buf.st_size);
    if (dst_fd > 0) close(dst_fd);
    if (fd > 0) close(fd);

    unlink(tmpfile);
    if (tmpfile) nvfree(tmpfile);
    if (url) nvfree(url);

    return info;

} /* get_updated_kernel_interfaces() */



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

    ret = ui_yes_no(op, TRUE, "The CC version check failed:\n\n%s\n\n"
                    "If you know what you are doing and want to "
                    "ignore the gcc version check, select \"No\" to "
                    "continue installation.  Otherwise, select \"Yes\" to "
                    "abort installation, set the CC environment variable to "
                    "the name of the compiler used to compile your kernel, "
                    "and restart installation.  Abort now?", result);
    
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
        ui_error(op, "%s", result);
        nvfree(result);

        return FALSE;
    }

    ui_log(op, "Performing nvidiafb check.");
    
    ret = run_conftest(op, p,"nvidiafb_sanity_check just_msg", &result);
    
    if (!ret) {
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
        ui_error(op, "%s", result);
        nvfree(result);

        return FALSE;
    }

    return TRUE;
    
} /* xen_check() */



/*
 * scan_dir() - scan through the specified directory for a matching
 * precompiled kernel interface.
 */

static PrecompiledInfo *scan_dir(Options *op, Package *p,
                                 const char *directory_name,
                                 const char *output_filename,
                                 const char *proc_version_string)
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
            ((strcmp(ent->d_name, "..")) == 0) ||
            strstr(ent->d_name, DETACHED_SIGNATURE_FILENAME)) continue;
            
        filename = nvstrcat(directory_name, "/", ent->d_name, NULL);
        
        info = precompiled_unpack(op, filename, output_filename,
                                  proc_version_string,
                                  p->version);
            
        if (info) break;
            
        free(filename);
        filename = NULL;
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
 * guess_kernel_module_filename() - parse uname to decide if the
 * kernel module filename is "nvidia.o" or "nvidia.ko".
 */

static char *guess_kernel_module_filename(Options *op)
{
    struct utsname uname_buf;
    char *tmp, *str, *dot0, *dot1;
    int major, minor;
    
    if (op->kernel_name) {
        str = op->kernel_name;
    } else {
        if (uname(&uname_buf) == -1) {
            ui_error (op, "Unable to determine kernel version (%s)",
                      strerror (errno));
            return NULL;
        }
        str = uname_buf.release;
    }
    
    tmp = nvstrdup(str);
    
    dot0 = strchr(tmp, '.');
    if (!dot0) goto fail;
    
    *dot0 = '\0';
    
    major = atoi(tmp);
    
    dot0++;
    dot1 = strchr(dot0, '.');
    if (!dot1) goto fail;
    
    *dot1 = '\0';
    
    minor = atoi(dot0);
    
    if ((major > 2) || ((major == 2) && (minor > 4))) {
        return nvstrdup("nvidia.ko");
    } else {
        return nvstrdup("nvidia.o");
    }

 fail:
    ui_error (op, "Unable to determine if kernel is 2.6.0 or greater from "
              "uname string '%s'; assuming the kernel module filename is "
              "'nvidia.o'.", str);
    return nvstrdup("nvidia.o");
    
} /* guess_kernel_module_filename() */



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
