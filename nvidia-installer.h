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
 *
 *
 * nvidia-installer.h
 */

#ifndef __NVIDIA_INSTALLER_H__
#define __NVIDIA_INSTALLER_H__

#include <sys/types.h>
#include <inttypes.h>

#include "common-utils.h"

/*
 * Enumerated type, listing each of the system utilities we'll need.
 * Keep this enum in sync with the needed_utils string array in
 * misc.c:find_system_utils().
 */

typedef enum {
    MIN_SYSTEM_UTILS = 0,
    LDCONFIG = MIN_SYSTEM_UTILS,
    GREP,
    DMESG,
    TAIL,
    MAX_SYSTEM_UTILS
} SystemUtils;

typedef enum {
    MIN_SYSTEM_OPTIONAL_UTILS = MAX_SYSTEM_UTILS,
    OBJCOPY = MIN_SYSTEM_OPTIONAL_UTILS,
    CHCON,
    SELINUX_ENABLED,
    GETENFORCE,
    EXECSTACK,
    PKG_CONFIG,
    XSERVER,
    OPENSSL,
    DKMS,
    SYSTEMCTL,
    TAR,
    MAX_SYSTEM_OPTIONAL_UTILS
} SystemOptionalUtils;

/*
 * Enumerated type, listing each of the module utilities we'll need.
 * Keep this enum in sync with the needed_utils string array in
 * misc.c:find_module_utils().
 */

typedef enum {
    MIN_MODULE_UTILS = MAX_SYSTEM_OPTIONAL_UTILS,
    MODPROBE = MIN_MODULE_UTILS,
    RMMOD,
    LSMOD,
    DEPMOD,
    MAX_MODULE_UTILS
} ModuleUtils;

/*
 * Enumerated type, listing each of the develop utilities we'll need.
 * Keep this enum in sync with the develop_utils string array in
 * misc.c:check_development_tools().
 */

typedef enum {
    MIN_DEVELOP_UTILS = MAX_MODULE_UTILS,
    CC = MIN_DEVELOP_UTILS,
    MAKE,
    LD,
    /* TR and SED aren't used directly by the installer, but conftest.sh
     * depends on them, so we check for their presence */
    TR,
    SED,
    MAX_DEVELOP_UTILS
} DevelopUtils;

#define MAX_UTILS MAX_DEVELOP_UTILS

/*
 * Types of installed files.  Keep in sync with
 * manifest.c:packageEntryFileTypeTable[]
 */
