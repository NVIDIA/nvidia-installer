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
 *
 * The format of a precompiled kernel interface package is:
 *
 * the first 8 bytes are: "NVIDIA  "
 *
 * the next 4 bytes (unsigned) are: CRC of the kernel interface module
 * 
 * the next 4 bytes (unsigned) are: the length of the version string (v)
 *
 * the next v bytes are the version string
 *
 * the next 4 bytes (unsigned) are: the length of the description (n)
 *
 * the next n bytes are the description
 *
 * the next 4 bytes (unsigned) are: the length of the proc version string (m)
 *
 * the next m bytes are the proc version string
 *
 * the rest of the file is the kernel interface module
 */

#define BINNAME "mkprecompiled"
#define NV_LINE_LEN 256
#define NV_VERSION_LEN 4096
#define PROC_VERSION "/proc/version"

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

#define _GNU_SOURCE /* XXX not portable */
#include <getopt.h>

#define CONSTANT_LENGTH (8 + 4 + 4 + 4 + 4)

typedef unsigned int uint32;
typedef unsigned char uint8;

/*
 * Options structure
 */

typedef struct {
    char *interface;
    char *output;
    char *unpack;
    char *description;
    char *proc_version_string;
    char *version;
    uint8  info;
    uint8  match;
} Options;


#include "crc.h"

/*
 * nv_alloc() - malloc wrapper that checks for errors, and zeros out
 * the memory; if an error occurs, an error is printed to stderr and
 * exit() is called -- this function will only return on success.
 */

static void *nv_alloc (size_t size)
{
    void *m = malloc (size);

    if (!m) {
        fprintf (stderr, "%s: memory allocation failure\n", BINNAME);
        exit (1);
    }
    memset (m, 0, size);
    return (m);

} /* nv_alloc() */


/*
 * XXX hack to resolve symbols used by crc.c
 */

void *nvalloc(size_t size);

void *nvalloc(size_t size)
{
    return nv_alloc(size);
}

void ui_warn(Options *op, const char *fmt, ...);

void ui_warn(Options *op, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}



/*
 * nv_open() - open(2) wrapper; prints an error message if open(2)
 * fails and calls exit().  This function only returns on success.
 */

static int nv_open(const char *pathname, int flags, mode_t mode)
{
    int fd;
    fd = open(pathname, flags, mode);
    if (fd == -1) {
        fprintf(stderr, "Failure opening %s (%s).\n",
                pathname, strerror(errno));
        exit(1);
    }
    return fd;

} /* nv_name() */



/*
 * nv_get_file_length() - stat(2) wrapper; prints an error message if
 * the system call fails and calls exit().  This function only returns
 * on success.
 */

static int nv_get_file_length(const char *filename)
{
    struct stat stat_buf;
    int ret;
    
    ret = stat(filename, &stat_buf);
    if (ret == -1) {
        fprintf(stderr, "Unable to determine '%s' file length (%s).\n",
                filename, strerror(errno));
        exit(1);
    }
    return stat_buf.st_size;

} /* nv_get_file_length() */



/*
 * nv_set_file_length() - wrapper for lseek() and write(); prints an
 * error message if the system calls fail and calls exit().  This
 * function only returns on success.
 */

static void nv_set_file_length(const char *filename, int fd, int len)
{
    if ((lseek(fd, len - 1, SEEK_SET) == -1) ||
        (write(fd, "", 1) == -1)) {
        fprintf(stderr, "Unable to set file '%s' length %d (%s).\n",
                filename, fd, strerror(errno));
        exit(1);
    }
} /* nv_set_file_length() */



/*
 * nv_mmap() - mmap(2) wrapper; prints an error message if mmap(2)
 * fails and calls exit().  This function only returns on success.
 */

static void *nv_mmap(const char *filename, size_t len, int prot,
                     int flags, int fd)
{
    void *ret;

    ret = mmap(0, len, prot, flags, fd, 0);
    if (ret == (void *) -1) {
        fprintf(stderr, "Unable to mmap file %s (%s).\n",
                filename, strerror(errno));
        exit(1);
    }
    return ret;
    
} /* nv_mmap() */



