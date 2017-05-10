/*
 * nvidia-installer: A tool for installing/un-installing the
 * NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2004-2010 NVIDIA Corporation
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
 * option_table.h
 */

#ifndef __OPT_TABLE_H__
#define __OPT_TABLE_H__

#include "nvgetopt.h"
#include "nvidia-installer.h"

#define NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL 0x00010000

/* make sure OPTION_APPLIES_TO_NVIDIA_UNINSTALL is in the approved range */
#if !(NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL & NVGETOPT_UNUSED_FLAG_RANGE)
#error NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL incorrectly defined
#endif


enum {
    XFREE86_PREFIX_OPTION = 1024,
    OPENGL_PREFIX_OPTION,
    OPENGL_LIBDIR_OPTION,
    KERNEL_INCLUDE_PATH_OPTION,
    KERNEL_INSTALL_PATH_OPTION,
    UNINSTALL_OPTION,
    SKIP_MODULE_UNLOAD_OPTION,
    PROC_MOUNT_POINT_OPTION,
    USER_INTERFACE_OPTION,
    LOG_FILE_NAME_OPTION,
    HELP_ARGS_ONLY_OPTION,
    TMPDIR_OPTION,
    OPENGL_HEADERS_OPTION,
    NO_NVIDIA_MODPROBE_OPTION,
    INSTALLER_PREFIX_OPTION,
    FORCE_TLS_OPTION,
    SANITY_OPTION,
    ADVANCED_OPTIONS_ARGS_ONLY_OPTION,
    UTILITY_PREFIX_OPTION,
    UTILITY_LIBDIR_OPTION,
    ADD_THIS_KERNEL_OPTION,
    RPM_FILE_LIST_OPTION,
    NO_RUNLEVEL_CHECK_OPTION,
    PRECOMPILED_KERNEL_INTERFACES_PATH_OPTION,
    PRECOMPILED_KERNEL_INTERFACES_URL_OPTION,
    NO_ABI_NOTE_OPTION,
    KERNEL_SOURCE_PATH_OPTION,
    NO_RPMS_OPTION,
    X_PREFIX_OPTION,
    KERNEL_OUTPUT_PATH_OPTION,
    FORCE_TLS_COMPAT32_OPTION,
    COMPAT32_CHROOT_OPTION,
    COMPAT32_PREFIX_OPTION,
    COMPAT32_LIBDIR_OPTION,
    UPDATE_OPTION,
    FORCE_SELINUX_OPTION,
    SELINUX_CHCON_TYPE_OPTION,
    NO_SIGWINCH_WORKAROUND_OPTION,
    X_MODULE_PATH_OPTION,
    DOCUMENTATION_PREFIX_OPTION,
    APPLICATION_PROFILE_PATH_OPTION,
    X_LIBRARY_PATH_OPTION,
    NO_KERNEL_MODULE_OPTION,
    NO_X_CHECK_OPTION,
    NO_CC_VERSION_CHECK_OPTION,
    NO_DISTRO_SCRIPTS_OPTION,
    NO_OPENGL_FILES_OPTION,
    KERNEL_MODULE_SOURCE_PREFIX_OPTION,
    KERNEL_MODULE_SOURCE_DIR_OPTION,
    NO_KERNEL_MODULE_SOURCE_OPTION,
    DKMS_OPTION,
    MODULE_SIGNING_SECRET_KEY_OPTION,
    MODULE_SIGNING_PUBLIC_KEY_OPTION,
    MODULE_SIGNING_SCRIPT_OPTION,
    MODULE_SIGNING_KEY_PATH_OPTION,
    MODULE_SIGNING_HASH_OPTION,
    MODULE_SIGNING_X509_HASH_OPTION,
    INSTALL_VDPAU_WRAPPER_OPTION,
    NO_CHECK_FOR_ALTERNATE_INSTALLS_OPTION,
    NO_UVM_OPTION,
    NO_DRM_OPTION,
    INSTALL_COMPAT32_LIBS_OPTION,
    X_SYSCONFIG_PATH_OPTION,
    FORCE_LIBGLX_INDIRECT,
    NO_LIBGLX_INDIRECT,
    INSTALL_LIBGLVND_OPTION,
    GLVND_GLX_CLIENT_OPTION,
    GLVND_EGL_CONFIG_FILE_PATH_OPTION,
    GLVND_EGL_CLIENT_OPTION,
};

