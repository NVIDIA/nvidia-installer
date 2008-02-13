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

#define TAB "  "

static void print_version(void);
static void print_help(void);
static void print_advanced_options(void);
static void print_help_args_only(int args_only);
static void print_advanced_options_args_only(int args_only);



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
 * print_help() - print usage information
 */

static void print_help(void)
{
    print_version();
    
    fmtout("");
    fmtout("nvidia-installer [options]");
    fmtout("");
    
    print_help_args_only(FALSE);

} /* print_help() */



static void print_advanced_options(void)
{
    print_version();
    
    fmtout("");
    fmtout("nvidia-installer [options]");
    fmtout("");
    
    fmtout("");
    fmtout("COMMON OPTIONS:");
    fmtout("");

    print_help_args_only(FALSE);

    fmtout("");
    fmtout("ADVANCED OPTIONS:");
    fmtout("");
    
    print_advanced_options_args_only(FALSE);

} /* print_advanced_options() */



static void print_help_args_only(int args_only)
{
    /*
     * the args_only parameter is used by makeself.sh to get our
     * argument list and description; in this case we don't want to
     * format to the width of the terminal, so hardcode the width to
     * 65.
     */

    if (args_only) reset_current_terminal_width(65);

    fmtout("-a, --accept-license");
    fmtoutp(TAB, "Bypass the display and prompting for acceptance of the "
            "NVIDIA Software License Agreement.  By passing this option to "
            "nvidia-installer, you indicate that you have read and accept the "
            "License Agreement contained in the file 'LICENSE' (in the top "
            "level directory of the driver package).");
    fmtout("");
    
    fmtout("--update");
    fmtoutp(TAB, "Connect to the NVIDIA ftp server '%s' and determine the "
            "latest available driver version.  If there is a more recent "
            "driver available, automatically download and install it.  Any "
            "other options given on the commandline will be passed on to the "
            "downloaded driver package when installing it.", DEFAULT_FTP_SITE);
    fmtout("");

    fmtout("-v, --version");
    fmtoutp(TAB, "Print the nvidia-installer version and exit.");
    fmtout("");
    
    fmtout("-h, --help");
    fmtoutp(TAB, "Print usage information for the common commandline options "
            "and exit.");
    fmtout("");
    
    fmtout("-A, --advanced-options");
    fmtoutp(TAB, "Print usage information for the common commandline options "
            "as well as the advanced options, and then exit.");
    fmtout("");
    
} /* print_help_args_only() */


