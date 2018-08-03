/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003-2010 NVIDIA Corporation
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
 * nvidia-installer.c
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>

#include "nvidia-installer.h"
#include "kernel.h"
#include "user-interface.h"
#include "backup.h"
#include "files.h"
#include "misc.h"
#include "sanity.h"
#include "option_table.h"
#include "msg.h"
#include "manifest.h"

static void print_version(void);
static void print_help(const char* name, int is_uninstall, int advanced);



/*
 * print_version() - print the current nvidia-installer version number
 */

extern const char *pNV_ID;

static void print_version(void)
{
    nv_info_msg(NULL, "");
    nv_info_msg(NULL, "%s", pNV_ID);
    nv_info_msg(TAB, "The NVIDIA Software Installer for Unix/Linux.");
    nv_info_msg(NULL, "");
    nv_info_msg(TAB, "This program is used to install, upgrade and uninstall "
                "The NVIDIA Accelerated Graphics Driver Set for %s-%s.",
                INSTALLER_OS, INSTALLER_ARCH);
    nv_info_msg(NULL, "");
}


/*
 * print_help() - print usage information
 */

static void print_help_helper(const char *name, const char *description)
{
    nv_info_msg(TAB, "%s", name);
    nv_info_msg(BIGTAB, "%s", description);
    nv_info_msg(NULL, "");
}

static void print_options(int is_uninstall, int advanced)
{
    unsigned int include_mask = 0;

    if (is_uninstall) {
        /* only print options with the UNINSTALL flag */
        include_mask |= NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL;
    }

    if (!advanced) {
        /* only print options with the ALWAYS flag */
        include_mask |= NVGETOPT_HELP_ALWAYS;
    }

    nvgetopt_print_help(__options, include_mask, print_help_helper);
}

static void print_help(const char* name, int is_uninstall, int advanced)
{
    print_version();

    nv_info_msg(NULL, "");
    nv_info_msg(NULL, "%s [options]", name);
    nv_info_msg(NULL, "");

    print_options(is_uninstall, advanced);
}


/*
 * load_default_options - Allocate an Options structure
 * and initialize it with default values.
 *
 */

static Options *load_default_options(void)
{
    Options *op;

    op = (Options *) nvalloc(sizeof(Options));
    if (!op) {
        return NULL;
    }

    /* statically initialized strings */
    op->proc_mount_point = DEFAULT_PROC_MOUNT_POINT;

    op->tmpdir = get_tmpdir(op);

    op->logging = TRUE; /* log by default */
    op->opengl_headers = FALSE; /* do not install our GL headers by default */
    op->nvidia_modprobe = TRUE;
    op->run_nvidia_xconfig = FALSE;
    op->selinux_option = SELINUX_DEFAULT;

    op->sigwinch_workaround = TRUE;
    op->run_distro_scripts = TRUE;
    op->no_kernel_module_source = FALSE;
    op->dkms = FALSE;
    op->check_for_alternate_installs = TRUE;
    op->install_uvm = TRUE;
    op->install_drm = TRUE;
    op->glvnd_glx_client = TRUE;
    op->glvnd_egl_client = TRUE;
    op->install_compat32_libs = NV_OPTIONAL_BOOL_DEFAULT;
    op->install_libglx_indirect = NV_OPTIONAL_BOOL_DEFAULT;
    op->install_libglvnd_libraries = NV_OPTIONAL_BOOL_DEFAULT;
    op->external_platform_json_path = DEFAULT_EGL_EXTERNAL_PLATFORM_JSON_PATH;

    return op;

} /* load_default_options() */


/* Parse the file type and destination out from the argument to the
 * --override-file-type-destination command line option. Return TRUE if
 * parsing was successful, or FALSE on a parse error. */
static int assign_file_type_override(Options *op, char *optarg)
{
    PackageEntryFileCapabilities dummy;
    PackageEntryFileType type;
    char *split;

    /* Terminate the string at the first ':' to get the file type, and
     * advance to the character after the ':' to get the destination. */
    split = strchr(optarg, ':');
    if (split == NULL) {
        return FALSE;
    }
    split[0] = '\0';
    split++;

    type = parse_manifest_file_type(optarg, &dummy);
    if (type == FILE_TYPE_NONE) {
        return FALSE;
    }

    op->file_type_destination_overrides[type] = nvstrdup(split);

    return TRUE;
}


