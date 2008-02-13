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
 * nv_installer.h
 */

#ifndef __NVIDIA_INSTALLER_H__
#define __NVIDIA_INSTALLER_H__

#include <sys/types.h>


/*
 * Enumerated type, listing each of the system utilities we'll need.
 * Keep this enum in sync with the needed_utils string array in
 * misc.c:find_system_utils().
 */

typedef enum {
    LDCONFIG = 0,
    LDD,
    LD,
    OBJCOPY,
    GREP,
    DMESG,
    TAIL,
    CUT,
    MAX_SYSTEM_UTILS
} SystemUtils;

typedef enum {
    CHCON = MAX_SYSTEM_UTILS,
    SELINUX_ENABLED,
    GETENFORCE,
    EXECSTACK,
    PKG_CONFIG,
    XSERVER,
    MAX_SYSTEM_OPTIONAL_UTILS
} SystemOptionalUtils;

/*
 * Enumerated type, listing each of the module utilities we'll need.
 * Keep this enum in sync with the needed_utils string array in
 * misc.c:find_module_utils().
 */

typedef enum {
    INSMOD = MAX_SYSTEM_OPTIONAL_UTILS,
    MODPROBE,
    RMMOD,
    LSMOD,
    DEPMOD,
    MAX_UTILS
} ModuleUtils;

/*
 * Enumerated type of distributions; this isn't an exhaustive list of
 * supported distributions... just distributions that have asked for
 * special behavior.
 */

typedef enum {
    SUSE,
    UNITED_LINUX,
    DEBIAN,
    UBUNTU,
    GENTOO,
    OTHER
} Distribution;


typedef unsigned int   uint32;
typedef unsigned short uint16;
typedef unsigned char  uint8;



/*
 * Options structure; malloced by and initialized by
 * parse_commandline() and used by all the other functions in the
 * installer.
 */


typedef struct __options {

    int accept_license;
    int update;
    int expert;
    int uninstall;
    int driver_info;
    int debug;
    int logging;
    int no_precompiled_interface;
    int no_ncurses_color;
    int latest;
    int force_update;
    int opengl_headers;
    int no_questions;
    int silent;
    int which_tls;
    int which_tls_compat32;
    int sanity;
    int add_this_kernel;
    int no_runlevel_check;
    int no_backup;
    int no_network;
    int kernel_module_only;
    int no_kernel_module;
    int no_abi_note;
    int no_rpms;
    int no_recursion;
    int run_nvidia_xconfig;
    int selinux_option;
    int selinux_enabled;
    int sigwinch_workaround;
    int no_x_check;
    int no_nvidia_xconfig_question;

    char *opengl_prefix;
    char *opengl_libdir;
    char *opengl_incdir;

    char *x_prefix;
    char *x_libdir;
    char *x_moddir;
    char *x_module_path;
    char *x_library_path;

    char *compat32_chroot;
    char *compat32_prefix;
    char *compat32_libdir;

    char *utility_prefix;
    char *utility_libdir;
    char *utility_bindir;
    char *installer_prefix;

    char *dot_desktopdir;

    char *documentation_prefix;
    char *documentation_docdir;
    char *documentation_mandir;

    int modular_xorg;

    char *kernel_source_path;
    char *kernel_output_path;
    char *kernel_include_path;
    char *kernel_module_installation_path;
    char *utils[MAX_UTILS];
    
    char *proc_mount_point;
    char *ui_str;
    char *log_file_name;

    char *ftp_site;

    char *tmpdir;
    char *update_arguments;
    char *kernel_name;
    char *rpm_file_list;
    char *precompiled_kernel_interfaces_path;
    const char *selinux_chcon_type;

    Distribution distro;

    void *ui_priv; /* for use by the ui's */

} Options;


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
    
    unsigned int flags;
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


typedef struct __package {

    char *description;
    char *version;
    char *kernel_module_filename;
    char *kernel_interface_filename;
    char *kernel_module_name;
    char **bad_modules;
    char **bad_module_filenames;
    char *kernel_module_build_directory;
    char *precompiled_kernel_interface_directory;
    
    PackageEntry *entries; /* array of filename/checksum/bytesize entries */
    int num_entries;

} Package;



typedef struct {
    char **t; /* the text rows */
    int n;    /* number of rows */
    int m;    /* maximum row length */
} TextRows;




/* define boolean values TRUE and FALSE */

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */
  
#ifndef FALSE
#define FALSE 0
#endif /* FALSE */


/* flags for passing into install_from_cwd() */

#define ADJUST_CWD  0x01


/* default line length for strings */

#define NV_LINE_LEN 1024
#define NV_MIN_LINE_LEN 256

/* file types */

#define FILE_TYPE_MASK               0x00ffffff