static void print_advanced_options_args_only(int args_only)
{
    /*
     * the args_only parameter is used by makeself.sh to get our
     * argument list and description; in this case we don't want to
     * format to the width of the terminal, so hardcode the width to
     * 65.
     */

    if (args_only) reset_current_terminal_width(65);

    fmtout("-i, --driver-info");
    fmtoutp(TAB, "Print information about the currently installed NVIDIA "
            "driver version.");
    fmtout("");
    
    fmtout("--uninstall");
    fmtoutp(TAB, "Uninstall the currently installed NVIDIA driver.");
    fmtout("");
    
    fmtout("--sanity");
    fmtoutp(TAB, "Perform basic sanity tests on an existing NVIDIA "
            "driver installation.");
    fmtout("");
    
    fmtout("-e, --expert");
    fmtoutp(TAB, "Enable 'expert' installation mode; more detailed questions "
            "will be asked, and more verbose output will be printed; "
            "intended for expert users.  The questions may be suppressed "
            "with the '--no-questions' commandline option.");
    fmtout("");

    fmtout("-q, --no-questions");
    fmtoutp(TAB, "Do not ask any questions; the default (normally 'yes') "
            "is assumed for "
            "all yes/no questions, and the default string is assumed in "
            "any situation where the user is prompted for string input.  "
            "The one question that is not bypassed by this option is "
            "license acceptance; the license may be accepted with the "
            "commandline option '--accept-license'.");
    fmtout("");

    fmtout("-s, --silent");
    fmtoutp(TAB, "Run silently; no questions are asked and no output is "
            "printed, except for error messages to stderr.  This option "
            "implies '--ui=none --no-questions --accept-license'.");
    fmtout("");

    fmtout("--x-prefix=[X PREFIX]");
    fmtoutp(TAB, "The prefix under which the X components of the "
            "NVIDIA driver will be installed; the default is: '%s'.  Only "
            "under rare circumstances should this option be used.",
            DEFAULT_XFREE86_INSTALLATION_PREFIX);
    fmtout("");
    
    fmtout("--opengl-prefix=[OPENGL PREFIX]");
    fmtoutp(TAB, "The prefix under which the OpenGL components of the "
            "NVIDIA driver will be installed; the default is: '%s'.  Only ",
            "under rare circumstances should this option be used.  The Linux "
            "OpenGL ABI (http://oss.sgi.com/projects/ogl-sample/ABI/) "
            "mandates this default value.",
            DEFAULT_OPENGL_INSTALLATION_PREFIX);
    fmtout("");
    
    fmtout("--installer-prefix=[INSTALLER PREFIX]");
    fmtoutp(TAB, "The prefix under which the installer binary will be "
            "installed; the default is: '%s'.  Note: use the "
            "\"--utility-prefix\" option instead.",
            DEFAULT_INSTALLER_INSTALLATION_PREFIX);
    fmtout("");
    
    fmtout("--utility-prefix=[UTILITY PREFIX]");
    fmtoutp(TAB, "The prefix under which the various NVIDIA utilities "
            "(nvidia-installer, nvidia-settings, nvidia-bug-report.sh) will "
            "be installed; the default is: '%s'.",
            DEFAULT_UTILITY_INSTALLATION_PREFIX);
    fmtout("");

    fmtout("--kernel-include-path=[KERNEL INCLUDE PATH]");
    fmtoutp(TAB, "The directory containing the kernel include files that "
            "should be used when compiling the NVIDIA kernel module.  "
            "This option is deprecated; please use '--kernel-source-path' "
            "instead.");
    fmtout("");
    
    fmtout("--kernel-source-path=[KERNEL SOURCE PATH]");
    fmtoutp(TAB, "The directory containing the kernel source files that "
            "should be used when compiling the NVIDIA kernel module.  "
            "When not specified, the installer will use "
            "'/lib/modules/`uname -r`/build', if that "
            "directory exists.  Otherwise, it will use "
            "'/usr/src/linux'.");
    fmtout("");
    
    fmtout("--kernel-install-path=[KERNEL INSTALL PATH]");
    fmtoutp(TAB, "The directory in which the NVIDIA kernel module should be "
            "installed.  The default value is either '/lib/modules/`uname "
            "-r`/kernel/drivers/video' (if '/lib/modules/`uname -r`/kernel' "
            "exists) or '/lib/modules/`uname -r`/video'.");
    fmtout("");

    fmtout("--proc-mount-point=[PROC FILESYSTEM MOUNT POINT]");
    fmtoutp(TAB, "The mount point for the proc file system; if not "
            "specified, then this value defaults to '%s' (which is normally "
            "correct).  The mount point of the proc filesystem is needed "
            "because the contents of '<proc filesystem>/version' is used when "
            "identifying if a precompiled kernel interface is available for "
            "the currently running kernel.  This option should only be needed "
            "in very rare circumstances.", DEFAULT_PROC_MOUNT_POINT);
    fmtout("");
    
    fmtout("--log-file-name=[LOG FILE NAME]");
    fmtoutp(TAB, "File name of the installation log file (the default is: "
            "'%s').", DEFAULT_LOG_FILE_NAME);
    fmtout("");

    fmtout("--tmpdir=[TMPDIR]");
    fmtoutp(TAB, "Use the specified directory as a temporary directory when "
            "downloading files from the NVIDIA ftp site; "
            "if not given, then the following list will be searched, and "
            "the first one that exists will be used: $TMPDIR, /tmp, ., "
            "$HOME.");
    fmtout("");

    fmtout("-m, --ftp-mirror=[FTP MIRROR]");
    fmtoutp(TAB, "Use the specified ftp mirror rather than the default '%s' "
            "when downloading driver updates.", DEFAULT_FTP_SITE);
    fmtout("");

    fmtout("-l, --latest");
    fmtoutp(TAB, "Connect to the NVIDIA ftp server %s (or use the ftp mirror "
            "specified with the '--ftp-mirror' option) and query the most "
            "recent %s-%s driver version number.", DEFAULT_FTP_SITE,
            INSTALLER_OS, INSTALLER_ARCH);
    fmtout("");
    
    fmtout("-f, --force-update");
    fmtoutp(TAB, "Forces an update to proceed, even if the installer "
            "thinks the latest driver is already installed; this option "
            "implies '--update'.");
    fmtout("");

    fmtout("--ui=[USER INTERFACE]");
    fmtoutp(TAB, "Specify what user interface to use, if available.  "
            "Valid values are 'ncurses' (the default) or 'none'. "
            "If the ncurses interface fails to initialize, or 'none' "
            "is specified, then a simple printf/scanf interface will "
            "be used.");
    fmtout("");
    
    fmtout("-c, --no-ncurses-color");
    fmtoutp(TAB, "Disable use of color in the ncurses user interface.");
    fmtout("");
    
    fmtout("--opengl-headers");
    fmtoutp(TAB, "Normally, installation does not install NVIDIA's OpenGL "
            "header files.  This option enables installation of the NVIDIA "
            "OpenGL header files.");
    fmtout("");

    fmtout("--force-tls=[TLS TYPE]");
    fmtoutp(TAB, "NVIDIA's OpenGL libraries are compiled with one of two "
            "different thread local storage (TLS) mechanisms: 'classic tls' "
            "which is used on systems with glibc 2.2 or older, and 'new tls' "
            "which is used on systems with tls-enabled glibc 2.3 or newer.  "
            "The nvidia-installer will select the OpenGL "
            "libraries appropriate for your system; however, you may use "
            "this option to force the installer to install one library "
            "type or another.  Valid values for [TLS TYPE] are 'new' and "
            "'classic'.");
    fmtout("");

    fmtout("-k, --kernel-name=[KERNELNAME]");
    fmtoutp(TAB, "Build and install the NVIDIA kernel module for the "
            "non-running kernel specified by [KERNELNAME] ([KERNELNAME] "
            "should be the output of `uname -r` when the target kernel is "
            "actually running).  This option implies "
            "'--no-precompiled-interface'.  If the options "
            "'--kernel-install-path' and '--kernel-source-path' are not "
            "given, then they will be inferred from [KERNELNAME]; eg: "
            "'/lib/modules/[KERNELNAME]/kernel/drivers/video/' and "
            "'/lib/modules/[KERNELNAME]/build/', respectively.");
    fmtout("");
    
    fmtout("-n, --no-precompiled-interface");
    fmtoutp(TAB, "Disable use of precompiled kernel interfaces.");
    fmtout("");

    fmtout("--no-runlevel-check");
    fmtoutp(TAB, "Normally, the installer checks the current runlevel and "
            "warns users if they are in runlevel 1: in runlevel 1, some "
            "services that are normally active are disabled (such as devfs), "
            "making it difficult for the installer to properly setup the "
            "kernel module configuration files.  This option disables the "
            "runlevel check.");
    fmtout("");

    fmtout("--no-abi-note");
    fmtoutp(TAB, "The NVIDIA OpenGL libraries contain an OS ABI note tag, "
            "which identifies the minimum kernel version needed to use the "
            "library.  This option causes the installer to remove this note "
            "from the OpenGL libraries during installation.");
    fmtout("");

    fmtout("--no-rpms");
    fmtoutp(TAB, "Normally, the installer will check for several rpms that "
            "conflict with the driver (specifically: NVIDIA_GLX and "
            "NVIDIA_kernel), and remove them if present.  This option "
            "disables this check.");
    fmtout("");

    fmtout("-b, --no-backup");
    fmtoutp(TAB, "During driver installation, conflicting files are backed "
            "up, so that they can be restored when the driver is "
            "uninstalled.  This option causes the installer to simply delete "
            "conflicting files, rather than back them up.");
    fmtout("");

    fmtout("--no-network");
    fmtoutp(TAB, "This option instructs the installer to not attempt to "
            "connect to the NVIDIA ftp site (for updated precompiled kernel "
            "interfaces, for example).");
    fmtout("");
    
    fmtout("-K, --kernel-module-only");
    fmtoutp(TAB, "Install a kernel module only, and don't uninstall the "
            "existing driver.  This is intended to be used to install kernel "
            "modules for additional kernels (in cases where you might boot "
            "between several different kernels).  To use this option, you "
            "must already have a driver installed, and the version of the "
            "installed driver must match the version of this kernel "
            "module.");
    fmtout("");

    fmtout("--precompiled-kernel-interfaces-path");
    fmtoutp(TAB, "Before searching for a precompiled kernel interface in the "
            ".run file, search in the specified directory.");
    fmtout("");
    
} /* print_advanced_options_args_only() */


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

