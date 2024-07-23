/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003 NVIDIA Corporation
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
 * files.c - this source file contains routines for manipulating
 * files and directories for the nv-instaler.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <limits.h>
#include <libgen.h>
#include <utime.h>
#include <time.h>
#include <sys/wait.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "files.h"
#include "misc.h"
#include "precompiled.h"
#include "backup.h"
#include "kernel.h"


static void  get_x_library_and_module_paths(Options *op);


/*
 * remove_directory() - recursively delete a directory (`rm -rf`)
 */

int remove_directory(Options *op, const char *victim)
{
    struct stat stat_buf;
    DIR *dir;
    struct dirent *ent;
    int success = TRUE;
    
    if (lstat(victim, &stat_buf) == -1) {
        ui_error(op, "failure to open '%s'", victim);
        return FALSE;
    }
    
    if (S_ISDIR(stat_buf.st_mode) == 0) {
        ui_error(op, "%s is not a directory", victim);
        return FALSE;
    }
    
    if ((dir = opendir(victim)) == NULL) {
        ui_error(op, "Failure reading directory %s", victim);
        return FALSE;
    }
    
    while (success && (ent = readdir(dir)) != NULL) {
        char *filename;
        int len;

        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;
        
        len = strlen(victim) + strlen(ent->d_name) + 2;
        filename = (char *) nvalloc(len);
        snprintf(filename, len, "%s/%s", victim, ent->d_name);
        
        if (lstat(filename, &stat_buf) == -1) {
            ui_error(op, "failure to open '%s'", filename);
            success = FALSE;
        } else {
            if (S_ISDIR(stat_buf.st_mode)) {
                success = remove_directory(op, filename);
            } else {
                if (unlink(filename) != 0) {
                    ui_error(op, "Failure removing file %s (%s)",
                             filename, strerror(errno));
                    success = FALSE;
                }
            }
        }

        free(filename);
    }

    closedir(dir);

    if (rmdir(victim) != 0) {
        ui_error(op, "Failure removing directory %s (%s)",
                 victim, strerror(errno));
        return FALSE;
    }

    return success;
}



/*
 * touch_directory() - recursively touch all files (and directories)
 * in the specified directory, bringing their access and modification
 * times up to date.
 */

int touch_directory(Options *op, const char *victim)
{
    struct stat stat_buf;
    DIR *dir;
    struct dirent *ent;
    struct utimbuf time_buf;
    int success = FALSE;

    if (lstat(victim, &stat_buf) == -1) {
        ui_error(op, "failure to open '%s'", victim);
        return FALSE;
    }
    
    if (S_ISDIR(stat_buf.st_mode) == 0) {
        ui_error(op, "%s is not a directory", victim);
        return FALSE;
    }
    
    if ((dir = opendir(victim)) == NULL) {
        ui_error(op, "Failure reading directory %s", victim);
        return FALSE;
    }

    /* get the current time */

    time_buf.actime = time(NULL);
    time_buf.modtime = time_buf.actime;

    /* loop over each entry in the directory */

    while ((ent = readdir(dir)) != NULL) {
        char *filename;
        int entry_failed = FALSE;
        
        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;
        
        filename = nvstrcat(victim, "/", ent->d_name, NULL);
        
        /* stat the file to get the type */
        
        if (lstat(filename, &stat_buf) == -1) {
            ui_error(op, "failure to open '%s'", filename);
            entry_failed = TRUE;
            goto entry_done;
        }
        
        /* if it is a directory, call this recursively */

        if (S_ISDIR(stat_buf.st_mode)) {
            if (!touch_directory(op, filename)) {
                entry_failed = TRUE;
                goto entry_done;
            }
        }

        /* finally, set the access and modification times */
        
        if (utime(filename, &time_buf) != 0) {
            ui_error(op, "Error setting modification time for %s", filename);
            entry_failed = TRUE;
            goto entry_done;
        }

 entry_done:
        nvfree(filename);
        if (entry_failed) {
            goto done;
        }
    }

    success = TRUE;

 done:

    if (closedir(dir) != 0) {
        ui_error(op, "Error while closing directory %s.", victim);
        success = FALSE;
    }

    return success;
}


/*
 * copy_file() - copy the file specified by srcfile to dstfile, using
 * mmap and memcpy.  The destination file is created with the
 * permissions specified by mode.  Roughly based on code presented by
 * Richard Stevens, in Advanced Programming in the Unix Environment,
 * 12.9.
 */

int copy_file(Options *op, const char *srcfile,
              const char *dstfile, mode_t mode)
{
    int src_fd = -1, dst_fd = -1;
    int success = FALSE;
    struct stat stat_buf;
    char *src, *dst;
    
    if ((src_fd = open(srcfile, O_RDONLY)) == -1) {
        ui_error (op, "Unable to open '%s' for copying (%s)",
                  srcfile, strerror (errno));
        goto done;
    }
    if (stat(dstfile, &stat_buf) == 0) {
        /* Unlink any existing destination file first, to ensure that the
          destination file will be newly created, rather than overwriting
          the contents of a file which may be in use by another program. */
        if (unlink(dstfile) == -1 && errno != ENOENT) {
            ui_error (op, "Unable to delete existing file '%s' (%s)",
                      dstfile, strerror (errno));
            goto done;
        }
    }
    if ((dst_fd = open(dstfile, O_RDWR | O_CREAT, mode)) == -1) {
        ui_error (op, "Unable to create '%s' for copying (%s)",
                  dstfile, strerror (errno));
        goto done;
    }
    if (fstat(src_fd, &stat_buf) == -1) {
        ui_error (op, "Unable to determine size of '%s' (%s)",
                  srcfile, strerror (errno));
        goto done;
    }
    if (stat_buf.st_size == 0) {
        success = TRUE;
        goto done;
    }
    if (lseek(dst_fd, stat_buf.st_size - 1, SEEK_SET) == -1) {
        ui_error (op, "Unable to set file size for '%s' (%s)",
                  dstfile, strerror (errno));
        goto done;
    }
    if (write(dst_fd, "", 1) != 1) {
        ui_error (op, "Unable to write file size for '%s' (%s)",
                  dstfile, strerror (errno));
        goto done;
    }
    if ((src = mmap(0, stat_buf.st_size, PROT_READ,
                    MAP_FILE | MAP_SHARED, src_fd, 0)) == (void *) -1) {
        ui_error (op, "Unable to map source file '%s' for copying (%s)",
                  srcfile, strerror (errno));
        goto done;
    }
    if ((dst = mmap(0, stat_buf.st_size, PROT_READ | PROT_WRITE,
                    MAP_FILE | MAP_SHARED, dst_fd, 0)) == (void *) -1) {
        ui_error (op, "Unable to map destination file '%s' for copying (%s)",
                  dstfile, strerror (errno));
        goto done;
    }
    
    memcpy (dst, src, stat_buf.st_size);
    
    if (munmap (src, stat_buf.st_size) == -1) {
        ui_error (op, "Unable to unmap source file '%s' after copying (%s)",
                 srcfile, strerror (errno));
        goto done;
    }
    if (munmap (dst, stat_buf.st_size) == -1) {
        ui_error (op, "Unable to unmap destination file '%s' after "
                 "copying (%s)", dstfile, strerror (errno));
        goto done;
    }

    success = TRUE;

 done:

    if (success) {
        /*
         * the mode used to create dst_fd may have been affected by the
         * user's umask; so explicitly set the mode again
         */

        fchmod(dst_fd, mode);
    }

    if (src_fd != -1) {
        close (src_fd);
    }
    if (dst_fd != -1) {
        close (dst_fd);
    }

    return success;
}



/*
 * write_temp_file() - write the given data to a temporary file,
 * setting the file's permissions to those specified in perm.  On
 * success the name of the temporary file is returned; on error NULL
 * is returned.
 */

char *write_temp_file(Options *op, const int len,
                      const void *data, mode_t perm)
{
    unsigned char *dst = (void *) -1;
    char *tmpfile = NULL;
    int fd = -1;
    int ret = FALSE;

    /* create a temporary file */

    tmpfile = nvstrcat(op->tmpdir, "/nv-tmp-XXXXXX", NULL);
    
    fd = mkstemp(tmpfile);
    if (fd == -1) {
        ui_warn(op, "Unable to create temporary file (%s).",
                strerror(errno));
        goto done;
    }

    /* If a length of zero or a NULL data pointer was provided, skip writing
     * to the file and just set the desired permissions. */

    if (len && data) {

        /* set the temporary file's size */

        if (lseek(fd, len - 1, SEEK_SET) == -1) {
            ui_warn(op, "Unable to set file size for temporary file (%s).",
                    strerror(errno));
            goto done;
        }
        if (write(fd, "", 1) != 1) {
            ui_warn(op, "Unable to write file size for temporary file (%s).",
                    strerror(errno));
            goto done;
        }

        /* mmap the temporary file */

        if ((dst = mmap(0, len, PROT_READ | PROT_WRITE,
                    MAP_FILE | MAP_SHARED, fd, 0)) == (void *) -1) {
        ui_warn(op, "Unable to map temporary file (%s).", strerror(errno));
        goto done;

        }
    
        /* copy the data out to the file */

        memcpy(dst, data, len);
    }

    /* set the desired permissions on the file */
    
    if (fchmod(fd, perm) == -1) {
        ui_warn(op, "Unable to set permissions %04o on temporary "
                "file (%s)", perm, strerror(errno));
        goto done;
    }
    
    ret = TRUE;

 done:
    
    /* unmap the temporary file */
    
    if (dst != (void *) -1) {
        if (munmap(dst, len) == -1) {
            ui_warn(op, "Unable to unmap temporary file (%s).",
                    strerror(errno));
        }
    }
    
    /* close the temporary file */

    if (fd != -1) close(fd);
    
    if (ret) {
        return tmpfile;
    } else {
        nvfree(tmpfile);
        return NULL;
    }
    
} /* write_temp_file() */


/*
 * check_libGLX_indirect_target() - Helper function for
 * check_libGLX_indirect_links.
 *
 * Checks to see if the installer should install (or overwrite) a
 * libGLX_indirect.so.0 symlink.
 *
 * (path) should be the path to where the symlink would be installed.
 */
static int check_libGLX_indirect_target(Options *op, const char *path)
{
    char *target = NULL;
    char *base = NULL;
    char *ext = NULL;
    struct stat stat_buf;
    int ret;

    if (lstat(path, &stat_buf) != 0) {
        // If the file doesn't exist, then we should create it.
        return TRUE;
    }

    if (!S_ISLNK(stat_buf.st_mode)) {
        // The file is not a symlink. Leave it alone.
        return FALSE;
    }

    if (stat(path, &stat_buf) != 0) {
        // If we can't resolve the link, then overwrite it.
        return TRUE;
    }

    // Resolve the symlink.
    target = get_resolved_symlink_target(op, path);
    while (target != NULL) {
        // Follow any more symlinks. The fact that the stat call above
        // succeeded means that the link is valid.
        char *nextTarget = NULL;
        if (lstat(target, &stat_buf) != 0) {
            free(target);
            target = NULL;
            break;
        }

        if (!S_ISLNK(stat_buf.st_mode)) {
            break;
        }

        nextTarget = get_resolved_symlink_target(op, target);
        free(target);
        target = nextTarget;
    }
    if (target == NULL) {
        // This should never happen.
        ui_error(op, "Unable to resolve symbolic link \"%s\"\n", path);
        return FALSE;
    }

    // Find the basename of the link target.
    base = basename(target);
    // Strip off the extension.
    ext = strchr(base, '.');
    if (ext != NULL) {
        *ext = '\0';
    }

    // Finally, see if the resulting name is "libGLX_indirect". If it is, then
    // we'll assume that the existing file is a dedicated indirect rendering
    // library. Otherwise, we'll assume that it's a link to another vendor
    // library.
    if (strcmp(base, "libGLX_indirect") == 0) {
        ret = FALSE;
    } else {
        ret = TRUE;
    }
    free(target);
    return ret;
}