#define FILE_TYPE_KERNEL_MODULE_SRC  0x00000001
#define FILE_TYPE_KERNEL_MODULE_CMD  0x00000002
#define FILE_TYPE_OPENGL_HEADER      0x00000004
#define FILE_TYPE_OPENGL_LIB         0x00000008
#define FILE_TYPE_XLIB_STATIC_LIB    0x00000010
#define FILE_TYPE_XLIB_SHARED_LIB    0x00000020
#define FILE_TYPE_DOCUMENTATION      0x00000040
#define FILE_TYPE_OPENGL_SYMLINK     0x00000080
#define FILE_TYPE_XLIB_SYMLINK       0x00000100
#define FILE_TYPE_KERNEL_MODULE      0x00000200
#define FILE_TYPE_INSTALLER_BINARY   0x00000400
#define FILE_TYPE_UTILITY_BINARY     0x00000800
#define FILE_TYPE_LIBGL_LA           0x00001000
#define FILE_TYPE_TLS_LIB            0x00002000
#define FILE_TYPE_TLS_SYMLINK        0x00004000
#define FILE_TYPE_UTILITY_LIB        0x00008000
#define FILE_TYPE_DOT_DESKTOP        0x00010000
#define FILE_TYPE_UTILITY_SYMLINK    0x00020000
#define FILE_TYPE_XMODULE_SHARED_LIB 0x00040000
#define FILE_TYPE_XMODULE_SYMLINK    0x00080000
#define FILE_TYPE_MANPAGE            0x00100000

/* file class: this is used to distinguish OpenGL libraries */

#define FILE_CLASS_MASK              0xff000000

#define FILE_CLASS_NEW_TLS           0x01000000
#define FILE_CLASS_CLASSIC_TLS       0x02000000
#define FILE_CLASS_NATIVE            0x04000000
#define FILE_CLASS_COMPAT32          0x08000000

#define FILE_TYPE_XLIB_LIB         (FILE_TYPE_XLIB_STATIC_LIB | \
                                    FILE_TYPE_XLIB_SHARED_LIB)

#define FILE_TYPE_XMODULE_LIB      (FILE_TYPE_XMODULE_SHARED_LIB)

#define FILE_TYPE_INSTALLABLE_FILE (FILE_TYPE_OPENGL_LIB         | \
                                    FILE_TYPE_XLIB_LIB           | \
                                    FILE_TYPE_TLS_LIB            | \
                                    FILE_TYPE_UTILITY_LIB        | \
                                    FILE_TYPE_DOCUMENTATION      | \
                                    FILE_TYPE_MANPAGE            | \
                                    FILE_TYPE_OPENGL_HEADER      | \
                                    FILE_TYPE_KERNEL_MODULE      | \
                                    FILE_TYPE_INSTALLER_BINARY   | \
                                    FILE_TYPE_UTILITY_BINARY     | \
                                    FILE_TYPE_LIBGL_LA           | \
                                    FILE_TYPE_XMODULE_LIB        | \
                                    FILE_TYPE_DOT_DESKTOP)

#define FILE_TYPE_HAVE_PATH        (FILE_TYPE_XMODULE_LIB        | \
                                    FILE_TYPE_XMODULE_SYMLINK    | \
                                    FILE_TYPE_MANPAGE            | \
                                    FILE_TYPE_OPENGL_HEADER      | \
                                    FILE_TYPE_TLS_LIB            | \
                                    FILE_TYPE_TLS_SYMLINK        | \
                                    FILE_TYPE_DOT_DESKTOP        | \
                                    FILE_TYPE_DOCUMENTATION)

#define FILE_TYPE_HAVE_ARCH        (FILE_TYPE_OPENGL_LIB         | \
                                    FILE_TYPE_OPENGL_SYMLINK     | \
                                    FILE_TYPE_LIBGL_LA           | \
                                    FILE_TYPE_TLS_LIB            | \
                                    FILE_TYPE_TLS_SYMLINK)

#define FILE_TYPE_HAVE_CLASS       (FILE_TYPE_TLS_LIB            | \
                                    FILE_TYPE_TLS_SYMLINK)

#define FILE_TYPE_SYMLINK          (FILE_TYPE_OPENGL_SYMLINK     | \
                                    FILE_TYPE_XLIB_SYMLINK       | \
                                    FILE_TYPE_TLS_SYMLINK        | \
                                    FILE_TYPE_XMODULE_SYMLINK    | \
                                    FILE_TYPE_UTILITY_SYMLINK)

#define FILE_TYPE_RTLD_CHECKED     (FILE_TYPE_OPENGL_LIB         | \
                                    FILE_TYPE_TLS_LIB)

