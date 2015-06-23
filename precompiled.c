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
 * precompiled.c - this source file contains functions for dealing
 * with precompiled kernel interfaces.
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



static int precompiled_read_fileinfo(Options *op, PrecompiledFileInfo *fileInfos,
                                     int index, char *buf, int offset, int size);

/*
 * read_uint32() - given a buffer and an offset, read the next 4 bytes from
 * the buffer at the given offset, build a uint32, and advance the offset.
 */

static uint32 read_uint32(const char *buf, int *offset)
{
    uint32 ret = 0;

    ret += (((uint32) buf[*offset + 3]) & 0xff);
    ret <<= 8;

    ret += (((uint32) buf[*offset + 2]) & 0xff);
    ret <<= 8;

    ret += (((uint32) buf[*offset + 1]) & 0xff);
    ret <<= 8;

    ret += (((uint32) buf[*offset]) & 0xff);
    ret <<= 0;

    *offset += sizeof(uint32);

    return ret;

}



/*
 * read_proc_version() - return the string contained in /proc/version
 *
 * XXX could use getmntent() to determine where the proc filesystem is
 * mounted.
 */

char *read_proc_version(Options *op, const char *proc_mount_point)
{
    int fd, len, version_len;
    char *version, *c = NULL;
    char *proc_verson_filename;
    
    len = strlen(proc_mount_point) + 9; /* strlen("/version") + 1 */
    proc_verson_filename = (char *) nvalloc(len);
    snprintf(proc_verson_filename, len, "%s/version", proc_mount_point);

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
        int ret;

        if (version_len == len) {
            version_len += NV_LINE_LEN;
            version = nvrealloc(version, version_len);
            c = version + len;
        }
        ret = read(fd, c, version_len - len);
        if (ret == -1) {
            ui_warn(op, "Error reading %s (%s).",
                    proc_verson_filename, strerror(errno));
            nvfree(version);
            version = NULL;
            goto done;
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

 done:
    nvfree(proc_verson_filename);
    close(fd);

    return version;
}



/*
 * get_precompiled_info() - load the the specified package into a
 * PrecompiledInfo record.  It's not really an error if we can't open the file
 * or if it's not the right format, so just throw an expert-only log message.
 */

PrecompiledInfo *get_precompiled_info(Options *op,
                                      const char *filename,
                                      const char *real_proc_version_string,
                                      const char *package_version,
                                      char *const *search_filelist)
{
    int fd, offset, num_files, i;
    char *buf;
    uint32 val, size;
    char *version, *description, *proc_version_string;
    struct stat stat_buf;
    PrecompiledInfo *info = NULL;
    PrecompiledFileInfo *fileInfos = NULL;

    fd = size = 0;
    buf = description = proc_version_string = version = NULL;

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

    if (size < PRECOMPILED_PKG_CONSTANT_LENGTH) {
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

    if (strncmp(buf + offset, PRECOMPILED_PKG_HEADER, 8) != 0) {
        ui_expert(op, "File '%s': unrecognized file format.", filename);
        goto done;
    }
    offset += 8;

    /* check the package format version */

    val = read_uint32(buf, &offset);
    if (val != PRECOMPILED_PKG_VERSION) {
        ui_expert(op, "Incompatible package format version %d: expected %d.",
                  val, PRECOMPILED_PKG_VERSION);
        goto done;
    }
    
    /* read the version */

    val = read_uint32(buf, &offset);
    if ((val + PRECOMPILED_PKG_CONSTANT_LENGTH) > size) {
        ui_expert(op, "Invalid file '%s' (bad version string length %d).",
                  filename, val);
        goto done;
    }
    if (val > 0) {
        version = nvalloc(val+1);
        memcpy(version, buf + offset, val);
        version[val] = '\0';
    } else {
        version = NULL;
    }
    offset += val;

    /* fail if the version could not be read, or if a package version was
       specified and the read version does not match it */

    if (!version ||
        (package_version && strcmp(version, package_version) != 0)) {
        goto done;
    }


    /* read the description */

    val = read_uint32(buf, &offset);
    if ((val + PRECOMPILED_PKG_CONSTANT_LENGTH) > size) {
        ui_expert(op, "Invalid file '%s' (bad description string length %d).",
                  filename, val);
        goto done;
    }
    description = nvalloc(val+1);
    memcpy(description, buf + offset, val);
    description[val] = '\0';
    offset += val;
    
    /* read the proc version string */

    val = read_uint32(buf, &offset);
    if ((val + PRECOMPILED_PKG_CONSTANT_LENGTH) > size) {
        ui_expert(op, "Invalid file '%s' (bad version string length %d).",
                  filename, val);
        goto done;
    }
    proc_version_string = nvalloc(val+1);
    memcpy(proc_version_string, buf + offset, val);
    offset += val;
    proc_version_string[val] = '\0';

    /* check if the running kernel matches */

    if (real_proc_version_string &&
        (strcmp(real_proc_version_string, proc_version_string) != 0)) {
        goto done;
    }
    
    ui_log(op, "A precompiled kernel interface for kernel '%s' has been "
           "found here: %s.", description, filename);

    num_files = read_uint32(buf, &offset);
    fileInfos = nvalloc(num_files * sizeof(PrecompiledFileInfo));
    for (i = 0; i < num_files; i++) {
        int ret;
        ret = precompiled_read_fileinfo(op, fileInfos, i, buf, offset, size);

        if (ret > 0) {
            offset += ret;
        } else {
            ui_log(op, "An error occurred while trying to parse '%s'.",
                   filename);
            goto done;
        }
    }

    /* 
     * Check for the package validity.
     * 
     * The package is valid if all of the files specified in 
     * search_filelist are present in the package.
     */

    if (search_filelist != NULL) {
        int index;

        for (index = 0; search_filelist[index]; index++) {
            int found = FALSE;

            for (i = 0; i < num_files; i++) {
                if (!strcmp(search_filelist[index], fileInfos[i].name)) {
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                ui_log(op, "Required file '%s' not found in package '%s'", 
                       search_filelist[index], filename);
                goto done;
            }
        }
    }
    

    /*
     * now that we can no longer fail, allocate and initialize the
     * PrecompiledInfo structure
     */

    info = (PrecompiledInfo *) nvalloc(sizeof(PrecompiledInfo));
    info->package_size = size;
    info->version = version;
    info->proc_version_string = proc_version_string;
    info->description = description;
    info->num_files = num_files;
    info->files = fileInfos;

    /*
     * XXX so that the proc version, description, and version strings, and the
     * PrecompiledFileInfo array aren't freed below
     */

    proc_version_string = description = version = NULL;
    fileInfos = NULL;

done:

    /* cleanup whatever needs cleaning up */

    if (buf) munmap(buf, size);
    if (fd >= 0) close(fd);
    nvfree(description);
    nvfree(proc_version_string);
    nvfree(fileInfos);
    nvfree(version);

    return info;

}


/*
 * precompiled_file_unpack() - Unpack an individual precompiled file to the
 * specified output directory.
 */

int precompiled_file_unpack(Options *op, const PrecompiledFileInfo *fileInfo,
                            const char *output_directory)
{
    int ret = FALSE, dst_fd = 0;
    char *dst_path, *dst = NULL;

    dst_path = nvstrcat(output_directory, "/", fileInfo->target_directory, "/",
                        fileInfo->name, NULL);

    /* extract file */

    if ((dst_fd = open(dst_path, O_CREAT | O_RDWR | O_TRUNC,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
        ui_error(op, "Unable to open output file '%s' (%s).", dst_path,
                 strerror(errno));
        goto done;
    }

    /* set the output file length */

    if ((lseek(dst_fd, fileInfo->size - 1, SEEK_SET) == -1) ||
        (write(dst_fd, "", 1) == -1)) {
        ui_error(op, "Unable to set output file '%s' length %d (%s).\n",
                 dst_path, fileInfo->size, strerror(errno));
        goto done;
    }

    /* mmap the dst */

    dst = mmap(0, fileInfo->size, PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, dst_fd, 0);
    if (dst == (void *) -1) {
        ui_error(op, "Unable to mmap output file %s (%s).\n",
                 dst_path, strerror(errno));
        goto done;
    }

    /* copy */

    memcpy(dst, fileInfo->data, fileInfo->size);

    ret = TRUE;

done:

    /* cleanup whatever needs cleaning up */

    nvfree(dst_path);
    if (dst) munmap(dst, fileInfo->size);
    if (dst_fd > 0) close(dst_fd);

    return ret;
}


/* precompiled_unpack() - unpack all of the files in a PrecompiledInfo into
 * the given destination directory */
int precompiled_unpack(Options *op, const PrecompiledInfo *info,
                       const char *output_directory)
{
    int i;

    if (!info) {
        return FALSE;
    }

    for (i = 0; i < info->num_files; i++) {
        if (!precompiled_file_unpack(op, &(info->files[i]), output_directory)) {
            return FALSE;
        }
    }

    return TRUE;
}




/*
 * encode_uint32() - given a uint32, a data buffer, and an offset into the data
 * buffer, write the integer to the data buffer and advance the offset by the
 * number of bytes written.
 */

static void encode_uint32(uint32 val, uint8 *data, int *offset)
{
    data[*offset + 0] = ((val >> 0)  & 0xff);
    data[*offset + 1] = ((val >> 8)  & 0xff);
    data[*offset + 2] = ((val >> 16) & 0xff);
    data[*offset + 3] = ((val >> 24) & 0xff);

    *offset += sizeof(uint32);
}


/*
 * precompiled_pack() - pack the specified precompiled kernel interface
 * file, prepended with a header, the CRC the driver version, a description
 * string, and the proc version string.
 */

int precompiled_pack(const PrecompiledInfo *info, const char *package_filename)
{
    int fd, offset;
    uint8 *out;
    int version_len, description_len, proc_version_len;
    int total_len, files_len, i;

    /*
     * get the lengths of the description, the proc version string,
     * and the files to be packaged along with the associated metadata.
     */

    version_len = strlen(info->version);
    description_len = strlen(info->description);
    proc_version_len = strlen(info->proc_version_string);

    for (files_len = i = 0; i < info->num_files; i++) {
        files_len += PRECOMPILED_FILE_CONSTANT_LENGTH +
                     strlen(info->files[i].name) +
                     strlen(info->files[i].linked_module_name) +
                     strlen(info->files[i].core_object_name) +
                     strlen(info->files[i].target_directory) +
                     info->files[i].size +
                     info->files[i].signature_size;
    }

    total_len = PRECOMPILED_PKG_CONSTANT_LENGTH +
        version_len + description_len + proc_version_len + files_len;

    /* open the output file for writing */

    fd = nv_open(package_filename, O_CREAT|O_RDWR|O_TRUNC,
                 S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

    /* set the output file length */

    nv_set_file_length(package_filename, fd, total_len);

    /* map the output file */

    out = nv_mmap(package_filename, total_len, PROT_READ|PROT_WRITE,
                  MAP_FILE|MAP_SHARED, fd);
    offset = 0;

    /* write the header */

    memcpy(&(out[0]), PRECOMPILED_PKG_HEADER, 8);
    offset += 8;

    /* write the package version */

    encode_uint32(PRECOMPILED_PKG_VERSION, out, &offset);

    /* write the version */

    encode_uint32(version_len, out, &offset);

    if (version_len) {
        memcpy(&(out[offset]), info->version, version_len);
        offset += version_len;
    }

    /* write the description */

    encode_uint32(description_len, out, &offset);

    if (description_len) {
        memcpy(&(out[offset]), info->description, description_len);
        offset += description_len;
    }

    /* write the proc version string */

    encode_uint32(proc_version_len, out, &offset);

    memcpy(&(out[offset]), info->proc_version_string, proc_version_len);
    offset += proc_version_len;

    /* write the number of files */

    encode_uint32(info->num_files, out, &offset);

    /* write the files */
    for (i = 0; i < info->num_files; i++) {
        PrecompiledFileInfo *file = &(info->files[i]);
        uint32 name_len = strlen(file->name);
        uint32 linked_module_name_len = strlen(file->linked_module_name);
        uint32 core_object_name_len = strlen(file->core_object_name);
        uint32 target_directory_len = strlen(file->target_directory);

        /* file header */
        memcpy(&(out[offset]), PRECOMPILED_FILE_HEADER, 4);
        offset += 4;

        /* file sequence number */
        encode_uint32(i, out, &offset);

        /* file type and attributes*/
        encode_uint32(file->type, out, &offset);
        encode_uint32(file->attributes, out, &offset);

        /* file name */
        encode_uint32(name_len, out, &offset);
        memcpy(&(out[offset]), file->name, name_len);
        offset += name_len;

        /* linked module name */
        encode_uint32(linked_module_name_len, out, &offset);
        memcpy(&(out[offset]), file->linked_module_name, linked_module_name_len);
        offset += linked_module_name_len;

        /* core object name */
        encode_uint32(core_object_name_len, out, &offset);
        memcpy(&(out[offset]), file->core_object_name, core_object_name_len);
        offset += core_object_name_len;

        /* target directory name */
        encode_uint32(target_directory_len, out, &offset);
        memcpy(&(out[offset]), file->target_directory, target_directory_len);
        offset += target_directory_len;

        /* crc */
        encode_uint32(file->crc, out, &offset);

        /* file */
        encode_uint32(file->size, out, &offset);
        memcpy(&(out[offset]), file->data, file->size);
        offset += file->size;

        /* redundant crc */
        encode_uint32(file->crc, out, &offset);

        /* linked module crc */
        encode_uint32(file->linked_module_crc, out, &offset);

        /* detached signature */
        encode_uint32(file->signature_size, out, &offset);
        if (file->signature_size) {
            memcpy(&(out[offset]), file->signature, file->signature_size);
            offset += file->signature_size;
        }

        /* redundant file sequence number */
        encode_uint32(i, out, &offset);

        /* file footer */
        memcpy(&(out[offset]), PRECOMPILED_FILE_FOOTER, 4);
        offset += 4;
    }

    /* unmap package */

    munmap(out, total_len);

    close(fd);

    return TRUE;

}


  
/*
 * free_precompiled() - free any malloced strings stored in a PrecompiledInfo,
 * then free the PrecompiledInfo.
 */
void free_precompiled(PrecompiledInfo *info)
{
    int i;

    if (!info) {
        return;
    }

    nvfree(info->description);
    nvfree(info->proc_version_string);
    nvfree(info->version);

    for (i = 0; i < info->num_files; i++) {
        free_precompiled_file_data(info->files[i]);
    }
    nvfree(info->files);

    nvfree(info);
}



void free_precompiled_file_data(PrecompiledFileInfo fileInfo)
{
    nvfree(fileInfo.name);
    nvfree(fileInfo.linked_module_name);
    nvfree(fileInfo.data);
    nvfree(fileInfo.signature);
    nvfree(fileInfo.target_directory);
}



/*
 * precompiled_read_file() - attempt to open the file at the specified path and
 * populate a PrecompiledFileInfo record with its contents and the appropriate
 * metadata. Return a pointer to a newly allocated PrecompiledFile record on
 * success, or NULL on failure.
 */

static int precompiled_read_file(PrecompiledFileInfo *fileInfo,
                                 const char *filename,
                                 const char *linked_module_name,
                                 const char *core_object_name,
                                 const char *target_directory,
                                 uint32 type)
{
    int fd;
    struct stat st;
    int success = FALSE, ret;

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        goto done;
    }

    if (fstat(fd, &st) != 0) {
        goto done;
    }

    fileInfo->size = st.st_size;
    fileInfo->data = nvalloc(fileInfo->size);

    ret = read(fd, fileInfo->data, fileInfo->size);

    if (ret != fileInfo->size) {
        goto done;
    }

    fileInfo->type = type;
    fileInfo->name = nv_basename(filename);
    fileInfo->linked_module_name = nvstrdup(linked_module_name);
    fileInfo->core_object_name = nvstrdup(core_object_name);
    fileInfo->target_directory = nvstrdup(target_directory);
    fileInfo->crc = compute_crc(NULL, filename);

    success = TRUE;

done:
    close(fd);
    return success;
}


int precompiled_read_interface(PrecompiledFileInfo *fileInfo,
                               const char *filename,
                               const char *linked_module_name,
                               const char *core_object_name,
                               const char *target_directory)
{
    return precompiled_read_file(fileInfo, filename, linked_module_name,
                                 core_object_name, target_directory,
                                 PRECOMPILED_FILE_TYPE_INTERFACE);
}

int precompiled_read_module(PrecompiledFileInfo *fileInfo, const char *filename,
                            const char *target_directory)
{
    return precompiled_read_file(fileInfo, filename, "", "", target_directory,
                                 PRECOMPILED_FILE_TYPE_MODULE);
}



/*
 * precompiled_read_fileinfo() - Read a PrecompiledFileInfo record from a binary
 * precompiled file package.
 *
 * Parameters:
 *   op:        A pointer to the options structure, used for ui_* functions
 *   fileInfos: A PrecompiledFileInfo array, where the decoded file data from
 *              the binary package will be stored
 *   index:     The index into fileInfos where the decoded file data will be
 *              stored. Used to verify that the file number recorded in the
 *              package is valid.
 *   buf:       The raw buffer containing the binary package data. This should
 *              point to the beginning of the package file.
 *   offset:    The offset into buf where the file data to be read begins.
 *   size:      The size of the package data buffer. This is used for bounds
 *              checking, to make sure that a reading a length specified in the
 *              won't go past the end of the package file.
 *
 * Return value:
 *   The number of bytes read from the package file on success, or -1 on error.
 */

static int precompiled_read_fileinfo(Options *op, PrecompiledFileInfo *fileInfos,
                                     int index, char *buf, int offset, int size)
{
    PrecompiledFileInfo *fileInfo = fileInfos + index;
    uint32 val;
    int oldoffset = offset;

    if (size - offset < PRECOMPILED_FILE_CONSTANT_LENGTH) {
        return -1;
    }

    if (strncmp(buf + offset, PRECOMPILED_FILE_HEADER, 4) != 0) {
        ui_log(op, "Unrecognized header for packaged file.");
        return -1;
    }
    offset += 4;

    val = read_uint32(buf, &offset);
    if (val != index) {
        ui_log(op, "Invalid file index %d; expected %d.", val, index);
        return -1;
    }

    fileInfo->type = read_uint32(buf, &offset);
    fileInfo->attributes = read_uint32(buf, &offset);

    val = read_uint32(buf, &offset);
    if (offset + val > size) {
        ui_log(op, "Bad filename length.");
        return -1;
    }

    fileInfo->name = nvalloc(val + 1);
    memcpy(fileInfo->name, buf + offset, val);
    offset += val;

    val = read_uint32(buf, &offset);
    if (offset + val > size) {
        ui_log(op, "Bad linked module name length.");
        return -1;
    }

    fileInfo->linked_module_name = nvalloc(val + 1);
    memcpy(fileInfo->linked_module_name, buf + offset, val);
    offset += val;

    val = read_uint32(buf, &offset);
    if (offset + val > size) {
        ui_log(op, "Bad core object file name length.");
        return -1;
    }

    fileInfo->core_object_name = nvalloc(val + 1);
    memcpy(fileInfo->core_object_name, buf + offset, val);
    offset += val;

    val = read_uint32(buf, &offset);
    if (offset + val > size) {
        ui_log(op, "Bad target directory name length.");
        return -1;
    }

    fileInfo->target_directory = nvalloc(val + 1);
    memcpy(fileInfo->target_directory, buf + offset, val);
    offset += val;

    fileInfo->crc = read_uint32(buf, &offset);

    fileInfo->size = read_uint32(buf, &offset);
    if (offset + fileInfo->size > size) {
        ui_log(op, "Bad file length.");
        return -1;
    }

    fileInfo->data = nvalloc(fileInfo->size);
    memcpy(fileInfo->data, buf + offset, fileInfo->size);
    offset += fileInfo->size;

    val = read_uint32(buf, &offset);
    if (val != fileInfo->crc) {
        ui_log(op, "The redundant stored CRC values %" PRIu32 " and %" PRIu32
               " disagree with each other; the file may be corrupted.",
               fileInfo->crc, val);
        return -1;
    }

    val = compute_crc_from_buffer(fileInfo->data, fileInfo->size);
    if (val != fileInfo->crc) {
        ui_log(op, "The CRC for the file '%s' (%" PRIu32 ") does not match the "
               "expected value (%" PRIu32 ").", fileInfo->name, val,
               fileInfo->crc);
    }

    fileInfo->linked_module_crc = read_uint32(buf, &offset);

    fileInfo->signature_size = read_uint32(buf, &offset);
    if(fileInfo->signature_size) {
        if (offset + fileInfo->signature_size > size) {
            ui_log(op, "Bad signature size");
            return -1;
        }
        fileInfo->signature = nvalloc(fileInfo->signature_size);
        memcpy(fileInfo->signature, buf + offset, fileInfo->signature_size);
        offset += fileInfo->signature_size;
    }

    val = read_uint32(buf, &offset);
    if (val != index) {
        ui_log(op, "Invalid file index %d; expected %d.", val, index);
        return -1;
    }

    if (strncmp(buf + offset, PRECOMPILED_FILE_FOOTER, 4) != 0) {
        ui_log(op, "Unrecognized footer for packaged file.");
        return -1;
    }
    offset += 4;

    return offset - oldoffset;
}


/*
 * precompiled_find_file() - search for a file with the given name within the
 * given PrecompiledInfo record, and return a pointer to it if found, or NULL
 * if not found.
 */

PrecompiledFileInfo *precompiled_find_file(const PrecompiledInfo *info,
                                           const char *file)
{
    int i;

    for (i = 0; i < info->num_files; i++) {
        if (strcmp(file, info->files[i].name) == 0) {
            return info->files + i;
        }
    }

    return NULL;
}

/*
 * precompiled_append_files() - append the given PrecompiledFileInfo array to
 * the already existing files in the given PrecompiledInfo.
 */

void precompiled_append_files(PrecompiledInfo *info, PrecompiledFileInfo *files,
                              int num_files)
{
    info->files = nvrealloc(info->files, (info->num_files + num_files) *
                            sizeof(PrecompiledFileInfo));
    memcpy(info->files + info->num_files, files,
           num_files * sizeof(PrecompiledFileInfo));
    info->num_files += num_files;
}

/*
 * precompiled_file_type_name() - return a pointer to a human-readable string
 * naming a file type. The string should not be freed.
 */
const char *precompiled_file_type_name(uint32 file_type)
{
    static const char *file_type_names[] = {
                                               "precompiled kernel interface",
                                               "precompiled kernel module",
                                           };

    if (file_type >= ARRAY_LEN(file_type_names)) {
        return "unknown file type";
    }

    return file_type_names[file_type];
}

/*
 * precompiled_file_attribute_names() - return a NULL-terminated list of
 * human-readable strings naming the attributes in the given file attribute
 * mask. The list should be freed when no longer used, but the constituent
 * strings should not be freed.
 */
const char **precompiled_file_attribute_names(uint32 attribute_mask)
{
    const char **ret;
    int i, attr = 0;

    static const char *file_attribute_names[] = {
                                                    "detached signature",
                                                    "linked module crc",
                                                    "embedded signature",
                                                };
    static const char *unknown_attribute = "unknown attribute";

    const int max_file_attribute_names = sizeof(attribute_mask) * 8;

    /* leave room for a NULL terminator */
    ret = nvalloc((max_file_attribute_names + 1) * sizeof(char *));

    for (i = 0; i < max_file_attribute_names; i++) {
        if (attribute_mask & (1 << i)) {
            if (i >= ARRAY_LEN(file_attribute_names)) {
                ret[attr++] = unknown_attribute;
            } else {
                ret[attr++] = file_attribute_names[i];
            }
        }
    }
    ret[attr] = NULL;

    return ret;
}



/*
 * byte_tail() - copy from infile, starting at the specified byte offset, and
 * going until the end of infile to a newly allocated buffer, a pointer to
 * which is stored at location given by the caller. Returns the size of the new
 * buffer on success; returns 0 and sets the caller pointer to NULL on failure.
 * This is needed because `tail -c`  is unreliable in some implementations.
 */
int byte_tail(const char *infile, int start, char **buf)
{
    FILE *in = NULL;
    int ret, end, size = 0;

    in = fopen(infile, "r");

    if (!in) {
	goto done;
    }

    ret = fseek(in, 0, SEEK_END);
    if (ret != 0) {
	goto done;
    }
    end = ftell(in);

    ret = fseek(in, start, SEEK_SET);
    if (ret != 0) {
	goto done;
    }

    size = end - start;
    *buf = nvalloc(size);

    ret = (fread(*buf, 1, size + 1, in));
    if (ret != size || ferror(in) || !feof(in)) {
	nvfree(*buf);
	*buf = NULL;
	goto done;
    }

done:
    if (in) {
        fclose(in);
    }
    return size;
}