static const NVGetoptOption __options[] = {
    /* These options are printed by "nvidia-installer --help" */

    { "accept-license", 'a', NVGETOPT_HELP_ALWAYS, NULL,
      "Bypass the display and prompting for acceptance of the "
      "NVIDIA Software License Agreement.  By passing this option to "
      "nvidia-installer, you indicate that you have read and accept the "
      "License Agreement contained in the file 'LICENSE' (in the top "
      "level directory of the driver package)." },

    { "update", UPDATE_OPTION, NVGETOPT_HELP_ALWAYS, NULL,
      "Connect to the NVIDIA FTP server ' " DEFAULT_FTP_SITE " ' and "
      "determine the latest available driver version.  If there is a more "
      "recent driver available, automatically download and install it.  Any "
      "other options given on the commandline will be passed on to the "
      "downloaded driver package when installing it." },

    { "version", 'v',
      NVGETOPT_HELP_ALWAYS | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Print the nvidia-installer version and exit." },

    { "help", 'h',
      NVGETOPT_HELP_ALWAYS | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Print usage information for the common commandline options "
      "and exit." },

    { "advanced-options", 'A',
      NVGETOPT_HELP_ALWAYS | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Print usage information for the common commandline options "
      "as well as the advanced options, and then exit." },

    /* These options are only printed by "nvidia-installer --advanced-help" */

    { "driver-info", 'i', 0, NULL,
      "Print information about the currently installed NVIDIA "
      "driver version." },

    { "uninstall", UNINSTALL_OPTION, 0, NULL,
      "Uninstall the currently installed NVIDIA driver." },

    { "skip-module-unload", SKIP_MODULE_UNLOAD_OPTION,
      NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "When uninstalling the driver, skip unloading of the NVIDIA kernel "
      "module. This option is ignored when the driver is being installed." },

    { "sanity", SANITY_OPTION, 0, NULL,
      "Perform basic sanity tests on an existing NVIDIA "
      "driver installation." },

    { "expert", 'e', NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Enable 'expert' installation mode; more detailed questions "
      "will be asked, and more verbose output will be printed; "
      "intended for expert users.  The questions may be suppressed "
      "with the '--no-questions' commandline option." },

    { "no-questions", 'q', NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Do not ask any questions; the default (normally 'yes') "
      "is assumed for "
      "all yes/no questions, and the default string is assumed in "
      "any situation where the user is prompted for string input.  "
      "The one question that is not bypassed by this option is "
      "license acceptance; the license may be accepted with the "
      "commandline option '--accept-license'." },

    { "silent", 's', NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Run silently; no questions are asked and no output is "
      "printed, except for error messages to stderr.  This option "
      "implies '--ui=none --no-questions --accept-license'." },

    { "x-prefix", X_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The prefix under which the X components of the "
      "NVIDIA driver will be installed; the default is '" DEFAULT_X_PREFIX
      "' unless nvidia-installer detects that X.Org >= 7.0 is installed, "
      "in which case the default is '" XORG7_DEFAULT_X_PREFIX "'.  Only "
      "under rare circumstances should this option be used." },

    { "xfree86-prefix", XFREE86_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "This is a deprecated synonym for --x-prefix." },

    { "x-module-path", X_MODULE_PATH_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The path under which the NVIDIA X server modules will be installed.  "
      "If this option is not specified, nvidia-installer uses the following "
      "search order and selects the first valid directory it finds: 1) "
      "`X -showDefaultModulePath`, 2) `pkg-config --variable=moduledir "
      "xorg-server`, or 3) the X library path (see the '--x-library-path' "
      "option) plus either '" DEFAULT_X_MODULEDIR "' (for X servers older "
      "than X.Org 7.0) or '" XORG7_DEFAULT_X_MODULEDIR "' (for X.Org 7.0 or "
      "later)." },

    { "x-library-path", X_LIBRARY_PATH_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The path under which the NVIDIA X libraries will be installed.  "
      "If this option is not specified, nvidia-installer uses the following "
      "search order and selects the first valid directory it finds: 1) "
      "`X -showDefaultLibPath`, 2) `pkg-config --variable=libdir "
      "xorg-server`, or 3) the X prefix (see the '--x-prefix' option) "
      "plus '" DEFAULT_LIBDIR "' on 32bit systems, and either '"
      DEFAULT_64BIT_LIBDIR "' or '" DEFAULT_LIBDIR "' on 64bit systems, "
      "depending on the installed Linux distribution." },

    { "x-sysconfig-path", X_SYSCONFIG_PATH_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The path under which X system configuration files will be installed.  "
      "If this option is not specified, nvidia-installer uses the following "
      "search order and selects the first valid directory it finds: 1) "
      "`pkg-config --variable=sysconfigdir xorg-server`, or 2) "
      DEFAULT_X_DATAROOT_PATH "/" DEFAULT_CONFDIR "." },

    { "opengl-prefix", OPENGL_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The prefix under which the OpenGL components of the "
      "NVIDIA driver will be installed; the default is: '" DEFAULT_OPENGL_PREFIX
      "'.  Only under rare circumstances should this option be used.  "
      "The Linux OpenGL ABI (http://oss.sgi.com/projects/ogl-sample/ABI/) "
      "mandates this default value." },

    { "opengl-libdir", OPENGL_LIBDIR_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The path relative to the OpenGL library installation prefix under "
      "which the NVIDIA OpenGL components will be installed.  The "
      "default is '" DEFAULT_LIBDIR "' on 32bit systems, and '"
      DEFAULT_64BIT_LIBDIR "' or '" DEFAULT_LIBDIR "' on 64bit systems, "
      "depending on the installed Linux distribution.  Only under very rare "
      "circumstances should this option be used." },

#if defined(NV_X86_64)
    { "compat32-chroot", COMPAT32_CHROOT_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL,
      "The top-level prefix (chroot) relative to which the 32bit "
      "compatibility libraries will be installed on Linux/x86-64 "
      "systems; this option is unset by default, the 32bit "
      "library installation prefix (see below) and the 32bit library "
      "path alone determine the target location.  Only under very rare "
      "circumstances should this option be used." },

    { "compat32-prefix", COMPAT32_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL,
      "The prefix under which the 32bit compatibility components "
      "of the NVIDIA driver will be installed; the default is: '"
      DEFAULT_OPENGL_PREFIX "'.  Only under rare circumstances should "
      "this option be used." },

    { "compat32-libdir", COMPAT32_LIBDIR_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL,
      "The path relative to the 32bit compatibility prefix under which the "
      "32bit compatibility components of the NVIDIA driver will "
      "be installed.  The default is '" DEFAULT_LIBDIR "' or '"
      DEFAULT_32BIT_LIBDIR "', depending on the installed Linux "
      "distribution.  Only under very rare circumstances should this "
      "option be used." },

    { "install-compat32-libs", INSTALL_COMPAT32_LIBS_OPTION,
      NVGETOPT_IS_BOOLEAN, NULL,
      "32-bit compatibility libraries may be optionally installed. Setting "
      "--install-compat32-libs will install these libraries. Setting "
      "--no-install-compat32-libs will skip installation of these libraries. "
      "Note: this option will have no effect on -no-compat32.run packages."  },
#endif /* NV_X86_64 */

    { "installer-prefix", INSTALLER_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL,
      "The prefix under which the installer binary will be "
      "installed; the default is: '" DEFAULT_UTILITY_PREFIX "'.  Note: please "
      "use the '--utility-prefix' option instead." },

    { "utility-prefix", UTILITY_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The prefix under which the NVIDIA utilities (nvidia-installer, "
      "nvidia-settings, nvidia-xconfig, nvidia-bug-report.sh) and the NVIDIA "
      "utility libraries will be installed; the default is: '"
      DEFAULT_UTILITY_PREFIX "'." },

    { "utility-libdir", UTILITY_LIBDIR_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The path relative to the utility installation prefix under which the "
      "NVIDIA utility libraries will be installed.  The default is '"
      DEFAULT_LIBDIR "' on 32bit systems, and '" DEFAULT_64BIT_LIBDIR
      "' or '" DEFAULT_LIBDIR "' on 64bit " "systems, depending on the "
      "installed Linux distribution." },

    { "documentation-prefix", DOCUMENTATION_PREFIX_OPTION,
      NVGETOPT_STRING_ARGUMENT,  NULL,
      "The prefix under which the documentation files for the NVIDIA "
      "driver will be installed.  The default is: '"
      DEFAULT_DOCUMENTATION_PREFIX "'." },

    { "application-profile-path", APPLICATION_PROFILE_PATH_OPTION,
        NVGETOPT_STRING_ARGUMENT, NULL,
        "The directory under which default application profiles for the NVIDIA "
        "driver will be installed. The default is: '"
        DEFAULT_APPLICATION_PROFILE_PATH "'." },

    { "kernel-include-path", KERNEL_INCLUDE_PATH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "The directory containing the kernel include files that "
      "should be used when compiling the NVIDIA kernel module.  "
      "This option is deprecated; please use '--kernel-source-path' "
      "instead." },

    { "kernel-source-path", KERNEL_SOURCE_PATH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "The directory containing the kernel source files that "
      "should be used when compiling the NVIDIA kernel module.  "
      "When not specified, the installer will use "
      "'/lib/modules/`uname -r`/build', if that "
      "directory exists.  Otherwise, it will use "
      "'/usr/src/linux'." },

    { "kernel-output-path", KERNEL_OUTPUT_PATH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "The directory containing any KBUILD output files if "
       "either one of the 'KBUILD_OUTPUT' or 'O' parameters were "
       "passed to KBUILD when building the kernel image/modules.  "
       "When not specified, the installer will assume that no "
       "separate output directory was used." },

    { "kernel-install-path", KERNEL_INSTALL_PATH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "The directory in which the NVIDIA kernel module should be "
      "installed.  The default value is either '/lib/modules/`uname "
      "-r`/kernel/drivers/video' (if '/lib/modules/`uname -r`/kernel' "
      "exists) or '/lib/modules/`uname -r`/video'." },

    { "proc-mount-point", PROC_MOUNT_POINT_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL,
      "The mount point for the proc file system; if not "
      "specified, then this value defaults to '" DEFAULT_PROC_MOUNT_POINT
      "' (which is normally "
      "correct).  The mount point of the proc filesystem is needed "
      "because the contents of '<proc filesystem>/version' is used when "
      "identifying if a precompiled kernel interface is available for "
      "the currently running kernel.  This option should only be needed "
      "in very rare circumstances." },

    { "log-file-name", LOG_FILE_NAME_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL,
      NULL, "File name of the installation log file (the default is: "
      "'" DEFAULT_LOG_FILE_NAME "')." },

    { "tmpdir", TMPDIR_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL,
      NULL, "Use the specified directory as a temporary directory when "
      "downloading files from the NVIDIA ftp site; "
      "if not given, then the following list will be searched, and "
      "the first one that exists will be used: $TMPDIR, /tmp, ., "
      "$HOME." },

    { "ftp-mirror", 'm', NVGETOPT_STRING_ARGUMENT, NULL,
      "Use the specified FTP mirror rather than the default ' "
      DEFAULT_FTP_SITE
      " ' when downloading driver updates." },

    { "latest", 'l', 0, NULL,
      "Connect to the NVIDIA FTP server ' " DEFAULT_FTP_SITE " ' "
      "(or use the ftp mirror "
      "specified with the '--ftp-mirror' option) and query the most "
      "recent " INSTALLER_OS "-" INSTALLER_ARCH " driver version number." },

    { "force-update", 'f', 0, NULL,
      "Forces an update to proceed, even if the installer "
      "thinks the latest driver is already installed; this option "
      "implies '--update'." },

    { "ui", USER_INTERFACE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL,
      NULL, "Specify what user interface to use, if available.  "
      "Valid values for &UI& are 'ncurses' (the default) or 'none'. "
      "If the ncurses interface fails to initialize, or 'none' "
      "is specified, then a simple printf/scanf interface will "
      "be used." },

    { "no-ncurses-color", 'c', NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL,
      NULL, "Disable use of color in the ncurses user interface." },

    { "opengl-headers", OPENGL_HEADERS_OPTION, 0, NULL,
      "Normally, installation will not install NVIDIA's OpenGL "
      "header files; the OpenGL header files packaged by the "
      "Linux distribution or available from "
      "http://www.opengl.org/registry/ should be preferred. "
      "However, http://www.opengl.org/registry/ does not yet provide "
      "a glx.h or gl.h.  Until that is resolved, NVIDIA's OpenGL "
      "header files can still be chosen, through this installer option." },

    { "no-nvidia-modprobe", NO_NVIDIA_MODPROBE_OPTION, 0, NULL,
      "Skip installation of 'nvidia-modprobe', a setuid root utility which "
      "nvidia-installer installs by default.  nvidia-modprobe can be used by "
      "user-space NVIDIA driver components to load the NVIDIA kernel module, "
      "and create the NVIDIA device files, when those components run without "
      "sufficient privileges to do so on their own, e.g., the CUDA driver run "
      "within the permissions of a non-privileged user.  This utility is only "
      "needed if other means of loading the NVIDIA kernel module and creating "
      "the NVIDIA device files are unavailable." },

#if defined(NV_TLS_TEST)
    { "force-tls", FORCE_TLS_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "NVIDIA's OpenGL libraries are compiled with one of two "
      "different thread local storage (TLS) mechanisms: 'classic tls' "
      "which is used on systems with glibc 2.2 or older, and 'new tls' "
      "which is used on systems with tls-enabled glibc 2.3 or newer.  "
      "nvidia-installer will select the OpenGL libraries appropriate "
      "for your system; however, you may use this option to force the "
      "installer to install one library type or another.  Valid values "
      "for &FORCE-TLS& are 'new' and 'classic'." },

#if defined(NV_X86_64)
    { "force-tls-compat32", FORCE_TLS_COMPAT32_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "This option forces the installer to install a specific "
      "32bit compatibility OpenGL TLS library; further details "
      "can be found in the description of the '--force-tls' option." },
#endif /* NV_X86_64 */
#endif

    { "kernel-name", 'k', NVGETOPT_STRING_ARGUMENT, NULL,
      "Build and install the NVIDIA kernel module for the "
      "non-running kernel specified by &KERNEL-NAME& (&KERNEL-NAME& "
      "should be the output of `uname -r` when the target kernel is "
      "actually running).  This option implies "
      "'--no-precompiled-interface'.  If the options "
      "'--kernel-install-path' and '--kernel-source-path' are not "
      "given, then they will be inferred from &KERNEL-NAME&; eg: "
      "'/lib/modules/&KERNEL-NAME&/kernel/drivers/video/' and "
      "'/lib/modules/&KERNEL-NAME&/build/', respectively." },

    { "no-precompiled-interface", 'n', 0, NULL,
      "Disable use of precompiled kernel interfaces." },

    { "no-abi-note", NO_ABI_NOTE_OPTION, 0, NULL,
      "The NVIDIA OpenGL libraries contain an OS ABI note tag, "
      "which identifies the minimum kernel version needed to use the "
      "library.  This option causes the installer to remove this note "
      "from the OpenGL libraries during installation." },

    { "no-rpms", NO_RPMS_OPTION, 0, NULL,
      "Normally, the installer will check for several rpms that "
      "conflict with the driver (specifically: NVIDIA_GLX and "
      "NVIDIA_kernel), and remove them if present.  This option "
      "disables this check." },

    { "no-backup", 'b', 0, NULL,
      "During driver installation, conflicting files are backed "
      "up, so that they can be restored when the driver is "
      "uninstalled.  This option causes the installer to simply delete "
      "conflicting files, rather than back them up." },

    { "no-network", 'N', 0, NULL,
      "This option instructs the installer to not attempt to access the "
      "network." },

    { "no-recursion", 'r', 0, NULL,
      "Normally, nvidia-installer will recursively search for "
      "potentially conflicting libraries under the default OpenGL "
      "and X server installation locations.  With this option set, "
      "the installer will only search in the top-level directories." },

    { "kernel-module-only", 'K',
      NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Install a kernel module only, and do not uninstall the "
      "existing driver.  This is intended to be used to install kernel "
      "modules for additional kernels (in cases where you might boot "
      "between several different kernels).  To use this option, you "
      "must already have a driver installed, and the version of the "
      "installed driver must match the version of this kernel "
      "module." },

    { "no-kernel-module", NO_KERNEL_MODULE_OPTION, 0, NULL,
      "Install everything but the kernel module, and do not remove any "
      "existing, possibly conflicting kernel modules.  This can be "
      "useful in some DEBUG environments.  If you use this option, you "
      "must be careful to ensure that a NVIDIA kernel module matching "
      "this driver version is installed seperately." },

    { "no-x-check", NO_X_CHECK_OPTION, 0, NULL,
      "Do not abort the installation if nvidia-installer detects that "
      "an X server is running.  Only under very rare circumstances should "
      "this option be used." },

    { "precompiled-kernel-interfaces-path",
      PRECOMPILED_KERNEL_INTERFACES_PATH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Before searching for a precompiled kernel interface in the "
      ".run file, search in the specified directory." },

    { "precompiled-kernel-interfaces-url",
      PRECOMPILED_KERNEL_INTERFACES_URL_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "If no precompiled kernel interfaces are found within the driver package "
      "or provided on the file system by the Linux distribution, check the "
      "specified URL for updates.  NVIDIA does not intend to provide updated "
      "precompiled kernel interfaces, but system administrators might use this "
      "for distributing precompiled kernel interfaces in a local area "
      "network." },

    { "no-nouveau-check", 'z', 0, NULL,
      "Normally, nvidia-installer aborts installation if the nouveau kernel "
      "driver is in use.  Use this option to disable this check." },

    { "disable-nouveau", 'Z', 0, NULL,
      "If the nouveau kernel module is detected by nvidia-installer, the "
      "installer offers to attempt to disable nouveau. The default action "
      "is to not attempt to disable nouveau; use this option to change the "
      "default action to attempt to disable nouveau."},

    { "run-nvidia-xconfig", 'X', 0, NULL,
      "nvidia-installer can optionally invoke the nvidia-xconfig utility.  "
      "This will update the system X configuration file so that the NVIDIA X "
      "driver is used.  The pre-existing X configuration file will be backed "
      "up.  At the end of installation, nvidia-installer will "
      "ask the user if they wish to run nvidia-xconfig; the default "
      "response is 'no'.  Use this option to make the default response "
      "'yes'.  This is useful with the '--no-questions' or '--silent' "
      "options, which assume the default values for all questions." },
    
    { "force-selinux", FORCE_SELINUX_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "Linux installations using SELinux (Security-Enhanced Linux) "
      "require that the security type of all shared libraries be set "
      "to 'shlib_t' or 'textrel_shlib_t', depending on the distribution. "
      "nvidia-installer will detect when to set the security type, "
      "and set it using chcon(1) on the shared libraries it installs.  "
      "If the execstack(8) system utility is present, nvidia-installer will "
      "use it to also clear the executable stack flag of the libraries.  "
      "Use this option to override nvidia-installer's detection of when "
      "to set the security type.  "
      "Valid values for &FORCE-SELINUX& are 'yes' (force setting of the "
      "security type), "
      "'no' (prevent setting of the security type), and 'default' "
      "(let nvidia-installer decide when to set the security type)." },

    { "selinux-chcon-type", SELINUX_CHCON_TYPE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL,
      NULL, "When SELinux support is enabled, nvidia-installer will try to "
      "determine which chcon argument to use by first trying "
      "'textrel_shlib_t', then 'texrel_shlib_t', then 'shlib_t'.  Use this "
      "option to override this detection logic." },

    { "no-sigwinch-workaround", NO_SIGWINCH_WORKAROUND_OPTION,
      NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Normally, nvidia-installer ignores the SIGWINCH signal before it "
      "forks to execute commands, e.g. to build the kernel module, and "
      "restores the SIGWINCH signal handler after the child process "
      "has terminated.  This option disables this behavior." },

    { "no-cc-version-check", NO_CC_VERSION_CHECK_OPTION, 0, NULL,
      "The NVIDIA kernel module should be compiled with the same compiler that "
      "was used to compile the currently running kernel. The layout of some "
      "Linux kernel data structures may be dependent on the version of gcc "
      "used to compile it. The Linux 2.6 kernel modules are tagged with "
      "information about the compiler and the Linux kernel's module loader "
      "performs a strict version match check. nvidia-installer checks for "
      "mismatches prior to building the NVIDIA kernel module and aborts the "
      "installation in case of failures. Use this option to override this "
      "check." },

    { "no-distro-scripts", NO_DISTRO_SCRIPTS_OPTION,
      NVGETOPT_OPTION_APPLIES_TO_NVIDIA_UNINSTALL, NULL,
      "Normally, nvidia-installer will run scripts from /usr/lib/nvidia before "
      "and after installing or uninstalling the driver.  Use this option to "
      "disable execution of these scripts." },

    { "no-opengl-files", NO_OPENGL_FILES_OPTION, 0, NULL,
      "Do not install any of the OpenGL-related driver files." },

    { "kernel-module-source-prefix", KERNEL_MODULE_SOURCE_PREFIX_OPTION, 
       NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a path where the source directory for the kernel module will "
      "be installed. Default: install source directory at /usr/src" },

    { "kernel-module-source-dir", KERNEL_MODULE_SOURCE_DIR_OPTION,
       NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify the name of the directory where the kernel module sources will "
      "be installed. Default: directory name is \"nvidia-VERSION\""},

    { "no-kernel-module-source", NO_KERNEL_MODULE_SOURCE_OPTION, 0, NULL,
      "Skip installation of the kernel module source."},

    { "dkms", DKMS_OPTION, 0, NULL,
      "nvidia-installer can optionally register the NVIDIA kernel module "
      "sources, if installed, with DKMS, then build and install a kernel "
      "module using the DKMS-registered sources.  This will allow the DKMS "
      "infrastructure to automatically build a new kernel module when "
      "changing kernels.  During installation, if DKMS is detected, "
      "nvidia-installer will ask the user if they wish to register the "
      "module with DKMS; the default response is 'no'.  This option will "
      "bypass the detection of DKMS, and cause the installer to attempt a "
      "DKMS-based installation regardless of whether DKMS is present."},

    { "module-signing-secret-key", MODULE_SIGNING_SECRET_KEY_OPTION, 
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a path to a private key to use for signing the NVIDIA kernel "
      "module. The corresponding public key must also be provided." },

    { "module-signing-public-key", MODULE_SIGNING_PUBLIC_KEY_OPTION, 
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a path to a public key to use for verifying the signature of "
      "the NVIDIA kernel module. The corresponding private key must also be "
      "provided." },

    { "module-signing-script", MODULE_SIGNING_SCRIPT_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a path to a program to use for signing the NVIDIA kernel "
      "module. The program will be called with the arguments: program-name "
      "<HASH> <PRIVATEKEY> <PUBLICKEY> <MODULE>; if the program returns an "
      "error status, it will be called again with the arguments: program-name "
      "<PRIVATEKEY> <PUBLICKEY> <MODULE>. Default: use the \"sign-file\" "
      "script in the kernel source directory." },

    { "module-signing-key-path", MODULE_SIGNING_KEY_PATH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a path where signing keys generated by nvidia-installer will "
      "be installed. Default: install keys to '" DEFAULT_MODULE_SIGNING_KEY_PATH
      "'." },

    { "module-signing-hash", MODULE_SIGNING_HASH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a cryptographic hash algorithm to use for signing kernel "
      "modules. This requires a module signing tool that allows explicit "
      "selection of the hash algorithm, and the hash algorithm name must "
      "be recognizable by the module signing tool. Default: select a hash "
      "algorithm automatically, based on the kernel's configuration." },

    { "module-signing-x509-hash", MODULE_SIGNING_X509_HASH_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL,
      "Specify a cryptographic hash algorithm to use for signing X.509 "
      "certificates generated by nvidia-installer. The hash algorithm "
      "name must be one of the message digest algorithms recognized by "
      "the x509(1) command." },

    { "no-check-for-alternate-installs", NO_CHECK_FOR_ALTERNATE_INSTALLS_OPTION,
      0, NULL,
      "Maintainers of alternate driver installation methods can report the "
      "presence and/or availability of an alternate driver installation to "
      "nvidia-installer. Setting this option skips the check for alternate "
      "driver installations." },

    { "no-unified-memory", NO_UVM_OPTION, 0, NULL,
      "Do not install the NVIDIA Unified Memory kernel module. This kernel "
      "module is required for CUDA on 64-bit systems, and if it is not "
      "installed, the CUDA driver and CUDA applications will not be able to "
      "run. The '--no-unified-memory' option should only be used to work "
      "around failures to build or install the Unified Memory kernel module on "
      "systems that do not need to run CUDA." },

    { "no-drm", NO_DRM_OPTION, 0, NULL,
      "Do not install the nvidia-drm kernel module. This kernel module "
      "provides several features, including X11 autoconfiguration, support for "
      "PRIME, and DRM-KMS. The latter is used to support modesetting on "
      "windowing systems that run independently of X11. The '--no-drm' option "
      "should only be used to work around failures to build or install the "
      "nvidia-drm kernel module on systems that do not need these features." },

    { "concurrency-level", 'j', NVGETOPT_INTEGER_ARGUMENT, NULL,
      "Set the concurrency level for operations such as building the kernel "
      "module which may be parallelized on SMP systems. By default, this will "
      "be set to the number of detected CPUs, or to '1', if nvidia-installer "
      "fails to detect the number of CPUs. Systems with a large number of "
      "CPUs will have the default concurrency level limited to 32; setting "
      "a higher level on the command line will override this limit." },

    { "force-libglx-indirect", FORCE_LIBGLX_INDIRECT, 0, NULL,
      "If the package includes a libglvnd-based OpenGL library, then always "
      "install a libGLX_indirect.so.0 symlink, overwriting one if it exists." },
    { "no-libglx-indirect", NO_LIBGLX_INDIRECT, 0, NULL,
      "Do not install a libGLX_indirect.so.0 symlink." },

    { "install-libglvnd", INSTALL_LIBGLVND_OPTION, NVGETOPT_IS_BOOLEAN, NULL,
      "If the package includes a libglvnd-based OpenGL library, then it will "
      "try to determine whether the libglvnd libraries are already available, "
      "and will install them if they're not. Use --install-libglvnd to always "
      "install the libglvnd libraries, overwriting any that already exist. "
      "Use --no-install-libglvnd to exclude the libglvnd libraries, even if "
      "they appear to be missing." },

    { "glvnd-glx-client", GLVND_GLX_CLIENT_OPTION, NVGETOPT_IS_BOOLEAN, NULL,
      "By default, the NVIDIA GLX driver will be installed with the new "
      "GLVND architecture, to support coexisting with other GLVND-compliant "
      "GLX drivers. However, some applications which do not conform to "
      "the Linux OpenGL ABI may not be fully compatible with a GLVND-based "
      "GLX driver. The --no-glvnd-glx-client option will select a "
      "non-GLVND GLX client library (libGL.so.1), which may help to avoid "
      "compatibility issues with such applications."
    },

    { "glvnd-egl-config-path", GLVND_EGL_CONFIG_FILE_PATH_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "If the package includes a libglvnd-based EGL library, then install the "
      "EGL vendor library config file to this directory. If the libglvnd "
      "libraries are already present, then by default the installer will try "
      "to determine the path by running `pkg-config --variable=datadir "
      "libglvnd`. If that fails, then it will default to "
      DEFAULT_GLVND_EGL_JSON_PATH "."
    },

    { "glvnd-egl-client", GLVND_EGL_CLIENT_OPTION, NVGETOPT_IS_BOOLEAN, NULL,
      "By default, the NVIDIA EGL driver will be installed with the new "
      "GLVND architecture, to support coexisting with other GLVND-compliant "
      "EGL drivers. However, some applications may not be fully compatible "
      "with a GLVND-based EGL driver. The --no-glvnd-egl-client option will "
      "select a non-GLVND EGL client library, which may help to avoid "
      "compatibility issues with such applications."
    },

    /* Orphaned options: These options were in the long_options table in
     * nvidia-installer.c but not in the help. */
    { "debug",                    'd', 0, NULL,NULL },
    { "help-args-only",           HELP_ARGS_ONLY_OPTION, 0, NULL, NULL },
    { "add-this-kernel",          ADD_THIS_KERNEL_OPTION, 0, NULL, NULL },
    { "rpm-file-list",            RPM_FILE_LIST_OPTION,
      NVGETOPT_STRING_ARGUMENT, NULL, NULL },
    { "no-rpms",                  NO_RPMS_OPTION, 0, NULL, NULL},
    { "advanced-options-args-only", ADVANCED_OPTIONS_ARGS_ONLY_OPTION, 0,
      NULL, NULL },

    /* Deprecated options: These options are no longer used, but
     * nvidia-installer will allow the user to set them anyway, for
     * backwards-compatibility purposes. */
    { "no-runlevel-check", NO_RUNLEVEL_CHECK_OPTION, 0, NULL, NULL },
    { "install-vdpau-wrapper", INSTALL_VDPAU_WRAPPER_OPTION, NVGETOPT_IS_BOOLEAN, NULL, NULL },

    { NULL, 0, 0, NULL, NULL },
};

#endif /* __OPT_TABLE_H__ */