#define FILE_TYPE_SHARED_LIB       (FILE_TYPE_OPENGL_LIB         | \
                                    FILE_TYPE_XLIB_SHARED_LIB    | \
                                    FILE_TYPE_TLS_LIB            | \
                                    FILE_TYPE_XMODULE_SHARED_LIB | \
                                    FILE_TYPE_UTILITY_LIB)

#define TLS_LIB_TYPE_FORCED         0x0001
#define TLS_LIB_NEW_TLS             0x0002
#define TLS_LIB_CLASSIC_TLS         0x0004

#define SELINUX_DEFAULT             0x0000
#define SELINUX_FORCE_YES           0x0001
#define SELINUX_FORCE_NO            0x0002

#define FORCE_CLASSIC_TLS          (TLS_LIB_CLASSIC_TLS | TLS_LIB_TYPE_FORCED)
#define FORCE_NEW_TLS              (TLS_LIB_NEW_TLS | TLS_LIB_TYPE_FORCED)

#define PERM_MASK (S_IRWXU|S_IRWXG|S_IRWXO)

#define PRECOMPILED_KERNEL_INTERFACE_FILENAME "precompiled-nv-linux.o"


/*
 * These are the default installation prefixes and the default
 * paths relative to the prefixes. Some of the defaults are
 * overriden on some distributions or when the new modular Xorg
 * is detected, all prefixes/paths can be overriden from the
 * command line.
 */
#define DEFAULT_OPENGL_PREFIX           "/usr"
#define DEFAULT_X_PREFIX                "/usr/X11R6"
#define DEFAULT_UTILITY_PREFIX          "/usr"
#define DEFAULT_DOCUMENTATION_PREFIX    "/usr"

#define DEFAULT_LIBDIR                  "lib"
#define DEFAULT_64BIT_LIBDIR            "lib64"
#define DEFAULT_BINDIR                  "bin"
#define DEFAULT_INCDIR                  "include"
#define DEFAULT_X_MODULEDIR             "modules"
#define DEFAULT_DOT_DESKTOPDIR          "share/applications"
#define DEFAULT_DOCDIR                  "share/doc"
#define DEFAULT_MANDIR                  "share/man"

/*
 * As of Xorg 7.x, X components need not be installed relative
 * to a special top-level directory, they can be integrated
 * more tightly with the rest of the system. The system must be
 * queried for the installation paths, but in the event that
 * this fails, the fallbacks below are chosen.
 */
#define XORG7_DEFAULT_X_PREFIX          "/usr"
#define XORG7_DEFAULT_X_MODULEDIR       "xorg/modules"

/*
 * Debian GNU/Linux for x86-64 installs 32-bit compatibility
 * libraries relative to a chroot-like top-level directory; the
 * prefix below is prepended to the full paths.
 */
#define DEBIAN_DEFAULT_COMPAT32_CHROOT  "/emul/ia32-linux"

/*
 * Debian GNU/Linux and Ubuntu do not follow the lib64 library
 * path naming convention used by other distributors. 64-bit
 * libraries are placed under ../lib.
 */
#define DEBIAN_DEFAULT_64BIT_LIBDIR     "lib"

/*
 * Ubuntu GNU/Linux and Gentoo Linux do not follow the "lib"
 * library path naming convention used for 32-bit compatibility
 * libraries by other distributors. These libraries are
 * placed under ../lib32.
 */
#define UBUNTU_DEFAULT_COMPAT32_LIBDIR  "lib32"


#define DEFAULT_PROC_MOUNT_POINT "/proc"

#define DEFAULT_FTP_SITE "ftp://download.nvidia.com"

#define LICENSE_FILE "LICENSE"

#define DEFAULT_LOG_FILE_NAME "/var/log/nvidia-installer.log"

#define NUM_TIMES_QUESTIONS_ASKED 3

#define LD_OPTIONS "-d -r"
#define NVIDIA_VERSION_PROC_FILE "/proc/driver/nvidia/version"

#define NV_BULLET_STR "-> "
#define NV_CMD_OUT_PREFIX "   "

/* useful macros */

#define NV_MIN(x,y) ((x) < (y) ? (x) : (y))
#define NV_MAX(x,y) ((x) > (y) ? (x) : (y))


/* prototypes of functions used throughout the installer */

void log_init(Options *op);
void log_printf(Options *op, const int wb,
                const char *prefix, const char *fmt, ...);

int  install_from_cwd(Options *op);
int  add_this_kernel(Options *op);

void add_package_entry(Package *p,
                       char *file,
                       char *path,
                       char *name,
                       char *target,
                       char *dst,
                       unsigned int flags,
                       mode_t mode);
/* XXX */

typedef TextRows *(*FormatTextRows)(const char*, const char*, int, int);


#endif /* __NVIDIA_INSTALLER_H__ */
