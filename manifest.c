/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2013 NVIDIA Corporation
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
 */

#include <string.h>

#include "manifest.h"

#define T TRUE
#define F FALSE

#define ENTRY(_name,                                                    \
              _has_arch,                                                \
              _has_tls_class,                                           \
              _installable,                                             \
              _has_path,                                                \
              _is_symlink,                                              \
              _is_shared_lib,                                           \
              _is_opengl,                                               \
              _is_temporary,                                            \
              _is_wrapper,                                              \
              _inherit_path,                                            \
              _glvnd_select                                             \
             )                                                          \
    #_name , FILE_TYPE_ ## _name ,                                      \
        {                                                               \
            .has_arch      = _has_arch,                                 \
            .has_tls_class = _has_tls_class,                            \
            .installable   = _installable,                              \
            .has_path      = _has_path,                                 \
            .is_symlink    = _is_symlink,                               \
            .is_shared_lib = _is_shared_lib,                            \
            .is_opengl     = _is_opengl,                                \
            .is_temporary  = _is_temporary,                             \
            .is_wrapper    = _is_wrapper,                               \
            .inherit_path  = _inherit_path,                             \
            .glvnd_select  = _glvnd_select,                             \
        }

/*
 * Define properties of each FILE_TYPE.  Keep in sync with the
 * PackageEntryFileType definition in nvidia-installer.h.
 */