/*
 * print_help()
 */

static void print_help(void)
{
    printf("\n%s [options] \n\n", BINNAME);

    printf("-i, --interface=<interface name>\n");
    printf("  Name of kernel interface file.\n\n");
    
    printf("-o, --output=<output name>\n");
    printf("  Name of output file.\n\n");

    printf("-u, --unpack=<filename>\n");
    printf("  Name of file to be unpacked.\n\n");

    printf("-d, --description=<kernel description>\n");
    printf("  Kernel description.\n\n");

    printf("-v, --proc-version=<string>\n");
    printf("  /proc/version string for target kernel.\n\n");

    printf("--version=<version string>\n\n");
    
    printf("--info\n");
    printf("  Print the description and version number of the file\n");
    printf("  specified by the unpack option.\n\n");

    printf("-m, --match\n");
    printf("  Check if the precompiled package matches the running\n");
    printf("  kernel.\n\n");

    printf("This program can be used to either pack a precompiled kernel\n");
    printf("module interface, or unpack it.\n\n");

} /* print_help() */



/*
 * parse_commandline() - parse the commandline arguments. do some
 * trivial validation, and return an initialized malloc'ed Options
 * structure.
 */

static Options *parse_commandline(int argc, char *argv[])
{
    Options *op;
    int c, option_index = 0;

#define INFO_OPTION          4
    
    static struct option long_options[] = {
        { "interface",    1, 0, 'i'                  },
        { "output",       1, 0, 'o'                  },
        { "unpack",       1, 0, 'u'                  },
        { "description",  1, 0, 'd'                  },
        { "help",         0, 0, 'h'                  },
        { "proc-version", 1, 0, 'v'                  },
        { "version",      1, 0, 'V'                  },
        { "info",         0, 0, INFO_OPTION          },
        { "match",        0, 0, 'm'                  },
        { 0,              0, 0, 0                    }
    };
    
    op = (Options *) nv_alloc(sizeof(Options));
    
    while (1) {
        c = getopt_long (argc, argv, "i:b:o:u:d:hv:m",
                         long_options, &option_index);
        if (c == -1)
            break;

        switch(c) {
        case 'i': op->interface = optarg; break;
        case 'o': op->output = optarg; break;
        case 'u': op->unpack = optarg; break;
        case 'd': op->description = optarg; break;
        case 'h': print_help(); exit(0); break;
        case 'v': op->proc_version_string = optarg; break;
        case 'V': op->version = optarg; break;
        case INFO_OPTION:
            op->info = 1; break;
        case 'm':
            op->match = 1; break;
        default:
            fprintf (stderr, "Invalid commandline, please run `%s --help` "
                     "for usage information.\n", argv[0]);
            exit (0);
        }
    }

    if (optind < argc) {
        fprintf (stderr, "Unrecognized arguments: ");
        while (optind < argc)
            fprintf (stderr, "%s ", argv[optind++]);
        fprintf (stderr, "\n");
        fprintf (stderr, "Invalid commandline, please run `%s --help` for "
                 "usage information.\n", argv[0]);
        exit (0);
    }
    
    /* validate options */
    
    if (!op->unpack && !(op->interface && op->proc_version_string)) {
        
        fprintf (stderr, "Incorrect options specified; please run "
                 "`%s --help` for usage information.\n", argv[0]);
        exit(1);
    }
    
    if (!op->info && !op->match && !op->output) {
        fprintf(stderr, "Output file not specified.\n");
        exit(1);
    }

    if (!op->version) {
        fprintf(stderr, "Driver version string not specified.\n");
        exit(1);
    }

    return op;

} /* parse_commandline() */