#define XFREE86_PREFIX_OPTION           1
#define OPENGL_PREFIX_OPTION            2
#define KERNEL_INCLUDE_PATH_OPTION      3
#define KERNEL_INSTALL_PATH_OPTION      4
#define UNINSTALL_OPTION                5
#define PROC_MOUNT_POINT_OPTION         6
#define USER_INTERFACE_OPTION           7
#define LOG_FILE_NAME_OPTION            8
#define HELP_ARGS_ONLY_OPTION           9
#define TMPDIR_OPTION                   10
#define OPENGL_HEADERS_OPTION           11
#define INSTALLER_PREFIX_OPTION         12
#define FORCE_TLS_OPTION                13
#define SANITY_OPTION                   14                 
#define ADVANCED_OPTIONS_ARGS_ONLY_OPTION 15
#define UTILITY_PREFIX_OPTION           16
#define ADD_THIS_KERNEL_OPTION          17
#define RPM_FILE_LIST_OPTION            18
#define NO_RUNLEVEL_CHECK_OPTION        19
#define NO_NETWORK_OPTION               20
#define PRECOMPILED_KERNEL_INTERFACES_PATH 21
#define NO_ABI_NOTE_OPTION              22
#define KERNEL_SOURCE_PATH_OPTION       23
#define NO_RPMS_OPTION                  24
#define X_PREFIX_OPTION                 25


    static struct option long_options[] = {
        { "accept-license",           0, NULL, 'a'                        },
        { "update",                   0, NULL, 'u'                        },
        { "force-update",             0, NULL, 'f'                        },
        { "expert",                   0, NULL, 'e'                        },
        { "version",                  0, NULL, 'v'                        },
        { "debug",                    0, NULL, 'd'                        },
        { "driver-info",              0, NULL, 'i'                        },
        { "no-precompiled-interface", 0, NULL, 'n'                        },
        { "no-ncurses-color",         0, NULL, 'c'                        },
        { "latest",                   0, NULL, 'l'                        },
        { "ftp-mirror",               1, NULL, 'm'                        },
        { "no-questions",             0, NULL, 'q'                        },
        { "kernel-name",              1, NULL, 'k'                        },
        { "silent",                   0, NULL, 's'                        },
        { "help",                     0, NULL, 'h'                        },
        { "advanced-options",         0, NULL, 'A'                        },
        { "no-backup",                0, NULL, 'b'                        },
        { "kernel-module-only",       0, NULL, 'K'                        },
        { "xfree86-prefix",           1, NULL, XFREE86_PREFIX_OPTION      },
        { "x-prefix",                 1, NULL, X_PREFIX_OPTION            },
        { "opengl-prefix",            1, NULL, OPENGL_PREFIX_OPTION       },
        { "installer-prefix",         1, NULL, INSTALLER_PREFIX_OPTION    },
        { "utility-prefix",           1, NULL, UTILITY_PREFIX_OPTION      },
        { "kernel-include-path",      1, NULL, KERNEL_INCLUDE_PATH_OPTION },
        { "kernel-source-path",       1, NULL, KERNEL_SOURCE_PATH_OPTION  },
        { "kernel-install-path",      1, NULL, KERNEL_INSTALL_PATH_OPTION },
        { "uninstall",                0, NULL, UNINSTALL_OPTION           },
        { "proc-mount-point",         1, NULL, PROC_MOUNT_POINT_OPTION    },
        { "ui",                       1, NULL, USER_INTERFACE_OPTION      },
        { "log-file-name",            1, NULL, LOG_FILE_NAME_OPTION       },
        { "help-args-only",           0, NULL, HELP_ARGS_ONLY_OPTION      },
        { "tmpdir",                   1, NULL, TMPDIR_OPTION              },
        { "opengl-headers",           0, NULL, OPENGL_HEADERS_OPTION      },
        { "force-tls",                1, NULL, FORCE_TLS_OPTION           },
        { "sanity",                   0, NULL, SANITY_OPTION              },
        { "add-this-kernel",          0, NULL, ADD_THIS_KERNEL_OPTION     },
        { "rpm-file-list",            1, NULL, RPM_FILE_LIST_OPTION       },
        { "no-runlevel-check",        0, NULL, NO_RUNLEVEL_CHECK_OPTION   },
        { "no-network",               0, NULL, NO_NETWORK_OPTION          },
        { "no-abi-note",              0, NULL, NO_ABI_NOTE_OPTION         },
        { "no-rpms",                  0, NULL, NO_RPMS_OPTION             },
        { "precompiled-kernel-interfaces-path", 1, NULL,
          PRECOMPILED_KERNEL_INTERFACES_PATH                              },
        { "advanced-options-args-only", 0, NULL,
          ADVANCED_OPTIONS_ARGS_ONLY_OPTION                               },
        {  0,                         0, NULL, 0                          }
    };
    
    op = (Options *) nvalloc(sizeof(Options));
    
    /* statically initialized strings */
    
    op->xfree86_prefix = DEFAULT_XFREE86_INSTALLATION_PREFIX;
    op->opengl_prefix = DEFAULT_OPENGL_INSTALLATION_PREFIX;
    op->installer_prefix = NULL;
    op->utility_prefix = DEFAULT_UTILITY_INSTALLATION_PREFIX;
    op->proc_mount_point = DEFAULT_PROC_MOUNT_POINT;
    op->log_file_name = DEFAULT_LOG_FILE_NAME;
    op->ftp_site = DEFAULT_FTP_SITE;
    op->tmpdir = get_tmpdir(op);
    op->no_runlevel_check = FALSE;
    op->no_backup = FALSE;
    op->no_network = FALSE;
    op->kernel_module_only = FALSE;
    op->no_abi_note = FALSE;    

    op->logging = TRUE; /* log by default */

    while (1) {
        
        c = getopt_long(argc, argv, "afg:evdinclm:qk:shAbK",
                        long_options, &option_index);
        if (c == -1)
            break;
        
        switch (c) {
            
        case 'a': op->accept_license = TRUE; break;
        case 'u': op->update = TRUE; break;
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
        case 'h': print_help(); exit(0); break;
        case 'A': print_advanced_options(); exit(0); break;
        case 'q': op->no_questions = TRUE; break;
        case 'b': op->no_backup = TRUE; break;
        case 'K': op->kernel_module_only = TRUE; break;
        case 's':
            op->silent = op->no_questions = op->accept_license = TRUE;
            op->ui_str = "none";
            break;

        case 'k':
            op->kernel_name = optarg;
            op->no_precompiled_interface = TRUE;
            break;
            
        case XFREE86_PREFIX_OPTION:
        case X_PREFIX_OPTION:
            op->xfree86_prefix = optarg; break;
        case OPENGL_PREFIX_OPTION:
            op->opengl_prefix = optarg; break;
        case INSTALLER_PREFIX_OPTION:
            op->installer_prefix = optarg; break;
        case UTILITY_PREFIX_OPTION:
            op->utility_prefix = optarg; break;
        case KERNEL_SOURCE_PATH_OPTION:
            op->kernel_source_path = optarg; break;
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
            print_help_args_only(TRUE); exit(0); break;
        case TMPDIR_OPTION:
            op->tmpdir = optarg; break;
        case OPENGL_HEADERS_OPTION:
            op->opengl_headers = TRUE; break;
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
        case SANITY_OPTION:
            op->sanity = TRUE;
            break;
        case ADD_THIS_KERNEL_OPTION:
            op->add_this_kernel = TRUE;
            break;
        case ADVANCED_OPTIONS_ARGS_ONLY_OPTION:
            print_advanced_options_args_only(TRUE); exit(0);
            break;
        case RPM_FILE_LIST_OPTION:
            op->rpm_file_list = optarg;
            break;
        case NO_RUNLEVEL_CHECK_OPTION:
            op->no_runlevel_check = TRUE;
            break;
        case NO_NETWORK_OPTION:
            op->no_network = TRUE;
            break;
        case PRECOMPILED_KERNEL_INTERFACES_PATH:
            op->precompiled_kernel_interfaces_path = optarg;
            break;
        case NO_ABI_NOTE_OPTION:
            op->no_abi_note = TRUE;
            break;
        case NO_RPMS_OPTION:
            op->no_rpms = TRUE;
            break;
            
        default:
            fmterr("");
            fmterr("Invalid commandline, please run `%s --help` "
                   "for usage information.", argv[0]);
            fmterr("");
            exit(1);
        }

        op->update_arguments = append_update_arguments(op->update_arguments,
                                                       c, optarg,
                                                       long_options);
    }
    
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
    
    op->distro = get_distribution(op);
    
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