/*
 * check_libGLX_indirect_links() - Finds the entries for the
 * "libGLX_indirect.so.0" symlinks, and figures out whether to install them.
 */
static void check_libGLX_indirect_links(Options *op, Package *p)
{
    int i;

    if (op->install_libglx_indirect == NV_OPTIONAL_BOOL_TRUE) {
        // The user specified that we should install the symlink.
        return;
    }

    // Find the entries for libGLX_indirect.so.0, and decide whether or not to
    // keep them.
    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].dst != NULL && p->entries[i].type == FILE_TYPE_OPENGL_SYMLINK) {
            if (strcmp(p->entries[i].name, "libGLX_indirect.so.0") == 0) {
                int overwrite = FALSE;
                if (op->install_libglx_indirect == NV_OPTIONAL_BOOL_DEFAULT) {
                    if (check_libGLX_indirect_target(op, p->entries[i].dst)) {
                        overwrite = TRUE;
                    }
                }
                if (!overwrite) {
                    invalidate_package_entry(&(p->entries[i]));
                }
            }
        }
    }
}

/*
 * set_destinations() - given the Options and Package structures,
 * assign the destination field in each Package entry, building from
 * the OpenGL and XFree86 prefixes, the path relative to the prefix,
 * and the filename.  This assumes that the prefixes have already been
 * assigned in the Options struct.
 */

int set_destinations(Options *op, Package *p)
{
    char *name;
    char *dir, *path;
    const char *prefix;
    int i;
    if (!op->kernel_module_src_dir) {
        op->kernel_module_src_dir = nvstrcat("nvidia-", p->version, NULL);
    }

    for (i = 0; i < p->num_entries; i++) {
        /* If a file type's destination was overridden, use the override */
        if (op->file_type_destination_overrides[p->entries[i].type] != NULL) {
            p->entries[i].dst = nvstrcat(
                op->file_type_destination_overrides[p->entries[i].type], "/",
                p->entries[i].name, NULL);
            collapse_multiple_slashes(p->entries[i].dst);

            continue;
        }

        switch (p->entries[i].type) {

        case FILE_TYPE_KERNEL_MODULE_SRC:
        case FILE_TYPE_DKMS_CONF:
            if (op->no_kernel_module_source) {
                /* Don't install kernel module source files if requested. */
                p->entries[i].dst = NULL;
                continue;
            }
            prefix = op->kernel_module_src_prefix;
            dir = op->kernel_module_src_dir;
            path = p->entries[i].path;
            break;

        case FILE_TYPE_OPENGL_LIB:
        case FILE_TYPE_OPENGL_SYMLINK:
        case FILE_TYPE_GLVND_LIB:
        case FILE_TYPE_GLVND_SYMLINK:
        case FILE_TYPE_GLX_CLIENT_LIB:
        case FILE_TYPE_GLX_CLIENT_SYMLINK:
        case FILE_TYPE_EGL_CLIENT_LIB:
        case FILE_TYPE_EGL_CLIENT_SYMLINK:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = "";
            break;
        case FILE_TYPE_WINE_LIB:
            prefix = op->wine_prefix;
            dir = op->wine_libdir;
            path = "";
            break;
        case FILE_TYPE_VDPAU_LIB:
        case FILE_TYPE_VDPAU_SYMLINK:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = p->entries[i].path;
            break;

        case FILE_TYPE_CUDA_LIB:
        case FILE_TYPE_CUDA_SYMLINK:
        case FILE_TYPE_OPENCL_LIB:
        case FILE_TYPE_OPENCL_WRAPPER_LIB:
        case FILE_TYPE_OPENCL_LIB_SYMLINK:
        case FILE_TYPE_OPENCL_WRAPPER_SYMLINK:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = p->entries[i].path;
            break;

        case FILE_TYPE_CUDA_ICD:
            prefix = DEFAULT_CUDA_ICD_PREFIX;
            dir = DEFAULT_CUDA_ICD_DIR;
            path = "";
            break;            
            
        case FILE_TYPE_XMODULE_SHARED_LIB:
        case FILE_TYPE_GLX_MODULE_SHARED_LIB:
        case FILE_TYPE_GLX_MODULE_SYMLINK:
            prefix = op->x_module_path;
            dir = "";
            path = p->entries[i].path;
            break;

        case FILE_TYPE_TLS_LIB:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = p->entries[i].path;
            break;

        case FILE_TYPE_UTILITY_LIB:
        case FILE_TYPE_UTILITY_LIB_SYMLINK:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->utility_prefix;
                dir = op->utility_libdir;
            }
            path = "";
            break;

        case FILE_TYPE_GBM_BACKEND_LIB_SYMLINK:
            p->entries[i].target = nvstrcat(op->utility_prefix, "/", op->utility_libdir, "/", p->entries[i].target, NULL);
            /* fallthrough */
        case FILE_TYPE_GBM_BACKEND_LIB:
            prefix = op->opengl_prefix;
            dir = op->gbm_backend_dir;
            path = "";
            break;

        case FILE_TYPE_NVCUVID_LIB:
        case FILE_TYPE_NVCUVID_LIB_SYMLINK:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = "";
            break;

        case FILE_TYPE_ENCODEAPI_LIB:
        case FILE_TYPE_ENCODEAPI_LIB_SYMLINK:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = "";
            break;

        case FILE_TYPE_VGX_LIB:
        case FILE_TYPE_VGX_LIB_SYMLINK:
            prefix = op->opengl_prefix;
            dir = op->opengl_libdir;
            path = "";
            break;

        case FILE_TYPE_GRID_LIB:
        case FILE_TYPE_GRID_LIB_SYMLINK:
        case FILE_TYPE_FLEXERA_LIB:
        case FILE_TYPE_FLEXERA_LIB_SYMLINK:
            prefix = op->opengl_prefix;
            dir = op->opengl_libdir;
            path = p->entries[i].path;
            break;

        case FILE_TYPE_INSTALLER_BINARY:
            prefix = op->utility_prefix;
            dir = op->utility_bindir;
            path = "";
            break;
            
        case FILE_TYPE_DOCUMENTATION:
            prefix = op->documentation_prefix;
            dir = op->documentation_docdir;
            path = p->entries[i].path;
            break;

        case FILE_TYPE_MANPAGE:
        case FILE_TYPE_NVIDIA_MODPROBE_MANPAGE:
            prefix = op->documentation_prefix;
            dir = op->documentation_mandir;
            path = p->entries[i].path;
            break;

        case FILE_TYPE_APPLICATION_PROFILE:
            prefix = op->application_profile_path;
            dir = path = "";
            break;

        case FILE_TYPE_UTILITY_BINARY:
        case FILE_TYPE_UTILITY_BIN_SYMLINK:
            prefix = op->utility_prefix;
            dir = op->utility_bindir;
            path = "";
            break;

        case FILE_TYPE_DOT_DESKTOP:
            prefix = op->xdg_data_dir;
            dir = "applications";
            path = "";
            break;

        case FILE_TYPE_ICON:
            prefix = op->icon_dir;
            dir = "";
            path = p->entries[i].path;
            break;

        case FILE_TYPE_KERNEL_MODULE:
            
            /*
             * the kernel module dst field has already been
             * initialized in add_kernel_module_to_package()
             */
            
            continue;

        case FILE_TYPE_MODULE_SIGNING_KEY:
            prefix = op->module_signing_key_path;
            dir = path = "";
            break;

        case FILE_TYPE_EXPLICIT_PATH:
        case FILE_TYPE_NVIDIA_MODPROBE:
        case FILE_TYPE_OPENGL_DATA:
            prefix = p->entries[i].path;
            dir = path = "";
            break;

        case FILE_TYPE_XORG_OUTPUTCLASS_CONFIG:
            prefix = op->x_sysconfig_path;
            dir = path = "";
            break;

        case FILE_TYPE_VULKAN_ICD_JSON:
            /*
             * Defined by the Vulkan Linux ICD loader specification.
             */
            prefix = "/etc/vulkan/";
            path = p->entries[i].path;
            dir = "";
            break;

        case FILE_TYPE_VULKANSC_ICD_JSON:
            /*
             * Defined by the VulkanSC Linux ICD loader specification.
             */
            prefix = "/etc/vulkansc/";
            path = p->entries[i].path;
            dir = "";
            break;

        case FILE_TYPE_GLVND_EGL_ICD_JSON:
            // We'll set this path later in check_libglvnd_files. We have to
            // wait until we figure out whether we're going to install our own
            // build of the libglvnd libraries, which will determine where the
            // JSON file goes.
            p->entries[i].dst = NULL;
            continue;

        case FILE_TYPE_EGL_EXTERNAL_PLATFORM_JSON:
            prefix = op->external_platform_json_path;
            dir = path = "";
            break;

        case FILE_TYPE_INTERNAL_UTILITY_BINARY:
        case FILE_TYPE_INTERNAL_UTILITY_LIB:
        case FILE_TYPE_INTERNAL_UTILITY_DATA:
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                // TODO: Avoid the chroot stuff below for this?
                prefix = "/usr/lib/nvidia/32";
            } else {
                prefix = "/usr/lib/nvidia";
            }
            dir = path = "";
            break;

        case FILE_TYPE_FIRMWARE:
            prefix = nvstrcat("/lib/firmware/nvidia/", p->version, "/", NULL);
            path = p->entries[i].path;
            dir = "";
            break;

        case FILE_TYPE_SYSTEMD_UNIT:
            prefix = op->systemd_unit_prefix;
            dir = path = "";
            break;

        case FILE_TYPE_SYSTEMD_UNIT_SYMLINK:
            /*
             * Construct the symlink target from the systemd unit prefix and
             * symlink name.
             */
            p->entries[i].target = nvstrcat(op->systemd_unit_prefix, "/", p->entries[i].name, NULL);

            prefix = op->systemd_sysconf_prefix;
            path = p->entries[i].path;
            dir = "";
            break;

        case FILE_TYPE_SYSTEMD_SLEEP_SCRIPT:
            prefix = op->systemd_sleep_prefix;
            dir = path = "";
            break;

        default:
            
            /* 
             * silently ignore anything that doesn't match; libraries
             * of the wrong TLS class may fall in here, for example.
             */
            
            p->entries[i].dst = NULL;
            continue;
        }

        if ((prefix == NULL) || (dir == NULL) || (path == NULL)) {
            p->entries[i].dst = NULL;
            continue;
        }
        
        name = p->entries[i].name;

        p->entries[i].dst = nvstrcat(prefix, "/", dir, "/", path, "/", name, NULL);
        collapse_multiple_slashes(p->entries[i].dst);

#if defined(NV_X86_64)
        if ((p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) &&
            (op->compat32_chroot != NULL)) {

            /*
             * prepend an additional prefix; this is currently only
             * used for Debian GNU/Linux on Linux/x86-64, but may see
             * more use in the future.
             */

            char *dst = p->entries[i].dst;
            p->entries[i].dst = nvstrcat(op->compat32_chroot, dst, NULL);

            nvfree(dst);
        }
#endif /* NV_X86_64 */
    }

    return TRUE;

} /* set_destinations() */



/*
 * get_prefixes() - if in expert mode, ask the user for the OpenGL and
 * XFree86 installation prefix.  The default prefixes are already set
 * in parse_commandline().
 */

