/*
 * gcc mkprecompiled.c -o mkprecompiled -Wall -g
 *
 * mkprecompiled - this program packages up a precompiled kernel
 * module interface with a list of unresolved symbols in the kernel
 * module.
 *
 * normally, this would be done much more simply with a perl or shell
 * script, but I've implemented it in C because we don't want the
 * installer to rely upon any system utilities that it doesn't
 * absolutely need.
 *
 * commandline options:
 *
 * -i, --interface=<filename>
 * -o, --output=<filename>
 * -u, --unpack=<filename>
 * -d, --description=<kernel description>
 *
 * There is nothing specific to the NVIDIA graphics driver in this
 * program, so it should be usable for the nforce drivers, for
 * example.
 */

#define BINNAME "mkprecompiled"
#define NV_LINE_LEN 256
#define NV_VERSION_LEN 4096
#define PROC_MOUNT_POINT "/proc"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>

#include <nvgetopt.h>

typedef unsigned int uint32;
typedef unsigned char uint8;

enum {
    PACK = 'p',
    UNPACK = 'u',
    INFO = 'i',
    MATCH = 'm',
};

/*
 * Options structure
 */

typedef struct {
    int action;
    char *package_file;
    char *output_directory;
    char *description;
    char *proc_version_string;
    char *proc_mount_point;
    char *version;
    int num_files;
    struct __precompiled_file_info *new_files;
    struct __precompiled_info *package;
} Options;


#include "common-utils.h"
#include "crc.h"
#include "precompiled.h"


/*
 * XXX hack to resolve symbols used by crc.c and precompiled.c
 */

void ui_warn(Options *op, const char *fmt, ...);