typedef enum {
    FILE_TYPE_NONE,
    FILE_TYPE_KERNEL_MODULE_SRC,
    FILE_TYPE_OPENGL_LIB,
    FILE_TYPE_WINE_LIB,
    FILE_TYPE_DOCUMENTATION,
    FILE_TYPE_OPENGL_SYMLINK,
    FILE_TYPE_KERNEL_MODULE,
    FILE_TYPE_INSTALLER_BINARY,
    FILE_TYPE_UTILITY_BINARY,
    FILE_TYPE_TLS_LIB,
    FILE_TYPE_UTILITY_LIB,
    FILE_TYPE_DOT_DESKTOP,
    FILE_TYPE_UTILITY_LIB_SYMLINK,
    FILE_TYPE_XMODULE_SHARED_LIB,
    FILE_TYPE_MANPAGE,
    FILE_TYPE_EXPLICIT_PATH,
    FILE_TYPE_CUDA_LIB,
    FILE_TYPE_OPENCL_LIB,
    FILE_TYPE_OPENCL_WRAPPER_LIB,
    FILE_TYPE_CUDA_SYMLINK,
    FILE_TYPE_OPENCL_LIB_SYMLINK,
    FILE_TYPE_OPENCL_WRAPPER_SYMLINK,
    FILE_TYPE_VDPAU_LIB,
    FILE_TYPE_VDPAU_SYMLINK,
    FILE_TYPE_UTILITY_BIN_SYMLINK,
    FILE_TYPE_CUDA_ICD,
    FILE_TYPE_NVCUVID_LIB,
    FILE_TYPE_NVCUVID_LIB_SYMLINK,
    FILE_TYPE_GLX_MODULE_SHARED_LIB,
    FILE_TYPE_GLX_MODULE_SYMLINK,
    FILE_TYPE_ENCODEAPI_LIB,
    FILE_TYPE_ENCODEAPI_LIB_SYMLINK,
    FILE_TYPE_VGX_LIB,
    FILE_TYPE_VGX_LIB_SYMLINK,
    FILE_TYPE_GRID_LIB,
    FILE_TYPE_GRID_LIB_SYMLINK,
    FILE_TYPE_APPLICATION_PROFILE,
    FILE_TYPE_NVIDIA_MODPROBE,
    FILE_TYPE_NVIDIA_MODPROBE_MANPAGE,
    FILE_TYPE_MODULE_SIGNING_KEY,
    FILE_TYPE_XORG_OUTPUTCLASS_CONFIG,
    FILE_TYPE_DKMS_CONF,
    FILE_TYPE_GLVND_LIB,
    FILE_TYPE_GLVND_SYMLINK,
    FILE_TYPE_VULKAN_ICD_JSON,
    FILE_TYPE_GLX_CLIENT_LIB,
    FILE_TYPE_GLX_CLIENT_SYMLINK,
    FILE_TYPE_GLVND_EGL_ICD_JSON,
    FILE_TYPE_EGL_CLIENT_LIB,
    FILE_TYPE_EGL_CLIENT_SYMLINK,
    FILE_TYPE_EGL_EXTERNAL_PLATFORM_JSON,
    FILE_TYPE_FLEXERA_LIB,
    FILE_TYPE_FLEXERA_LIB_SYMLINK,
    FILE_TYPE_INTERNAL_UTILITY_BINARY,
    FILE_TYPE_INTERNAL_UTILITY_LIB,
    FILE_TYPE_INTERNAL_UTILITY_DATA,
    FILE_TYPE_FIRMWARE,
    FILE_TYPE_SYSTEMD_UNIT,
    FILE_TYPE_SYSTEMD_UNIT_SYMLINK,
    FILE_TYPE_SYSTEMD_SLEEP_SCRIPT,
    FILE_TYPE_GBM_BACKEND_LIB,
    FILE_TYPE_GBM_BACKEND_LIB_SYMLINK,
    FILE_TYPE_MAX
} PackageEntryFileType;


typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;



/*
 * Options structure; malloced by and initialized by
 * parse_commandline() and used by all the other functions in the
 * installer.
 */