int get_prefixes (Options *op)
{
    char *ret;
 
    if (op->expert) {
        ret = ui_get_input(op, op->x_prefix,
                           "X installation prefix (only under "
                           "rare circumstances should this be changed "
                           "from the default)");
        if (ret && ret[0]) {
            op->x_prefix = ret; 
            if (!confirm_path(op, op->x_prefix)) return FALSE;
        }
    }

    remove_trailing_slashes(op->x_prefix);
    ui_expert(op, "X installation prefix is: '%s'", op->x_prefix);
    
    /*
     * assign the X module and library paths; this must be done
     * after the default prefixes/paths are assigned.
     */

    if (op->x_files_packaged) {
        get_x_library_and_module_paths(op);
    }

    if (op->expert) {
        ret = ui_get_input(op, op->x_library_path,
                           "X library installation path (only under "
                           "rare circumstances should this be changed "
                           "from the default)");
        if (ret && ret[0]) {
            op->x_library_path = ret; 
            if (!confirm_path(op, op->x_library_path)) return FALSE;
        }
    }

    remove_trailing_slashes(op->x_library_path);
    ui_expert(op, "X library installation path is: '%s'", op->x_library_path);

    if (op->expert) {
        ret = ui_get_input(op, op->x_module_path,
                           "X module installation path (only under "
                           "rare circumstances should this be changed "
                           "from the default)");
        if (ret && ret[0]) {
            op->x_module_path = ret; 
            if (!confirm_path(op, op->x_module_path)) return FALSE;
        }
    }

    remove_trailing_slashes(op->x_module_path);
    ui_expert(op, "X module installation path is: '%s'", op->x_module_path);
        
    if (op->expert) {
        ret = ui_get_input(op, op->opengl_prefix,
                           "OpenGL installation prefix (only under "
                           "rare circumstances should this be changed "
                           "from the default)");
        if (ret && ret[0]) {
            op->opengl_prefix = ret;
            if (!confirm_path(op, op->opengl_prefix)) return FALSE;
        }
    }

    remove_trailing_slashes(op->opengl_prefix);
    ui_expert(op, "OpenGL installation prefix is: '%s'", op->opengl_prefix);


    if (op->expert) {
        ret = ui_get_input(op, op->documentation_prefix,
                           "Documentation installation prefix (only under "
                           "rare circumstances should this be changed "
                           "from the default)");
        if (ret && ret[0]) {
            op->documentation_prefix = ret;
            if (!confirm_path(op, op->documentation_prefix)) return FALSE;
        }
    }

    remove_trailing_slashes(op->documentation_prefix);
    ui_expert(op, "Documentation installation prefix is: '%s'", op->documentation_prefix);


    if (op->expert) {
        ret = ui_get_input(op, op->utility_prefix,
                           "Utility installation prefix (only under "
                           "rare circumstances should this be changed "
                           "from the default)");
        if (ret && ret[0]) {
            op->utility_prefix = ret;
            if (!confirm_path(op, op->utility_prefix)) return FALSE;
        }
    }

    remove_trailing_slashes(op->utility_prefix);
    ui_expert(op, "Utility installation prefix is: '%s'", op->utility_prefix);

    if (op->expert) {
        op->no_kernel_module_source =
            !ui_yes_no(op, !op->no_kernel_module_source,
                      "Do you want to install kernel module sources?");
    }

    if (!op->no_kernel_module_source) {
        if (op->expert) {
            ret = ui_get_input(op, op->kernel_module_src_prefix,
                               "Kernel module source installation prefix");
            if (ret && ret[0]) {
                op->kernel_module_src_prefix = ret;
                if (!confirm_path(op, op->kernel_module_src_prefix))
                    return FALSE;
            }
        }
    
        remove_trailing_slashes(op->kernel_module_src_prefix);
        ui_expert(op, "Kernel module source installation prefix is: '%s'",
                  op->kernel_module_src_prefix);
    
        if (op->expert) {
            ret = ui_get_input(op, op->kernel_module_src_dir,
                               "Kernel module source installation directory");
            if (ret && ret[0]) {
                op->kernel_module_src_dir = ret;
                if (!confirm_path(op, op->kernel_module_src_dir)) return FALSE;
            }
        }
    
        remove_trailing_slashes(op->kernel_module_src_dir);
        ui_expert(op, "Kernel module source installation directory is: '%s'",
                  op->kernel_module_src_dir);
    }

#if defined(NV_X86_64)
    if (op->expert) {
        ret = ui_get_input(op, op->compat32_chroot,
                           "Compat32 installation chroot (only under "
                           "rare circumstances should this be "
                           "changed from the default)");
        if (ret && ret[0]) {
            op->compat32_chroot = ret;
            if (!confirm_path(op, op->compat32_chroot)) return FALSE;
        }
    }

    remove_trailing_slashes(op->compat32_chroot);
    ui_expert(op, "Compat32 installation chroot is: '%s'",
              op->compat32_chroot);

    if (op->expert) {
        ret = ui_get_input(op, op->compat32_prefix,
                           "Compat32 installation prefix (only under "
                           "rare circumstances should this be "
                           "changed from the default)");
        if (ret && ret[0]) {
            op->compat32_prefix = ret;
            if (!confirm_path(op, op->compat32_prefix)) return FALSE;
        }
    }

    remove_trailing_slashes(op->compat32_prefix);
    ui_expert(op, "Compat32 installation prefix is: '%s'",
              op->compat32_prefix);
#endif /* NV_X86_64 */

    return TRUE;
    
} /* get_prefixes() */


/*
 * add_kernel_module_helper() - append a kernel module (contained in
 * p->kernel_module_build_directory/subdir) to the package list.
 */

static void add_kernel_module_helper(Options *op, Package *p,
                                     const char *filename)
{
    char *file, *name, *dst;

    file = nvstrcat(p->kernel_module_build_directory, "/", filename, NULL);

    name = strrchr(file, '/');

    if (name && name[0]) {
        name++;
    }

    if (!name || !name[0]) {
        name = file;
    }

    dst = nvstrcat(op->kernel_module_installation_path, "/", filename, NULL);

    add_package_entry(p,
                      file,
                      NULL, /* path */
                      name,
                      NULL, /* target */
                      dst,
                      FILE_TYPE_KERNEL_MODULE,
                      FILE_COMPAT_ARCH_NONE,
                      0644);
}

/*
 * add_kernel_modules_to_package() - add any to-be-installed kernel modules
 * to the package list for installation.
 */

void add_kernel_modules_to_package(Options *op, Package *p)
{
    int i;

    for (i = 0; i < p->num_kernel_modules; i++) {
        add_kernel_module_helper(op, p, p->kernel_modules[i].module_filename);
    }
}



/*
 * Invalidate each package entry that is not type
 * FILE_TYPE_KERNEL_MODULE{,_SRC} or FILE_TYPE_DKMS_CONF.
 */

void remove_non_kernel_module_files_from_package(Package *p)
{
    int i;

    for (i = 0; i < p->num_entries; i++) {
        if ((p->entries[i].type != FILE_TYPE_KERNEL_MODULE) &&
            (p->entries[i].type != FILE_TYPE_KERNEL_MODULE_SRC) &&
            (p->entries[i].type != FILE_TYPE_DKMS_CONF)) {
            invalidate_package_entry(&(p->entries[i]));
        }
    }
}


/*
 * package_entry_is_in_kernel_module_build_directory() - returns TRUE if the
 * package entry at index i is in p->kernel_module_build_directory.
 */
static int package_entry_is_in_kernel_module_build_directory(Package *p, int i)
{
    const char *build_dir = p->kernel_module_build_directory;
    const char *file = p->entries[i].file;
    const char *cwd_prefix = "./";


    /* Remove any leading "./" */
    while (strncmp(build_dir, cwd_prefix, strlen(cwd_prefix)) == 0) {
        build_dir += strlen(cwd_prefix);
    }
    while (strncmp(file, cwd_prefix, strlen(cwd_prefix)) == 0) {
        file += strlen(cwd_prefix);
    }

    /* Check if the build directory is an initial substring of the file path */
    if (strncmp(file, build_dir, strlen(build_dir)) == 0) {
        if (build_dir[strlen(build_dir) - 1] == '/') {
            /* If the last character of the directory name is a '/', then the
             * strncmp(3) test above matched the full directory name, including
             * a trailing directory separator character. */
            return TRUE;
        }
        if (file[strlen(build_dir)] == '/') {
            /* If the next character after the matched portion of the file name
             * is a '/', then the directory name match is not a partial one. */ 
            return TRUE;
        }
    }

    return FALSE;
}

void remove_non_installed_kernel_module_source_files_from_package(Package *p)
{
    int i;

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].type == FILE_TYPE_KERNEL_MODULE_SRC) {
            if (!package_entry_is_in_kernel_module_build_directory(p, i)) {
                invalidate_package_entry(&(p->entries[i]));
            }
        }
    }
}


/*
 * Invalidate each package entry that is an OpenGL file
 */
void remove_opengl_files_from_package(Package *p)
{
    int i;

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].caps.is_opengl) {
            invalidate_package_entry(&(p->entries[i]));
        }
    }
}


/*
 * Invalidate each package entry that is an Wine file
 */
void remove_wine_files_from_package(Package *p)
{
    int i;

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].type == FILE_TYPE_WINE_LIB) {
            invalidate_package_entry(&(p->entries[i]));
        }
    }
}


/*
 * Invalidate each package entry that is a systemd file
 */
void remove_systemd_files_from_package(Package *p)
{
    int i;

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].type == FILE_TYPE_SYSTEMD_UNIT ||
            p->entries[i].type == FILE_TYPE_SYSTEMD_UNIT_SYMLINK ||
            p->entries[i].type == FILE_TYPE_SYSTEMD_SLEEP_SCRIPT) {
            invalidate_package_entry(&(p->entries[i]));
        }
    }
}


/*
 * mode_string_to_mode() - convert the string s 
 */

int mode_string_to_mode(Options *op, char *s, mode_t *mode)
{
    char *endptr;
    long int ret;
    
    ret = strtol(s, &endptr, 8);

    if ((ret == LONG_MIN) || (ret == LONG_MAX) || (*endptr != '\0')) {
        ui_error(op, "Error parsing permission string '%s' (%s)",
                 s, strerror (errno));
        return FALSE;
    }

    *mode = (mode_t) ret;

    return TRUE;
    
} /* mode_string_to_mode() */



/*
 * mode_to_permission_string() - given a mode bitmask, allocate and
 * write a permission string.
 */

char *mode_to_permission_string(mode_t mode)
{
    char *s = (char *) nvalloc(10);
    memset (s, '-', 9);
    
    if (mode & (1 << 8)) s[0] = 'r';
    if (mode & (1 << 7)) s[1] = 'w';
    if (mode & (1 << 6)) s[2] = 'x';
    
    if (mode & (1 << 5)) s[3] = 'r';
    if (mode & (1 << 4)) s[4] = 'w';
    if (mode & (1 << 3)) s[5] = 'x';
    
    if (mode & (1 << 2)) s[6] = 'r';
    if (mode & (1 << 1)) s[7] = 'w';
    if (mode & (1 << 0)) s[8] = 'x';
    
    s[9] = '\0';
    return s;

} /* mode_to_permission_string() */



/*
 * confirm_path() - check that the path exists; if not, ask the user
 * if it's OK to create it and then do mkdir().
 *
 * XXX for a while, I had thought that it would be better for this
 * function to only ask the user if it was OK to create the directory;
 * there are just too many reasons why mkdir might fail, though, so
 * it's better to do mkdir() so that we can fail at this point in the
 * installation.
 */

int confirm_path(Options *op, const char *path)
{
    const char *choices[2] = {
        "Create directory",
        "Abort installation"
    };

    /* return TRUE if the path already exists and is a directory */

    if (directory_exists(path)) return TRUE;
    
    if (ui_multiple_choice(op, choices, 2, 0, "The directory '%s' does not "
                           "exist; would you like to create it, or would you "
                           "prefer to abort installation?", path) == 0) {
        if (mkdir_with_log(op, path, 0755)) {
            return TRUE;
        } else {
            return FALSE;
        }
    }
    
    ui_message(op, "Not creating directory '%s'; aborting installation.",
               path);
    
    return FALSE;

} /* confirm_path() */



/*
 * mkdir_recursive() - create the path specified, also creating parent
 * directories as needed; this is equivalent to `mkdir -p`. Log created
 * directories if the "log" parameter is set.
 */

int mkdir_recursive(Options *op, const char *path, const mode_t mode, int log)
{
    char *error_str = NULL;
    char *log_str   = NULL;
    int success = FALSE;

    success = nv_mkdir_recursive(path, mode, &error_str, log ? &log_str : NULL);

    if (log_str) {
        log_mkdir(op, log_str);
        free(log_str);
    }

    if (error_str) {
        ui_error(op, "%s", error_str);
        free(error_str);
    }

    return success;
}



