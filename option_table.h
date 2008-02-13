#define NVOPT_HAS_ARGUMENT 0x1
#define NVOPT_IS_BOOLEAN   0x2

#define OPTION_HELP_ALWAYS 0x8000

typedef struct {
    const char *name;
    int val;
    unsigned int flags;
    char *description; /* not used by nvgetopt() */
} NVOption;

enum {
    XFREE86_PREFIX_OPTION = 1,
    OPENGL_PREFIX_OPTION,
    OPENGL_LIBDIR_OPTION,
    KERNEL_INCLUDE_PATH_OPTION,
    KERNEL_INSTALL_PATH_OPTION,
    UNINSTALL_OPTION,
    PROC_MOUNT_POINT_OPTION,
    USER_INTERFACE_OPTION,
    LOG_FILE_NAME_OPTION,
    HELP_ARGS_ONLY_OPTION,
    TMPDIR_OPTION,
    NO_OPENGL_HEADERS_OPTION,
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
    NO_ABI_NOTE_OPTION,
    KERNEL_SOURCE_PATH_OPTION,
    NO_RPMS_OPTION,
    X_PREFIX_OPTION,
    KERNEL_OUTPUT_PATH_OPTION,
    NO_RECURSION_OPTION,
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
    X_LIBRARY_PATH_OPTION,
    NO_KERNEL_MODULE_OPTION,
    NO_X_CHECK_OPTION
};

