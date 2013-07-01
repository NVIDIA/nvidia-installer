/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003-2013 NVIDIA Corporation
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
 * precompiled.h: common definitions for mkprecompiled and nvidia-installer's
 *                precompiled kernel interface/module package format
 *
 * The format of a precompiled kernel interface package is:
 *
 * the first 8 bytes are: "\aNVIDIA\a"
 *
 * the next 4 bytes (unsigned) are: version number of the package format
 *
 * the next 4 bytes (unsigned) are: the length of the version string (v)
 *
 * the next v bytes are the version string
 *
 * the next 4 bytes (unsigned) are: the length of the description (d)
 *
 * the next d bytes are the description
 *
 * the next 4 bytes (unsigned) are: the length of the proc version string (p)
 *
 * the next p bytes are the proc version string
 *
 * the next 4 bytes (unsigned) are: the number of files in this package (f)
 *
 * for each of the f packaged files:
 *
 *   the first 4 bytes are: "FILE"
 *
 *   the next 4 bytes (unsigned) are: the 0-indexed sequence number of this file
 *
 *   the next 4 bytes (unsigned) are the file type:
 *     0: precompiled interface
 *     1: precompiled kernel module
 *
 *   the next 4 bytes are an attribute mask:
 *     1: has detached signature
 *     2: has linked module CRC
 *     4: has embedded signature
 *
 *   the next 4 bytes (unsigned) are: length of the file name (n)
 *
 *   the next n bytes are the file name
 *
 *   the next 4 bytes (unsigned) are: length of the linked module name (m)
 *
 *   the next m bytes are the linked module name (for kernel interfaces only)
 *
 *   the next 4 bytes (unsigned) are: length of the core object file name (o)
 *
 *   the next o bytes are the core object file name (for kernel interfaces only)
 *
 *   the next 4 bytes (unsigned) are: CRC of the packaged file
 *
 *   the next 4 bytes (unsigned) are: size of the packaged file (l)
 *
 *   the next l bytes is the packaged file
 *
 *   the next 4 bytes (unsigned) are: CRC of the packaged file, again
 *
 *   the next 4 bytes (unsigned) are: CRC of linked module, when appropriate;
 *   undefined if "has linked module CRC" attribute is not set
 *
 *   the next 4 bytes (unsigned) are: length of detached signature (s), when
 *   appropriate; 0 if "has detached signature" attribute is not set
 *
 *   the next (s) bytes are: detached signature
 *
 *   the next 4 bytes (unsigned) are: the 0-indexed sequence number of this file
 *
 *   the next 4 bytes are: "END."
 */

#ifndef __NVIDIA_INSTALLER_PRECOMPILED_H__
#define __NVIDIA_INSTALLER_PRECOMPILED_H__

#define PRECOMPILED_PKG_CONSTANT_LENGTH (8 + /* precompiled package header */ \
                                         4 + /* package format version */ \
                                         4 + /* driver version string length */ \
                                         4 + /* description string length */ \
                                         4 + /* proc version string length */ \
                                         4)  /* number of files */

#define PRECOMPILED_PKG_HEADER "\aNVIDIA\a"

#define PRECOMPILED_PKG_VERSION 1

#define PRECOMPILED_FILE_CONSTANT_LENGTH (4 + /* precompiled file header */ \
                                          4 + /* file serial number */ \
                                          4 + /* file type */ \
                                          4 + /* attributes mask */ \
                                          4 + /* file name length */ \
                                          4 + /* linked module name length */ \
                                          4 + /* core object name length */ \
                                          4 + /* file crc */ \
                                          4 + /* file size */ \
                                          4 + /* redundant file crc */ \
                                          4 + /* linked module crc */ \
                                          4 + /* detached signature length */ \
                                          4 + /* redundant file serial number */ \
                                          4)  /* precompiled file footer*/

#define PRECOMPILED_FILE_HEADER "FILE"
#define PRECOMPILED_FILE_FOOTER "END."

enum {
    PRECOMPILED_FILE_TYPE_INTERFACE = 0,
    PRECOMPILED_FILE_TYPE_MODULE,
};

enum {
    PRECOMPILED_FILE_HAS_DETACHED_SIGNATURE = 0,
    PRECOMPILED_FILE_HAS_LINKED_MODULE_CRC,
    PRECOMPILED_FILE_HAS_EMBEDDED_SIGNATURE,
};

#define PRECOMPILED_ATTR(attr) (1 << PRECOMPILED_FILE_HAS_##attr)

typedef struct __precompiled_file_info {
    uint32 type;
    uint32 attributes;
    char *name;
    char *linked_module_name;
    char *core_object_name;
    uint32 crc;
    uint32 size;
    uint8 *data;
    uint32 linked_module_crc;
    uint32 signature_size;
    char *signature;
} PrecompiledFileInfo;

typedef struct __precompiled_info {

    uint32 package_size;
    char *version;
    char *proc_version_string;
    char *description;
    int num_files;
    PrecompiledFileInfo *files;

} PrecompiledInfo;


char *read_proc_version(Options *op, const char *proc_mount_point);

PrecompiledInfo *get_precompiled_info(Options *op,
                                      const char *filename,
                                      const char *real_proc_version_string,
                                      const char *package_version);

PrecompiledFileInfo *precompiled_find_file(const PrecompiledInfo *info,
                                           const char *file);

int precompiled_file_unpack(Options *op, const PrecompiledFileInfo *fileInfo,
                            const char *output_directory);
int precompiled_unpack(Options *op, const PrecompiledInfo *info,
                       const char *output_filename);

int precompiled_pack(const PrecompiledInfo *info, const char *package_filename);

void free_precompiled(PrecompiledInfo *info);
void free_precompiled_file_data(PrecompiledFileInfo fileInfo);
int precompiled_read_interface(PrecompiledFileInfo *fileInfo,
                               const char *filename,
                               const char *linked_module_name,
                               const char *core_object_name);
int precompiled_read_module(PrecompiledFileInfo *fileInfo, const char *filename);
void precompiled_append_files(PrecompiledInfo *info, PrecompiledFileInfo *files,
                              int num_files);

const char *precompiled_file_type_name(uint32 file_type);
const char **precompiled_file_attribute_names(uint32 attribute_mask);

int byte_tail(const char *infile, int start, char **buf);

#endif /* __NVIDIA_INSTALLER_PRECOMPILED_H__ */