/*
 * mkdir_with_log() - Wrap mkdir_recursive() to create the path specified
 * with any needed parent directories.
 */

int mkdir_with_log(Options *op, const char *path, const mode_t mode)
{
    return mkdir_recursive(op, path, mode, TRUE);
}



/*
 * get_symlink_target() - get the target of the symbolic link
 * 'filename'.  On success, a newly malloced string containing the
 * target is returned.  On error, an error message is printed and NULL
 * is returned.
 */

char *get_symlink_target(Options *op, const char *filename)
{
    struct stat stat_buf;
    int ret, len = 0;
    char *buf = NULL;

    if (lstat(filename, &stat_buf) == -1) {
        ui_error(op, "Unable to get file properties for '%s' (%s).",
                 filename, strerror(errno));
        return NULL;
    }
    
    if (!S_ISLNK(stat_buf.st_mode)) {
        ui_error(op, "File '%s' is not a symbolic link.", filename);
        return NULL;
    }
    
    /*
     * grow the buffer to be passed into readlink(2), until the buffer
     * is big enough to hold the whole target.
     */

    do {
        len += NV_LINE_LEN;
        if (buf) free(buf);
        buf = nvalloc(len);
        ret = readlink(filename, buf, len - 1);
        if (ret == -1) {
            ui_error(op, "Failure while reading target of symbolic "
                     "link %s (%s).", filename, strerror(errno));
            free(buf);
            return NULL;
        }
    } while (ret >= (len - 1));

    buf[ret] = '\0';
    
    return buf;

} /* get_symlink_target() */



/*
 * get_resolved_symlink_target() - same as get_symlink_target, except that
 * relative links get resolved to an absolute path.
 */
char * get_resolved_symlink_target(Options *op, const char *filename)
{
    char *target = get_symlink_target(op, filename);
    if (target[0] != '/') {
        /* target is relative; canonicalize */
        char *filename_copy, *target_dir, *full_target_path;

        /* dirname(3) may modify the string passed into it; make a copy */
        filename_copy = nvstrdup(filename);
        target_dir = dirname(filename_copy);

        full_target_path = nvstrcat(target_dir, "/", target, NULL);

        nvfree(filename_copy);
        nvfree(target);

        target = nvalloc(PATH_MAX);
        target = realpath(full_target_path, target);

        nvfree(full_target_path);
    }

    return target;

} /* get_resolved_symlink_target() */



/*
 * install_file() - install srcfile as dstfile; this is done by
 * extracting the directory portion of dstfile, and then calling
 * copy_file().
 */ 

int install_file(Options *op, const char *srcfile,
                 const char *dstfile, mode_t mode)
{   
    int retval; 
    char *dirc, *dname;

    dirc = nvstrdup(dstfile);
    dname = dirname(dirc);
    
    if (!mkdir_with_log(op, dname, 0755)) {
        free(dirc);
        return FALSE;
    }

    retval = copy_file(op, srcfile, dstfile, mode);
    free(dirc);

    return retval;

} /* install_file() */


/*
 * install_symlink() - install dstfile as a symlink to linkname; this
 * is done by extracting the directory portion of dstfile, creating
 * that directory if it does not exist, and then calling symlink().
 */ 

int install_symlink(Options *op, const char *linkname, const char *dstfile)
{   
    char *dirc, *dname;

    dirc = nvstrdup(dstfile);
    dname = dirname(dirc);
    
    if (!mkdir_with_log(op, dname, 0755)) {
        free(dirc);
        return FALSE;
    }

    if (symlink(linkname, dstfile)) {
        free(dirc);
        return FALSE;
    }

    free(dirc);
    return TRUE;

} /* install_symlink() */



size_t get_file_size(Options *op, const char *filename)
{
    struct stat stat_buf;
    
    if (stat(filename, &stat_buf) == -1) {
        ui_error(op, "Unable to determine file size of '%s' (%s).",
                 filename, strerror(errno));
        return 0;
    }

    return stat_buf.st_size;

} /* get_file_size() */



size_t fget_file_size(Options *op, const int fd)
{
    struct stat stat_buf;
    
    if (fstat(fd, &stat_buf) == -1) {
        ui_error(op, "Unable to determine file size of file "
                 "descriptor %d (%s).", fd, strerror(errno));
        return 0;
    }

    return stat_buf.st_size;

} /* fget_file_size() */



char *get_tmpdir(Options *op)
{
    char *tmpdirs[] = { NULL, "/tmp", ".", NULL };
    int i;

    tmpdirs[0] = getenv("TMPDIR");
    tmpdirs[3] = getenv("HOME");
    
    for (i = 0; i < 4; i++) {
        if (tmpdirs[i] && directory_exists(tmpdirs[i])) {
            return (tmpdirs[i]);
        }
    }
    
    return NULL;

} /* get_tmpdir() */



/*
 * make_tmpdir() - create a temporary directory; XXX we should really
 * use mkdtemp, but it is not available on all systems.
 */

char *make_tmpdir(Options *op)
{
    char tmp[32], *tmpdir;
    
    snprintf(tmp, 32, "%d", getpid());
    
    tmpdir = nvstrcat(op->tmpdir, "/nvidia-", tmp, NULL);
    
    if (directory_exists(tmpdir)) {
        remove_directory(op, tmpdir);
    }
    
    if (!mkdir_recursive(op, tmpdir, 0755, FALSE)) {
        return NULL;
    }
    
    return tmpdir;
    
} /* make_tmpdir() */



/*
 * nvrename() - replacement for rename(2), because rename(2) can't
 * cross filesystem boundaries.  Get the src file attributes, copy the
 * src file to the dst file, stamp the dst file with the src file's
 * timestamp, and delete the src file.  Returns FALSE on error, TRUE
 * on success.
 */

int nvrename(Options *op, const char *src, const char *dst)
{
    struct stat stat_buf;
    struct utimbuf utime_buf;

    if (stat(src, &stat_buf) == -1) {
        ui_error(op, "Unable to determine file attributes of file "
                 "%s (%s).", src, strerror(errno));
        return FALSE;
    }
        
    if (!copy_file(op, src, dst, stat_buf.st_mode)) return FALSE;

    utime_buf.actime = stat_buf.st_atime; /* access time */
    utime_buf.modtime = stat_buf.st_mtime; /* modification time */

    if (utime(dst, &utime_buf) == -1) {
        ui_warn(op, "Unable to transfer timestamp from '%s' to '%s' (%s).",
                   src, dst, strerror(errno));
    }
    
    if (unlink(src) == -1) {
        ui_error(op, "Unable to delete '%s' (%s).", src, strerror(errno));
        return FALSE;
    }

    return TRUE;
    
} /* nvrename() */



/*
 * check_for_existing_rpms() - check if any of the previous NVIDIA
 * rpms are installed on the system.  If we find any, ask the user if
 * we may remove them.
 */

int check_for_existing_rpms(Options *op)
{
    /* list of rpms to remove; should be in dependency order */

    const char *rpms[2] = { "NVIDIA_GLX", "NVIDIA_kernel" };

    char *data;
    int i, ret;

    if (op->no_rpms) {
        ui_log(op, "Skipping check for conflicting rpms.");
        return TRUE;
    }

    for (i = 0; i < 2; i++) {
        ret = run_command(op, NULL, FALSE, NULL, TRUE,
                          "env LD_KERNEL_ASSUME=2.2.5 rpm --query ",
                          rpms[i], NULL);

        if (ret == 0) {
            if (ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
                                   NUM_CONTINUE_ABORT_CHOICES,
                                   CONTINUE_CHOICE, /* Default choice */
                                   "An %s rpm appears to already be installed "
                                   "on your system.  As part of installing the "
                                   "new driver, this %s rpm will be "
                                   "uninstalled. Are you sure you want to "
                                   "continue?",
                                   rpms[i], rpms[i]) == ABORT_CHOICE) {
                ui_log(op, "Installation aborted.");
                return FALSE;
            }

            ret = run_command(op, &data, op->expert, NULL, TRUE,
                              "rpm --erase --nodeps ", rpms[i], NULL);

            if (ret == 0) {
                ui_log(op, "Removed %s.", rpms[i]);
            } else {
                ui_warn(op, "Unable to erase %s rpm: %s", rpms[i], data);
            }
            
            nvfree(data);
        }
    }

    return TRUE;
    
} /* check_for_existing_rpms() */



/*
 * copy_directory_contents() - recursively copy the contents of directory src to
 * directory dst.  Special files are ignored.
 */

int copy_directory_contents(Options *op, const char *src, const char *dst)
{
    DIR *dir;
    struct dirent *ent;
    int status = FALSE;

    if ((dir = opendir(src)) == NULL) {
        ui_error(op, "Unable to open directory '%s' (%s).",
                 src, strerror(errno));
        return FALSE;
    }

    while ((ent = readdir(dir)) != NULL) {
        struct stat stat_buf;
        char *srcfile, *dstfile;
        int ret;

        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;

        srcfile = nvstrcat(src, "/", ent->d_name, NULL);
        dstfile = nvstrcat(dst, "/", ent->d_name, NULL);

        ret = (stat(srcfile, &stat_buf) != -1);

        if (ret) {
            /* recurse into subdirectories */
            if (S_ISDIR(stat_buf.st_mode)) {
                ret = mkdir_recursive(op, dstfile, stat_buf.st_mode, FALSE) &&
                      copy_directory_contents(op, srcfile, dstfile);
            } else if (S_ISREG(stat_buf.st_mode)) {
                ret = copy_file(op, srcfile, dstfile, stat_buf.st_mode);
            }
        }

        nvfree(srcfile);
        nvfree(dstfile);

        if (!ret) {
            goto done;
        }
    }

    status = TRUE;

  done:

    if (closedir(dir) != 0) {
        ui_error(op, "Failure while closing directory '%s' (%s).",
                 src, strerror(errno));

        return FALSE;
    }

    return status;

}



/*
 * pack_precompiled_files() - Create a new precompiled files package for the
 * given PrecompiledFileInfo array and save it to disk.
 */

int pack_precompiled_files(Options *op, Package *p, int num_files,
                           PrecompiledFileInfo *files)
{
    char time_str[256], *proc_version_string;
    char *outfile = NULL, *descr = NULL;
    char *precompiled_dir = precompiled_kernel_interface_path(p);
    time_t t;
    struct utsname buf;
    int ret = FALSE;
    PrecompiledInfo *info = NULL;

    ui_log(op, "Packaging precompiled kernel interface.");

    /* make sure the precompiled kernel interface directory exists */

    if (!mkdir_recursive(op, precompiled_dir, 0755, FALSE)) {
        ui_error(op, "Failed to create the directory '%s'!", precompiled_dir);
        goto done;
    }
    
    /* use the time in the output string... should be fairly unique */

    t = time(NULL);
    snprintf(time_str, 256, "%lu", t);

    /* use the uname string as the description */

    if (uname(&buf) != 0) {
        ui_error(op, "Failed to retrieve uname identifiers from the kernel!");
        return FALSE;
    }
    descr = nvstrcat(buf.sysname, " ",
                     buf.release, " ",
                     buf.version, " ",
                     buf.machine, NULL);
    
    /* read the proc version string */

    proc_version_string = read_proc_version(op, op->proc_mount_point);

    /* build the PrecompiledInfo struct */

    info = nvalloc(sizeof(PrecompiledInfo));

    outfile = nvstrcat(precompiled_dir, "/",
                       PRECOMPILED_PACKAGE_FILENAME, "-", p->version,
                       ".", time_str, NULL);

    info->version = nvstrdup(p->version);
    info->proc_version_string = proc_version_string;
    info->description = descr;
    info->num_files = num_files;
    info->files = files;

    ret = precompiled_pack(info, outfile);

done:

    nvfree(precompiled_dir);
    nvfree(outfile);
    free_precompiled(info);

    if (!ret) {
        ui_error(op, "Unable to package precompiled kernel interface.");
    }

    return ret;
}



