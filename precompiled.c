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
 * precompiled.c - this source file contains functions for dealing
 * with precompiled kernel interfaces.
 *
 * XXX portions of these functions are lifted from mkprecompiled (it
 * was much easier to duplicate them than to finesse the code to be
 * shared between the installer and mkprecompiled).
 */


#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "precompiled.h"
#include "misc.h"
#include "crc.h"

#define PRECOMPILED_CONSTANT_LENGTH (8 + 4 + 12 + 4 + 4)

/*
 * decode_uint32() - given an index into a buffer, read the next 4
 * bytes, and build a uint32.
 */

static uint32 decode_uint32(char *buf)
{
    uint32 ret = 0;

    ret += (uint32) buf[3];
    ret <<= 8;

    ret += (uint32) buf[2];
    ret <<= 8;

    ret += (uint32) buf[1];
    ret <<= 8;

    ret += (uint32) buf[0];
    ret <<= 0;

    return ret;

} /* decode_uint32() */



/*
 * read_proc_version() - return the string contained in /proc/version
 *
 * XXX could use getmntent() to determine where the proc filesystem is
 * mounted.
 */

char *read_proc_version(Options *op)
{
    int fd, ret, len, version_len;
    char *version, *c = NULL;
    char *proc_verson_filename;
    
    len = strlen(op->proc_mount_point) + 9; /* strlen("/version") + 1 */
    proc_verson_filename = (char *) nvalloc(len);
    snprintf(proc_verson_filename, len, "%s/version", op->proc_mount_point);

    if ((fd = open(proc_verson_filename, O_RDONLY)) == -1) {
        ui_warn(op, "Unable to open the file '%s' (%s).",
                proc_verson_filename, strerror(errno));
        return NULL;
    }

    /*
     * it would be more convenient if we could just mmap(2)
     * /proc/version, but proc files do not support mmap(2), so read
     * the file instead
     */
    
    len = version_len = 0;
    version = NULL;

    while (1) {
        if (version_len == len) {
            version_len += NV_LINE_LEN;
            version = nvrealloc(version, version_len);
            c = version + len;
        }
        ret = read(fd, c, version_len - len);
        if (ret == -1) {
            ui_warn(op, "Error reading %s (%s).",
                    proc_verson_filename, strerror(errno));
            free(version);
            return NULL;
        }
        if (ret == 0) {
            *c = '\0';
            break;
        }
        len += ret;
        c += ret;
    }

    /* replace a newline with a null-terminator */

    c = version;
    while ((*c != '\0') && (*c != '\n')) c++;
    *c = '\0';

    free(proc_verson_filename);

    return version;

} /* read_proc_version() */



/*
 * precompiled_unpack() - unpack the specified package.  It's not
 * really an error if we can't open the file or if it's not the right
 * format, so just throw an expert-only log message.
 */