/*
 * parse_commandline() - Populate the Options structure with
 * appropriate values, based on the arguments passed at the commandline.
 * It is intended that this function does only minimal sanity checking --
 * just enough error trapping to ensure correct syntax of the
 * commandline options.  Validation of the actual data specified
 * through the options is left for the functions that use this data.
 */

static void parse_commandline(int argc, char *argv[], Options *op)
{
    int c;
    int print_help_after = FALSE;
    int print_help_args_only_after = FALSE;
    int print_advanced_help = FALSE;
    char *strval = NULL, *program_name = NULL;
    int boolval;
    int intval = 0;

    /*
     * if the installer was invoked as "nvidia-uninstall", perform an
     * uninstallation.
     */
    program_name = strdup(argv[0]);
    if (strcmp(basename(program_name), "nvidia-uninstall") == 0) {
        op->uninstall = TRUE;
    }
    free(program_name);

    while (1) {

        c = nvgetopt(argc, argv, __options, &strval, &boolval, &intval,
                     NULL,  /* doubleval */
                     NULL); /* disable_val */

        if (c == -1)
            break;
        
        switch (c) {
            
        case 'a': /* ignored (but retained for compatibility) */ break;
        case 'e': op->expert = TRUE; break;
        case 'v': print_version(); exit(0); break;
        case 'd': op->debug = TRUE; break;
        case 'i':
            op->driver_info = TRUE;
            op->ui_str = "none";
            break;
        case 'n': op->no_precompiled_interface = TRUE; break;
        case 'c': op->no_ncurses_color = TRUE; break;
        case 'h': print_help_after = TRUE; break;
        case 'A':
            print_help_after = TRUE;
            print_advanced_help = TRUE;
            break;
        case 'q': op->no_questions = TRUE; break;
        case 'b': op->no_backup = TRUE; break;
        case 'K':
            op->kernel_module_only = TRUE;
            op->no_kernel_module = FALSE; /* conflicts  */
            break;
        case 'X': op->run_nvidia_xconfig = TRUE; break;
        case 's':
            op->silent = op->no_questions = TRUE;
            op->ui_str = "none";
            break;
        case 'z': op->no_nouveau_check = TRUE; break;
        case 'Z': op->disable_nouveau = TRUE; break;
        case 'k':
            op->kernel_name = strval;
            op->no_precompiled_interface = TRUE;
            op->ignore_cc_version_check = TRUE;
            break;
        case 'j':
            if (intval < 1) {
                nv_error_msg("Invalid concurrency level %d: nvidia-installer "
                             "will attempt to autodetect the number of CPUs.",
                             intval);
                intval = 0;
            }
            op->concurrency_level = intval;
            break;
            
        case XFREE86_PREFIX_OPTION:
        case X_PREFIX_OPTION:
            op->x_prefix = strval; break;
        case X_LIBRARY_PATH_OPTION:
            op->x_library_path = strval; break;
        case X_MODULE_PATH_OPTION:
            op->x_module_path = strval; break;
        case X_SYSCONFIG_PATH_OPTION:
            op->x_sysconfig_path = strval; break;
        case OPENGL_PREFIX_OPTION:
            op->opengl_prefix = strval; break;
        case OPENGL_LIBDIR_OPTION:
            op->opengl_libdir = strval; break;
#if defined(NV_X86_64)
        case COMPAT32_CHROOT_OPTION:
            op->compat32_chroot = strval; break;
        case COMPAT32_PREFIX_OPTION:
            op->compat32_prefix = strval; break;
        case COMPAT32_LIBDIR_OPTION:
            op->compat32_libdir = strval; break;
        case INSTALL_COMPAT32_LIBS_OPTION:
            op->install_compat32_libs = boolval ? NV_OPTIONAL_BOOL_TRUE :
                                                  NV_OPTIONAL_BOOL_FALSE;
            break;
#endif
        case DOCUMENTATION_PREFIX_OPTION:
            op->documentation_prefix = strval; break;
        case APPLICATION_PROFILE_PATH_OPTION:
            op->application_profile_path = strval; break;
        case INSTALLER_PREFIX_OPTION:
            op->installer_prefix = strval; break;
        case UTILITY_PREFIX_OPTION:
            op->utility_prefix = strval; break;
        case UTILITY_LIBDIR_OPTION:
            op->utility_libdir = strval; break;
        case KERNEL_SOURCE_PATH_OPTION:
            op->kernel_source_path = strval; break;
        case KERNEL_OUTPUT_PATH_OPTION:
            op->kernel_output_path = strval; break;
        case KERNEL_INCLUDE_PATH_OPTION:
            op->kernel_include_path = strval; break;
        case KERNEL_INSTALL_PATH_OPTION:
            op->kernel_module_installation_path = strval; break;
        case UNINSTALL_OPTION:
            op->uninstall = TRUE; break;
        case SKIP_MODULE_UNLOAD_OPTION:
            op->skip_module_unload = TRUE; break;
        case PROC_MOUNT_POINT_OPTION:
            op->proc_mount_point = strval; break;
        case USER_INTERFACE_OPTION:
            op->ui_str = strval; break;
        case LOG_FILE_NAME_OPTION:
            op->log_file_name = strval; break;
        case HELP_ARGS_ONLY_OPTION:
            print_help_args_only_after = TRUE;
            break;
        case TMPDIR_OPTION:
            op->tmpdir = strval; break;
        case OPENGL_HEADERS_OPTION:
            op->opengl_headers = TRUE; break;
        case NO_NVIDIA_MODPROBE_OPTION:
            op->nvidia_modprobe = FALSE; break;
#if defined(NV_TLS_TEST)
        case FORCE_TLS_OPTION:
            if (strcasecmp(strval, "new") == 0)
                op->which_tls = FORCE_NEW_TLS;
            else if (strcasecmp(strval, "classic") == 0)
                op->which_tls = FORCE_CLASSIC_TLS;
            else {
                nv_error_msg("Invalid parameter for '--force-tls'");
                goto fail;
            }
            break;
#if defined(NV_X86_64)
        case FORCE_TLS_COMPAT32_OPTION:
            if (strcasecmp(strval, "new") == 0)
                op->which_tls_compat32 = FORCE_NEW_TLS;
            else if (strcasecmp(strval, "classic") == 0)
                op->which_tls_compat32 = FORCE_CLASSIC_TLS;
            else {
                nv_error_msg("Invalid parameter for '--force-tls-compat32'");
                goto fail;
            }
            break;
#endif /* NV_X86_64 */
#endif /* NV_TLS_TEST */
        case SANITY_OPTION:
            op->sanity = TRUE;
            break;
        case ADD_THIS_KERNEL_OPTION:
            op->add_this_kernel = TRUE;
            break;
        case ADVANCED_OPTIONS_ARGS_ONLY_OPTION:
            print_help_args_only_after = TRUE;
            print_advanced_help = TRUE;
            break;
        case RPM_FILE_LIST_OPTION:
            op->rpm_file_list = strval;
            break;
        case NO_RUNLEVEL_CHECK_OPTION:
            /* This option is no longer used; ignore it. */
            nv_warning_msg("The '--no-runlevel-check' option is deprecated:  "
                           "nvidia-installer will ignore this option.");
            break;
        case 'N':
            /* This option is no longer used; ignore it. */
            nv_warning_msg("The '--no-network' option is deprecated:  "
                           "nvidia-installer will ignore this option.");
            break;
        case PRECOMPILED_KERNEL_INTERFACES_PATH_OPTION:
            op->precompiled_kernel_interfaces_path = strval;
            break;
        case NO_ABI_NOTE_OPTION:
            op->no_abi_note = TRUE;
            break;
        case NO_RPMS_OPTION:
            op->no_rpms = TRUE;
            break;
        case 'r':
            op->no_recursion = TRUE;
            break;
        case FORCE_SELINUX_OPTION:
            if (strcasecmp(strval, "yes") == 0)
                op->selinux_option = SELINUX_FORCE_YES;
            else if (strcasecmp(strval, "no") == 0)
                op->selinux_option = SELINUX_FORCE_NO;
            else if (strcasecmp(strval, "default")) {
                nv_error_msg("Invalid parameter for '--force-selinux'");
                goto fail;
            }
            break;
        case SELINUX_CHCON_TYPE_OPTION:
            op->selinux_chcon_type = strval; break;
        case NO_SIGWINCH_WORKAROUND_OPTION:
            op->sigwinch_workaround = FALSE;
            break;
        case NO_KERNEL_MODULE_OPTION:
            op->no_kernel_module = TRUE;
            op->kernel_module_only = FALSE; /* conflicts */
            op->no_kernel_module_source = TRUE;
            break;
        case NO_X_CHECK_OPTION:
            op->no_x_check = TRUE;
            break;
        case NO_CC_VERSION_CHECK_OPTION:
            op->ignore_cc_version_check = TRUE;
            break;
        case NO_DISTRO_SCRIPTS_OPTION:
            op->run_distro_scripts = FALSE;
            break;
        case NO_OPENGL_FILES_OPTION:
            op->no_opengl_files = TRUE;
            break;
        case KERNEL_MODULE_SOURCE_PREFIX_OPTION:
            op->kernel_module_src_prefix = strval;
            break;
        case KERNEL_MODULE_SOURCE_DIR_OPTION:
            op->kernel_module_src_dir = strval;
            break;
        case NO_KERNEL_MODULE_SOURCE_OPTION:
            op->no_kernel_module_source = TRUE;
            break;
        case DKMS_OPTION:
            op->dkms = TRUE;
            break;
        case MODULE_SIGNING_SECRET_KEY_OPTION:
            op->module_signing_secret_key = strval;
            break;
        case MODULE_SIGNING_PUBLIC_KEY_OPTION:
            op->module_signing_public_key = strval;
            break;
        case MODULE_SIGNING_SCRIPT_OPTION:
            op->module_signing_script = strval;
            break;
        case MODULE_SIGNING_KEY_PATH_OPTION:
            op->module_signing_key_path = strval;
            break;
        case MODULE_SIGNING_HASH_OPTION:
            op->module_signing_hash = strval;
            break;
        case MODULE_SIGNING_X509_HASH_OPTION:
            op->module_signing_x509_hash = strval;
            break;
        case INSTALL_VDPAU_WRAPPER_OPTION:
            if (boolval) {
                nv_error_msg("This driver package does not contain a "
                             "pre-compiled copy of libvdpau.  Please see the "
                             "README for instructions on how to install "
                             "libvdpau.");
                goto fail;
            }
            break;
        case NO_UVM_OPTION:
            op->install_uvm = FALSE;
        break;
        case NO_DRM_OPTION:
            op->install_drm = FALSE;
        break;
        case NO_CHECK_FOR_ALTERNATE_INSTALLS_OPTION:
            op->check_for_alternate_installs = FALSE;
        break;
        case FORCE_LIBGLX_INDIRECT:
            op->install_libglx_indirect = NV_OPTIONAL_BOOL_TRUE;
            break;
        case NO_LIBGLX_INDIRECT:
            op->install_libglx_indirect = NV_OPTIONAL_BOOL_FALSE;
            break;
        case INSTALL_LIBGLVND_OPTION:
            op->install_libglvnd_libraries = boolval ? NV_OPTIONAL_BOOL_TRUE :
                                                       NV_OPTIONAL_BOOL_FALSE;
            break;
        case GLVND_GLX_CLIENT_OPTION:
            op->glvnd_glx_client = boolval;
            break;
        case GLVND_EGL_CONFIG_FILE_PATH_OPTION:
            op->libglvnd_json_path = strval;
            break;
        case GLVND_EGL_CLIENT_OPTION:
            op->glvnd_egl_client = boolval;
            break;
        case EGL_EXTERNAL_PLATFORM_CONFIG_FILE_PATH_OPTION:
            op->external_platform_json_path = strval;
            break;
        case OVERRIDE_FILE_TYPE_DESTINATION_OPTION:
            if (!assign_file_type_override(op, strval)) {
                goto fail;
            }
            break;
        default:
            goto fail;
        }

    }

    if (print_help_after) {
        print_help(argv[0], op->uninstall, print_advanced_help);
        exit(0);
    }

    if (print_help_args_only_after) {
        /*
         * We are printing help text for use by makeself.sh; we do not
         * want this formatted to the width of the current terminal,
         * so hardcode the width used by nv_info_msg() to 65.
         */
        reset_current_terminal_width(65);

        print_options(op->uninstall, print_advanced_help);
        exit(0);
    }

    /* Disable DKMS if module signing was requested; the two options are
     * mutually exclusive. */
    if (op->dkms &&
        op->module_signing_secret_key && op->module_signing_public_key) {
        ui_warn(op, "You selected the DKMS kernel module option, and also "
                "specified a pair of module signing keys. Keys cannot be "
                "managed securely under the automated DKMS framework. The "
                "DKMS option will be disabled.");
        op->dkms = FALSE;
    }


    /*
     * if the installer prefix was not specified, default it to the
     * utility prefix; this is done so that the installer prefix is
     * preferred, but if not specified, we default to what was
     * specified for the utility prefix.
     */

    if (!op->installer_prefix) {
        op->installer_prefix = op->utility_prefix;
    }

    /*
     * Set the default log file path. This is deferred until after the
     * command line has been parsed so that the installer has a chance
     * to determine whether it should be run in uninstall mode.
     */

    if (!op->log_file_name) {
        op->log_file_name = op->uninstall ? DEFAULT_UNINSTALL_LOG_FILE_NAME :
                                            DEFAULT_LOG_FILE_NAME;
    }

    return;
    
 fail:
    nv_error_msg("Invalid commandline, please run `%s --help` "
                 "for usage information.", argv[0]);
    nvfree((void*)op);
    exit(1);
} /* parse_commandline() */