typedef struct __options {

    int expert;
    int uninstall;
    int skip_module_unload;
    int driver_info;
    int debug;
    int logging;
    int no_precompiled_interface;
    int no_ncurses_color;
    int nvidia_modprobe;
    int no_questions;
    int silent;
    int sanity;
    int add_this_kernel;
    int no_backup;
    int kernel_modules_only;
    int no_kernel_modules;
    int no_abi_note;
    int no_rpms;
    int no_recursion;
    int run_nvidia_xconfig;
    int selinux_option;
    int selinux_enabled;
    int sigwinch_workaround;
    int no_x_check;
    int no_nvidia_xconfig_question;
    int run_distro_scripts;
    int no_nouveau_check;
    int disable_nouveau;
    int no_opengl_files;
    int no_wine_files;
    int no_kernel_module_source;
    int dkms;
    int check_for_alternate_installs;
    int install_uvm;
    int install_drm;
    int install_peermem;
    int compat32_files_packaged;
    int x_files_packaged;
    int vulkan_icd_json_packaged;
    int concurrency_level;
    int skip_module_load;
    int skip_depmod;

    NVOptionalBool install_libglx_indirect;
    NVOptionalBool install_libglvnd_libraries;
    NVOptionalBool install_compat32_libs;

    char *file_type_destination_overrides[FILE_TYPE_MAX];

    char *opengl_prefix;
    char *opengl_libdir;

    char *wine_prefix;
    char *wine_libdir;

    char *x_prefix;
    char *x_libdir;
    char *x_moddir;
    char *x_module_path;
    char *x_library_path;
    char *x_sysconfig_path;

    char *compat32_chroot;
    char *compat32_prefix;
    char *compat32_libdir;

    char *utility_prefix;
    char *utility_libdir;
    char *utility_bindir;
    char *installer_prefix;

    char *gbm_backend_dir;

    char *dot_desktopdir;

    char *documentation_prefix;
    char *documentation_docdir;
    char *documentation_mandir;

    char *application_profile_path;

    int modular_xorg;
    int xorg_supports_output_class;

    char *kernel_source_path;
    char *kernel_output_path;
    char *kernel_include_path;
    char *kernel_module_installation_path;
    char *kernel_module_src_prefix;
    char *kernel_module_src_dir;
    char *utils[MAX_UTILS];

    char *proc_mount_point;
    char *ui_str;
    char *log_file_name;

    char *tmpdir;
    char *kernel_name;
    char *rpm_file_list;
    char *precompiled_kernel_interfaces_path;
    const char *selinux_chcon_type;

    char *module_signing_secret_key;
    char *module_signing_public_key;
    char *module_signing_script;
    char *module_signing_key_path;
    char *module_signing_hash;
    char *module_signing_x509_hash;

    char *libglvnd_json_path;

    char *external_platform_json_path;

    int kernel_module_signed;
    int dkms_registered;

    void *ui_priv; /* for use by the ui's */

    NVOptionalBool use_systemd;
    char *systemd_unit_prefix;
    char *systemd_sleep_prefix;
    char *systemd_sysconf_prefix;

    char *kernel_module_build_directory_override;

} Options;

typedef enum {
    FILE_COMPAT_ARCH_NONE,
    FILE_COMPAT_ARCH_NATIVE,
    FILE_COMPAT_ARCH_COMPAT32,
} PackageEntryFileCompatArch;

typedef struct {
    unsigned int has_arch       : 1;
    unsigned int installable    : 1;
    unsigned int has_path       : 1;
    unsigned int is_symlink     : 1;
    unsigned int is_shared_lib  : 1;
    unsigned int is_opengl      : 1;
    unsigned int is_temporary   : 1;
    unsigned int is_conflicting : 1;
    unsigned int inherit_path   : 1;
} PackageEntryFileCapabilities;

/*
 * PackageEntryFileTypeList::types[] are booleans, indexed by
 * PackageEntryFileType enum value.
 */
typedef struct {
    uint8_t types[FILE_TYPE_MAX];
} PackageEntryFileTypeList;


typedef struct __package_entry {
    
    char *file;     /*
                     * filename in the package, relative to cwd of the
                     * .manifest file.
                     */
    
    char *path;     /*
                     * 
                     */

    
    char *name;     /*
                     * filename without any leading directory
                     * components; this is just a pointer into the
                     * 'file' field.
                     */

    char *target;   /*
                     * For Package entries that are symbolic links,
                     * the target indicates the target of the link;
                     * NULL for non-symbolic link Package entries.
                     */
    
    char *dst;      /*
                     * The fully-qualified filename of the destination
                     * location of the Package entry; NULL for entries
                     * that are not installed on the filesystem.  This
                     * field is assigned by the set_destinations()
                     * function.
                     */

    PackageEntryFileCapabilities caps;
    PackageEntryFileType type;
    PackageEntryFileCompatArch compat_arch;
    int inherit_path_depth;

    mode_t mode;

    ino_t inode;
    dev_t device;   /*
                     * inode of the file after extraction from the
                     * package; this is needed to compare against the
                     * files on the user's system that we consider for
                     * removal, so that symlink loops don't confuse us
                     * into deleting the files from the package.
                     */
} PackageEntry;

