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
