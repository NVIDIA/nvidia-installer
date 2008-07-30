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


static int get_latest_driver_version_and_filename(Options *op,
                                                  char **, char **);



/*
 * update() - determine if there is a more recent driver available; if
 * so, download it and install it.
 */

int update(Options *op)
{
    char *descr = NULL;
    char *filename = NULL;
    char *tmpfile = NULL;
    char *url = NULL;
    char *cmd;
    char *installedVersion = NULL;
    char *latestVersion = NULL;
    int fd, installedRet, latestRet, localRet;
    int ret = FALSE;

    installedRet = get_installed_driver_version_and_descr(op,
                                                          &installedVersion,
                                                          &descr);
    
    latestRet = get_latest_driver_version_and_filename(op, &latestVersion,
                                                       &filename);
    if (!latestRet) {
        goto done;
    }
    
    if (installedRet && !op->force_update) {

        /*
         * if the currently installed driver version is the same as
         * the latest, don't update.
         */
        
        if (strcmp(installedVersion, latestVersion) == 0) {
            ui_message(op, "The latest %s (version %s) is already "
                       "installed.", descr, installedVersion);
            ret = TRUE;
            goto done;
        }
    }
    
    /* build the temporary file and url strings */
    
    tmpfile = nvstrcat(op->tmpdir, "/nv-update-XXXXXX", NULL);
    url = nvstrcat(op->ftp_site, "/XFree86/", INSTALLER_OS, "-",
                   INSTALLER_ARCH, "/", filename, NULL);
    
    /* create the temporary file */

    if ((fd = mkstemp(tmpfile)) == -1) {
        ui_error(op, "Unable to create temporary file (%s)", strerror(errno));
        goto done;
    }

    /* download the file */

    if (!snarf(op, url, fd, SNARF_FLAGS_STATUS_BAR)) {
        ui_error(op, "Unable to download driver %s.", url);
        goto done;
    }
    
    close(fd);

    /* XXX once we setup gpg validate the binary here */

    /* check the binary */

    cmd = nvstrcat("sh ", tmpfile, " --check", NULL);
    localRet = run_command(op, cmd, NULL, FALSE, FALSE, TRUE);
    nvfree(cmd);

    if (localRet != 0) {
        ui_error(op, "The downloaded file does not pass its integrity check.");
        goto done;
    }

    /*
     * We're ready to execute the binary; first, close down the ui so
     * that the new installer can take over.
     */
    
    ui_close(op);

    /* execute `sh <downloaded file> <arguments>` */

    cmd = nvstrcat("sh ", tmpfile, " ", op->update_arguments, NULL);
    localRet = system(cmd);
    nvfree(cmd);
    
    /* remove the downloaded file */
    
    unlink(tmpfile);
    
    /*
     * we've already shut down the ui, so no need to return from this
     * function.
     */

    exit(localRet);

    ret = TRUE;
    
 done:
    
    nvfree(installedVersion);
    nvfree(descr);
    nvfree(latestVersion);
    nvfree(filename);
    
    nvfree(tmpfile);
    nvfree(url);

    return ret;

} /* update() */



/*
 * report_latest_driver_version() - 
 */

int report_latest_driver_version(Options *op)
{
    char *descr = NULL;
    char *filename = NULL;
    char *url = NULL;
    char *installedVersion = NULL;
    char *latestVersion = NULL;
    int installedRet, latestRet;

    installedRet = get_installed_driver_version_and_descr(op,
                                                          &installedVersion,
                                                          &descr);
    
    latestRet = get_latest_driver_version_and_filename(op, &latestVersion,
                                                       &filename);
    
    if (!latestRet) {
        nvfree(descr);
        nvfree(installedVersion);
        return FALSE;
    }

    url = nvstrcat(op->ftp_site, "/XFree86/", INSTALLER_OS, "-",
                   INSTALLER_ARCH, "/", filename, NULL);
    
    if (installedRet) {
        ui_message(op, "Currently installed version: %s; "
                   "latest available version: %s; latest driver "
                   "file: %s.", installedVersion, latestVersion, url);
    } else {
        ui_message(op, "Latest version: %s; latest driver file: %s.",
                   latestVersion, url);
    }
    
    nvfree(descr);
    nvfree(installedVersion);
    nvfree(filename);
    nvfree(latestVersion);
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
 * get_latest_driver_version() - download and parse the latest.txt
 * file; the format of this file is:
 *
 *   [old format version] [path to .run file]
 *   [new format version]
 *
 * This is done for backwards compatibility -- old nvidia-installers
 * will read only the first line and parse the old format version
 * string; new nvidia-installers will try to find the version string
 * on the second line.  If we are unable to find the version string on
 * the second line, then fall back to the old format string on the
 * first line.
 */

static int get_latest_driver_version_and_filename(Options *op,
                                                  char **pVersion,
                                                  char **pFileName)
{
    int fd = -1;
    int length;
    int ret = FALSE;
    char *tmpfile = NULL;
    char *url = NULL;
    char *str = (void *) -1;
    char *s = NULL;
    char *buf = NULL;
    char *buf2 = NULL;
    char *ptr;
    char *version = NULL;
    struct stat stat_buf;
    
    tmpfile = nvstrcat(op->tmpdir, "/nv-latest-XXXXXX", NULL);
    url = nvstrcat(op->ftp_site, "/XFree86/", INSTALLER_OS, "-",
                   INSTALLER_ARCH, "/latest.txt", NULL);
    
    /* check for no_network option */

    if (op->no_network) {
        ui_error(op, "Unable to determine most recent NVIDIA %s-%s driver "
                   "version: cannot access '%s', because the '--no-network' "
                   "commandline option was specified.", INSTALLER_OS, INSTALLER_ARCH, url);
        goto done;
    }
    
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
    
    
    /*
     * read in the first two lines from the file; the second line may
     * optionally contain a version string with the new format
     */

    buf = get_next_line(str, &ptr, str, length);
    buf2 = get_next_line(ptr, NULL, str, length);

    version = extract_version_string(buf2);
    if (!version) version = extract_version_string(buf);
    
    if (!version) {
        ui_error(op, "Unable to determine latest NVIDIA driver "
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
    *pFileName = nvstrdup(s);
    *pVersion = strdup(version);

    ret = TRUE;

 done:
    
    nvfree(buf);
    nvfree(buf2);
    if (str != (void *) -1) munmap(str, stat_buf.st_size);
    if (fd != -1) close(fd);

    unlink(tmpfile);

    nvfree(tmpfile);
    nvfree(url);
    nvfree(version);

    return ret;

} /* get_latest_driver_version() */