/*
 * Information about a conflicting file; nvidia-installer searches for existing
 * files that conflict with files that are to be installed.
 */

typedef struct {
    const char *name;
    int len;

    /*
     * if requiredString is non-NULL, then a file must have this
     * string in order to be considered a conflicting file; we use
     * this to only consider "libglx.*" files conflicts if they have
     * the string "glxModuleData".
     */

    const char *requiredString;
} ConflictingFileInfo;


/*
 * KernelModuleInfo: store information about a kernel module that is useful
 * for building the module or identifying it or its component objects.
 */
typedef struct {
    char *module_name;               /* e.g. "nvidia" */
    char *module_filename;           /* e.g. "nvidia.ko" */
    int has_separate_interface_file; /* e.g. FALSE for "nvidia-uvm" */
    char *interface_filename;        /* e.g. "nv-linux.o" */
    char *core_object_name;          /* e.g. "nv-kernel.o" */
    int is_optional;                 /* e.g. TRUE for "nvidia-uvm" */
    char *optional_module_dependee;  /* e.g. "CUDA" for "nvidia-uvm" */
    char *disable_option;            /* e.g. "--no-unified-memory" */
    int option_offset;               /* offset in Options struct for option */
} KernelModuleInfo;


typedef struct __package {

    char *description;
    char *version;
    char *kernel_module_build_directory;
    char *precompiled_kernel_interface_directory;
    char *kernel_make_logs;

    PackageEntry *entries; /* array of filename/checksum/bytesize entries */
    int num_entries;

    KernelModuleInfo *kernel_modules;
    int num_kernel_modules;
    char *excluded_kernel_modules;

} Package;


/* flags for passing into install_from_cwd() */

#define ADJUST_CWD  0x01


/* default line length for strings */

#define NV_LINE_LEN 1024
#define NV_MIN_LINE_LEN 256

#define SELINUX_DEFAULT             0x0000
#define SELINUX_FORCE_YES           0x0001
#define SELINUX_FORCE_NO            0x0002

#define PERM_MASK (S_IRWXU|S_IRWXG|S_IRWXO)

#define PRECOMPILED_PACKAGE_FILENAME "nvidia-precompiled"

/*
 * These are the default installation prefixes and the default
 * paths relative to the prefixes. Some of the defaults are
 * overridden on some distributions or when the new modular Xorg
 * is detected, all prefixes/paths can be overridden from the
 * command line.
 */
#define DEFAULT_OPENGL_PREFIX            "/usr"
#define DEFAULT_X_PREFIX                 "/usr/X11R6"
#define DEFAULT_UTILITY_PREFIX           "/usr"
#define DEFAULT_DOCUMENTATION_PREFIX     "/usr"
#define DEFAULT_GBM_PREFIX               "/usr"
#define DEFAULT_APPLICATION_PROFILE_PATH "/usr/share/nvidia"
#define DEFAULT_WINE_PREFIX              "/usr"

#define DEFAULT_LIBDIR                  "lib"
#define DEFAULT_32BIT_LIBDIR            "lib32"
#define DEFAULT_64BIT_LIBDIR            "lib64"
#define DEFAULT_IA32_TRIPLET_LIBDIR     "lib/i386-linux-gnu"
#define DEFAULT_AMD64_TRIPLET_LIBDIR    "lib/x86_64-linux-gnu"
#define DEFAULT_ARMV7_TRIPLET_LIBDIR    "lib/arm-linux-gnueabi"
#define DEFAULT_ARMV7HF_TRIPLET_LIBDIR  "lib/arm-linux-gnueabihf"
#define DEFAULT_AARCH64_TRIPLET_LIBDIR  "lib/aarch64-linux-gnu"
#define DEFAULT_PPC64LE_TRIPLET_LIBDIR  "lib/powerpc64le-linux-gnu"
#define DEFAULT_WINE_LIBDIR_SUFFIX      "nvidia/wine"
#define DEFAULT_BINDIR                  "bin"
#define DEFAULT_X_MODULEDIR             "modules"
#define DEFAULT_DOT_DESKTOPDIR          "share/applications"
#define DEFAULT_DOCDIR                  "share/doc"
#define DEFAULT_MANDIR                  "share/man"
#define DEFAULT_CONFDIR                 "X11/xorg.conf.d"

