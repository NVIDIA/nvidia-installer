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
 * nv_installer.c
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>

#define _GNU_SOURCE /* XXX not very portable */
#include <getopt.h>

#include "nvidia-installer.h"
#include "kernel.h"
#include "user-interface.h"
#include "backup.h"
#include "files.h"
#include "misc.h"
#include "update.h"
#include "format.h"
#include "sanity.h"
#include "option_table.h"

#define TAB "  "
#define BIGTAB "      "

static void print_version(void);
static void print_help(int advanced);
static void print_help_args_only(int args_only, int advanced);



/*
 * print_version() - print the current nvidia-installer version number
 */

extern const char *pNV_ID;

static void print_version(void)
{
    fmtout("");
    fmtout(pNV_ID);
    fmtoutp(TAB, "The NVIDIA Software Installer for Unix/Linux.");
    fmtout("");
    fmtoutp(TAB, "This program is used to install and upgrade "
                "The NVIDIA Accelerated Graphics Driver Set for %s-%s.",
                INSTALLER_OS, INSTALLER_ARCH);
    fmtout("");
    fmtoutp(TAB, "Copyright (C) 2003 NVIDIA Corporation.");
    fmtout("");
}


/*
 * cook_description() - the description string may contain text within
 * brackets, which is used by the manpage generator to denote text to
 * be italicized.  We want to omit the bracket characters here.
 */

static char *cook_description(const char *description)
{
    int len;
    char *s, *dst;
    const char *src;
    
    len = strlen(description);
    s = nvalloc(len + 1);
    
    for (src = description, dst = s; *src; src++) {
        if (*src != '[' && (*src != ']')) {
            *dst = *src;
            dst++;
        }
    }

    *dst = '\0';

    return s;
    
} /* cook_description() */


/*
 * print_help() - print usage information
 */

static void print_help(int advanced)
{
    print_version();
    
    fmtout("");
    fmtout("nvidia-installer [options]");
    fmtout("");
    
    print_help_args_only(FALSE, advanced);

} /* print_help() */


static void print_help_args_only(int args_only, int advanced)
{
    int i, j, len;
    char *msg, *tmp, scratch[64];
    const NVOption *o;
    /*
     * the args_only parameter is used by makeself.sh to get our
     * argument list and description; in this case we don't want to
     * format to the width of the terminal, so hardcode the width to
     * 65.
     */

    if (args_only) reset_current_terminal_width(65);

    for (i = 0; __options[i].name; i++) {
        o = &__options[i];

        /*
         * if non-advanced help is requested, and the ALWAYS flag is
         * not set, then skip this option
         */

        if (!advanced && !(o->flags & OPTION_HELP_ALWAYS)) continue;

        /* Skip options with no help text */
        if (!o->description) continue;

        if (o->flags & NVOPT_IS_BOOLEAN) {
            msg = nvstrcat("--", o->name, "/--no-", o->name, NULL);
        } else if (isalnum(o->val)) {
            sprintf(scratch, "%c", o->val);
            msg = nvstrcat("-", scratch, ", --", o->name, NULL);
        } else {
            msg = nvstrcat("--", o->name, NULL);
        }
        if (o->flags & NVOPT_HAS_ARGUMENT) {
            len = strlen(o->name);
            for (j = 0; j < len; j++) scratch[j] = toupper(o->name[j]);
            scratch[len] = '\0';
            tmp = nvstrcat(msg, "=", scratch, NULL);
            nvfree(msg);
            msg = tmp;
        }
        fmtoutp(TAB, msg);
        if (o->description) {
            tmp = cook_description(o->description);
            fmtoutp(BIGTAB, tmp);
            free(tmp);
        }
        fmtout("");
        nvfree(msg);
    }
} /* print_help_args_only() */