/*
 * nv_strreplace() - we can't assume that the user has sed installed
 * on their system, so use this function to perform simple string
 * search and replacement.  Returns a newly allocated string that is a
 * duplicate of src, with all instances of 'orig' replaced with
 * 'replace'.
 */

char *nv_strreplace(const char *src, const char *orig, const char *replace)
{
    const char *prev_s, *end_s, *s;
    char *d, *dst;
    int len, dst_len, orig_len, replace_len;
    int done = 0;

    prev_s = s = src;
    end_s = src + strlen(src) + 1;
    
    dst = NULL;
    dst_len = 0;

    orig_len = strlen(orig);
    replace_len = strlen(replace);

    do {
        /* find the next instances of orig in src */

        s = strstr(prev_s, orig);
        
        /*
         * if no match, then flag that we are done once we finish
         * copying the src into dst
         */

        if (!s) {
            s = end_s;
            done = 1;
        }
        
        /* copy the characters between prev_s and s into dst */
        
        len = s - prev_s;
        dst = realloc(dst, dst_len + len + 1);
        d = dst + dst_len;
        strncpy(d, prev_s, len);
        d[len] = '\0';
        dst_len += len;
        
        /* if we are not done, then append the replace string */

        if (!done) {
            dst = realloc(dst, dst_len + replace_len + 1);
            d = dst + dst_len;
            memcpy(d, replace, replace_len);
            d[replace_len] = '\0';
            dst_len += replace_len;
        }

        /* skip past the orig string */

        if (!done) prev_s = s + orig_len;

    } while (!done);
    
    return dst;

} /* nv_strreplace() */



/*
 * process_template_file() - copy the specified template file to
 * a temporary file, replacing specified tokens with specified
 * replacement strings.  Return the temporary file's path to the
 * caller or NULL, if an error occurs.
 */

char *process_template_file(Options *op, PackageEntry *pe,
                            char **tokens, char **replacements)
{
    int failed, src_fd, dst_fd, len;
    struct stat stat_buf;
    char *src, *dst, *tmp, *tmp0, *tmpfile = NULL;
    char *token, *replacement;

    failed = FALSE;
    src_fd = dst_fd = -1;
    tmp = tmp0 = src = dst = tmpfile = NULL;
    len = 0;
    
    /* open the file */

    if ((src_fd = open(pe->file, O_RDONLY)) == -1) {
        ui_error(op, "Unable to open '%s' for copying (%s)",
                 pe->file, strerror(errno));
        return NULL;
    }

    /* get the size of the file */

    if (fstat(src_fd, &stat_buf) == -1) {
        ui_error(op, "Unable to determine size of '%s' (%s)",
                 pe->file, strerror(errno));
        failed = TRUE; goto done;
    }

    /* mmap the file */

    if ((src = mmap(0, stat_buf.st_size, PROT_READ,
                    MAP_FILE|MAP_SHARED, src_fd, 0)) == MAP_FAILED) {
        ui_error (op, "Unable to map source file '%s' for "
                  "copying (%s)", pe->file, strerror(errno));
        src = NULL;
        failed = TRUE; goto done;
    }

    if (!src) {
        ui_log(op, "%s is empty; skipping.", pe->file);
        failed = TRUE; goto done;
    }

    /*
     * allocate a string to hold the contents of the mmap'ed file,
     * plus explicit NULL termination
     */

    tmp = nvalloc(stat_buf.st_size + 1);
    memcpy(tmp, src, stat_buf.st_size);
    tmp[stat_buf.st_size] = '\0';

    /* setup to walk the tokens and replacements arrays */
    
    token = *tokens;
    replacement = *replacements;

    while (token != NULL && replacement != NULL) {
        /*
         * Replace any occurrences of 'token' with 'replacement' in
         * the source string and free the source
         */
        tmp0 = nv_strreplace(tmp, token, replacement);
        nvfree(tmp);
        tmp = tmp0;
        token = *(++tokens);
        replacement = *(++replacements);
    }

    /* create a temporary file to store the processed template file */
    
    tmpfile = nvstrcat(op->tmpdir, "/template-XXXXXX", NULL);
    if ((dst_fd = mkstemp(tmpfile)) == -1) {
        ui_error(op, "Unable to create temporary file (%s)",
                 strerror(errno));
        failed = TRUE; goto done;
    }

    /* set the size of the new file */

    len = strlen(tmp);

    if (lseek(dst_fd, len - 1, SEEK_SET) == -1) {
        ui_error(op, "Unable to set file size for '%s' (%s)",
                  tmpfile, strerror(errno));
        failed = TRUE; goto done;
    }
    if (write(dst_fd, "", 1) != 1) {
        ui_error(op, "Unable to write file size for '%s' (%s)",
                 tmpfile, strerror(errno));
        failed = TRUE; goto done;
    }

    /* mmap the new file */

    if ((dst = mmap(0, len, PROT_READ | PROT_WRITE,
                    MAP_FILE|MAP_SHARED, dst_fd, 0)) == MAP_FAILED) {
        ui_error(op, "Unable to map destination file '%s' for "
                 "copying (%s)", tmpfile, strerror(errno));
        dst = NULL;
        failed = TRUE; goto done;
    }

    /* write the processed data out to the temporary file */

    memcpy(dst, tmp, len);

done:

    if (src) {
        if (munmap(src, stat_buf.st_size) == -1) {
            ui_error(op, "Unable to unmap source file '%s' after "
                     "copying (%s)", pe->file,
                     strerror(errno));
        }
    }
    
    if (dst) {
        if (munmap(dst, len) == -1) {
            ui_error (op, "Unable to unmap destination file '%s' "
                      "after copying (%s)", tmpfile, strerror(errno));
        }
    }

    if (src_fd != -1) close(src_fd);
    if (dst_fd != -1) {
        close(dst_fd);
        /* in case an error occurred, delete the temporary file */
        if (failed) unlink(tmpfile);
    }

    if (failed) {
        nvfree(tmpfile); tmpfile = NULL;
    }

    nvfree(tmp);

    return tmpfile;

} /* process_template_files() */



/*
 * process_dot_desktop_files() - for any .desktop files in the
 * package, copy them to a temporary file, replacing __UTILS_PATH__ as
 * appropriate.  Then, add the new file to the package list.
 */

void process_dot_desktop_files(Options *op, Package *p)
{
    int i;
    char *tmpfile;

    char *tokens[2] = { "__UTILS_PATH__", NULL };
    char *replacements[2] = { NULL, NULL };

    int package_num_entries = p->num_entries;

    replacements[0] = nvstrcat(op->utility_prefix,
                               "/", op->utility_bindir, NULL);

    remove_trailing_slashes(replacements[0]);
    collapse_multiple_slashes(replacements[0]);

    for (i = 0; i < package_num_entries; i++) {
        if ((p->entries[i].type == FILE_TYPE_DOT_DESKTOP)) {
    
            /* invalidate the template file */

            invalidate_package_entry(&(p->entries[i]));

            tmpfile = process_template_file(op, &p->entries[i], tokens,
                                            replacements);
            if (tmpfile != NULL) {
                /* add this new file to the package */

                /*
                 * XXX 'name' is the basename (non-directory part) of
                 * the file to be installed; normally, 'name' just
                 * points into 'file', but in this case 'file' is
                 * mkstemp(3)-generated, so doesn't have the same
                 * basename; instead, we just strdup the name from the
                 * template package entry; yes, 'name' will get leaked
                 */

                add_package_entry(p,
                                  tmpfile,
                                  nvstrdup(p->entries[i].path),
                                  nvstrdup(p->entries[i].name),
                                  NULL, /* target */
                                  NULL, /* dst */
                                  FILE_TYPE_DOT_DESKTOP,
                                  p->entries[i].compat_arch,
                                  p->entries[i].mode);
            }
        }
    }

    nvfree(replacements[0]);

} /* process_dot_desktop_files() */



/*
 * process_dkms_conf() - copy dkms.conf to a temporary file and perform
 * some substitutions. Then, add the new file to the package list.
 */

void process_dkms_conf(Options *op, Package *p)
{
    int i;
    char *tmpfile;

    char *tokens[] = { "__VERSION_STRING", "__DKMS_MODULES", "__JOBS",
                       "__EXCLUDE_MODULES", "will be generated", NULL };
    char *replacements[] = { p->version, NULL, NULL, p->excluded_kernel_modules,
                             "was generated", NULL };

    int package_num_entries = p->num_entries;

    replacements[1] = nvstrdup("");
    replacements[2] = nvasprintf("%d", op->concurrency_level);

    /* Build the list of kernel modules to be installed */
    for (i = 0; i < p->num_kernel_modules; i++) {
        char *old_modules = replacements[1];
        char *index = nvasprintf("%d", i);

        replacements[1] = nvstrcat(old_modules,
                                   "BUILT_MODULE_NAME[", index, "]=\"",
                                   p->kernel_modules[i].module_name, "\"\n",
                                   "DEST_MODULE_LOCATION[", index, "]=",
                                   "\"/kernel/drivers/video\"\n", NULL);

        nvfree(index);
        nvfree(old_modules);
    }

    for (i = 0; i < package_num_entries; i++) {
        if ((p->entries[i].type == FILE_TYPE_DKMS_CONF)) {

            /* invalidate the template file */

            invalidate_package_entry(&(p->entries[i]));

            /* only process template files that are in the build directory */

            if (package_entry_is_in_kernel_module_build_directory(p, i)) {
                tmpfile = process_template_file(op, &p->entries[i], tokens,
                                                replacements);
                if (tmpfile != NULL) {
                    /* add this new file to the package */

                    /*
                     * XXX 'name' is the basename (non-directory part) of
                     * the file to be installed; normally, 'name' just
                     * points into 'file', but in this case 'file' is
                     * mkstemp(3)-generated, so doesn't have the same
                     * basename; instead, we just strdup the name from the
                     * template package entry; yes, 'name' will get leaked
                     */

                    add_package_entry(p,
                                      tmpfile,
                                      nvstrdup(p->entries[i].path),
                                      nvstrdup(p->entries[i].name),
                                      NULL, /* target */
                                      NULL, /* dst */
                                      FILE_TYPE_DKMS_CONF,
                                      p->entries[i].compat_arch,
                                      p->entries[i].mode);
                }
            }
        }
    }

    nvfree(replacements[2]);
    nvfree(replacements[1]);

}

/*
 * set_security_context() - set the security context of the file to 'type'
 * Returns TRUE on success or if SELinux is disabled, FALSE otherwise.
 * This relies on chcon(1), which might not work on some idiosyncratic
 * systems: a possible alternative would be to directly use the xattr(7)
 * API, but for now, chcon(1) is better for abstracting away the intimate
 * details of the "security.selinux" xattr format. (See bug 3876232)
 */
int set_security_context(Options *op, const char *filename, const char *type)
{
    int ret = FALSE;
    
    if (op->selinux_enabled == FALSE) {
        return TRUE;
    } 

    ret = run_command(op, NULL, FALSE, NULL, TRUE,
                      op->utils[CHCON], " -t ", type, " ", filename, NULL);

    return ret == 0;
}


static char * const native_libdirs[] = {
#if defined(NV_X86_64)
    DEFAULT_AMD64_TRIPLET_LIBDIR,
#elif defined(NV_X86)
    DEFAULT_IA32_TRIPLET_LIBDIR,
#elif defined(NV_ARMV7)
#if defined(NV_GNUEABIHF)
    DEFAULT_ARMV7HF_TRIPLET_LIBDIR,
#else
    DEFAULT_ARMV7_TRIPLET_LIBDIR,
#endif /* GNUEABIHF */
#elif defined(NV_AARCH64)
    DEFAULT_AARCH64_TRIPLET_LIBDIR,
#elif defined(NV_PPC64LE)
    DEFAULT_PPC64LE_TRIPLET_LIBDIR,
#else
#error Unknown architecture! Please update utils.mk to add support for this \
TARGET_ARCH, and make sure that an architecture-specific NV_$ARCH macro gets \
defined, and that NV_ARCH_BITS gets defined to the correct word size in bits.
#endif

#if NV_ARCH_BITS == 32
    DEFAULT_32BIT_LIBDIR,
#elif NV_ARCH_BITS == 64
    DEFAULT_64BIT_LIBDIR,
#endif

    DEFAULT_LIBDIR,
    NULL
};