#define DEFAULT_MODULE_SIGNING_KEY_PATH "/usr/share/nvidia"
#define DEFAULT_KERNEL_MODULE_SRC_PREFIX "/usr/src"
#define DEFAULT_X_DATAROOT_PATH         "/usr/share"
#define DEFAULT_SYSTEMD_UNIT_PREFIX     "/usr/lib/systemd/system"
#define DEFAULT_SYSTEMD_SLEEP_PREFIX    "/usr/lib/systemd/system-sleep"
#define DEFAULT_SYSTEMD_SYSCONF_PREFIX  "/etc/systemd/system"

/*
 * As of Xorg 7.x, X components need not be installed relative
 * to a special top-level directory, they can be integrated
 * more tightly with the rest of the system. The system must be
 * queried for the installation paths, but in the event that
 * this fails, the fallbacks below are chosen.
 */
#define XORG7_DEFAULT_X_PREFIX          "/usr"
#define XORG7_DEFAULT_X_MODULEDIR       "xorg/modules"

#define DEFAULT_GLVND_EGL_JSON_PATH     "/usr/share/glvnd/egl_vendor.d"

#define DEFAULT_EGL_EXTERNAL_PLATFORM_JSON_PATH "/usr/share/egl/egl_external_platform.d"

/*
 * Older versions of Debian GNU/Linux for x86-64 install 32-bit
 * compatibility libraries relative to a chroot-like top-level 
 * directory; the prefix below is prepended to the full paths.
 */
#define DEBIAN_DEFAULT_COMPAT32_CHROOT  "/emul/ia32-linux"

#define DEFAULT_PROC_MOUNT_POINT "/proc"

#define DEFAULT_LOG_FILE_NAME "/var/log/nvidia-installer.log"
#define DEFAULT_UNINSTALL_LOG_FILE_NAME "/var/log/nvidia-uninstall.log"

#define NUM_TIMES_QUESTIONS_ASKED 3

#define LD_OPTIONS "-d -r"
#define NVIDIA_VERSION_PROC_FILE "/proc/driver/nvidia/version"

#define NV_BULLET_STR "-> "
#define NV_CMD_OUT_PREFIX "   "

/*
 * The OpenCL ICD Loader will look for the NVIDIA ICD
 * in /etc/OpenCL/vendors
 */
#define DEFAULT_CUDA_ICD_PREFIX          "/etc"
#define DEFAULT_CUDA_ICD_DIR             "OpenCL/vendors"

/* useful macros */

#define NV_MIN(x,y) ((x) < (y) ? (x) : (y))
#define NV_MAX(x,y) ((x) > (y) ? (x) : (y))

#define TAB "  "
#define BIGTAB "      "

/* prototypes of functions used throughout the installer */

void log_init(Options *op, int argc, char * const argv[]);
void log_printf(Options *op, const char *prefix, const char *fmt, ...) NV_ATTRIBUTE_PRINTF(3, 4);

int  install_from_cwd(Options *op);
int  add_this_kernel(Options *op);

void add_package_entry(Package *p,
                       char *file,
                       char *path,
                       char *name,
                       char *target,
                       char *dst,
                       PackageEntryFileType type,
                       PackageEntryFileCompatArch compat_arch,
                       mode_t mode);
/* XXX */

typedef TextRows *(*FormatTextRows)(const char*, const char*, int, int);


#endif /* __NVIDIA_INSTALLER_H__ */