static const struct {
    const char *name;
    PackageEntryFileType type;
    PackageEntryFileCapabilities caps;
} packageEntryFileTypeTable[] = {

    /*
     * glvnd_select  ---------------------------------------------+
     * inherit_path  ------------------------------------------+  |
     * is_wrapper    ---------------------------------------+  |  |
     * is_temporary  ------------------------------------+  |  |  |
     * is_opengl     ---------------------------------+  |  |  |  |
     * is_shared_lib ------------------------------+  |  |  |  |  |
     * is_symlink    ---------------------------+  |  |  |  |  |  |
     * has_path      ------------------------+  |  |  |  |  |  |  |
     * installable   ---------------------+  |  |  |  |  |  |  |  |
     * has_tls_class ------------------+  |  |  |  |  |  |  |  |  |
     * has_arch      ---------------+  |  |  |  |  |  |  |  |  |  |
     *                              |  |  |  |  |  |  |  |  |  |  |
     */
    { ENTRY(KERNEL_MODULE_SRC,      F, F, T, F, F, F, F, F, F, T, F) },
    { ENTRY(KERNEL_MODULE,          F, F, T, F, F, F, F, F, F, F, F) },
    { ENTRY(OPENGL_HEADER,          F, F, T, T, F, F, T, F, F, F, F) },
    { ENTRY(CUDA_ICD,               F, F, T, F, F, F, F, F, F, F, F) },
    { ENTRY(OPENGL_LIB,             T, F, T, F, F, T, T, F, F, F, F) },
    { ENTRY(CUDA_LIB,               T, F, T, T, F, T, F, F, F, F, F) },
    { ENTRY(OPENCL_LIB,             T, F, T, T, F, T, F, F, F, F, F) },
    { ENTRY(OPENCL_WRAPPER_LIB,     T, F, T, T, F, T, F, F, T, F, F) },
    { ENTRY(OPENCL_LIB_SYMLINK,     T, F, F, T, T, F, F, F, F, F, F) },
    { ENTRY(OPENCL_WRAPPER_SYMLINK, T, F, F, T, T, F, F, F, T, F, F) },
    { ENTRY(LIBGL_LA,               T, F, T, F, F, F, T, T, F, F, F) },
    { ENTRY(TLS_LIB,                T, T, T, T, F, T, T, F, F, F, F) },
    { ENTRY(UTILITY_LIB,            T, F, T, F, F, T, F, F, F, F, F) },
    { ENTRY(DOCUMENTATION,          F, F, T, T, F, F, F, F, F, F, F) },
    { ENTRY(APPLICATION_PROFILE,    F, F, T, T, F, F, F, F, F, F, F) },
    { ENTRY(MANPAGE,                F, F, T, T, F, F, F, F, F, F, F) },
    { ENTRY(EXPLICIT_PATH,          F, F, T, T, F, F, F, F, F, F, F) },
    { ENTRY(OPENGL_SYMLINK,         T, F, F, F, T, F, T, F, F, F, F) },
    { ENTRY(CUDA_SYMLINK,           T, F, F, T, T, F, F, F, F, F, F) },
    { ENTRY(TLS_SYMLINK,            T, T, F, T, T, F, T, F, F, F, F) },
    { ENTRY(UTILITY_LIB_SYMLINK,    T, F, F, F, T, F, F, F, F, F, F) },
    { ENTRY(INSTALLER_BINARY,       F, F, T, F, F, F, F, F, F, F, F) },
    { ENTRY(UTILITY_BINARY,         F, F, T, F, F, F, F, F, F, F, F) },
    { ENTRY(UTILITY_BIN_SYMLINK,    F, F, F, F, T, F, F, F, F, F, F) },
    { ENTRY(DOT_DESKTOP,            F, F, T, T, F, F, F, T, F, F, F) },
    { ENTRY(XMODULE_SHARED_LIB,     F, F, T, T, F, T, F, F, F, F, F) },
    { ENTRY(XMODULE_SYMLINK,        F, F, F, T, T, F, F, F, F, F, F) },
    { ENTRY(GLX_MODULE_SHARED_LIB,  F, F, T, T, F, T, T, F, F, F, F) },
    { ENTRY(GLX_MODULE_SYMLINK,     F, F, F, T, T, F, T, F, F, F, F) },
    { ENTRY(XMODULE_NEWSYM,         F, F, F, T, T, F, F, F, F, F, F) },
    { ENTRY(VDPAU_LIB,              T, F, T, T, F, T, F, F, F, F, F) },
    { ENTRY(VDPAU_SYMLINK,          T, F, F, T, T, F, F, F, F, F, F) },
    { ENTRY(NVCUVID_LIB,            T, F, T, F, F, T, F, F, F, F, F) },
    { ENTRY(NVCUVID_LIB_SYMLINK,    T, F, F, F, T, F, F, F, F, F, F) },
    { ENTRY(ENCODEAPI_LIB,          T, F, T, F, F, T, F, F, F, F, F) },
    { ENTRY(ENCODEAPI_LIB_SYMLINK,  T, F, F, F, T, F, F, F, F, F, F) },
    { ENTRY(VGX_LIB,                F, F, T, F, F, T, F, F, F, F, F) },
    { ENTRY(VGX_LIB_SYMLINK,        F, F, F, F, T, F, F, F, F, F, F) },
    { ENTRY(GRID_LIB,               F, F, T, T, F, T, F, F, F, F, F) },
    { ENTRY(GRID_LIB_SYMLINK,       F, F, F, T, T, F, F, F, F, F, F) },
    { ENTRY(NVIDIA_MODPROBE,        F, F, T, T, F, F, F, F, F, F, F) },
    { ENTRY(NVIDIA_MODPROBE_MANPAGE,F, F, T, T, F, F, F, F, F, F, F) },
    { ENTRY(MODULE_SIGNING_KEY,     F, F, T, F, F, F, F, T, F, F, F) },
    { ENTRY(NVIFR_LIB,              T, F, T, F, F, T, F, F, F, F, F) },
    { ENTRY(NVIFR_LIB_SYMLINK,      T, F, F, F, T, F, F, F, F, F, F) },
    { ENTRY(XORG_OUTPUTCLASS_CONFIG,F, F, T, F, F, F, F, F, F, F, F) },
    { ENTRY(DKMS_CONF              ,F, F, T, F, F, F, F, T, F, T, F) },
    { ENTRY(GLVND_LIB,              T, F, T, F, F, T, T, F, F, F, F) },
    { ENTRY(GLVND_SYMLINK,          T, F, F, F, T, F, T, F, F, F, F) },
    { ENTRY(GLX_CLIENT_LIB,         T, F, T, F, F, T, T, F, F, F, T) },
    { ENTRY(GLX_CLIENT_SYMLINK,     T, F, F, F, T, F, T, F, F, F, T) },
    { ENTRY(VULKAN_ICD_JSON,        F, F, T, F, F, F, F, F, F, F, F) },
    { ENTRY(GLVND_EGL_ICD_JSON,     F, F, T, F, F, F, T, F, F, F, F) },
    { ENTRY(EGL_CLIENT_LIB,         T, F, T, F, F, T, T, F, F, F, T) },
    { ENTRY(EGL_CLIENT_SYMLINK,     T, F, F, F, T, F, T, F, F, F, T) },
};