/*
 * parse_commandline() - malloc an Options structure, initialize it,
 * and fill in any pertinent data from the commandline arguments; it
 * is intended that this function do only minimal sanity checking --
 * just enough error trapping to ensure correct syntax of the
 * commandline options.  Validation of the actual data specified
 * through the options is left for the functions that use this data.
 *
 * XXX Would it be better to do more validation now?
 *
 * XXX this implementation uses getopt_long(), which isn't portable to
 * non-glibc based systems...
 */

Options *parse_commandline(int argc, char *argv[])
{
    Options *op;
    int c, option_index = 0;

    const int num_opts = sizeof(__options) / sizeof(__options[0]) - 1;
    /* Allocate space for the long options. */
    struct option *long_options = nvalloc(num_opts * sizeof(struct option));
    /* Allocate space for the short options: leave enough room for a letter and
     * ':' for each option, plus the '\0'. */
    char *short_options = nvalloc(num_opts * 2 + 1);
    char *pShort = short_options;

    /* Generate the table for getopt_long and the string for the short options. */
    for (c = 0; c < num_opts; c++) {
        struct option* op = &long_options[c];
        const NVOption* o = &__options[c];

        op->name = o->name;
        op->has_arg =
            (o->flags & NVOPT_HAS_ARGUMENT) ?
            required_argument : no_argument;
        op->flag = NULL;
        op->val = o->val;

        if (isalnum(o->val)) {
            *pShort++ = o->val;

            if (o->flags & NVOPT_HAS_ARGUMENT)
                *pShort++ = ':';
        }
    }
    *pShort = '\0';

    op = (Options *) nvalloc(sizeof(Options));
    
    /* statically initialized strings */
    
    op->proc_mount_point = DEFAULT_PROC_MOUNT_POINT;
    op->log_file_name = DEFAULT_LOG_FILE_NAME;
    op->ftp_site = DEFAULT_FTP_SITE;

    op->tmpdir = get_tmpdir(op);
    op->distro = get_distribution(op);

    op->logging = TRUE; /* log by default */
    op->opengl_headers = TRUE; /* We now install our GL headers by default */
    op->run_nvidia_xconfig = FALSE;
    op->selinux_option = SELINUX_DEFAULT;

    op->sigwinch_workaround = TRUE;

    while (1) {
        
        c = getopt_long(argc, argv, short_options,
                        long_options, &option_index);
        if (c == -1)
            break;
        
        switch (c) {
            
        case 'a': op->accept_license = TRUE; break;
        case UPDATE_OPTION: op->update = TRUE; break;
        case 'e': op->expert = TRUE; break;
        case 'v': print_version(); exit(0); break;
        case 'd': op->debug = TRUE; break;
        case 'i':
            op->driver_info = TRUE;
            op->ui_str = "none";
            break;
            
        case 'n': op->no_precompiled_interface = TRUE; break;
        case 'c': op->no_ncurses_color = TRUE; break;
        case 'l': op->latest = TRUE; break;
        case 'm': op->ftp_site = optarg; break;
        case 'f': op->update = op->force_update = TRUE; break;
        case 'h': print_help(FALSE); exit(0); break;
        case 'A': print_help(TRUE); exit(0); break;
        case 'q': op->no_questions = TRUE; break;
        case 'b': op->no_backup = TRUE; break;
        case 'K':
            op->kernel_module_only = TRUE;
            op->no_kernel_module = FALSE; /* conflicts  */
            break;
        case 'X': op->run_nvidia_xconfig = TRUE; break;
        case 's':
            op->silent = op->no_questions = op->accept_license = TRUE;
            op->ui_str = "none";
            break;

        case 'k':
            op->kernel_name = optarg;
            op->no_precompiled_interface = TRUE;
            op->ignore_cc_version_check = TRUE;
            break;
            
        case XFREE86_PREFIX_OPTION:
        case X_PREFIX_OPTION:
            op->x_prefix = optarg; break;
        case X_LIBRARY_PATH_OPTION:
            op->x_library_path = optarg; break;
        case X_MODULE_PATH_OPTION:
            op->x_module_path = optarg; break;
        case OPENGL_PREFIX_OPTION:
            op->opengl_prefix = optarg; break;
        case OPENGL_LIBDIR_OPTION:
            op->opengl_libdir = optarg; break;
#if defined(NV_X86_64)
        case COMPAT32_CHROOT_OPTION:
            op->compat32_chroot = optarg; break;
        case COMPAT32_PREFIX_OPTION:
            op->compat32_prefix = optarg; break;
        case COMPAT32_LIBDIR_OPTION:
            op->compat32_libdir = optarg; break;
#endif
        case DOCUMENTATION_PREFIX_OPTION:
            op->documentation_prefix = optarg; break;
        case INSTALLER_PREFIX_OPTION:
            op->installer_prefix = optarg; break;
        case UTILITY_PREFIX_OPTION:
            op->utility_prefix = optarg; break;
        case UTILITY_LIBDIR_OPTION:
            op->utility_libdir = optarg; break;
        case KERNEL_SOURCE_PATH_OPTION:
            op->kernel_source_path = optarg; break;
        case KERNEL_OUTPUT_PATH_OPTION:
            op->kernel_output_path = optarg; break;
        case KERNEL_INCLUDE_PATH_OPTION:
            op->kernel_include_path = optarg; break;
        case KERNEL_INSTALL_PATH_OPTION:
            op->kernel_module_installation_path = optarg; break;
        case UNINSTALL_OPTION:
            op->uninstall = TRUE; break;
        case PROC_MOUNT_POINT_OPTION:
            op->proc_mount_point = optarg; break;
        case USER_INTERFACE_OPTION:
            op->ui_str = optarg; break;
        case LOG_FILE_NAME_OPTION:
            op->log_file_name = optarg; break;
        case HELP_ARGS_ONLY_OPTION:
            print_help_args_only(TRUE, FALSE); exit(0); break;
        case TMPDIR_OPTION:
            op->tmpdir = optarg; break;
        case NO_OPENGL_HEADERS_OPTION:
            op->opengl_headers = FALSE; break;
        case FORCE_TLS_OPTION:
            if (strcasecmp(optarg, "new") == 0)
                op->which_tls = FORCE_NEW_TLS;
            else if (strcasecmp(optarg, "classic") == 0)
                op->which_tls = FORCE_CLASSIC_TLS;
            else {
                fmterr("");
                fmterr("Invalid parameter for '--force-tls'; please "
                       "run `%s --help` for usage information.", argv[0]);
                fmterr("");
                exit(1);
            }
            break;
#if defined(NV_X86_64)
        case FORCE_TLS_COMPAT32_OPTION:
            if (strcasecmp(optarg, "new") == 0)
                op->which_tls_compat32 = FORCE_NEW_TLS;
            else if (strcasecmp(optarg, "classic") == 0)
                op->which_tls_compat32 = FORCE_CLASSIC_TLS;
            else {
                fmterr("");
                fmterr("Invalid parameter for '--force-tls-compat32'; "
                       "please run `%s --help` for usage information.",
                       argv[0]);
                fmterr("");
                exit(1);
            }
            break;
#endif
        case SANITY_OPTION:
            op->sanity = TRUE;
            break;
        case ADD_THIS_KERNEL_OPTION:
            op->add_this_kernel = TRUE;
            break;
        case ADVANCED_OPTIONS_ARGS_ONLY_OPTION:
            print_help_args_only(TRUE, TRUE); exit(0);
            break;
        case RPM_FILE_LIST_OPTION:
            op->rpm_file_list = optarg;
            break;
        case NO_RUNLEVEL_CHECK_OPTION:
            op->no_runlevel_check = TRUE;
            break;
        case 'N':
            op->no_network = TRUE;
            break;
        case PRECOMPILED_KERNEL_INTERFACES_PATH_OPTION:
            op->precompiled_kernel_interfaces_path = optarg;
            break;
        case NO_ABI_NOTE_OPTION:
            op->no_abi_note = TRUE;
            break;
        case NO_RPMS_OPTION:
            op->no_rpms = TRUE;
            break;
        case NO_RECURSION_OPTION:
            op->no_recursion = TRUE;
            break;
        case FORCE_SELINUX_OPTION:
            if (strcasecmp(optarg, "yes") == 0)
                op->selinux_option = SELINUX_FORCE_YES;
            else if (strcasecmp(optarg, "no") == 0)
                op->selinux_option = SELINUX_FORCE_NO;
            else if (strcasecmp(optarg, "default")) {
                fmterr("");
                fmterr("Invalid parameter for '--force-selinux'; "
                       "please run `%s --help` for usage information.",
                       argv[0]);
                fmterr("");
                exit(1);
            }
            break;
        case SELINUX_CHCON_TYPE_OPTION:
            op->selinux_chcon_type = optarg; break;
        case NO_SIGWINCH_WORKAROUND_OPTION:
            op->sigwinch_workaround = FALSE;
            break;
        case NO_KERNEL_MODULE_OPTION:
            op->no_kernel_module = TRUE;
            op->kernel_module_only = FALSE; /* conflicts */
            break;
        case NO_X_CHECK_OPTION:
            op->no_x_check = TRUE;
            break;
        case NO_CC_VERSION_CHECK_OPTION:
            op->ignore_cc_version_check = TRUE;
            break;

        default:
            fmterr("");
            fmterr("Invalid commandline, please run `%s --help` "
                   "for usage information.", argv[0]);
            fmterr("");
            exit(1);
        }

        /*
         * as we go, build a list of options that we would pass on to
         * a new invocation of the installer if we were to download a
         * new driver and run its installer (update mode).  Be sure
         * not to place "--update" or "--force-update" in the update
         * argument list (avoid infinite loop)
         */
    
        if ((c != UPDATE_OPTION) && (c != 'f')) {
            
            op->update_arguments =
                append_update_arguments(op->update_arguments,
                                        c, optarg,
                                        long_options);
        }
    }

    nvfree((void*)long_options);
    nvfree((void*)short_options);

    if (optind < argc) {
        fmterr("");
        fmterr("Unrecognized arguments:");
        while (optind < argc)
            fmterrp("  ", argv[optind++]);
        fmterr("Invalid commandline, please run `%s --help` for "
               "usage information.", argv[0]);
        fmterr("");
        exit(1);
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

    return (op);
    
} /* parse_commandline() */



/*
 * main program entry point
 */

int main(int argc, char *argv[])
{
    Options *op;
    int ret = FALSE;
    
    /* parse the commandline options */
    
    op = parse_commandline(argc, argv);
    
    /* init the log file */
    
    log_init(op);

    /* chdir() to the directory containing the binary */
    
    if (!adjust_cwd(op, argv[0])) return 1;

    /* initialize the user interface */
    
    if (!ui_init(op)) return 1;
    
    /* check that we're running as root */
    
    if (!check_euid(op)) goto done;
    
    /* check that we're in a safe runlevel */

    if (!check_runlevel(op)) goto done;

    /*
     * find the system utilities we'll need
     *
     * XXX we won't always need all of these... should only look for
     * the ones we need.
     */
    
    if (!find_system_utils(op)) goto done;
    if (!find_module_utils(op)) goto done;
    if (!check_selinux(op)) goto done;

    /* check if we need to worry about modular Xorg */

    op->modular_xorg =
        check_for_modular_xorg(op);
    
    /* get the default installation prefixes/paths */

    get_default_prefixes_and_paths(op);

    /* get the latest available driver version */

    if (op->latest) {
        ret = report_latest_driver_version(op);
    }

    /* get driver information */

    else if (op->driver_info) {
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

    /* update */
    
    else if (op->update) {
        ret = update(op);
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
    
    return (ret ? 0 : 1);
    
} /* main() */
