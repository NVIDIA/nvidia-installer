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

#include "nvidia-installer.h"
#include "user-interface.h"
#include "files.h"
#include "misc.h"
#include "precompiled.h"


static char *get_xdg_data_dir(void);
static void  get_x_library_and_module_paths(Options *op);


/*
 * remove_directory() - recursively delete a direcotry (`rm -rf`)
 */

int remove_directory(Options *op, const char *victim)
{
    struct stat stat_buf;
    DIR *dir;
    struct dirent *ent;
    char *filename;
    int len;
    
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
    
    while ((ent = readdir(dir)) != NULL) {

        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;
        
        len = strlen(victim) + strlen(ent->d_name) + 2;
        filename = (char *) nvalloc(len);
        snprintf(filename, len, "%s/%s", victim, ent->d_name);
        
        if (lstat(filename, &stat_buf) == -1) {
            ui_error(op, "failure to open '%s'", filename);
            free(filename);
            return FALSE;
        }
        
        if (S_ISDIR(stat_buf.st_mode)) {
            remove_directory(op, filename);
        } else {
            if (unlink(filename) != 0) {
                ui_error(op, "Failure removing file %s (%s)",
                         filename, strerror(errno));
            }
        }
        free(filename);
    }

    if (rmdir(victim) != 0) {
        ui_error(op, "Failure removing directory %s (%s)",
                 victim, strerror(errno));
        return FALSE;
    }

    return TRUE;
    
} /* remove_directory() */



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
    char *filename;

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
        
        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;
        
        filename = nvstrcat(victim, "/", ent->d_name, NULL);
        
        /* stat the file to get the type */
        
        if (lstat(filename, &stat_buf) == -1) {
            ui_error(op, "failure to open '%s'", filename);
            nvfree(filename);
            return FALSE;
        }
        
        /* if it is a directory, call this recursively */

        if (S_ISDIR(stat_buf.st_mode)) {
            if (!touch_directory(op, filename)) {
                nvfree(filename);
                return FALSE;
            }
        }

        /* finally, set the access and modification times */
        
        if (utime(filename, &time_buf) != 0) {
            ui_error(op, "Error setting modification time for %s", filename);
            nvfree(filename);
            return FALSE;
        }

        nvfree(filename);
    }

    if (closedir(dir) != 0) {
        ui_error(op, "Error while closing directory %s.", victim);
        return FALSE;
    }
    
    return TRUE;

} /* touch_directory() */


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
    int src_fd, dst_fd;
    struct stat stat_buf;
    char *src, *dst;
    
    if ((src_fd = open(srcfile, O_RDONLY)) == -1) {
        ui_error (op, "Unable to open '%s' for copying (%s)",
                  srcfile, strerror (errno));
        goto fail;
    }
    if ((dst_fd = open(dstfile, O_RDWR | O_CREAT | O_TRUNC, mode)) == -1) {
        ui_error (op, "Unable to create '%s' for copying (%s)",
                  dstfile, strerror (errno));
        goto fail;
    }
    if (fstat(src_fd, &stat_buf) == -1) {
        ui_error (op, "Unable to determine size of '%s' (%s)",
                  srcfile, strerror (errno));
        goto fail;
    }
    if (stat_buf.st_size == 0)
        goto done;
    if (lseek(dst_fd, stat_buf.st_size - 1, SEEK_SET) == -1) {
        ui_error (op, "Unable to set file size for '%s' (%s)",
                  dstfile, strerror (errno));
        goto fail;
    }
    if (write(dst_fd, "", 1) != 1) {
        ui_error (op, "Unable to write file size for '%s' (%s)",
                  dstfile, strerror (errno));
        goto fail;
    }
    if ((src = mmap(0, stat_buf.st_size, PROT_READ,
                    MAP_FILE | MAP_SHARED, src_fd, 0)) == (void *) -1) {
        ui_error (op, "Unable to map source file '%s' for copying (%s)",
                  srcfile, strerror (errno));
        goto fail;
    }
    if ((dst = mmap(0, stat_buf.st_size, PROT_READ | PROT_WRITE,
                    MAP_FILE | MAP_SHARED, dst_fd, 0)) == (void *) -1) {
        ui_error (op, "Unable to map destination file '%s' for copying (%s)",
                  dstfile, strerror (errno));
        goto fail;
    }
    
    memcpy (dst, src, stat_buf.st_size);
    
    if (munmap (src, stat_buf.st_size) == -1) {
        ui_error (op, "Unable to unmap source file '%s' after copying (%s)",
                 srcfile, strerror (errno));
        goto fail;
    }
    if (munmap (dst, stat_buf.st_size) == -1) {
        ui_error (op, "Unable to unmap destination file '%s' after "
                 "copying (%s)", dstfile, strerror (errno));
        goto fail;
    }

 done:
    /*
     * the mode used to create dst_fd may have been affected by the
     * user's umask; so explicitly set the mode again
     */

    fchmod(dst_fd, mode);

    close (src_fd);
    close (dst_fd);
    
    return TRUE;

 fail:
    return FALSE;

} /* copy_file() */



