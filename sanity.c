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
 * sanity.c
 */

#include "nvidia-installer.h"
#include "command-list.h"
#include "user-interface.h"
#include "backup.h"
#include "misc.h"
#include "sanity.h"

#include <sys/types.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


/*
 * sanity() - perform sanity tests on an existing installation
 */

int sanity(Options *op)
{
    char *descr;
    int major, minor, patch;

    /* check that there's a driver installed at all */

    descr = get_installed_driver_version_and_descr(op, &major, &minor, &patch);
    
    if (!descr) {
        ui_error(op, "Unable to find any installed NVIDIA driver.  The sanity "
                 "check feature is only intended to be used with an existing "
                 "NVIDIA driver installation.");
        return FALSE;
    }

    ui_message(op, "The currently installed driver is: '%s' "
               "(version: %d.%d-%d).  nvidia-installer will now check "
               "that all installed files still exist.",
               descr, major, minor, patch);

    /* check that all the files are still where we placed them */

    if (!test_installed_files(op)) {
        ui_message(op, "The '%s' installation has been altered "
                   "since it was originally installed.  It is recommended "
                   "that you reinstall.", descr);
        return FALSE;
    }

    /* check that shared memory works */

    if (!check_sysvipc(op)) return FALSE;

    /*
     * XXX There are lots of additional tests that could be added:
     *
     * - check for any conflicting libraries
     *
     * - check that the permissions on the /dev/nvidia* files haven't
     *   been screwed up by pam
     *
     * - check that /dev/zero has appropriate permissions
     *
     * - check for possible kernel config problems (IPC, mtrr support,
     *   etc).
     */
    
    ui_message(op, "'%s' (version: %d.%d-%d) appears to be installed "
               "correctly.", descr, major, minor, patch);
    
    nvfree(descr);
    
    return TRUE;
    
} /* sanity() */


/*
 * check_sysvipc() - test that shmat() and friends work
 */

int check_sysvipc(Options *op)
{
    int shmid = -1;
    int ret = FALSE;
    int size = sysconf(_SC_PAGESIZE);
    void *address = (void *) -1;

    shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
    if (shmid == -1) goto done;

    address = shmat(shmid, 0, 0);
    if (address == (void *) -1) goto done;

    ret = TRUE;

 done:

    if (shmid != -1) shmctl(shmid, IPC_RMID, 0);
    if (address != (void *) -1) shmdt(address);

    if (ret) {
        ui_log(op, "Shared memory test passed.");
    } else {
        ui_message(op, "Shared memory test failed (%s): please check that "
                   "your kernel has CONFIG_SYSVIPC enabled.", strerror(errno));
    }
    
    return ret;

} /* check_sysvipc() */


#if 0


/*
 * XXX don't have time to finish implementing and testing this right
 * now...
 */




/*
 * find_conflicting_libraries() - 
 */