void ui_warn(Options *op, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void ui_expert(Options *op, const char *fmt, ...);

void ui_expert(Options *op, const char *fmt, ...)
{
}

void ui_error(Options *op, const char *fmt, ...);

void ui_error(Options *op, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void ui_log(Options *op, const char *fmt, ...);

void ui_log(Options *op, const char *fmt, ...)
{
}




/*
 * print_help()
 */

static void print_help(void)
{
    printf("\n%s: pack/unpack precompiled files, and get information about\n"
           "existing precompiled file packages.\n\n"
           "USAGE: <action> <package-file> [options] \n\n", BINNAME);

    printf("<action> may be one of:\n\n"
           "    -p | --pack     add files to a package\n"
           "    -u | --unpack   unpack files from a package\n"
           "    -i | --info     display information about a package\n"
           "    -m | --match    check if a package matches the running kernel\n"
           "    -h | --help     print this help text and exit\n\n"
           "<package-file> is the package file to pack/unpack/test. It must be\n"
           "an existing, valid package file for the --unpack, --info, and\n"
           "--match actions. For the --pack action, if <package-file> does not\n"
           "exist, it is created; if it exists but is not a valid package file,\n"
           "it is overwritten; and if it exists and is a valid package file,\n"
           "files will be added to the existing package.\n\n"
           "--pack options:\n"
           "    -v | --driver-version      (REQUIRED for new packages)\n"
           "        The version of the packaged components.\n"
           "    -P | --proc-version-string (RECOMMENDED for new packages)\n"
           "        The kernel version, as reported by '/proc/version', for the\n"
           "        target kernel. Default: the contents of the '/proc/version'\n"
           "        file on the current system.\n"
           "    -d | --description         (RECOMMENDED for new packages)\n"
           "        A human readable description of the package.\n"
           "    --kernel-interface <file> --linked-module-name <module-name>\\\n"
           "                              --core-object-name <core-name>\\\n"
           "                            [ --linked-module <linked-kmod-file> \\\n"
           "                              --signed-module <signed-kmod-file> ]\n"
           "        Pack <file> as a precompiled kernel interface.\n"
           "        <module-name> specifies the name of the kernel module file\n"
           "        that is produced by linking the precompiled kernel interface\n"
           "        with a separate precompiled core object file. <core-name>\n"
           "        specifies the name of the core object file that is linked\n"
           "        together with the precompiled interface to produce the final\n"
           "        kernel module.\n"
           "        A detached module signature may be produced by specifying\n"
           "        both the --linked-module and --signed-module options.\n"
           "        <linked-kmod-file> is a linked .ko file that is the result\n"
           "        of linking the precompiled interface with the remaining\n"
           "        object file(s) required to produce the finished module, and\n"
           "        <signed-kmod-file> is a copy of <linked-kmod-file> which an\n"
           "        appended module signature. In order for the signature to be\n"
           "        correctly applied on the target system, the linking should\n"
           "        be performed with the same linker and flags that will be\n"
           "        used on the target system.\n"
           "        The --linked-module and --signed-module options must be\n"
           "        given after the --kernel-interface option for the kernel\n"
           "        interface file with which they are associated, and before\n"
           "        any additional --kernel-interface or --kernel-module files.\n"
           "    --kernel-module <file> [ --signed ]\n"
           "        Pack <file> as a precompiled kernel module. The --signed\n"
           "        option specifies that <file> includes a module signature.\n"
           "        The --signed option must be given after the --kernel-module\n"
           "        option for the kernel module with which it is associated,\n"
           "        and before any additional --kernel-interface or\n"
           "        --kernel-module files.\n\n"
           "    If --driver-version, --proc-version-string, or --description\n"
           "    are given with an existing package file, the values in that\n"
           "    package file will be updated with new ones. At least one file\n"
           "    must be given with either --kernel-interface or --kernel-module\n"
           "    when using the --pack option.\n\n"
           "--unpack options:\n"
           "    -o | --output-directory\n"
           "        The target directory where files will be unpacked. Default:\n"
           "        unpack files in the current directory.\n\n"
           "Additional options:\n"
           "    --proc-mount-point\n"
           "        The procfs mount point on the current system, where the \n"
           "        '/proc/version' file may be found. Used by the --match\n"
           "        action, as well as to supply the default value of the\n"
           "        --proc-version-string option of the --pack action.\n"
           "        Default value: '/proc'\n");

} /* print_help() */


enum {
    PROC_MOUNT_POINT_OPTION = 1024,
    KERNEL_INTERFACE_OPTION,
    KERNEL_MODULE_OPTION,
    SIGNED_FILE_OPTION,
    LINKED_MODULE_OPTION,
    LINKED_AND_SIGNED_MODULE_OPTION,
    LINKED_MODULE_NAME_OPTION,
    CORE_OBJECT_NAME_OPTION,
};


static void grow_file_array(Options *op, int *array_size)
{
    if (op->num_files < 0) {
        op->num_files = 0;
    }

    if (op->num_files >= *array_size) {
        *array_size *= 2;
        op->new_files = nvrealloc(op->new_files,
                                  sizeof(PrecompiledFileInfo) * *array_size);
    }
}


static uint32 file_type_from_option(int option) {
    switch (option) {
        case KERNEL_INTERFACE_OPTION:
            return PRECOMPILED_FILE_TYPE_INTERFACE;
        case KERNEL_MODULE_OPTION:
            return PRECOMPILED_FILE_TYPE_MODULE;
        default:
            fprintf(stderr, "Unrecognized file type!");
            exit(1);
    }
}


static void check_file_option_validity(Options *op, const char *option_name)
{
    if (op->num_files < 0) {
        fprintf(stderr, "The --%s option cannot be specified before a file "
                "name.\n", option_name);
        exit(1);
    }
}


static int create_detached_signature(Options *op, PrecompiledFileInfo *file,
                                     const char *linked_module,
                                     const char *signed_module)
{
    if (file->type != PRECOMPILED_FILE_TYPE_INTERFACE) {
        return TRUE;
    } else if (linked_module && signed_module) {
        struct stat st;

        if (stat(linked_module, &st) != 0) {
            fprintf(stderr, "Unable to stat the linked kernel module file '%s'."
                    "\n", linked_module);
            return FALSE;
        }

        file->linked_module_crc = compute_crc(op, linked_module);
        file->attributes |= PRECOMPILED_ATTR(LINKED_MODULE_CRC);

        file->signature_size = byte_tail(signed_module, st.st_size,
                                         &(file->signature));

        if (file->signature_size > 0 && file->signature != NULL) {
            file->attributes |= PRECOMPILED_ATTR(DETACHED_SIGNATURE);
            return TRUE;
        } else {
            fprintf(stderr, "Failed to create a detached signature from signed "
                    "kernel module '%s'.\n", signed_module);
        }

    } else if (linked_module || signed_module) {
        fprintf(stderr, "Both --linked-module and --signed-module must be "
                "specified to create a detached signature for precompiled "
                "kernel interface file '%s'.\n", file->name);
    } else {
        return TRUE;
    }

    return FALSE;
}


static void set_action(Options *op, int action)
{
    if (op->action) {
        fprintf(stderr, "Invalid command line; multiple actions cannot be "
                "specified at the same time.\n");
        exit(1);
    }

    op->action = action;
}

static void pack_a_file(Options *op, PrecompiledFileInfo *file,
                        char *name, uint32 type, char **linked_name,
                        char **core_name, char **linked_module_file,
                        char **signed_module_file)
{
    switch(type) {
    case PRECOMPILED_FILE_TYPE_INTERFACE:
        if (*linked_name == NULL || *core_name == NULL) {
            fprintf(stderr, "Each kernel interface file must have both the "
                    "--linked-module-name and --core-object-name options set "
                    "in order to be added to a package.\n");
            exit(1);
        }

        if (!precompiled_read_interface(file, name, *linked_name, *core_name)) {
            fprintf(stderr, "Failed to read kernel interface '%s'.\n", name);
        exit(1);
        }
        break;

    case PRECOMPILED_FILE_TYPE_MODULE:
        if (!precompiled_read_module(file, name)) {
            fprintf(stderr, "Failed to read kernel module '%s'.\n", name);
        }
        break;

    default:
        exit(1);
    }
    nvfree(name);

    if (!create_detached_signature(op, file, *linked_module_file,
                                   *signed_module_file)) {
        exit(1);
    }

    op->num_files++;

    nvfree(*linked_name);
    nvfree(*core_name);
    nvfree(*linked_module_file);
    nvfree(*signed_module_file);
    *linked_name = *core_name = *linked_module_file = *signed_module_file = NULL;
}

/*
 * parse_commandline() - parse the commandline arguments. do some
 * trivial validation, and return an initialized malloc'ed Options
 * structure.
 */

static Options *parse_commandline(int argc, char *argv[])
{
    Options *op;
    int c, file_array_size = 16;
    uint32 type = 0;
    PrecompiledFileInfo *file = NULL;
    char *strval, *signed_mod = NULL, *linked_mod = NULL, *filename = NULL,
         *linked_name = NULL, *core_name = NULL;
    char see_help[1024];

    static const NVGetoptOption long_options[] = {
        { "pack",                PACK,   NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "unpack",              UNPACK, NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "info",                INFO,   NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "match",               MATCH,  NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "help",                'h',    0,                        NULL, NULL },
        { "description",         'd',    NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "output-directory",    'o',    NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "driver-version",      'v',    NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "proc-version-string", 'P',    NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "proc-mount-point",    PROC_MOUNT_POINT_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "kernel-interface",    KERNEL_INTERFACE_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "kernel-module",       KERNEL_MODULE_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "signed",              SIGNED_FILE_OPTION,
                                         0,                        NULL, NULL },
        { "linked-module",       LINKED_MODULE_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "signed-module",       LINKED_AND_SIGNED_MODULE_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "linked-module-name",  LINKED_MODULE_NAME_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { "core-object-name",    CORE_OBJECT_NAME_OPTION,
                                         NVGETOPT_STRING_ARGUMENT, NULL, NULL },
        { NULL,                  0,   0,                        NULL, NULL }
    };

    snprintf(see_help, sizeof(see_help), "Please run `%s --help` for usage "
             "information.\n", argv[0]);
    see_help[sizeof(see_help) - 1] = '\0';

    op = (Options *) nvalloc(sizeof(Options));
    op->new_files = nvalloc(sizeof(PrecompiledFileInfo) * file_array_size);
    op->num_files = -1;

    op->proc_mount_point = PROC_MOUNT_POINT;

    while (1) {
        c = nvgetopt(argc, argv, long_options, &strval,
                     NULL, /* boolval */
                     NULL, /* intval */
                     NULL, /* doubleval */
                     NULL  /* disable_val */);

        if (c == -1)
            break;

        switch(c) {
        case PACK: case UNPACK: case INFO: case MATCH:
            set_action(op, c);
            op->package_file = strval;
            break;

        case 'h': print_help(); exit(0); break;
        case 'f': op->package_file = strval; break;
        case 'd': op->description = strval; break;
        case 'o': op->output_directory = strval; break;
        case 'v': op->version = strval; break;
        case 'P': op->proc_version_string = strval; break;
        case PROC_MOUNT_POINT_OPTION: op->proc_mount_point = strval; break;

        case KERNEL_INTERFACE_OPTION: case KERNEL_MODULE_OPTION:

            grow_file_array(op, &file_array_size);

            if (file) {
                pack_a_file(op, file, filename, type, &linked_name, &core_name,
                            &linked_mod, &signed_mod);
            }

            file = op->new_files + op->num_files;
            filename = strval;
            type = file_type_from_option(c);

            break;

        case SIGNED_FILE_OPTION:

            check_file_option_validity(op, "signed");

            /*
             * for interfaces, signedness is implied by the presence of a linked
             * module crc and a detached signature, so only set the signed file
             * attribute in the case of a precompiled kernel module.
             */

            if (type == PRECOMPILED_FILE_TYPE_MODULE) {
                file->attributes |= PRECOMPILED_ATTR(EMBEDDED_SIGNATURE);
            }

            break;

        case LINKED_MODULE_OPTION:

            check_file_option_validity(op, "linked-module");
            linked_mod = strval;

            break;

        case LINKED_AND_SIGNED_MODULE_OPTION:

            check_file_option_validity(op, "signed-module");
            signed_mod = strval;

            break;

        case LINKED_MODULE_NAME_OPTION:

            check_file_option_validity(op, "linked-module-name");
            linked_name = strval;

            break;

        case CORE_OBJECT_NAME_OPTION:

            check_file_option_validity(op, "core-object-name");
            core_name = strval;

            break;

        default:
            fprintf (stderr, "Invalid commandline; %s", see_help);
            exit(0);
        }
    }

    
    /* validate options */

    if (!op->action) {
        fprintf(stderr, "No action specified; one of --pack, --unpack, --info, "
                "or --match options must be given. %s", see_help);
        exit(1);
    }

    switch (op->action) {
    case PACK:
        if (file) {
            pack_a_file(op, file, filename, type, &linked_name, &core_name,
                        &linked_mod, &signed_mod);
        }

        if (op->num_files < 1) {
            fprintf(stderr, "At least one file to pack must be specified "
                    "when using the --pack option; %s", see_help);
            exit(1);
        }
        break;

    case UNPACK:
        if (!op->output_directory) {
            op->output_directory = ".";
        }
        break;

    case INFO: case MATCH: default: /* XXX should never hit default case */
        break;
    }

    op->package = get_precompiled_info(op, op->package_file, NULL, NULL, NULL);

    if (!op->package && op->action != PACK) {
        fprintf(stderr, "Unable to read package file '%s'.\n",
                op->package_file);
        exit(1);
    }

    return op;

} /* parse_commandline() */




/*
 * check_match() - read /proc/version, and do a strcmp with str.
 * Returns 1 if the strings match, 0 if they don't match.
 */

static int check_match(Options *op, char *str)
{
    int ret = 0;
    char *version = read_proc_version(op, op->proc_mount_point);
    
    if (strcmp(version, str) == 0) {
        ret = 1;
        printf("kernel interface matches.\n");
    } else {
        ret = 0;
        printf("kernel interface doesn't match.\n");
    }

    free(version);
        
    return ret;
    
} /* check_match() */



/*
 * program entry point
 */

int main(int argc, char *argv[])
{
    Options *op;
    int ret = 1;

    op = parse_commandline(argc, argv);


    switch (op->action) {
    int i;

    case PACK:
        if (!op->package) {
            if (!op->version) {
                fprintf (stderr, "The --driver-version option must be specified "
                         "when using the --pack option to create a new package; "
                         "Please run `%s --help` for usage information.\n",
                         argv[0]);
                exit (1);
            }

            op->package = nvalloc(sizeof(PrecompiledInfo));

            op->package->description = op->description ? op->description :
                                                         nvstrdup("");
            op->package->proc_version_string =
                op->proc_version_string ?
                    op->proc_version_string :
                    read_proc_version(op, op->proc_mount_point);
            op->package->version = op->version;
        }

        precompiled_append_files(op->package, op->new_files, op->num_files);

        if (precompiled_pack(op->package, op->package_file)) {
            ret = 0;
        } else {
            fprintf(stderr, "An error occurred while writing the package "
                    "file '%s'.\n", op->package_file);
        }

        break;

    case UNPACK:
        if (precompiled_unpack(op, op->package, op->output_directory)) {
            ret = 0;
        } else {
            fprintf(stderr, "An error occurred while unpacking the package "
                    "file '%s' to '%s'.\n", op->package_file,
                     op->output_directory);
        }
        break;

    case INFO:

        printf("description: %s\n", op->package->description);
        printf("version: %s\n", op->package->version);
        printf("proc version: %s\n", op->package->proc_version_string);
        printf("number of files: %d\n\n", op->package->num_files);

        for (i = 0; i < op->package->num_files; i++) {
            PrecompiledFileInfo *file = op->package->files + i;
            const char **attrs, **attr;

            attrs = precompiled_file_attribute_names(file->attributes);

            printf("file %d:\n", i + 1);
            printf("  name: '%s'\n", file->name);
            printf("  type: %s\n",
                   precompiled_file_type_name(file->type));
            printf("  attributes: ");

            for(attr = attrs; *attr; attr++) {
                if (attr > attrs) {
                    printf(", ");
                }
                printf("%s", *attr);
            }

            printf("\n");

            printf("  size: %d bytes\n", file->size);
            printf("  crc: %" PRIu32 "\n", file->crc);

            if (file->type == PRECOMPILED_FILE_TYPE_INTERFACE) {
                printf("  core object name: %s\n", file->core_object_name);
                printf("  linked module name: %s\n", file->linked_module_name);
                if (file->signature_size) {
                    printf("  linked module crc: %" PRIu32 "\n",
                           file->linked_module_crc);
                    printf("  signature size: %d\n", file->signature_size);
                }
            }

            printf("\n");

        }

        ret = 0;
        break;

    case MATCH:

        ret = check_match(op, op->package->proc_version_string);
        break;

    default: /* XXX should never get here */ break;

    }

    free_precompiled(op->package);
 
    return ret;

} /* main() */