/*
 * write_temp_file() - write the given data to a temporary file,
 * setting the file's permissions to those specified in perm.  On
 * success the name of the temporary file is returned; on error NULL
 * is returned.
 */

char *write_temp_file(Options *op, const int len,
                      const unsigned char *data, mode_t perm)
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
        if (tmpfile) nvfree(tmpfile);
        return NULL;
    }
    
} /* write_temp_file() */



/*
 * select_tls_class() - determine which tls class should be installed
 * on the user's machine; if tls_test() fails, just install the
 * classic tls libraries.  If tls_test() passes, install both OpenGL
 * sets, but only the new tls libglx.
 */

void select_tls_class(Options *op, Package *p)
{
    int i;

    if (!tls_test(op, FALSE)) {
        op->which_tls = (op->which_tls & TLS_LIB_TYPE_FORCED);
        op->which_tls |= TLS_LIB_CLASSIC_TLS;

        /*
         * tls libraries will not run on this system; just install the
         * classic OpenGL libraries: clear the FILE_TYPE of any
         * FILE_CLASS_NEW_TLS package entries.
         */

        ui_log(op, "Installing classic TLS OpenGL libraries.");
        
        for (i = 0; i < p->num_entries; i++) {
            if ((p->entries[i].flags & FILE_CLASS_NEW_TLS) &&
                (p->entries[i].flags & FILE_CLASS_NATIVE)) {
                /*
                 * XXX don't try to free the destination string for
                 * these invalidated TLS libraries; this prevents
                 * a crash on some Slackware 10.0 installations that
                 * I've been unable to reproduce/root cause.
                 */
                /* nvfree(p->entries[i].dst); */
                p->entries[i].flags &= ~FILE_TYPE_MASK;
                p->entries[i].dst = NULL;
            }
        }
    } else {
        op->which_tls = (op->which_tls & TLS_LIB_TYPE_FORCED);
        op->which_tls |= TLS_LIB_NEW_TLS;

        /*
         * tls libraries will run on this system: install both the
         * classic and new TLS libraries.
         */

        ui_log(op, "Installing both new and classic TLS OpenGL libraries.");
    }

#if defined(NV_X86_64)

    /*
     * If we are installing on amd64, then we need to perform a
     * similar test for the 32bit compatibility libraries
     */

    if (!tls_test(op, TRUE)) {
        op->which_tls_compat32 = (op->which_tls_compat32 & TLS_LIB_TYPE_FORCED);
        op->which_tls_compat32 |= TLS_LIB_CLASSIC_TLS;

        /*
         * 32bit tls libraries will not run on this system; just
         * install the classic OpenGL libraries: clear the FILE_TYPE
         * of any FILE_CLASS_NEW_TLS_32 package entries.
         */

        ui_log(op, "Installing classic TLS 32bit OpenGL libraries.");
        
        for (i = 0; i < p->num_entries; i++) {
            if ((p->entries[i].flags & FILE_CLASS_NEW_TLS) &&
                (p->entries[i].flags & FILE_CLASS_COMPAT32)) {
                /*
                 * XXX don't try to free the destination string for
                 * these invalidated TLS libraries; this prevents
                 * a crash on some Slackware 10.0 installations that
                 * I've been unable to reproduce/root cause.
                 */
                /* nvfree(p->entries[i].dst); */
                p->entries[i].flags &= ~FILE_TYPE_MASK;
                p->entries[i].dst = NULL;
            }
        }
    } else {
        op->which_tls_compat32 = (op->which_tls_compat32 & TLS_LIB_TYPE_FORCED);
        op->which_tls_compat32 |= TLS_LIB_NEW_TLS;

        /*
         * 32bit tls libraries will run on this system: install both
         * the classic and new TLS libraries.
         */

        ui_log(op, "Installing both new and classic TLS 32bit "
               "OpenGL libraries.");
    }

#endif /* NV_X86_64 */

} /* select_tls_class() */