static int find_conflicting_libraries(Options *op)
{
    FileList *l;
    struct stat stat_buf;
    int i;
    
    l = (FileList *) nvalloc(sizeof(FileList));

    /* search for possibly conflicting libraries */
    
    find_conflicting_xfree86_libraries(op, DEFAULT_XFREE86_INSTALLATION_PREFIX, l);
    
    if (strcmp(DEFAULT_XFREE86_INSTALLATION_PREFIX, op->xfree86_prefix) != 0)
        find_conflicting_xfree86_libraries(op, op->xfree86_prefix, l);
    
    find_conflicting_opengl_libraries(op, DEFAULT_OPENGL_INSTALLATION_PREFIX, l);
    
    if (strcmp(DEFAULT_OPENGL_INSTALLATION_PREFIX, op->opengl_prefix) != 0)
        find_conflicting_opengl_libraries(op, op->opengl_prefix, l);

#if defined(NV_X86_64)
    if (op->compat32_prefix != NULL) {
        char *prefix = nvstrcat(op->compat32_prefix,
                                DEFAULT_OPENGL_INSTALLATION_PREFIX, NULL);
        find_conflicting_opengl_libraries(op, prefix, l);
        nvfree(prefix);

        if (strcmp(DEFAULT_OPENGL_INSTALLATION_PREFIX,
            op->opengl_prefix) != 0) {
            prefix = nvstrcat(op->compat32_prefix, op->opengl_prefix, NULL);
            find_conflicting_opengl_libraries(op, prefix, l);
            nvfree(prefix);
        }
    }
#endif /* NV_X86_64 */
    
    /* condense the file list */

    condense_file_list(l);

    /* for each file in the list, check if it's an NVIDIA file */

    for (i = 0; i < l->num; i++) {
        if (lstat(l->filename[i], &stat_buf) == -1) {
            ui_error(op, "Unable to determine properties for file '%s' (%s).",
                     l->filename[i], strerror(errno));
            continue;
        }

        if (S_ISREG(stat_buf.st_mode)) {
            ret = is_nvidia_library(op, l->filename[i]);
        } else if (S_ISLNK(stat_buf.st_mode)) {
            ret = is_nvidia_symlink(op, l->filename[i]);
        }
    }




    /* free the FileList */

    for (i = 0; i < l->num; i++) free(l->filename[i]);
    free(l->filename);
    free(l);

} /* find_conflicting_libraries() */



/*
 * is_nvidia_library() - mmap the file and scan through it for the
 * nvidia string.
 */

static int is_nvidia_library(Options *op, const char *filename)
{
    int fd = -1, ret = FALSE;
    struct stat stat_buf;
    char *buf = (void *) -1, char *found;

    if ((fd = open(filename, O_RDONLY)) == -1) {
        ui_error(op, "Unable to open '%s' for reading (%s)",
                 filename, strerror(errno));
        goto done:
    }

    if (fstat(fd, &stat_buf) == -1) {
        ui_error(op, "Unable to determine size of '%s' (%s)",
                 filename, strerror(errno));
        goto done;
    }

    if ((buf = mmap(0, stat_buf.st_size, PROT_READ,
                    MAP_FILE | MAP_SHARED, fd, 0)) == (void *) -1) {
        ui_error(op, "Unable to map file '%s' for reading (%s)",
                 filename, strerror(errno));
        goto done;
    }

    found = strstr(buf, "nvidia id: ");
    
    if (found) ret = TRUE;
    
 done:

    if (buf != (void *) -1) munmap(buf, stat_buf.st_size);
    if (fd != -1) close(fd);

    return ret;
    
} /* is_nvidia_library() */


/*
 * is_nvidia_symlink() - returns TRUE if this symlink should be moved
 * out of the way.  Find the target of the symlink, and recursively
 * call is_nvidia_symlink() if the target is a symlink, or call
 * is_nvidia_library() if the target is a regular file.
 *
 * XXX do we need to do anything about cyclic links?
 */

static int is_nvidia_symlink(Options *op, const char *filename)
{
    char *tmp, *tmp2, *dir, *target;
    int ret = TRUE;
    struct stat stat_buf;

    tmp = get_symlink_target(op, filename);
    if (!tmp) return FALSE;
    
    /*
     * prepend the basename of the file, unless the target is an
     * abosolute path
     */

    if (tmp[0] != '/') {
        tmp2 = nvstrdup(tmp);
        dir = dirname(tmp2);
        target = nvstrcat(dir, "/", tmp, NULL);
        nvfree(tmp);
        nvfree(tmp2);
    } else {
        target = tmp;
    }


    if (lstat(target, &stat_buf) == -1) {
        ui_error(op, "Unable to determine properties for file '%s' (%s).",
                 target, strerror(errno));
        return TRUE; /* return TRUE so that we don't try to back it up */
    }

    if (S_ISREG(stat_buf.st_mode)) {
        ret = is_nvidia_library(op, target);
    } else if (S_ISLNK(stat_buf.st_mode)) {
        ret = is_nvidia_symlink(op, target);
    }
    
    nvfree(target);

    return ret;

} /* is_nvidia_symlink() */


#endif