static char *read_proc_version(void)
{
    int fd, ret, len, version_len;
    char *version, *c = NULL;
    
    fd = nv_open(PROC_VERSION, O_RDONLY, 0);

    /*
     * it would be more convenient if we could just mmap(2)
     * /proc/version, but that's not supported, so just read in the
     * whole file
     */
    
    len = version_len = 0;
    version = NULL;

    while (1) {
        if (version_len == len) {
            version_len += NV_VERSION_LEN;
            version = realloc(version, version_len);
            c = version + len;
        }
        ret = read(fd, c, version_len - len);
        if (ret == -1) {
            fprintf(stderr, "Error reading %s (%s).\n",
                    PROC_VERSION, strerror(errno));
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

    return version;

} /* read_proc_version() */



/*
 * check_match() - read /proc/version, and do a strcmp with str.
 * Returns 1 if the strings match, 0 if they don't match.
 */

static int check_match(char *str)
{
    int ret = 0;
    char *version = read_proc_version();
    
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
 * encode_uint32() - given a uint32, and a 4 byte data buffer, write
 * the integer to the data buffer.
 */

static void encode_uint32(uint32 val, uint8 data[4])
{
    data[0] = ((val >> 0)  & 0xff);
    data[1] = ((val >> 8)  & 0xff);
    data[2] = ((val >> 16) & 0xff);
    data[3] = ((val >> 24) & 0xff);

} /* encode_uint32() */



/*
 * decode_uint32() - given an index into a buffer, read the next 4
 * bytes, and build a uint32.
 */

static uint32 decode_uint32(char *buf)
{
    uint32 ret = 0;

    ret += (((uint32) buf[3]) & 0xff);
    ret <<= 8;

    ret += (((uint32) buf[2]) & 0xff);
    ret <<= 8;

    ret += (((uint32) buf[1]) & 0xff);
    ret <<= 8;

    ret += (((uint32) buf[0]) & 0xff);
    ret <<= 0;

    return ret;

} /* decode_uint32() */



/*
 * pack() - pack the specified precompiled kernel interface file,
 * prepended with a header, the CRC the driver version, a description
 * string, and the proc version string.
 */ 

static int pack(Options *op)
{
    int fd, offset, src_fd;
    uint8 *out, *src, data[4];
    uint32 crc;
    int version_len, description_len, proc_version_len;
    int interface_len, total_len;
    
    /*
     * get the lengths of the description, the proc version string,
     * and the interface file.
     */

    version_len = strlen(op->version);
    description_len = strlen(op->description);
    proc_version_len = strlen(op->proc_version_string);
    interface_len = nv_get_file_length(op->interface);
    
    total_len = CONSTANT_LENGTH +
        version_len + description_len + proc_version_len + interface_len;
    
    /* compute the crc of the kernel interface */

    crc = compute_crc(NULL, op->interface);

    /* open the output file for writing */
    
    fd = nv_open(op->output, O_CREAT|O_RDWR|O_TRUNC,
                 S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    
    /* set the output file length */

    nv_set_file_length(op->output, fd, total_len);
    
    /* map the input file */

    out = nv_mmap(op->output, total_len, PROT_READ|PROT_WRITE,
                  MAP_FILE|MAP_SHARED, fd);
    offset = 0;

    /* write the header */
    
    memcpy(&(out[0]), "NVIDIA  ", 8);
    offset += 8;

    /* write the crc */

    encode_uint32(crc, data);
    memcpy(&(out[offset]), data, 4);
    offset += 4;

    /* write the version */

    encode_uint32(version_len, data);
    memcpy(&(out[offset]), data, 4);
    offset += 4;
    
    if (version_len) {
        memcpy(&(out[offset]), op->version, version_len);
        offset += version_len;
    }

    /* write the description */

    encode_uint32(description_len, data);
    memcpy(&(out[offset]), data, 4);
    offset += 4;

    if (description_len) {
        memcpy(&(out[offset]), op->description, description_len);
        offset += description_len;
    }

    /* write the proc version string */
    
    encode_uint32(proc_version_len, data);
    memcpy(&(out[offset]), data, 4);
    offset += 4;
    
    memcpy(&(out[offset]), op->proc_version_string, proc_version_len);
    offset += proc_version_len;

    /* open the precompiled kernel module interface for reading */

    src_fd = nv_open(op->interface, O_RDONLY, 0);
    
    /* mmap() the kernel module interface */

    src = nv_mmap(op->interface, interface_len, PROT_READ,
                  MAP_FILE|MAP_SHARED, src_fd);
    
    memcpy(out + offset, src, interface_len);

    /* unmap src and dst */

    munmap(src, interface_len);
    munmap(out, total_len);
    
    close(src_fd);
    close(fd);

    return 0;

} /* pack() */



/*
 * unpack() - unpack the specified package
 */

static int unpack(Options *op)
{
    int dst_fd, fd, ret, offset, len = 0;
    char *buf, *dst;
    uint32 crc, val, size;
    char *version, *description, *proc_version_string;

    fd = dst_fd = 0;
    buf = dst = NULL;
    ret = 1;
    
    /* open the file to be unpacked */
    
    fd = nv_open(op->unpack, O_RDONLY, 0);
    
    /* get the file length */
    
    size = nv_get_file_length(op->unpack);

    /* check for a minimum length */

    if (size < CONSTANT_LENGTH) {
        fprintf(stderr, "File '%s' appears to be too short.\n", op->unpack);
        goto done;
    }
    
    /* mmap(2) the input file */

    buf = nv_mmap(op->unpack, size, PROT_READ, MAP_FILE|MAP_SHARED, fd);
    offset = 0;

    /* check for the header */

    if (strncmp(buf, "NVIDIA  ", 8) != 0) {
        fprintf(stderr, "File '%s': unrecognized file format.\n", op->unpack);
        goto done;
    }
    offset += 8;

    /* read the crc */

    crc = decode_uint32(buf + offset);
    offset += 4;
    
    /* read the version */

    val = decode_uint32(buf + offset);
    offset += 4;

    if (val > 0) {
        version = nv_alloc(val+1);
        memcpy(version, buf + offset, val);
        version[val] = '\0';
    } else {
        version = NULL;
    }
    offset += val;

    /* read the description */

    val = decode_uint32(buf + offset);
    offset += 4;
    if ((val + CONSTANT_LENGTH) > size) {
        fprintf(stderr, "Invalid file.\n");
        goto done;
    }
    if (val > 0) {
        description = nv_alloc(val+1);
        memcpy(description, buf + offset, val);
        description[val] = '\0';
    } else {
        description = NULL;
    }
    offset += val;
    
    /* read the proc version string */

    val = decode_uint32(buf + offset);
    offset += 4;
    if ((val + CONSTANT_LENGTH) > size) {
        fprintf(stderr, "Invalid file.\n");
        goto done;
    }
    proc_version_string = nv_alloc(val+1);
    memcpy(proc_version_string, buf + offset, val);
    offset += val;
    proc_version_string[val] = '\0';

    /*
     * if info was requested, print the description, driver version,
     * crc, proc version, and exit
     */
    
    if (op->info) {
        printf("description: %s\n", description);
        printf("version: %s\n", version);
        printf("crc: %u\n", crc);
        printf("proc version: %s\n", proc_version_string);
        return 0;
    }
    
    /* check if the running kernel matches */

    if (op->match) {
        return check_match(proc_version_string);
    }
    
    /* extract kernel interface module */
    
    len = size - offset;

    dst_fd = nv_open(op->output, O_CREAT | O_RDWR | O_TRUNC,
                     S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    
    /* set the output file length */

    nv_set_file_length(op->output, dst_fd, len);
    
    /* mmap the dst */

    dst = nv_mmap(op->output, len, PROT_READ|PROT_WRITE,
                  MAP_FILE|MAP_SHARED, dst_fd);
    
    /* copy */
    
    memcpy(dst, buf + offset, len);
    
    ret = 0;

 done:
    if (dst) munmap(dst, len);
    if (buf) munmap(buf, size);
    if (fd > 0) close(fd);
    if (dst_fd > 0) close(dst_fd);
    return ret;
    
} /* unpack() */



/*
 * program entry point
 */

int main(int argc, char *argv[])
{
    Options *op;
    int ret;

    op = parse_commandline(argc, argv);

    if (op->unpack) {
        ret = unpack(op);
    } else { /* pack */
        ret = pack(op);
    }
 
    return ret;

} /* main() */