static const NVOption __options[] = {
    /* These options are printed by "nvidia-installer --help" */

    { "accept-license", 'a', OPTION_HELP_ALWAYS,
      "Bypass the display and prompting for acceptance of the "
      "NVIDIA Software License Agreement.  By passing this option to "
      "nvidia-installer, you indicate that you have read and accept the "
      "License Agreement contained in the file 'LICENSE' (in the top "
      "level directory of the driver package)." },

    { "update", UPDATE_OPTION, OPTION_HELP_ALWAYS,
      "Connect to the NVIDIA FTP server ' " DEFAULT_FTP_SITE " ' and determine the "
      "latest available driver version.  If there is a more recent "
      "driver available, automatically download and install it.  Any "
      "other options given on the commandline will be passed on to the "
      "downloaded driver package when installing it." },

    { "version", 'v', OPTION_HELP_ALWAYS,
      "Print the nvidia-installer version and exit." },

    { "help", 'h', OPTION_HELP_ALWAYS,
      "Print usage information for the common commandline options "
      "and exit." },

    { "advanced-options", 'A', OPTION_HELP_ALWAYS,
      "Print usage information for the common commandline options "
      "as well as the advanced options, and then exit." },

    /* These options are only printed by "nvidia-installer --advanced-help" */

    { "driver-info", 'i', 0,
      "Print information about the currently installed NVIDIA "
      "driver version." },

    { "uninstall", UNINSTALL_OPTION, 0,
      "Uninstall the currently installed NVIDIA driver." },

    { "sanity", SANITY_OPTION, 0,
      "Perform basic sanity tests on an existing NVIDIA "
      "driver installation." },

    { "expert", 'e', 0,
      "Enable 'expert' installation mode; more detailed questions "
      "will be asked, and more verbose output will be printed; "
      "intended for expert users.  The questions may be suppressed "
      "with the '--no-questions' commandline option." },

    { "no-questions", 'q', 0,
      "Do not ask any questions; the default (normally 'yes') "
      "is assumed for "
      "all yes/no questions, and the default string is assumed in "
      "any situation where the user is prompted for string input.  "
      "The one question that is not bypassed by this option is "
      "license acceptance; the license may be accepted with the "
      "commandline option '--accept-license'." },

    { "silent", 's', 0,
      "Run silently; no questions are asked and no output is "
      "printed, except for error messages to stderr.  This option "
      "implies '--ui=none --no-questions --accept-license'." },

    { "x-prefix", X_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "The prefix under which the X components of the "
      "NVIDIA driver will be installed; the default is '" DEFAULT_X_PREFIX
      "' unless nvidia-installer detects that X.Org >= 7.0 is installed, "
      "in which case the default is '" XORG7_DEFAULT_X_PREFIX "'.  Only "
      "under rare circumstances should this option be used." },

    { "xfree86-prefix", XFREE86_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "This is a deprecated synonym for --x-prefix." },

    { "x-module-path", X_MODULE_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "The path under which the NVIDIA X server modules will be installed.  "
      "If this option is not specified, nvidia-installer uses the following "
      "search order and selects the first valid directory it finds: 1) "
      "`X -showDefaultModulePath`, 2) `pkg-config --variable=moduledir "
      "xorg-server`, or 3) the X library path (see the '--x-library-path' "
      "option) plus either '" DEFAULT_X_MODULEDIR "' (for X servers older "
      "than X.Org 7.0) or '" XORG7_DEFAULT_X_MODULEDIR "' (for X.Org 7.0 or "
      "later)." },

    { "x-library-path", X_LIBRARY_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "The path under which the NVIDIA X libraries will be installed.  "
      "If this option is not specified, nvidia-installer uses the following "
      "search order and selects the first valid directory it finds: 1) "
      "`X -showDefaultLibPath`, 2) `pkg-config --variable=libdir "
      "xorg-server`, or 3) the X prefix (see the '--x-prefix' option) "
      "plus '" DEFAULT_LIBDIR "' on 32bit systems, and either '"
      DEFAULT_64BIT_LIBDIR "' or '" DEFAULT_LIBDIR "' on 64bit systems, "
      "depending on the installed Linux distribution." },

    { "opengl-prefix", OPENGL_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "The prefix under which the OpenGL components of the "
      "NVIDIA driver will be installed; the default is: '" DEFAULT_OPENGL_PREFIX
      "'.  Only under rare circumstances should this option be used.  "
      "The Linux OpenGL ABI (http://oss.sgi.com/projects/ogl-sample/ABI/) "
      "mandates this default value." },

    { "opengl-libdir", OPENGL_LIBDIR_OPTION, NVOPT_HAS_ARGUMENT,
      "The path relative to the OpenGL library installation prefix under "
      "which the NVIDIA OpenGL components will be installed.  The "
      "default is '" DEFAULT_LIBDIR "' on 32bit systems, and '"
      DEFAULT_64BIT_LIBDIR "' or '" DEFAULT_LIBDIR "' on 64bit systems, "
      "depending on the installed Linux distribution.  Only under very rare "
      "circumstances should this option be used." },

#if defined(NV_X86_64)
    { "compat32-chroot", COMPAT32_CHROOT_OPTION, NVOPT_HAS_ARGUMENT,
      "The top-level prefix (chroot) relative to which the 32bit "
      "compatibility OpenGL libraries will be installed on Linux/x86-64 "
      "systems; this option is unset by default, the 32bit OpenGL "
      "library installation prefix (see below) and the 32bit library "
      "path alone determine the target location.  Only under very rare "
      "circumstances should this option be used." },

    { "compat32-prefix", COMPAT32_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "The prefix under which the 32bit compatibility OpenGL components "
      "of the NVIDIA driver will be installed; the default is: '"
      DEFAULT_OPENGL_PREFIX "'.  Only under rare circumstances should "
      "this option be used." },

    { "compat32-libdir", COMPAT32_LIBDIR_OPTION, NVOPT_HAS_ARGUMENT,
      "The path relative to the 32bit compatibility prefix under which the "
      "32bit compatibility OpenGL components of the NVIDIA driver will "
      "be installed.  The default is '" DEFAULT_LIBDIR "' or '"
      UBUNTU_DEFAULT_COMPAT32_LIBDIR "', depending on the installed Linux "
      "distribution.  Only under very rare circumstances should this "
      "option be used." },
#endif /* NV_X86_64 */

    { "installer-prefix", INSTALLER_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "The prefix under which the installer binary will be "
      "installed; the default is: '" DEFAULT_UTILITY_PREFIX "'.  Note: please "
      "use the '--utility-prefix' option instead." },

    { "utility-prefix", UTILITY_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "The prefix under which the NVIDIA utilities (nvidia-installer, "
      "nvidia-settings, nvidia-xconfig, nvidia-bug-report.sh) and the NVIDIA "
      "utility libraries will be installed; the default is: '"
      DEFAULT_UTILITY_PREFIX "'." },

    { "utility-libdir", UTILITY_LIBDIR_OPTION, NVOPT_HAS_ARGUMENT,
      "The path relative to the utility installation prefix under which the "
      "NVIDIA utility libraries will be installed.  The default is '"
      DEFAULT_LIBDIR "' on 32bit systems, and '" DEFAULT_64BIT_LIBDIR
      "' or '" DEFAULT_LIBDIR "' on 64bit " "systems, depending on the "
      "installed Linux distribution." },

    { "documentation-prefix", DOCUMENTATION_PREFIX_OPTION, NVOPT_HAS_ARGUMENT,
      "The prefix under which the documentation files for the NVIDIA "
      "driver will be installed.  The default is: '"
      DEFAULT_DOCUMENTATION_PREFIX "'." },

    { "kernel-include-path", KERNEL_INCLUDE_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "The directory containing the kernel include files that "
      "should be used when compiling the NVIDIA kernel module.  "
      "This option is deprecated; please use '--kernel-source-path' "
      "instead." },

    { "kernel-source-path", KERNEL_SOURCE_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "The directory containing the kernel source files that "
      "should be used when compiling the NVIDIA kernel module.  "
      "When not specified, the installer will use "
      "'/lib/modules/`uname -r`/build', if that "
      "directory exists.  Otherwise, it will use "
      "'/usr/src/linux'." },

    { "kernel-output-path", KERNEL_OUTPUT_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "The directory containing any KBUILD output files if "
       "either one of the 'KBUILD_OUTPUT' or 'O' parameters were "
       "passed to KBUILD when building the kernel image/modules.  "
       "When not specified, the installer will assume that no "
       "separate output directory was used." },

    { "kernel-install-path", KERNEL_INSTALL_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "The directory in which the NVIDIA kernel module should be "
      "installed.  The default value is either '/lib/modules/`uname "
      "-r`/kernel/drivers/video' (if '/lib/modules/`uname -r`/kernel' "
      "exists) or '/lib/modules/`uname -r`/video'." },

    { "proc-mount-point", PROC_MOUNT_POINT_OPTION, NVOPT_HAS_ARGUMENT,
      "The mount point for the proc file system; if not "
      "specified, then this value defaults to '" DEFAULT_PROC_MOUNT_POINT
      "' (which is normally "
      "correct).  The mount point of the proc filesystem is needed "
      "because the contents of '<proc filesystem>/version' is used when "
      "identifying if a precompiled kernel interface is available for "
      "the currently running kernel.  This option should only be needed "
      "in very rare circumstances." },

    { "log-file-name", LOG_FILE_NAME_OPTION, NVOPT_HAS_ARGUMENT,
      "File name of the installation log file (the default is: "
      "'" DEFAULT_LOG_FILE_NAME "')." },

    { "tmpdir", TMPDIR_OPTION, NVOPT_HAS_ARGUMENT,
      "Use the specified directory as a temporary directory when "
      "downloading files from the NVIDIA ftp site; "
      "if not given, then the following list will be searched, and "
      "the first one that exists will be used: $TMPDIR, /tmp, ., "
      "$HOME." },

    { "ftp-mirror", 'm', NVOPT_HAS_ARGUMENT,
      "Use the specified FTP mirror rather than the default ' "
      DEFAULT_FTP_SITE
      " ' when downloading driver updates." },

    { "latest", 'l', 0,
      "Connect to the NVIDIA FTP server ' " DEFAULT_FTP_SITE " ' "
      "(or use the ftp mirror "
      "specified with the '--ftp-mirror' option) and query the most "
      "recent " INSTALLER_OS "-" INSTALLER_ARCH " driver version number." },

    { "force-update", 'f', 0,
      "Forces an update to proceed, even if the installer "
      "thinks the latest driver is already installed; this option "
      "implies '--update'." },

    { "ui", USER_INTERFACE_OPTION, NVOPT_HAS_ARGUMENT,
      "Specify what user interface to use, if available.  "
      "Valid values for [UI] are 'ncurses' (the default) or 'none'. "
      "If the ncurses interface fails to initialize, or 'none' "
      "is specified, then a simple printf/scanf interface will "
      "be used." },

    { "no-ncurses-color", 'c', 0,
      "Disable use of color in the ncurses user interface." },

    { "no-opengl-headers", NO_OPENGL_HEADERS_OPTION, 0,
      "Normally, installation will install NVIDIA's OpenGL "
      "header files.  This option disables installation of the NVIDIA "
      "OpenGL header files." },

    { "force-tls", FORCE_TLS_OPTION, NVOPT_HAS_ARGUMENT,
      "NVIDIA's OpenGL libraries are compiled with one of two "
      "different thread local storage (TLS) mechanisms: 'classic tls' "
      "which is used on systems with glibc 2.2 or older, and 'new tls' "
      "which is used on systems with tls-enabled glibc 2.3 or newer.  "
      "nvidia-installer will select the OpenGL libraries appropriate "
      "for your system; however, you may use this option to force the "
      "installer to install one library type or another.  Valid values "
      "for [FORCE-TLS] are 'new' and 'classic'." },

#if defined(NV_X86_64)
    { "force-tls-compat32", FORCE_TLS_COMPAT32_OPTION, NVOPT_HAS_ARGUMENT,
      "This option forces the installer to install a specific "
      "32bit compatibility OpenGL TLS library; further details "
      "can be found in the description of the '--force-tls' option." },
#endif /* NV_X86_64 */

    { "kernel-name", 'k', NVOPT_HAS_ARGUMENT,
      "Build and install the NVIDIA kernel module for the "
      "non-running kernel specified by [KERNEL-NAME] ([KERNEL-NAME] "
      "should be the output of `uname -r` when the target kernel is "
      "actually running).  This option implies "
      "'--no-precompiled-interface'.  If the options "
      "'--kernel-install-path' and '--kernel-source-path' are not "
      "given, then they will be inferred from [KERNEL-NAME]; eg: "
      "'/lib/modules/[KERNEL-NAME]/kernel/drivers/video/' and "
      "'/lib/modules/[KERNEL-NAME]/build/', respectively." },

    { "no-precompiled-interface", 'n', 0,
      "Disable use of precompiled kernel interfaces." },

    { "no-runlevel-check", NO_RUNLEVEL_CHECK_OPTION, 0,
      "Normally, the installer checks the current runlevel and "
      "warns users if they are in runlevel 1: in runlevel 1, some "
      "services that are normally active are disabled (such as devfs), "
      "making it difficult for the installer to properly setup the "
      "kernel module configuration files.  This option disables the "
      "runlevel check." },

    { "no-abi-note", NO_ABI_NOTE_OPTION, 0,
      "The NVIDIA OpenGL libraries contain an OS ABI note tag, "
      "which identifies the minimum kernel version needed to use the "
      "library.  This option causes the installer to remove this note "
      "from the OpenGL libraries during installation." },

    { "no-rpms", NO_RPMS_OPTION, 0,
      "Normally, the installer will check for several rpms that "
      "conflict with the driver (specifically: NVIDIA_GLX and "
      "NVIDIA_kernel), and remove them if present.  This option "
      "disables this check." },

    { "no-backup", 'b', 0,
      "During driver installation, conflicting files are backed "
      "up, so that they can be restored when the driver is "
      "uninstalled.  This option causes the installer to simply delete "
      "conflicting files, rather than back them up." },

    { "no-network", 'N', 0,
      "This option instructs the installer to not attempt to "
      "connect to the NVIDIA ftp site (for updated precompiled kernel "
      "interfaces, for example)." },

    { "no-recursion", NO_RECURSION_OPTION, 0,
      "Normally, nvidia-installer will recursively search for "
      "potentially conflicting libraries under the default OpenGL "
      "and X server installation locations.  With this option set, "
      "the installer will only search in the top-level directories." },

    { "kernel-module-only", 'K', 0,
      "Install a kernel module only, and do not uninstall the "
      "existing driver.  This is intended to be used to install kernel "
      "modules for additional kernels (in cases where you might boot "
      "between several different kernels).  To use this option, you "
      "must already have a driver installed, and the version of the "
      "installed driver must match the version of this kernel "
      "module." },

    { "no-kernel-module", NO_KERNEL_MODULE_OPTION, 0,
      "Install everything but the kernel module, and do not remove any "
      "existing, possibly conflicting kernel modules.  This can be "
      "useful in some DEBUG environments.  If you use this option, you "
      "must be careful to ensure that a NVIDIA kernel module matching "
      "this driver version is installed seperately." },

    { "no-x-check", NO_X_CHECK_OPTION, 0,
      "Do not abort the installation if nvidia-installer detects that "
      "an X server is running.  Only under very rare circumstances should "
      "this option be used." },

    { "precompiled-kernel-interfaces-path",
      PRECOMPILED_KERNEL_INTERFACES_PATH_OPTION, NVOPT_HAS_ARGUMENT,
      "Before searching for a precompiled kernel interface in the "
      ".run file, search in the specified directory." },

    { "run-nvidia-xconfig", 'X', 0,
      "nvidia-installer can optionally invoke the nvidia-xconfig utility.  "
      "This will update the system X configuration file so that the NVIDIA X "
      "driver is used.  The pre-existing X configuration file will be backed "
      "up.  At the end of installation, nvidia-installer will "
      "ask the user if they wish to run nvidia-xconfig; the default "
      "response is 'no'.  Use this option to make the default response "
      "'yes'.  This is useful with the '--no-questions' or '--silent' "
      "options, which assume the default values for all questions." },
    
    { "force-selinux", FORCE_SELINUX_OPTION, NVOPT_HAS_ARGUMENT,
      "Linux installations using SELinux (Security-Enhanced Linux) "
      "require that the security type of all shared libraries be set "
      "to 'shlib_t' or 'textrel_shlib_t', depending on the distribution. "
      "nvidia-installer will detect when to set "
      "the security type, and set it using chcon(1) on the shared "
      "libraries it installs.  Use this option to override "
      "nvidia-installer's detection of when to set the security type.  "
      "Valid values for [FORCE-SELINUX] are 'yes' (force setting of the "
      "security type), "
      "'no' (prevent setting of the security type), and 'default' "
      "(let nvidia-installer decide when to set the security type)." },

    { "selinux-chcon-type", SELINUX_CHCON_TYPE_OPTION, NVOPT_HAS_ARGUMENT,
      "When SELinux support is enabled, nvidia-installer will try to determine "
      "which chcon argument to use by first trying 'textrel_shlib_t', then "
      "'texrel_shlib_t', then 'shlib_t'.  Use this option to override this "
      "detection logic." },

    { "no-sigwinch-workaround", NO_SIGWINCH_WORKAROUND_OPTION, 0,
      "Normally, nvidia-installer ignores the SIGWINCH signal before it "
      "forks to execute commands, e.g. to build the kernel module, and "
      "restores the SIGWINCH signal handler after the child process "
      "has terminated.  This option disables this behavior." },

    /* Orphaned options: These options were in the long_options table in
     * nvidia-installer.c but not in the help. */
    { "debug",                    'd', 0, NULL },
    { "help-args-only",           HELP_ARGS_ONLY_OPTION, 0, NULL },
    { "add-this-kernel",          ADD_THIS_KERNEL_OPTION, 0, NULL },
    { "rpm-file-list",            RPM_FILE_LIST_OPTION, NVOPT_HAS_ARGUMENT, NULL },
    { "no-rpms",                  NO_RPMS_OPTION, 0, NULL},
    { "advanced-options-args-only", ADVANCED_OPTIONS_ARGS_ONLY_OPTION, 0, NULL },

    { NULL, 0, 0, NULL },
};