/*
 * Scan packageEntryFileTypeTable[] for the given file type. If we find
 * it, return the capabilities for the type.
 */
PackageEntryFileCapabilities get_file_type_capabilities(
    PackageEntryFileType type
)
{
    int i;
    PackageEntryFileCapabilities nullCaps = { F, F, F, F, F, F, F, F, F, F, F };

    for (i = 0; i < ARRAY_LEN(packageEntryFileTypeTable); i++) {
        if (type == packageEntryFileTypeTable[i].type) {
            return packageEntryFileTypeTable[i].caps;
        }
    }

    return nullCaps;
}

/*
 * Scan packageEntryFileTypeTable[] for the given string.  If we find
 * it, return the corresponding type and assign the capabilities for
 * the type.
 */
PackageEntryFileType parse_manifest_file_type(
    const char *str,
    PackageEntryFileCapabilities *caps
)
{
    int i;

    for (i = 0; i < ARRAY_LEN(packageEntryFileTypeTable); i++) {
        if (strcmp(str, packageEntryFileTypeTable[i].name) == 0) {
            *caps = packageEntryFileTypeTable[i].caps;
            return packageEntryFileTypeTable[i].type;
        }
    }

    return FILE_TYPE_NONE;
}

/*
 * Return a list of what file types should be considered installable.
 */
void get_installable_file_type_list(
    Options *op,
    PackageEntryFileTypeList *installable_file_types
)
{
    int i;

    memset(installable_file_types, 0, sizeof(*installable_file_types));

    for (i = 0; i < ARRAY_LEN(packageEntryFileTypeTable); i++) {

        PackageEntryFileType type = packageEntryFileTypeTable[i].type;

        if ((type == FILE_TYPE_OPENGL_HEADER) && !op->opengl_headers) {
            continue;
        }

        if (((type == FILE_TYPE_KERNEL_MODULE_SRC) ||
             (type == FILE_TYPE_DKMS_CONF)) &&
            op->no_kernel_module_source) {
            continue;
        }

        if (((type == FILE_TYPE_NVIDIA_MODPROBE) ||
             (type == FILE_TYPE_NVIDIA_MODPROBE_MANPAGE)) &&
            !op->nvidia_modprobe) {
            continue;
        }

        if (!packageEntryFileTypeTable[i].caps.installable) {
            continue;
        }

        if ((type == FILE_TYPE_XORG_OUTPUTCLASS_CONFIG) &&
            !op->xorg_supports_output_class) {
            continue;
        }

        installable_file_types->types[type] = 1;
    }
}

/*
 * Add symlink file types to the given file list.  This is used when
 * building a list of existing files to remove.  The NEWSYM type
 * requires special handling: while it is a symlink, we do not remove
 * it from the filesystem if it already exists.
 */
void add_symlinks_to_file_type_list(PackageEntryFileTypeList *file_type_list)
{
    int i;

    for (i = 0; i < ARRAY_LEN(packageEntryFileTypeTable); i++) {

        PackageEntryFileType type = packageEntryFileTypeTable[i].type;

        if (!packageEntryFileTypeTable[i].caps.is_symlink) {
            continue;
        }

        if (type == FILE_TYPE_XMODULE_NEWSYM) {
            continue;
        }

        file_type_list->types[type] = 1;
    }
}