#if defined(NV_X86_64)
static char * const compat_libdirs[] = {
    DEFAULT_IA32_TRIPLET_LIBDIR,
    DEFAULT_32BIT_LIBDIR,
    DEFAULT_LIBDIR,
    NULL
};
#endif



/*
 * get_ldconfig_cache() - retrieve the ldconfig(8) cache, or NULL on error
 */

static char *get_ldconfig_cache(Options *op)
{
    char *data;
    int ret;

    ret = run_command(op, &data, FALSE, NULL, FALSE,
                      op->utils[LDCONFIG], " -p", NULL);

    if (ret != 0) {
        nvfree(data);
        return NULL;
    }

    return data;
}


/*
 * find_libdir() - search in 'prefix' (optionally under 'chroot'/'prefix')
 * for directories in 'list', either in the ldconfig(8) cache or on the
 * filesystem. return the first directory found, or NULL if none found.
 */

static char *find_libdir(char * const * list, const char *prefix,
                         const char *ldconfig_cache, const char *chroot)
{
    int i;
    char *path = NULL;

    for (i = 0; list[i]; i++) {
        nvfree(path);

        path = nvstrcat(chroot ? chroot : "",
                        "/", prefix, "/", list[i], "/", NULL);
        collapse_multiple_slashes(path);

        if (ldconfig_cache) {
            if (strstr(ldconfig_cache, path)) {
                break;
            }
        } else {
            if (directory_exists(path)) {
                break;
            }
        }
    }

    nvfree(path);
    return list[i];
}


/*
 * find_libdir_and_fall_back() - search for the first available directory from
 * 'list' under 'prefix' that appears in the 'ldconfig_cache'. If no directory
 * is found in 'ldconfig_cache', test for directory existence; if no directory
 * from 'list' exists under 'prefix', default to DEFAULT_LIBDIR and print a
 * warning message.
 */
static char * find_libdir_and_fall_back(Options *op, char * const * list,
                                        const char *prefix,
                                        const char *ldconfig_cache,
                                        const char *name)
{
    char *libdir = find_libdir(list, prefix, ldconfig_cache, NULL);
    if (!libdir) {
        libdir = find_libdir(list, prefix, NULL, NULL);
    }
    if (!libdir) {
        libdir = DEFAULT_LIBDIR;
        ui_warn(op, "Unable to determine the default %s path. The path %s/%s "
                    "will be used, but this path was not detected in the "
                    "ldconfig(8) cache, and no directory exists at this path, "
                    "so it is likely that libraries installed there will not "
                    "be found by the loader.", name, prefix, libdir);
    }

    return libdir;
}


/*
 * get_default_prefixes_and_paths() - assign the default prefixes and
 * paths depending on the architecture, distribution and the X.Org
 * version installed on the system.
 */

void get_default_prefixes_and_paths(Options *op)
{
    char *default_libdir, *ldconfig_cache;

    if (!op->opengl_prefix)
        op->opengl_prefix = DEFAULT_OPENGL_PREFIX;

    ldconfig_cache = get_ldconfig_cache(op);

    default_libdir = find_libdir_and_fall_back(op, native_libdirs,
                                               op->opengl_prefix,
                                               ldconfig_cache, "library");


    if (!op->opengl_libdir)
        op->opengl_libdir = default_libdir;

    if (!op->gbm_backend_dir)
        op->gbm_backend_dir = nvstrcat(default_libdir, "/", "gbm", NULL);

    if (!op->x_prefix) {
        if (op->modular_xorg) {
            op->x_prefix = XORG7_DEFAULT_X_PREFIX;
        } else {
            op->x_prefix = DEFAULT_X_PREFIX;
        }
    }
    if (!op->x_libdir) {
        /* XXX if we just inherit default_libdir from above, we could end up
         * with e.g. /usr/lib/x86_64-linux-gnu/xorg/modules as the module path.
         * In practice, we haven't seen the tuplet-based paths used for X
         * module paths, so skip the first (tuplet-based) entry from
         * native_libdirs when getting a default value for x_libdir. This is
         * only used when we have to guess the paths when the query fails. */
        op->x_libdir = find_libdir_and_fall_back(op, &native_libdirs[1],
                                                 op->x_prefix, ldconfig_cache,
                                                 "X library");
    }

    nvfree(ldconfig_cache);

    if (!op->x_moddir) {
        if (op->modular_xorg) {
            op->x_moddir = XORG7_DEFAULT_X_MODULEDIR ;
        } else {
            op->x_moddir = DEFAULT_X_MODULEDIR;
        }
    }

#if defined(NV_X86_64)
    if (!op->compat32_prefix)
        op->compat32_prefix = DEFAULT_OPENGL_PREFIX;
#endif

    if (!op->utility_prefix)
        op->utility_prefix = DEFAULT_UTILITY_PREFIX;
    if (!op->utility_libdir)
        op->utility_libdir = default_libdir;
    if (!op->utility_bindir)
        op->utility_bindir = DEFAULT_BINDIR;

    if (!op->xdg_data_dir)
        op->xdg_data_dir = nvstrcat(op->utility_prefix, "/",
                                    DEFAULT_XDG_DATA_DIR, NULL);
    op->icon_dir = nvstrcat(op->xdg_data_dir, "/icons/hicolor", NULL);

    if (!op->documentation_prefix)
        op->documentation_prefix = DEFAULT_DOCUMENTATION_PREFIX;
    if (!op->documentation_docdir)
        op->documentation_docdir = DEFAULT_DOCDIR;
    if (!op->documentation_mandir)
        op->documentation_mandir = DEFAULT_MANDIR;

    if (!op->application_profile_path)
        op->application_profile_path = DEFAULT_APPLICATION_PROFILE_PATH;

    if (!op->kernel_module_src_prefix)
        op->kernel_module_src_prefix = DEFAULT_KERNEL_MODULE_SRC_PREFIX;

    if (!op->module_signing_key_path)
        op->module_signing_key_path = DEFAULT_MODULE_SIGNING_KEY_PATH;
    /* kernel_module_src_dir's default value is set in set_destinations() */

    if (!op->systemd_unit_prefix)
        op->systemd_unit_prefix = DEFAULT_SYSTEMD_UNIT_PREFIX;
    if (!op->systemd_sleep_prefix)
        op->systemd_sleep_prefix = DEFAULT_SYSTEMD_SLEEP_PREFIX;
    if (!op->systemd_sysconf_prefix)
        op->systemd_sysconf_prefix = DEFAULT_SYSTEMD_SYSCONF_PREFIX;

    if (!op->wine_prefix)
        op->wine_prefix = DEFAULT_WINE_PREFIX;
    if (!op->wine_libdir)
    {
        op->wine_libdir = nvstrcat(op->opengl_libdir, "/",
                                   DEFAULT_WINE_LIBDIR_SUFFIX, NULL);
        collapse_multiple_slashes(op->wine_libdir);
    }

} /* get_default_prefixes_and_paths() */


#if defined NV_X86_64
/*
 * compat32_conflict() - given a value for the compatibility library directory,
 * determines whether a conflict exists between the proposed compatibility
 * library path and the existing OpenGL library path. e.g. if the native path
 * is /usr/lib/x86_64-linux-gnu, then /usr/lib is probably not a good path for
 * compat32 libs. On the other hand, /usr/lib/i386-linux-gnu might be a valid
 * compat32 directory for a system where /usr/lib is the native library
 * directory, so the comparison is one-directional.
 *
 * XXX this assumes that the native directory would never be a subdirectory of
 *     the compat directory, which might not actually be true.
 */
static int compat32_conflict(Options *op, const char *compat_libdir)
{
    char *native_path, *compat_path;
    int ret, is_subdir;

    native_path = nvstrcat(op->opengl_prefix, "/", op->opengl_libdir, NULL);
    compat_path = nvstrcat(op->compat32_chroot ? op->compat32_chroot : "",
                           "/", op->compat32_prefix, "/", compat_libdir, NULL);

    ret = is_subdirectory(compat_path, native_path, &is_subdir);

    if (!ret) {
        ui_error(op, "Failed to determine whether '%s' is a subdirectory of "
                 "'%s'; '%s' will not be considered as a candidate location "
                 "for installing 32-bit compatibility libraries.",
                 native_path, compat_path, compat_path);
        is_subdir = TRUE;
    }

    nvfree(native_path);
    nvfree(compat_path);

    return is_subdir;
}
#endif


/*
 * get_compat32_path() - detect the appropriate path for installing the 32-bit
 * compatibility files. This function must be called after parse_manifest(),
 * and before set_destinations().
 */
void get_compat32_path(Options *op)
{
#if defined(NV_X86_64)
    char *ldconfig_cache = get_ldconfig_cache(op);

    if (!op->compat32_prefix)
        op->compat32_prefix = DEFAULT_OPENGL_PREFIX;

    if (!op->compat32_libdir) {
        char *compat_libdir;


        /* First, search the ldconfig(8) cache and filesystem normally */
        compat_libdir = find_libdir(compat_libdirs, op->compat32_prefix,
                                    ldconfig_cache, op->compat32_chroot);

        if (!compat_libdir || compat32_conflict(op, compat_libdir)) {
            compat_libdir = find_libdir(compat_libdirs, op->compat32_prefix,
                                        NULL, op->compat32_chroot);
        }

        /*
         * If we still didn't find a suitable directory, and the user did not
         * specify an explicit chroot, try the old Debian 32-bit chroot.
         */
        if ((!compat_libdir || compat32_conflict(op, compat_libdir)) &&
            !op->compat32_chroot) {
            op->compat32_chroot = DEBIAN_DEFAULT_COMPAT32_CHROOT;

            compat_libdir = find_libdir(compat_libdirs, op->compat32_prefix,
                                        ldconfig_cache, op->compat32_chroot);

            if (!compat_libdir || compat32_conflict(op, compat_libdir)) {
                compat_libdir = find_libdir(compat_libdirs, op->compat32_prefix,
                                            NULL, op->compat32_chroot);
            }

            /*
             * If we still didn't find a suitable path in the old Debian chroot,
             * reset the chroot path and the detected directory.
             */
            if (!compat_libdir || compat32_conflict(op, compat_libdir)) {
                op->compat32_chroot = NULL;
                compat_libdir = NULL;
            }
        }

        /* If we still failed to find a directory, don't install 32-bit files */
        if (op->install_compat32_libs != NV_OPTIONAL_BOOL_FALSE &&
            (!compat_libdir || compat32_conflict(op, compat_libdir))) {
            ui_warn(op, "Unable to find a suitable destination to install "
                    "32-bit compatibility libraries. Your system may not "
                    "be set up for 32-bit compatibility. 32-bit "
                    "compatibility files will not be installed; if you "
                    "wish to install them, re-run the installation and set "
                    "a valid directory with the --compat32-libdir option.");
            op->install_compat32_libs = NV_OPTIONAL_BOOL_FALSE;
        }

        if (op->install_compat32_libs != NV_OPTIONAL_BOOL_FALSE) {
            op->compat32_libdir = compat_libdir;
        }
    }

    nvfree(ldconfig_cache);
#endif
}


/*
 * extract_x_path() - take a comma-separated list of directories, and
 * extract the next available directory in the list.  Assign the
 * 'next' pointer so that it points to where we should continue during
 * the next call of extract_x_path().
 *
 * On success, return a pointer to the next directory in the string,
 * and update the 'next' pointer.  When we have exhausted the list,
 * NULL is returned.
 *
 * Note that this will destructively replace commas with NULL
 * terminators in the string.
 */