/*
 * set_destinations() - given the Options and Package structures,
 * assign the destination field in each Package entry, building from
 * the OpenGL and XFree86 prefixes, the path relative to the prefix,
 * and the filename.  This assumes that the prefixes have already been
 * assigned in the Options struct.
 */

int set_destinations(Options *op, Package *p)
{
    char *name, *s;
    char *prefix, *dir, *path;
    char *xdg_data_dir;
    int i;
    s = NULL;
    
    for (i = 0; i < p->num_entries; i++) {

        switch (p->entries[i].flags & FILE_TYPE_MASK) {
            
        case FILE_TYPE_KERNEL_MODULE_CMD:
        case FILE_TYPE_KERNEL_MODULE_SRC:
            
            /* we don't install kernel module sources */
            
            p->entries[i].dst = NULL;
            continue;
            
        case FILE_TYPE_OPENGL_LIB:
        case FILE_TYPE_OPENGL_SYMLINK:
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = "";
            break;

        case FILE_TYPE_VDPAU_LIB:
        case FILE_TYPE_VDPAU_SYMLINK:
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
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
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
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
            
        case FILE_TYPE_XLIB_SHARED_LIB:
        case FILE_TYPE_XLIB_STATIC_LIB:
        case FILE_TYPE_XLIB_SYMLINK:
            prefix = op->x_library_path;
            dir = path = "";
            break;

        case FILE_TYPE_XMODULE_SHARED_LIB:
        case FILE_TYPE_XMODULE_SYMLINK:
        case FILE_TYPE_XMODULE_NEWSYM:
            prefix = op->x_module_path;
            dir = "";
            path = p->entries[i].path;
            break;

        case FILE_TYPE_TLS_LIB:
        case FILE_TYPE_TLS_SYMLINK:
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
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
            prefix = op->utility_prefix;
            dir = op->utility_libdir;
            path = "";
            break;

        case FILE_TYPE_LIBGL_LA:
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = "";
            break;

        case FILE_TYPE_NVCUVID_LIB:
        case FILE_TYPE_NVCUVID_SYMLINK:
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
                prefix = op->compat32_prefix;
                dir = op->compat32_libdir;
            } else {
                prefix = op->opengl_prefix;
                dir = op->opengl_libdir;
            }
            path = "";
            break;

        case FILE_TYPE_OPENGL_HEADER:
            prefix = op->opengl_prefix;
            dir = op->opengl_incdir;
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
            prefix = op->documentation_prefix;
            dir = op->documentation_mandir;
            path = p->entries[i].path;
            break;

        case FILE_TYPE_UTILITY_BINARY:
        case FILE_TYPE_UTILITY_BIN_SYMLINK:
            prefix = op->utility_prefix;
            dir = op->utility_bindir;
            path = "";
            break;

        case FILE_TYPE_DOT_DESKTOP:
            xdg_data_dir = get_xdg_data_dir();
            if (xdg_data_dir) {
                prefix = xdg_data_dir;
                dir = nvstrdup("applications");
            } else {
                prefix = op->utility_prefix;
                dir = op->dot_desktopdir;
            }
            path = "";
            break;

        case FILE_TYPE_KERNEL_MODULE:
            
            /*
             * the kernel module dst field has already been
             * initialized in add_kernel_module_to_package()
             */
            
            continue;

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
        if ((p->entries[i].flags & FILE_CLASS_COMPAT32) &&
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
 * get_license_acceptance() - stat the license file to find out its
 * length, open the file, mmap it, and pass it to the ui for
 * acceptance.
 */

int get_license_acceptance(Options *op)
{
    struct stat buf;
    char *text, *tmp;
    int fd;

    /* trivial accept if the user accepted on the command line */

    if (op->accept_license) {
        ui_log(op, "License accepted by command line option.");
        return TRUE;
    }

    if ((fd = open(LICENSE_FILE, 0x0)) == -1) goto failed;
   
    if (fstat(fd, &buf) != 0) goto failed;
    
    if ((text = (char *) mmap(NULL, buf.st_size, PROT_READ,
                              MAP_FILE|MAP_SHARED,
                              fd, 0x0)) == (char *) -1) goto failed;
    
    /*
     * the mmap'ed license file may not be NULL terminated, so copy it
     * into a temporary buffer and explicity NULL terminate the string
     */

    tmp = nvalloc(buf.st_size + 1);
    memcpy(tmp, text, buf.st_size);
    tmp[buf.st_size] = '\0';
    
    if (!ui_display_license(op, tmp)) {
        ui_message(op, "License not accepted.  Aborting installation.");
        nvfree(tmp);
        munmap(text, buf.st_size);
        close(fd);
        return FALSE;
    }
    
    ui_log(op, "License accepted.");
    
    nvfree(tmp);
    munmap(text, buf.st_size);
    close(fd);
    
    return TRUE;

 failed:
    
    ui_error(op, "Unable to open License file '%s' (%s)",
             LICENSE_FILE, strerror(errno));
    return FALSE;

} /* get_license_acceptance() */



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

    get_x_library_and_module_paths(op);
    

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
 * add_kernel_module_to_package() - append the kernel module
 * (contained in p->kernel_module_build_directory) to the package list
 * for installation.
 */

int add_kernel_module_to_package(Options *op, Package *p)
{
    char *file, *name, *dst;

    file = nvstrcat(p->kernel_module_build_directory, "/",
                    p->kernel_module_filename, NULL);

    name = strrchr(file, '/');
    if (name) name++;
    if (!name) name = file;

    dst = nvstrcat(op->kernel_module_installation_path, "/",
                   p->kernel_module_filename, NULL);

    add_package_entry(p,
                      file,
                      NULL, /* path */
                      name,
                      NULL, /* target */
                      dst,
                      FILE_TYPE_KERNEL_MODULE,
                      0644);

    return TRUE;

} /* add_kernel_module_to_package() */



/*
 * remove_non_kernel_module_files_from_package() - clear the
 * FILE_TYPE_MASK bits for each package entry that is not of type
 * FILE_TYPE_KERNEL_MODULE
 */

void remove_non_kernel_module_files_from_package(Options *op, Package *p)
{
    int i;

    for (i = 0; i < p->num_entries; i++) {
        uint64_t flags = p->entries[i].flags & FILE_TYPE_MASK;
        if ((flags != FILE_TYPE_KERNEL_MODULE) &&
            (flags != FILE_TYPE_KERNEL_MODULE_CMD))
            p->entries[i].flags &= ~FILE_TYPE_MASK;
    }
    
} /* remove_non_kernel_module_files_from_package() */



/*
 * remove_trailing_slashes() - begin at the end of the given string,
 * and overwrite slashes with NULL as long as we find slashes.
 */

void remove_trailing_slashes(char *s)
{
    int len;

    if (s == NULL) return;
    len = strlen(s);

    while (s[len-1] == '/') s[--len] = '\0';
    
} /* remove_trailing_slashes() */



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
 * directory_exists() - 
 */

int directory_exists(Options *op, const char *dir)
{
    struct stat stat_buf;

    if ((stat (dir, &stat_buf) == -1) || (!S_ISDIR(stat_buf.st_mode))) {
        return FALSE;
    } else {
        return TRUE;
    }
} /* directory_exists() */



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
    /* return TRUE if the path already exists and is a directory */

    if (directory_exists(op, path)) return TRUE;
    
    if (ui_yes_no(op, TRUE, "The directory '%s' does not exist; "
                  "create?", path)) {
        if (mkdir_recursive(op, path, 0755)) {
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
 * directories as needed; this is equivalent to `mkdir -p`
 */

int mkdir_recursive(Options *op, const char *path, const mode_t mode)
{
    char *c, *tmp, ch;
    
    if (!path || !path[0]) return FALSE;
        
    tmp = nvstrdup(path);
    remove_trailing_slashes(tmp);
        
    c = tmp;
    do {
        c++;
        if ((*c == '/') || (*c == '\0')) {
            ch = *c;
            *c = '\0';
            if (!directory_exists(op, tmp)) {
                if (mkdir(tmp, mode) != 0) {
                    ui_error(op, "Failure creating directory '%s': (%s)",
                             tmp, strerror(errno));
                    free(tmp);
                    return FALSE;
                }
            }
            *c = ch;
        }
    } while (*c);

    free(tmp);
    return TRUE;

} /* mkdir_recursive() */



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
    
    if (!mkdir_recursive(op, dname, 0755)) {
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
    
    if (!mkdir_recursive(op, dname, 0755)) {
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
        if (tmpdirs[i] && directory_exists(op, tmpdirs[i])) {
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
    
    if (directory_exists(op, tmpdir)) {
        remove_directory(op, tmpdir);
    }
    
    if (!mkdir_recursive(op, tmpdir, 0655)) {
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

    char *data, *cmd;
    int i, ret;

    if (op->no_rpms) {
        ui_log(op, "Skipping check for conflicting rpms.");
        return TRUE;
    }

    for (i = 0; i < 2; i++) {
        
        cmd = nvstrcat("env LD_KERNEL_ASSUME=2.2.5 rpm --query ",
                       rpms[i], NULL);
        ret = run_command(op, cmd, NULL, FALSE, 0, TRUE);
        nvfree(cmd);

        if (ret == 0) {
            if (!ui_yes_no(op, TRUE, "An %s rpm appears to already be "
                           "installed on your system.  As part of installing "
                           "the new driver, this %s rpm will be uninstalled.  "
                           "Are you sure you want to continue? ('no' will "
                           "abort installation)", rpms[i], rpms[i])) {
                ui_log(op, "Installation aborted.");
                return FALSE;
            }
            
            cmd = nvstrcat("rpm --erase --nodeps ", rpms[i], NULL);
            ret = run_command(op, cmd, &data, op->expert, 0, TRUE);
            nvfree(cmd);
            
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
 * copy_directory_contents() - copy the contents of directory src to
 * directory dst.  This only copies files; subdirectories are ignored.
 */

int copy_directory_contents(Options *op, const char *src, const char *dst)
{
    DIR *dir;
    struct dirent *ent;
    char *srcfile, *dstfile;
    struct stat stat_buf;

    if ((dir = opendir(src)) == NULL) {
        ui_error(op, "Unable to open directory '%s' (%s).",
                 src, strerror(errno));
        return FALSE;
    }

    while ((ent = readdir(dir)) != NULL) {
        
        if (((strcmp(ent->d_name, ".")) == 0) ||
            ((strcmp(ent->d_name, "..")) == 0)) continue;
        
        srcfile = nvstrcat(src, "/", ent->d_name, NULL);

        /* only copy regular files */

        if ((stat(srcfile, &stat_buf) == -1) || !(S_ISREG(stat_buf.st_mode))) {
            nvfree(srcfile);
            continue;
        }
        
        dstfile = nvstrcat(dst, "/", ent->d_name, NULL);
        
        if (!copy_file(op, srcfile, dstfile, stat_buf.st_mode)) return FALSE;

        nvfree(srcfile);
        nvfree(dstfile);
    }

    if (closedir(dir) != 0) {
        ui_error(op, "Failure while closing directory '%s' (%s).",
                 src, strerror(errno));
        
        return FALSE;
    }
    
    return TRUE;
    
} /* copy_directory_contents() */



/*
 * pack_precompiled_kernel_interface() - 
 */

int pack_precompiled_kernel_interface(Options *op, Package *p)
{
    char *cmd, time_str[256], *proc_version_string;
    char *result, *descr;
    time_t t;
    struct utsname buf;
    int ret;

    ui_log(op, "Packaging precompiled kernel interface.");

    /* make sure the precompiled_kernel_interface_directory exists */

    mkdir_recursive(op, p->precompiled_kernel_interface_directory, 0755);
    
    /* use the time in the output string... should be fairly unique */

    t = time(NULL);
    snprintf(time_str, 256, "%lu", t);
    
    /* read the proc version string */

    proc_version_string = read_proc_version(op);

    /* use the uname string as the description */

    uname(&buf);
    descr = nvstrcat(buf.sysname, " ",
                     buf.release, " ",
                     buf.version, " ",
                     buf.machine, NULL);
    
    /* build the mkprecompiled command */

    cmd = nvstrcat("./mkprecompiled --interface=",
                   p->kernel_module_build_directory, "/",
                   PRECOMPILED_KERNEL_INTERFACE_FILENAME,
                   " --output=", p->precompiled_kernel_interface_directory,
                   "/", PRECOMPILED_KERNEL_INTERFACE_FILENAME,
                   "-", p->version, ".", time_str,
                   " --description=\"", descr, "\"",
                   " --proc-version=\"", proc_version_string, "\"",
                   " --version=", p->version, NULL);

    /* execute the command */
    
    ret = run_command(op, cmd, &result, FALSE, 0, TRUE);
    
    nvfree(cmd);
    nvfree(proc_version_string);
    nvfree(descr);
    
    /* remove the old kernel interface file */

    cmd = nvstrcat(p->kernel_module_build_directory, "/",
                   PRECOMPILED_KERNEL_INTERFACE_FILENAME, NULL);
    
    unlink(cmd); /* XXX what to do if this fails? */

    nvfree(cmd);
    
    if (ret != 0) {
        ui_error(op, "Unable to package precompiled kernel interface: %s",
                 result);
    }

    nvfree(result);

    if (ret == 0) return TRUE;
    else return FALSE;
    
} /* pack_kernel_interface() */



/*
 * nv_strreplace() - we can't assume that the user has sed installed
 * on their system, so use this function to preform simple string
 * search and replacement.  Returns a newly allocated string that is a
 * duplicate of src, with all instances of 'orig' replaced with
 * 'replace'.
 */

static char *nv_strreplace(char *src, char *orig, char *replace)
{
    char *prev_s, *end_s, *s;
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
            strncpy(d, replace, replace_len);
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
         * Replace any occurances of 'token' with 'replacement' in
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
 * process_libGL_la_files() - for any libGL.la files in the package,
 * copy them to a temporary file, replacing __GENERATED_BY__ and
 * __LIBGL_PATH__ as appropriate.  Then, add the new file to the
 * package list.
 */

void process_libGL_la_files(Options *op, Package *p)
{
    int i;
    char *tmpfile;

    char *tokens[3] = { "__LIBGL_PATH__", "__GENERATED_BY__", NULL };
    char *replacements[3] = { NULL, NULL, NULL };

    int package_num_entries = p->num_entries;

    replacements[1] = nvstrcat(PROGRAM_NAME, ": ",
                               NVIDIA_INSTALLER_VERSION, NULL);

    for (i = 0; i < package_num_entries; i++) {
        if ((p->entries[i].flags & FILE_TYPE_LIBGL_LA)) {
    
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
                replacements[0] = nvstrcat(op->compat32_prefix,
                                           "/", op->compat32_libdir, NULL);
            } else {
                replacements[0] = nvstrcat(op->opengl_prefix,
                                           "/", op->opengl_libdir, NULL);
            }

            /* invalidate the template file */

            p->entries[i].flags &= ~FILE_TYPE_MASK;
            p->entries[i].dst = NULL;

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
                                  p->entries[i].path,
                                  nvstrdup(p->entries[i].name),
                                  NULL, /* target */
                                  NULL, /* dst */
                                  ((p->entries[i].flags & FILE_CLASS_MASK) |
                                   FILE_TYPE_LIBGL_LA),
                                  p->entries[i].mode);
            }

            nvfree(replacements[0]);
        }
    }

    nvfree(replacements[1]);

} /* process_libGL_la_files() */



/*
 * process_dot_desktop_files() - for any .desktop files in the
 * package, copy them to a temporary file, replacing __UTILS_PATH__
 * and __PIXMAP_PATH__ as appropriate.  Then, add the new file to
 * the package list.
 */

void process_dot_desktop_files(Options *op, Package *p)
{
    int i;
    char *tmpfile;

    char *tokens[3] = { "__UTILS_PATH__", "__PIXMAP_PATH__", NULL };
    char *replacements[3] = { NULL, NULL, NULL };

    int package_num_entries = p->num_entries;

    replacements[0] = nvstrcat(op->utility_prefix,
                               "/", op->utility_bindir, NULL);

    remove_trailing_slashes(replacements[0]);
    collapse_multiple_slashes(replacements[0]);

    for (i = 0; i < package_num_entries; i++) {
        if ((p->entries[i].flags & FILE_TYPE_DOT_DESKTOP)) {
    
            /* invalidate the template file */

            p->entries[i].flags &= ~FILE_TYPE_MASK;
            p->entries[i].dst = NULL;

            nvfree(replacements[1]);

            replacements[1] = nvstrcat(op->documentation_prefix,
                                       "/", op->documentation_docdir,
                                       "/", p->entries[i].path,
                                       NULL);

            remove_trailing_slashes(replacements[1]);
            collapse_multiple_slashes(replacements[1]);

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
                                  p->entries[i].path,
                                  nvstrdup(p->entries[i].name),
                                  NULL, /* target */
                                  NULL, /* dst */
                                  ((p->entries[i].flags & FILE_CLASS_MASK) |
                                   FILE_TYPE_DOT_DESKTOP),
                                  p->entries[i].mode);
            }
        }
    }

    nvfree(replacements[0]);
    nvfree(replacements[1]);

} /* process_dot_desktop_files() */


/*
 * set_security_context() - set the security context of the file to 'shlib_t'
 * Returns TRUE on success or if SELinux is disabled, FALSE otherwise
 */
int set_security_context(Options *op, const char *filename) 
{
    char *cmd = NULL;
    int ret = FALSE;
    
    if (op->selinux_enabled == FALSE) {
        return TRUE;
    } 
    
    cmd = nvstrcat(op->utils[CHCON], " -t ", op->selinux_chcon_type, " ",
                   filename, NULL);
    
    ret = run_command(op, cmd, NULL, FALSE, 0, TRUE);
    
    ret = ((ret == 0) ? TRUE : FALSE);
    if (cmd) nvfree(cmd);
    
    return ret;
} /* set_security_context() */


/*
 * get_default_prefixes_and_paths() - assign the default prefixes and
 * paths depending on the architecture, distribution and the X.Org
 * version installed on the system.
 */

void get_default_prefixes_and_paths(Options *op)
{
    char *default_libdir;
    
#if defined(NV_X86_64)
    if ((op->distro == DEBIAN) ||
        (op->distro == UBUNTU)) {
        default_libdir = DEBIAN_DEFAULT_64BIT_LIBDIR;
    } else {
        default_libdir = DEFAULT_64BIT_LIBDIR;
    }
#else
    default_libdir = DEFAULT_LIBDIR;
#endif

    if (!op->opengl_prefix)
        op->opengl_prefix = DEFAULT_OPENGL_PREFIX;
    if (!op->opengl_libdir)
        op->opengl_libdir = default_libdir;
    if (!op->opengl_incdir)
        op->opengl_incdir = DEFAULT_INCDIR;

    if (!op->x_prefix) {
        if (op->modular_xorg) {
            op->x_prefix = XORG7_DEFAULT_X_PREFIX;
        } else {
            op->x_prefix = DEFAULT_X_PREFIX;
        }
    }
    if (!op->x_libdir)
        op->x_libdir = default_libdir;
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

    if (!op->compat32_libdir) {
        if ((op->distro == UBUNTU) ||
            (op->distro == GENTOO)) {
            op->compat32_libdir = UBUNTU_DEFAULT_COMPAT32_LIBDIR;
        } else {
            op->compat32_libdir = DEFAULT_LIBDIR;
        }
    }

    if (op->distro == DEBIAN && !op->compat32_chroot) {
        /*
         * Newer versions of Debian have moved to the Ubuntu style of compat32
         * path, so use that if it exists.
         */
        char *default_path = nvstrcat(op->compat32_prefix, "/"
                                      UBUNTU_DEFAULT_COMPAT32_LIBDIR, NULL);
        struct stat buf;
        if (lstat(default_path, &buf) < 0 || S_ISLNK(buf.st_mode)) {
            /*
             * Path doesn't exist or is a symbolic link.  Use the compat32
             * chroot path instead.
             */
            op->compat32_chroot = DEBIAN_DEFAULT_COMPAT32_CHROOT;
        } else {
            /* <prefix>/lib32 exists, so use that */
            op->compat32_libdir = UBUNTU_DEFAULT_COMPAT32_LIBDIR;
        }
        nvfree(default_path);
    }

#endif

    if (!op->utility_prefix)
        op->utility_prefix = DEFAULT_UTILITY_PREFIX;
    if (!op->utility_libdir)
        op->utility_libdir = default_libdir;
    if (!op->utility_bindir)
        op->utility_bindir = DEFAULT_BINDIR;

    if (!op->dot_desktopdir)
        op->dot_desktopdir = DEFAULT_DOT_DESKTOPDIR;

    if (!op->documentation_prefix)
        op->documentation_prefix = DEFAULT_DOCUMENTATION_PREFIX;
    if (!op->documentation_docdir)
        op->documentation_docdir = DEFAULT_DOCDIR;
    if (!op->documentation_mandir)
        op->documentation_mandir = DEFAULT_MANDIR;

} /* get_default_prefixes_and_paths() */


/*
 * get_xdg_data_dir() - determine if the XDG_DATA_DIRS environment
 * variable is set; if set and not empty, return the first path
 * in the list, else return NULL.
 */

static char *get_xdg_data_dir(void)
{
    /*
     * If XDG_DATA_DIRS is set, then derive the installation path
     * from the first entry; complies with:
     *   http://www.freedesktop.org/Standards/basedir-spec
     */
    char *xdg_data_dir = getenv("XDG_DATA_DIRS");
    if ((xdg_data_dir != NULL) && strlen(xdg_data_dir))
        return nvstrdup(strtok(xdg_data_dir, ":"));
    return NULL;
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



/*
 * get_x_paths_helper() - helper function for determining the X
 * library and module paths; returns 'TRUE' if we had to guess at the
 * path
 */

static int get_x_paths_helper(Options *op,
                              int library,
                              char *xserver_cmd,
                              char *pkg_config_cmd,
                              char *name,
                              char **path)
{
    char *dirs, *cmd, *dir, *next;
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
     *
     * xorg-server >= 1.3 has a stupid version number regression because the
     * reported Xorg version was tied to the server version, meaning what used
     * to report 7.2 now reports, for example, 1.2.99.903.  This means we set
     * op->modular_xorg to FALSE.  However, any server with this regression will
     * also support the -showDefaultModulePath option so we should still be able
     * to get the right path.
     */

    /*
     * first, try the X server commandline option; this is the
     * recommended query mechanism as of X.Org 7.2
     */
    if (op->utils[XSERVER]) {

        dirs = NULL;
        cmd = nvstrcat(op->utils[XSERVER], " ", xserver_cmd, NULL);
        ret = run_command(op, cmd, &dirs, FALSE, 0, TRUE);
        nvfree(cmd);

        if ((ret == 0) && dirs) {
            
            next = NULL;

            dir = extract_x_path(dirs, &next);
            
            while (dir) {
                
                if (directory_exists(op, dir)) {
                    
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
        cmd = nvstrcat(op->utils[PKG_CONFIG], " ",
                pkg_config_cmd, NULL);
        ret = run_command(op, cmd, &dirs, FALSE, 0, TRUE);
        nvfree(cmd);

        if ((ret == 0) && dirs) {

            next = NULL;
            
            dir = extract_x_path(dirs, &next);
 
            while (dir) {
                
                if (directory_exists(op, dir)) {

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
    
    if (library) {
        *path = nvstrcat(op->x_prefix, "/", op->x_libdir, NULL);
    } else {
        *path = nvstrcat(op->x_library_path, "/", op->x_moddir, NULL);
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
                                  TRUE,
                                  "-showDefaultLibPath",
                                  "--variable=libdir xorg-server",
                                  "library",
                                  &op->x_library_path);
    
    guessed |= get_x_paths_helper(op,
                                  FALSE,
                                  "-showDefaultModulePath",
                                  "--variable=moduledir xorg-server",
                                  "module",
                                  &op->x_module_path);
    
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