/*
 * main program entry point
 */

int main(int argc, char *argv[])
{
    Options *op;
    int ret = FALSE;

    /* Ensure created files get the permissions we expect */
    umask(022);

    /* Load defaults */

    op = load_default_options();
    if (!op) {
        fprintf(stderr, "\nOut of memory error.\n\n");
        return 1;
    }
    
    /* parse the commandline options */
    
    parse_commandline(argc, argv, op);
    
    /* init the log file */
    
    log_init(op, argc, argv);

    /* chdir() to the directory containing the binary */
    
    if (!adjust_cwd(op, argv[0])) return 1;

    /* initialize the user interface */
    
    if (!ui_init(op)) return 1;

    /* determine the concurrency level: do this early on, to allow for
     * parallelization of as much of the install as possible. */

    set_concurrency_level(op);
    
    /* check that we're running as root */
    
    if (!check_euid(op)) goto done;
    
    /*
     * find the system utilities we'll need
     *
     * XXX we won't always need all of these... should only look for
     * the ones we need.
     */
    
    if (!find_system_utils(op)) goto done;
    if (!find_module_utils(op)) goto done;
    if (!check_selinux(op)) goto done;

    /* check for X server properties based on the version of the server */

    query_xorg_version(op);

    /* get the default installation prefixes/paths */

    get_default_prefixes_and_paths(op);

    /* get driver information */

    if (op->driver_info) {
        ret = report_driver_information(op);
    }

    /* perform sanity tests */

    else if (op->sanity) {
        ret = sanity(op);
    }
    
    /* uninstall */

    else if (op->uninstall) {
        ret = uninstall_existing_driver(op, TRUE);
    }

    /* add this kernel */

    else if (op->add_this_kernel) {
        ret = add_this_kernel(op);
    }

    /* install from the cwd */
    
    else {
        ret = install_from_cwd(op);
    }

 done:
    
    ui_close(op);

    nvfree((void*)op);
    
    return (ret ? 0 : 1);
    
} /* main() */