static char *extract_x_path(char *str, char **next)
{
    char *start;
    
    /*
     * choose where to start in the string: either we start at the
     * beginning, or we start immediately after where we found a comma
     * last time
     */

    start = str;
    
    if (*next) start = *next;
    
    /* skip past any commas at the start */

    while (*start == ',') start++;
    
    /* if we hit the end of the string, return now */

    if (*start == '\0') return NULL;
    
    /*
     * find the next comma in the string; if we find one, change it to
     * a NULL terminator (and move 'next' to the character immediately
     * after the comma); if we don't find a comma, move the 'next'
     * pointer to the end of the string, so that we terminate on the
     * next call to extract_x_path()
     */

    *next = strchr(start, ',');

    if (*next) {
        **next = '\0';
        (*next)++;
    } else {
        *next = strchr(start, '\0');
    }
    
    return start;
    
} /* extract_x_path() */


enum XPathType {
    XPathLibrary,
    XPathModule,
    XPathSysConfig
};

/*
 * get_x_paths_helper() - helper function for determining the X
 * library, module, and system xorg.conf.d paths; returns 'TRUE' if we had to
 * guess at the path
 */

static int get_x_paths_helper(Options *op,
                              enum XPathType pathType,
                              char *xserver_cmd,
                              char *pkg_config_cmd,
                              char *name,
                              char **path,
                              int require_existing_directory)
{
    char *dirs, *dir, *next;
    int ret, guessed = 0;

    /*
     * if the path was already specified (i.e.: by a command
     * line option), then we are done with this iteration
     */
    
    if (*path != NULL) {
        return FALSE;
    }
    
    /*
     * attempt to determine the path through the various query mechanisms
     */

    /*
     * first, try the X server commandline option; this is the
     * recommended query mechanism as of X.Org 7.2
     */
    if (op->utils[XSERVER] && xserver_cmd) {

        dirs = NULL;
        ret = run_command(op, &dirs, FALSE, NULL, TRUE,
                          op->utils[XSERVER], " ", xserver_cmd, NULL);

        if ((ret == 0) && dirs) {
            
            next = NULL;

            dir = extract_x_path(dirs, &next);
            
            while (dir) {
                
                if (!require_existing_directory || directory_exists(dir)) {

                    ui_expert(op, "X %s path '%s' determined from `%s %s`",
                              name, dir, op->utils[XSERVER], xserver_cmd);
                    
                    *path = nvstrdup(dir);
                    
                    nvfree(dirs);
                    
                    return FALSE;
                    
                } else {
                    ui_warn(op, "You appear to be using a modular X.Org "
                            "release, but the X %s installation "
                            "path, '%s', reported by `%s %s` does not exist.  "
                            "Please check your X.Org installation.",
                            name, dir, op->utils[XSERVER], xserver_cmd);
                }

                dir = extract_x_path(dirs, &next);
            }
        }

        nvfree(dirs);
    }

    /*
     * then, try the pkg-config command; this was the the
     * pseudo-recommended query mechanism between X.Org 7.0 and
     * X.Org 7.2
     */
    if (op->utils[PKG_CONFIG]) {

        dirs = NULL;
        ret = run_command(op, &dirs, FALSE, NULL, TRUE,
                          op->utils[PKG_CONFIG], " ", pkg_config_cmd, NULL);

        if ((ret == 0) && dirs) {

            next = NULL;
            
            dir = extract_x_path(dirs, &next);
 
            while (dir) {
                
                if (!require_existing_directory || directory_exists(dir)) {

                    ui_expert(op, "X %s path '%s' determined from `%s %s`",
                              name, dir, op->utils[PKG_CONFIG],
                              pkg_config_cmd);

                    *path = nvstrdup(dir);
                    
                    nvfree(dirs);
                    
                    return FALSE;
                
                } else {
                    ui_warn(op, "You appear to be using a modular X.Org "
                            "release, but the X %s installation "
                            "path, '%s', reported by `%s %s` does not exist.  "
                            "Please check your X.Org installation.",
                            name, dir, op->utils[PKG_CONFIG], pkg_config_cmd);
                }

                dir = extract_x_path(dirs, &next);
            }
        }

        nvfree(dirs);
    }

    /*
     * neither of the above mechanisms yielded a usable path; fall
     * through to constructing the path by hand.  If this is a modular X server,
     * record that we have to guess the path so that we can print a warning when
     * we are done.  For non-modular X, the default of /usr/X11R6/lib is
     * standard.
     */

    if (op->modular_xorg)
        guessed = TRUE;


    /* build the path */

    switch (pathType) {
        case XPathLibrary:
            *path = nvstrcat(op->x_prefix, "/", op->x_libdir, NULL);
            break;

        case XPathModule:
            *path = nvstrcat(op->x_library_path, "/", op->x_moddir, NULL);
            break;

        case XPathSysConfig:
            *path = nvstrcat(DEFAULT_X_DATAROOT_PATH, "/", DEFAULT_CONFDIR, NULL);
            break;
    }
    
    remove_trailing_slashes(*path);
    collapse_multiple_slashes(*path);
    
    return guessed;
}


/*
 * get_x_library_and_module_paths() - assign op->x_library_path and
 * op->x_module_path; this cannot fail.
 */

static void get_x_library_and_module_paths(Options *op)
{
    int guessed = FALSE;
    
    /*
     * get the library path, and then get the module path; note that
     * the module path depends on already having the library path
     */
    
    guessed |= get_x_paths_helper(op,
                                  XPathLibrary,
                                  "-showDefaultLibPath",
                                  "--variable=libdir xorg-server",
                                  "library",
                                  &op->x_library_path,
                                  TRUE);
    
    guessed |= get_x_paths_helper(op,
                                  XPathModule,
                                  "-showDefaultModulePath",
                                  "--variable=moduledir xorg-server",
                                  "module",
                                  &op->x_module_path,
                                  TRUE);

    /*
     * Get the sysconfig path (typically /usr/share/X11/xorg.conf.d).  This is
     * only needed if the nvidia.conf OutputClass config snippet is going to be
     * installed.  Don't complain if we had to guess the path; the server will
     * still work without it if xorg.conf is set up.
     */
    get_x_paths_helper(op,
                       XPathSysConfig,
                       NULL,
                       "--variable=sysconfigdir xorg-server",
                       "sysconfig",
                       &op->x_sysconfig_path,
                       FALSE);

    /*
     * done assigning op->x_library_path and op->x_module_path; if we
     * had to guess at either of the paths, print a warning
     */
    
    if (guessed) {
        ui_warn(op, "nvidia-installer was forced to guess the X library "
                "path '%s' and X module path '%s'; these paths were not "
                "queryable from the system.  If X fails to find the "
                "NVIDIA X driver module, please install the `pkg-config` "
                "utility and the X.Org SDK/development package for your "
                "distribution and reinstall the driver.",
                op->x_library_path, op->x_module_path);
    }
    
} /* get_x_library_and_module_paths() */



/*
 * get_filename() - Prompt the user for the path to a file. If no file exists
 * at the given path, keep reprompting until a valid path to a regular file or
 * symbolic link is given. This is just a thin wrapper around ui_get_input().
 */
char *get_filename(Options *op, const char *def, const char *msg)
{
    char *oldfile = nvstrdup(def);

    /* XXX This function should never be called if op->no_questions is set,
     * but just in case that happens by accident, do something besides looping
     * infinitely if def is a filename that doesn't exist. */
    if (op->no_questions) {
        return oldfile;
    }

    while (TRUE) {
        struct stat stat_buf;
        char *file = ui_get_input(op, oldfile, "%s", msg);

        nvfree(oldfile);

        if (file && stat(file, &stat_buf) != -1 &&
           (S_ISREG(stat_buf.st_mode) || S_ISLNK(stat_buf.st_mode))) {
            return file;
        }

        ui_message(op, "File \"%s\" does not exist, or is not a regular "
                   "file. Please enter another filename.", file ?
                   file : "(null)");

        oldfile = file;
    }
}



/*
 * secure_delete() - Securely delete a file, using `shred -u`. If `shred` isn't
 * found, fall back to just unlinking, but print a warning message.
 */
int secure_delete(Options *op, const char *file)
{
    char *cmd;

    cmd = find_system_util("shred");

    if (cmd) {
        int ret;
        char *cmdline = nvstrcat(cmd, " -u \"", file, "\"", NULL);

        ret = run_command(op, NULL, FALSE, NULL, TRUE, cmdline, NULL);
        log_printf(op, NULL, "%s: %s", cmdline, ret == 0 ? "" : "failed!");

        nvfree(cmd);
        nvfree(cmdline);

        return ret == 0;
    } else {
        ui_warn(op, "`shred` was not found on the system. The file %s will "
                "be deleted, but not securely. It may be possible to recover "
                "the file after deletion.", file);
        unlink(file);
        return FALSE;
    }
} /* secure_delete() */


/* invalidate_package_entry() - clear a package entry */

void invalidate_package_entry(PackageEntry *entry)
{
    entry->type = FILE_TYPE_NONE;
    /*
     * XXX don't try to free the destination string for
     * these invalidated package entries; this prevents
     * a crash on some Slackware 10.0 installations that
     * we've been unable to reproduce/root cause.
     */
    entry->dst = NULL;
    memset(&(entry->caps), 0, sizeof(entry->caps));
}


/*
 * is_subdirectory() - test whether subdir is a subdir of dir
 * returns TRUE on successful test, or FALSE on error
 * sets *is_subdir based on the result of a successful test
 *
 * The testing is performed two ways: first, walk from subdir up to "/"
 * and check at each step along the way if subdir's device and inode
 * match dir's. If a match is not found this way, simply compare dir
 * and subdir as normalized strings, up to the length of subdir, since
 * the first method will fail if subdir is a symlink located inside dir
 * whose target is outside of dir.
 */

int is_subdirectory(const char *dir, const char *subdir, int *is_subdir)
{
    struct stat root_st, dir_st;

    if (stat("/", &root_st) != 0 || stat(dir, &dir_st) != 0) {
        return FALSE;
    } else {
        struct stat testdir_st;
        char *testdir = nvstrdup(subdir);

        *is_subdir = FALSE;

        do {
            char *oldtestdir;

            if (stat(testdir, &testdir_st) != 0) {
                nvfree(testdir);
                return FALSE;
            }

            if (testdir_st.st_dev == dir_st.st_dev &&
                testdir_st.st_ino == dir_st.st_ino) {
                *is_subdir = TRUE;
                break;
            }

            oldtestdir = testdir;
            testdir = nvstrcat(oldtestdir, "/..", NULL);
            nvfree(oldtestdir);
        } while (testdir_st.st_dev != root_st.st_dev ||
                 testdir_st.st_ino != root_st.st_ino);

        nvfree(testdir);
    }

    if (!*is_subdir) {
        char *dir_with_slash, *subdir_with_slash;

        dir_with_slash = nvstrcat(dir, "/", NULL);
        collapse_multiple_slashes(dir_with_slash);
        subdir_with_slash = nvstrcat(subdir, "/", NULL);
        collapse_multiple_slashes(subdir_with_slash);

        *is_subdir = (strncmp(dir_with_slash, subdir_with_slash,
                              strlen(dir_with_slash)) == 0);

        nvfree(dir_with_slash);
        nvfree(subdir_with_slash);
    }

    return TRUE;
}

/*
 * directory_equals() - if a is a subdirectory of b, and b is a subdirectory
 * of a, then a and b are the same directory. Uses is_subdirectory() to do the
 * subdirectory check, since the inode-based comparison will be resilient
 * against symbolic links in either direction.
 */
static int directory_equals(const char *a, const char *b)
{
    int a_b, b_a;

    if (is_subdirectory(a, b, &a_b) && is_subdirectory(b, a, &b_a)) {
        return a_b && b_a;
    }

    return FALSE;
}

/*
 * get_opengl_libdir() - get the path where OpenGL libraries will be installed.
 */
static char *get_opengl_libdir(const Options *op)
{
    return nvstrcat(op->opengl_prefix, "/", op->opengl_libdir, "/", NULL);
}

/*
 * get_compat32_libdir() - get the path where 32-bit compatibility libraries
 * will be installed, where applicable, or NULL when not applicable.
 */