PrecompiledInfo *precompiled_unpack(Options *op,
                                    const char *filename,
                                    const char *output_filename,
                                    const char *real_proc_version_string,
                                    const int package_major,
                                    const int package_minor,
                                    const int package_patch)
{
    int dst_fd, fd, offset, len = 0;
    char *buf, *dst;
    uint32 crc, major, minor, patch, val, size;
    char *description, *proc_version_string;
    struct stat stat_buf;
    PrecompiledInfo *info = NULL;

    fd = dst_fd = size = 0;
    buf = dst = description = proc_version_string = NULL;
    
    /* open the file to be unpacked */
    
    if ((fd = open(filename, O_RDONLY)) == -1) {
        ui_expert(op, "Unable to open precompiled kernel interface file "
                  "'%s' (%s)", filename, strerror(errno));
        goto done;
    }
    
    /* get the file length */
    
    if (fstat(fd, &stat_buf) == -1) {
        ui_expert(op, "Unable to determine '%s' file length (%s).",
                  filename, strerror(errno));
        goto done;
    }
    size = stat_buf.st_size;

    /* check for a minimum length */

    if (size < PRECOMPILED_CONSTANT_LENGTH) {
        ui_expert(op, "File '%s' appears to be too short.", filename);
        goto done;
    }
    
    /* mmap(2) the input file */

    buf = mmap(0, size, PROT_READ, MAP_FILE|MAP_SHARED, fd, 0);
    if (buf == (void *) -1) {
        ui_expert(op, "Unable to mmap file %s (%s).",
                  filename, strerror(errno));
        goto done;
    }
    offset = 0;

    /* check for the header */

    if (strncmp(buf + offset, "NVIDIA  ", 8) != 0) {
        ui_expert(op, "File '%s': unrecognized file format.", filename);
        goto done;
    }
    offset += 8;

    /* read the crc */

    crc = decode_uint32(buf + offset);
    offset += 4;
    
    /* read the version */

    major = decode_uint32(buf + offset + 0);
    minor = decode_uint32(buf + offset + 4);
    patch = decode_uint32(buf + offset + 8);
    offset += 12;

    /* check if this precompiled kernel interface is the right driver
       version */

    if ((major != package_major) ||
        (minor != package_minor) ||
        (patch != package_patch)) {
        goto done;
    }


    /* read the description */

    val = decode_uint32(buf + offset);
    offset += 4;
    if ((val + PRECOMPILED_CONSTANT_LENGTH) > size) {
        ui_expert(op, "Invalid file '%s' (bad description string length %d).",
                  filename, val);
        goto done;
    }
    if (val > 0) {
        description = nvalloc(val+1);
        memcpy(description, buf + offset, val);
        description[val] = '\0';
    } else {
        description = NULL;
    }
    offset += val;
    
    /* read the proc version string */

    val = decode_uint32(buf + offset);
    offset += 4;
    if ((val + PRECOMPILED_CONSTANT_LENGTH) > size) {
        ui_expert(op, "Invalid file '%s' (bad version string length %d).",
                  filename, val);
        goto done;
    }
    proc_version_string = nvalloc(val+1);
    memcpy(proc_version_string, buf + offset, val);
    offset += val;
    proc_version_string[val] = '\0';

    /* check if the running kernel matches */

    if (strcmp(real_proc_version_string, proc_version_string) != 0) {
        goto done;
    }
    
    ui_log(op, "A precompiled kernel interface for kernel '%s' has been "
           "found here: %s.", description, filename);
    
    /* extract kernel interface module */
    
    len = size - offset;

    if ((dst_fd = open(output_filename, O_CREAT | O_RDWR | O_TRUNC,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
        ui_error(op, "Unable to open output file '%s' (%s).", output_filename,
                 strerror(errno));
        goto done;
    }
    
    /* set the output file length */

    if ((lseek(dst_fd, len - 1, SEEK_SET) == -1) ||
        (write(dst_fd, "", 1) == -1)) {
        ui_error(op, "Unable to set output file '%s' length %d (%s).\n",
                 output_filename, len, strerror(errno));
        goto done;
    }

    /* mmap the dst */

    dst = mmap(0, len, PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, dst_fd, 0);
    if (dst == (void *) -1) {
        ui_error(op, "Unable to mmap output file %s (%s).\n",
                 output_filename, strerror(errno));
        goto done;
    }
    
    /* copy */
    
    memcpy(dst, buf + offset, len);
    
    /*
     * now that we can no longer fail, allocate and initialize the
     * PrecompiledInfo structure
     */

    info = (PrecompiledInfo *) nvalloc(sizeof(PrecompiledInfo));
    info->crc = crc;
    info->major = major;
    info->minor = minor;
    info->patch = patch;
    info->proc_version_string = proc_version_string;
    info->description = description;

    /*
     * XXX so that the proc version and description strings aren't
     * freed below
     */
    
    proc_version_string = description = NULL;

 done:
    
    /* cleanup whatever needs cleaning up */

    if (dst) munmap(dst, len);
    if (buf) munmap(buf, size);
    if (fd > 0) close(fd);
    if (dst_fd > 0) close(dst_fd);
    if (description) free(description);
    if (proc_version_string) free(proc_version_string);

    return info;
    
} /* mkprecompiled_unpack() */
