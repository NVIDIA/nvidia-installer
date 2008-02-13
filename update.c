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
 * update.c
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "nvidia-installer.h"
#include "misc.h"
#include "snarf.h"
#include "backup.h"
#include "user-interface.h"
#include "update.h"


static char *get_latest_driver_version_and_filename(Options *op,
                                                    int *, int *, int *);



/*
 * update() - determine if there is a more recent driver available; if
 * so, download it and install it.
 */

int update(Options *op)
{
    char *descr, *filename, *tmpfile, *url, *cmd;
    int x0, y0, z0, x1, y1, z1, ret, fd;

    descr = get_installed_driver_version_and_descr(op, &x0, &y0, &z0);
    
    filename = get_latest_driver_version_and_filename(op, &x1, &y1, &z1);
    if (!filename) return FALSE;

    if (descr && !op->force_update) {

        /*
         * if the currently installed driver version is the same as
         * the latest, don't update.
         */

        if ((x0 == x1) && (y0 == y1) && (z0 == z1)) {
            ui_message(op, "The latest %s (version %d.%d-%d) is already "
                       "installed.", descr, x0, y0, z0);
            nvfree(descr);
            return TRUE;
        }
    }
    
    /* build the temporary file and url strings */
    
    tmpfile = nvstrcat(op->tmpdir, "/nv-update-XXXXXX", NULL);
    url = nvstrcat(op->ftp_site, "/XFree86/", INSTALLER_OS, "-",
                   INSTALLER_ARCH, "/", filename, NULL);
    nvfree(filename);

    /* create the temporary file */

    if ((fd = mkstemp(tmpfile)) == -1) {
        ui_error(op, "Unable to create temporary file (%s)", strerror(errno));
        return FALSE;
    }

    /* download the file */

    if (!snarf(op, url, fd, SNARF_FLAGS_STATUS_BAR)) {
        ui_error(op, "Unable to download driver %s.", url);
        return FALSE;
    }
    
    close(fd);

    /* XXX once we setup gpg validate the binary here */

    /* check the binary */

    cmd = nvstrcat("sh ", tmpfile, " --check", NULL);
    ret = run_command(op, cmd, NULL, FALSE, FALSE, TRUE);
    nvfree(cmd);

    if (ret != 0) {
        ui_error(op, "The downloaded file does not pass its integrety check.");
        return FALSE;
    }

    /*
     * We're ready to execute the binary; first, close down the ui so
     * that the new installer can take over.
     */
    
    ui_close(op);

    /* execute `sh <downloaded file> <arguments>` */

    cmd = nvstrcat("sh ", tmpfile, " ", op->update_arguments, NULL);
    ret = system(cmd);
    nvfree(cmd);
    
    /* remove the downloaded file */
    
    unlink(tmpfile);
    
    /*
     * we've already shut down the ui, so no need to return from this
     * function.
     */

    exit(ret);

    return TRUE;
    
} /* update() */



/*
 * report_latest_driver_version() - 
 */

int report_latest_driver_version(Options *op)
{
    char *descr, *filename, *url;
    int x0, y0, z0, x1, y1, z1;

    descr = get_installed_driver_version_and_descr(op, &x0, &y0, &z0);
    
    filename = get_latest_driver_version_and_filename(op, &x1, &y1, &z1);

    if (!filename) {
        nvfree(descr);
        return FALSE;
    }

    url = nvstrcat(op->ftp_site, "/XFree86/", INSTALLER_OS, "-",
                   INSTALLER_ARCH, "/", filename, NULL);
    
    if (descr) {
        ui_message(op, "Currently installed version: %d.%d-%d; "
                   "latest available version: %d.%d-%d; latest driver "
                   "file: %s.", x0, y0, z0, x1, y1, z1, url);
        nvfree(descr);
    } else {
        ui_message(op, "Latest version: %d.%d-%d; latest driver file: %s.",
                   x1, y1, z1, url);
    }
    
    nvfree(filename);
    nvfree(url);
    
    return TRUE;

} /* report_latest_driver_version() */



/*
 * append_update_arguments() - append the specified argument to the
 * update_arguments string.
 */

char *append_update_arguments(char *s, int c, const char *arg,
                              struct option l[])
{
    char *t;
    int i = 0;

    if (!s) s = nvstrcat(" ", NULL);
    
    do {
        if (l[i].val == c) {
            t = nvstrcat(s, " --", l[i].name, NULL);
            nvfree(s);
            s = t;
            if (l[i].has_arg) {
                t = nvstrcat(s, "=", arg, NULL);
                nvfree(s);
                s = t;
            }
            return (s);
        }
    } while (l[++i].name);
    
    return s;
    
} /* append_update_arguments() */



/*
 * get_latest_driver_version() - 
 */

static char *get_latest_driver_version_and_filename(Options *op, int *major,
                                                    int *minor, int *patch)
{
    int fd = -1;
    int length;
    char *tmpfile = NULL;
    char *url = NULL;
    char *str = (void *) -1;
    char *s = NULL;
    char *buf = NULL;
    char *filename = NULL;
    struct stat stat_buf;
    
    tmpfile = nvstrcat(op->tmpdir, "/nv-latest-XXXXXX", NULL);
    url = nvstrcat(op->ftp_site, "/XFree86/", INSTALLER_OS, "-",
                   INSTALLER_ARCH, "/latest.txt", NULL);
    
    if ((fd = mkstemp(tmpfile)) == -1) {
        ui_error(op, "Unable to create temporary file (%s)", strerror(errno));
        goto done;
    }

    if (!snarf(op, url, fd, SNARF_FLAGS_DOWNLOAD_SILENT)) {
        ui_error(op, "Unable to determine most recent NVIDIA %s-%s driver "
                 "version.", INSTALLER_OS, INSTALLER_ARCH);
        goto done;
    }
    
    if (fstat(fd, &stat_buf) == -1) {
        ui_error(op, "Unable to determine most recent NVIDIA %s-%s driver "
                 "version (%s).", INSTALLER_OS, INSTALLER_ARCH,
                 strerror(errno));
        goto done;
    }

    length = stat_buf.st_size;
    
    str = mmap(0, length, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
    if (str == (void *) -1) {
        ui_error(op, "Unable to determine most recent NVIDIA %s-%s driver "
                 "version (%s).", INSTALLER_OS, INSTALLER_ARCH,
                 strerror(errno));
        goto done;
    }
    
    buf = get_next_line(str, NULL, str, length);

    if (!nvid_version(buf, major, minor, patch)) {
        ui_error(op, "Unable to determine latest NVIDIA %s-%s driver "
                 "version (no version number found in %s)", url);
        goto done;
    }
    
    /* everything after the space is the filename */

    s = strchr(buf, ' ');
    if (!s) {
        ui_error(op, "Unable to read filename from %s.", url);
        goto done;
    }

    s++;
    filename = nvstrdup(s);

 done:
    
    if (buf) nvfree(buf);
    if (str != (void *) -1) munmap(str, stat_buf.st_size);
    if (fd != -1) close(fd);

    unlink(tmpfile);

    if (tmpfile) nvfree(tmpfile);
    if (url) nvfree(url);

    return filename;

} /* get_latest_driver_version() */