static char *get_compat32_libdir(const Options *op)
{
#if defined NV_X86_64
    return nvstrcat(op->compat32_chroot ? op->compat32_chroot : "", "/",
                    op->compat32_prefix, "/", op->compat32_libdir, "/", NULL);
#else
    return NULL;
#endif
}

/*
 * add_libgl_abi_symlink() - check to see if either native or compatibility
 * OpenGL libraries are destined to be installed to /usr/lib. If not, then
 * create a symlink /usr/lib/libGL.so.1 that points to the native libGL.so.1,
 * for compliance with the OpenGL ABI. Note: this function must be called
 * after set_destinations() to avoid the hardcoded "/usr/lib" destination from
 * being overwritten.
 */
void add_libgl_abi_symlink(Options *op, Package *p)
{
    static const char *usrlib = "/usr/lib/";
    char *libgl = nvstrdup("libGL.so.1");
    char *opengl_path = get_opengl_libdir(op);
    char *opengl32_path = get_compat32_libdir(op);

    if (!directory_equals(usrlib, opengl_path)
#if defined (NV_X86_64)
        && !directory_equals(usrlib, opengl32_path)
#endif
       ) {
        char *target = nvstrcat(opengl_path, "libGL.so.1", NULL);
        add_package_entry(p,
                          libgl,
                          NULL,
                          libgl,
                          target,
                          nvstrcat(usrlib, libgl, NULL),
                          FILE_TYPE_OPENGL_SYMLINK,
                          FILE_COMPAT_ARCH_NATIVE,
                          0000);
    } else {
        nvfree(libgl);
    }

    nvfree(opengl_path);
    nvfree(opengl32_path);
}

typedef enum {
    LIBGLVND_CHECK_RESULT_INSTALLED = 0,
    LIBGLVND_CHECK_RESULT_NOT_INSTALLED = 1,
    LIBGLVND_CHECK_RESULT_PARTIAL = 2,
    LIBGLVND_CHECK_RESULT_ERROR = 3,
} LibglvndInstallCheckResult;

/*
 * run_libglvnd_script() - Finds and runs the libglvnd install checker script.
 *
 * This runs a helper script which determines whether the libglvnd libraries
 * are installed, missing, or partially installed.
 *
 * If the helper script is not included in the package, then it will just
 * return LIBGLVND_CHECK_RESULT_NOT_INSTALLED.
 */
static LibglvndInstallCheckResult run_libglvnd_script(Options *op, Package *p,
                                                      char **missing_libs)
{
    const char *scriptPath = "./libglvnd_install_checker/check-libglvnd-install.sh";
    char *output = NULL;
    int status;
    LibglvndInstallCheckResult result = LIBGLVND_CHECK_RESULT_ERROR;

    if (missing_libs) {
        *missing_libs = "";
    }

    log_printf(op, NULL, "Looking for install checker script at %s", scriptPath);
    if (access(scriptPath, R_OK) != 0) {
        // We don't have the install check script, so assume that it's not
        // installed.
        log_printf(op, NULL, "No libglvnd install checker script, assuming not installed.");
        result = LIBGLVND_CHECK_RESULT_NOT_INSTALLED;
        goto done;
    }

    status = run_command(op, &output, TRUE, NULL, FALSE,
                         "/bin/sh ", scriptPath, NULL);
    if (WIFEXITED(status)) {
        result = WEXITSTATUS(status);
    } else {
        result = LIBGLVND_CHECK_RESULT_ERROR;
    }

    // If the installation is partial, pass the list of missing libraries
    // reported by the script back to the caller.
    if (result == LIBGLVND_CHECK_RESULT_PARTIAL && missing_libs) {
        char *line, *end, *buf;
        int len = strlen(output);
        static const char *missing_label = "Missing libglvnd libraries: ";

        for (buf = output;
             (line = get_next_line(buf, &end, output, len));
             buf = end) {
            int found = FALSE;

            if (strncmp(line, missing_label, strlen(missing_label)) == 0) {
                *missing_libs = nvstrdup(line + strlen(missing_label));
                found = TRUE;
            }

            free(line);

            if (found) {
                break;
            }
        }
    }

done:
    nvfree(output);
    return result;
}

/*
 * set_libglvnd_egl_json_path() - Tries to figure out what path to install the
 * JSON file to for a libglvnd EGL vendor library.
 *
 * This is only needed to work with an existing copy of the libglvnd libraries.
 * If we're installing our own build, then we already know what path libEGL
 * expects.
 */
static void set_libglvnd_egl_json_path(Options *op)
{
    if (op->libglvnd_json_path == NULL) {
        char *path = get_pkg_config_variable(op, "libglvnd", "datadir");
        if (path != NULL) {
            op->libglvnd_json_path = nvstrcat(path, "/glvnd/egl_vendor.d", NULL);
            collapse_multiple_slashes(op->libglvnd_json_path);
            nvfree(path);
        }
    }

    if (op->libglvnd_json_path == NULL) {
        ui_warn(op, "Unable to determine the path to install the "
                "libglvnd EGL vendor library config files. Check that "
                "you have pkg-config and the libglvnd development "
                "libraries installed, or specify a path with "
                "--glvnd-egl-config-path.");
        op->libglvnd_json_path = nvstrdup(DEFAULT_GLVND_EGL_JSON_PATH);
    }
}

/*
 * Reports whether the given library is an optional part of libglvnd.
 */
static int library_is_optional(const char *library)
{
    const char * const optional_libs[] = {
        "libOpenGL.so", // libOpenGL.so is not needed for classic GLX/EGL ABIs
    };
    int i;

    for (i = 0; i < ARRAY_LEN(optional_libs); i++) {
        // Do a partial name match to allow versioned and unversioned SONAMEs
        if (strncmp(library, optional_libs[i], strlen(optional_libs[i])) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * Reports whether the given space-delimited list of libraries includes a
 * mandatory part of the libglvnd stack.
 */
static int list_includes_essential_library(const char *libraries)
{
    int essential_library_found = FALSE;
    char *libs = nvstrdup(libraries);
    char *lib;

    for (lib = strtok(libs, " "); lib; lib = strtok(NULL, " ")) {
        if (!library_is_optional(lib)) {
            essential_library_found = TRUE;
            break;
        }
    }

    nvfree(libs);
    return essential_library_found;
}

/*
 * check_libglvnd_files() - Checks whether or not the installer should install
 * the libglvnd libraries.
 *
 * If the libglvnd libraries are already installed, then we'll leave them
 * alone. If they're not already installed, then we'll install our own copies.
 *
 * Note that we only check for the native libraries, and use that result for
 * both the native and 32-bit compatibility libraries. The reason for that is
 * that the conflicting file list can't distinguish between 32-bit and 64-bit
 * files that have the same filename, so it won't correctly handle installing
 * one but not the other.
 */
int check_libglvnd_files(Options *op, Package *p)
{
    int shouldInstall = op->install_libglvnd_libraries;
    int foundAnyFiles = FALSE;
    int foundJSONFile = FALSE;
    int i;

    // Start by checking for any libGLX_indirect.so.0 links.
    check_libGLX_indirect_links(op, p);

    // Then, check to see if there are any libglvnd files in the package.
    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].type == FILE_TYPE_GLVND_LIB ||
            p->entries[i].type == FILE_TYPE_GLVND_SYMLINK) {
            foundAnyFiles = TRUE;
        }

        if (p->entries[i].type == FILE_TYPE_GLVND_EGL_ICD_JSON) {
            foundAnyFiles = TRUE;
            foundJSONFile = TRUE;
        }
    }
    if (!foundAnyFiles) {
        return TRUE;
    }

    if (shouldInstall == NV_OPTIONAL_BOOL_DEFAULT) {
        char *missing_libs;

        // Try to figure out whether libglvnd is already installed. We'll defer
        // to a separate program to do that.

        LibglvndInstallCheckResult result = run_libglvnd_script(op, p, &missing_libs);
        if (result == LIBGLVND_CHECK_RESULT_INSTALLED) {
            // The libraries are already installed, so leave them alone.
            shouldInstall = NV_OPTIONAL_BOOL_FALSE;
        } else if (result == LIBGLVND_CHECK_RESULT_NOT_INSTALLED) {
            // The libraries are not installed, so install our own copies.
            shouldInstall = NV_OPTIONAL_BOOL_TRUE;
        } else if (result == LIBGLVND_CHECK_RESULT_PARTIAL) {
            // The libraries are partially installed. Ask the user what to do.
            static int partialAction = -1;
            if (partialAction < 0) {
                static const char *ANSWERS[] = {
                    "Don't install libglvnd files",
                    "Install and overwrite existing files",
                    "Abort installation."
                };

                int default_choice;
                const char *optional_only;

                // If GLVND was partially installed, but none of the missing
                // libraries are essential, default to allowing the installation
                // to continue without installing libglvnd by. If any of the
                // missing libraries in a partial libglvnd installation are
                // essential, default to aborting the installation.
                if (list_includes_essential_library(missing_libs)) {
                    default_choice = 2; // Abort installation
                    optional_only = "";
                } else {
                    default_choice = 0; // Don't install
                    optional_only = "All of the essential libglvnd libraries "
                                    "are present, but one or more optional "
                                    "components are missing. ";
                }

                partialAction = ui_multiple_choice(op, ANSWERS, 3, default_choice,
                        "An incomplete installation of libglvnd was found. %s"
                        "Do you want to install a full copy of libglvnd? "
                        "This will overwrite any existing libglvnd libraries.",
                        optional_only);
            }
            if (partialAction == 0) {
                // Don't install
                shouldInstall = NV_OPTIONAL_BOOL_FALSE;
            } else if (partialAction == 1) {
                // Install and overwrite
                shouldInstall = NV_OPTIONAL_BOOL_TRUE;
            } else {
                // Abort.
                return FALSE;
            }
        } else {
            // Some error occurred.
            return FALSE;
        }
    }

    // Sanity check: We should have set shouldInstall to true or false by now.
    if (shouldInstall != NV_OPTIONAL_BOOL_FALSE && shouldInstall != NV_OPTIONAL_BOOL_TRUE) {
        ui_error(op, "Internal error: Could not determine whether to install libglvnd");
        return FALSE;
    }

    if (shouldInstall != NV_OPTIONAL_BOOL_TRUE) {
        log_printf(op, NULL, "Will not install libglvnd libraries.");
        for (i = 0; i < p->num_entries; i++) {
            if (p->entries[i].type == FILE_TYPE_GLVND_LIB ||
                p->entries[i].type == FILE_TYPE_GLVND_SYMLINK ||
                p->entries[i].type == FILE_TYPE_GLX_CLIENT_LIB ||
                p->entries[i].type == FILE_TYPE_GLX_CLIENT_SYMLINK ||
                p->entries[i].type == FILE_TYPE_EGL_CLIENT_LIB ||
                p->entries[i].type == FILE_TYPE_EGL_CLIENT_SYMLINK) {
                ui_log(op, "Skipping GLVND file: \"%s\"", p->entries[i].file);
                invalidate_package_entry(&(p->entries[i]));
            }
        }

        if (foundJSONFile) {
            set_libglvnd_egl_json_path(op);
        }
    } else {
        log_printf(op, NULL, "Will install libglvnd libraries.");

        if (foundJSONFile && op->libglvnd_json_path == NULL) {
            op->libglvnd_json_path = nvstrdup(DEFAULT_GLVND_EGL_JSON_PATH);
        }
    }
    if (foundJSONFile) {
        log_printf(op, NULL,
                "Will install libEGL vendor library config file to %s",
                op->libglvnd_json_path);
        for (i = 0; i < p->num_entries; i++) {
            if (p->entries[i].type == FILE_TYPE_GLVND_EGL_ICD_JSON) {
                p->entries[i].dst = nvstrcat(op->libglvnd_json_path, "/", p->entries[i].name, NULL);
                collapse_multiple_slashes(p->entries[i].dst);
            }
        }
    }
    return TRUE;
}
