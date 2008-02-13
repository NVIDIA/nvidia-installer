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
 * misc.c - this source file contains miscellaneous routines for use
 * by the nvidia-installer.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <libgen.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "kernel.h"
#include "files.h"
#include "misc.h"
#include "crc.h"

static int check_symlink(Options*, const char*, const char*, const char*);
static int check_file(Options*, const char*, const mode_t, const uint32);
static char *find_system_util(const char *util);


/*
 * nvalloc() - malloc wrapper that checks for errors, and zeros out
 * the memory; if an error occurs, an error is printed to stderr and
 * exit is called -- this function will only return on success.
 */

void *nvalloc(size_t size)
{
    void *m = malloc(size);
    
    if (!m) {
        fprintf(stderr, "%s: memory allocation failure (%s)! \n",
                PROGRAM_NAME, strerror(errno));
        exit(1);
    }
    memset((char *) m, 0, size);
    return m;
    
} /* nvalloc() */



/*
 * nvrealloc() - realloc wrapper that checks for errors; if an error
 * occurs, an error is printed to stderr and exit is called -- this
 * function will only return on success.
 */

void *nvrealloc(void *ptr, size_t size)
{
    void *m;

    if (ptr == NULL) return nvalloc(size);
    
    m = realloc(ptr, size);
    if (!m) {
        fprintf(stderr, "%s: memory re-allocation failure (%s)! \n",
                PROGRAM_NAME, strerror(errno));
        exit(1);
    }
    return m;

} /* nvrealloc() */



/*
 * nvstrdup() - wrapper for strdup() that checks the return value; if
 * an error occurs, an error is printed to stderr and exit is called
 * -- this function will only return on success.
 */

char *nvstrdup(const char *s)
{
    char *m;

    if (!s) return NULL;

    m = strdup(s);
    
    if (!m) {
        fprintf(stderr, "%s: memory allocation failure during strdup (%s)! \n",
                PROGRAM_NAME, strerror(errno));
        exit(1);
    }
    return m;
    
} /* nvstrdup() */



/*
 * nvfree() - 
 */
void nvfree(char *s)
{
    if (s) free(s);

} /* nvfree() */



/*
 * nvstrtolower() - convert the given string to lowercase.
 */

char *nvstrtolower(char *s)
{
    char *start = s;

    if (s == NULL) return NULL;

    while (*s) {
        *s = tolower(*s);
        s++;
    }

    return start;

} /* nvstrtolower() */



/*
 * nvstrcat() - allocate a new string, copying all given strings
 * into it.  taken from glib
 */

char *nvstrcat(const char *str, ...)
{
    unsigned int l;
    va_list args;
    char *s;
    char *concat;
  
    l = 1 + strlen(str);
    va_start(args, str);
    s = va_arg(args, char *);

    while (s) {
        l += strlen(s);
        s = va_arg(args, char *);
    }
    va_end(args);
  
    concat = nvalloc(l);
    concat[0] = 0;
  
    strcat(concat, str);
    va_start(args, str);
    s = va_arg(args, char *);
    while (s) {
        strcat(concat, s);
        s = va_arg(args, char *);
    }
    va_end(args);
  
    return concat;

} /* nvstrcat() */



/*
 * read_next_word() - given a string buf, skip any whitespace, and
 * then copy the next set of characters until more white space is
 * encountered.  A new string containing this next word is returned.
 * The passed-by-reference parameter e, if not NULL, is set to point
 * at the where the end of the word was, to facilitate multiple calls
 * of read_next_word().
 */

char *read_next_word (char *buf, char **e)
{
    char *c = buf;
    char *start, *ret;
    int len;
    
    while ((*c) && (isspace (*c)) && (*c != '\n')) c++;
    start = c;
    while ((*c) && (!isspace (*c)) && (*c != '\n')) c++;
    
    len = c - start;

    if (len == 0) return NULL;
    
    ret = (char *) nvalloc (len + 1);

    strncpy (ret, start, len);
    ret[len] = '\0';

    if (e) *e = c;

    return ret;
    
} /* read_next_word() */



/*
 * check_euid() - this function checks that the effective uid of this
 * application is root, and calls the ui to print an error if it's not
 * root.
 */

int check_euid(Options *op)
{
    uid_t euid;

    euid = geteuid();
    
    if (euid != 0) {
        ui_error(op, "nvidia-installer must be run as root");
        return FALSE;
    }
    
    return TRUE;

} /* check_euid() */



/*
 * check_runlevel() - attempt to run the `runlevel` program.  If we
 * are in runlevel 1, explain why that is bad, and ask the user if
 * they want to continue anyway.
 */

int check_runlevel(Options *op)
{
    int ret;
    char *data, *cmd;
    char ignore, runlevel;

    if (op->no_runlevel_check) return TRUE;

    cmd = find_system_util("runlevel");
    if (!cmd) {
        ui_warn(op, "Skipping the runlevel check (the utility "
                "`runlevel` was not found)."); 
        return TRUE;
    }

    ret = run_command(op, cmd, &data, FALSE, FALSE, TRUE);
    nvfree(cmd);
    
    if ((ret != 0) || (!data)) {
        ui_warn(op, "Skipping the runlevel check (the utility "
                "`runlevel` failed to run)."); 
        return TRUE;
    }

    ret = sscanf(data, "%c %c", &ignore, &runlevel);

    if (ret != 2) {
        ui_warn(op, "Skipping the runlevel check (unrecognized output from "
                "the `runlevel` utility: '%d').", data);
        nvfree(data);
        return TRUE;
    }

    nvfree(data);

    if (runlevel == 's' || runlevel == 'S' || runlevel == '1') {
        ret = ui_yes_no(op, TRUE, "You appear to be running in runlevel 1; "
                        "this may cause problems.  For example: some "
                        "distributions that use devfs do not run the devfs "
                        "daemon in runlevel 1, making it difficult for "
                        "nvidia-installer to correctly setup the kernel "
                        "module configuration files.  It is recommended "
                        "that you quit installation now and switch to "
                        "runlevel 3 (`telinit 3`) before installing.\n\n"
                        "Quit installation now? (select 'No' to continue "
                        "installation)");
        
        if (ret) return FALSE;
    }

    return TRUE;

} /* check_runlevel() */



/* 
 * adjust_cwd() - this function scans through program_name (ie
 * argv[0]) for any possible relative paths, and chdirs into the
 * relative path it finds.  The point of all this is to make the
 * directory with the executed binary the cwd.
 *
 * It is assumed that the user interface has not yet been initialized
 * by the time this function is called.
 */

int adjust_cwd(Options *op, const char *program_name)
{
    char *c, *path;
    int len;
    
    /*
     * extract any pathname portion out of the program_name and chdir
     * to it
     */
    
    c = strrchr(program_name, '/');
    if (c) {
        len = c - program_name + 1;
        path = (char *) nvalloc(len + 1);
        strncpy(path, program_name, len);
        path[len] = '\0';
        if (op->expert) log_printf(op, TRUE, NULL, "chdir(\"%s\")", path);
        if (chdir(path)) {
            fprintf(stderr, "Unable to chdir to %s (%s)",
                    path, strerror(errno));
            return FALSE;
        }
        free(path);
    }
    
    return TRUE;
    
} /* adjust_cwd() */



/*
 * fget_next_line() - read from the given FILE stream until a newline,
 * EOF, or null terminator is encountered, writing data into a
 * growable buffer.  The eof parameter is set to TRUE when EOF is
 * encountered.  In all cases, the returned string is null-terminated.
 *
 * XXX this function will be rather slow because it uses fgetc() to
 * pull each character off the stream one at a time; this is done so
 * that each character can be examined as it's read so that we can
 * appropriately deal with EOFs and newlines.  A better implementation
 * would use fgets(), but that would still require us to parse each
 * read line, checking for newlines or guessing if we hit an EOF.
 */

char *fget_next_line(FILE *fp, int *eof)
{
    char *buf = NULL, *tmpbuf;
    char *c = NULL;
    int len = 0, buflen = 0;
    
    if (eof) *eof = FALSE;
    
    while (1) {
        if (buflen == len) { /* buffer isn't big enough -- grow it */
            buflen += NV_LINE_LEN;
            tmpbuf = (char *) nvalloc (buflen);
            if (buf) {
                memcpy (tmpbuf, buf, len);
                free (buf);
            }
            buf = tmpbuf;
            c = buf + len;
        }

        *c = fgetc(fp);
        
        if ((*c == EOF) && (eof)) *eof = TRUE;
        if ((*c == EOF) || (*c == '\n') || (*c == '\0')) {
            *c = '\0';
            return buf;
        }

        len++;
        c++;

    } /* while (1) */
    
    return NULL; /* should never get here */
   
} /* fget_next_line() */



/*
 * get_next_line() - this function scans for the next newline or
 * carriage return in buf.  If non-NULL, the passed-by-reference
 * parameter e is set to point to the next printable character in the
 * buffer, or NULL if EOF is encountered.
 *
 * On success, a newly allocated buffer is allocated containing the
 * next line of text (with a NULL terminator in place of the
 * newline/carriage return).
 *
 * On error, NULL is returned.
 */

char *get_next_line(char *buf, char **e)
{
    char *c, *retbuf;
    int len;

    if (e) *e = NULL;
    
    if ((!buf) || (*buf == '\0') || (*buf == EOF)) return NULL;

    c = buf;
    while ((*c != '\0') && (*c != EOF) && (*c != '\n') && (*c != '\r')) c++;

    len = c - buf;
    retbuf = nvalloc(len + 1);
    strncpy(retbuf, buf, len);
    retbuf[len] = '\0';
    
    if (e) {
        while ((*c != '\0') && (*c != EOF) && (!isprint(*c))) c++;
        if ((*c == '\0') || (*c == EOF)) *e = NULL;
        else *e = c;
    }
    
    return retbuf;

} /* get_next_line() */



/*
 * run_command() - this function runs the given command and assigns
 * the data parameter to a malloced buffer containing the command's
 * output, if any.  The caller of this function should free the data
 * string.  The return value of the command is returned from this
 * function.
 *
 * The output parameter controls whether command output is sent to the
 * ui; if this is TRUE, then everyline of output that is read is sent
 * to the ui.
 *
 * If the status parameter is greater than 0, it is interpretted as a
 * rough estimate of how many lines of output will be generated by the
 * command.  This is used to compute the value that should be passed
 * to ui_status_update() for every line of output that is received.
 *
 * The redirect argument tells run_command() to redirect stderr to
 * stdout so that all output is collected, or just stdout.
 *
 * XXX maybe we should do something to cap the time we allow the
 * command to run?
 */

int run_command(Options *op, const char *cmd, char **data, int output,
                int status, int redirect)
{
    int n, len, buflen, ret;
    char *cmd2, *buf, *tmpbuf;
    FILE *stream = NULL;
    float percent;
    
    if (data) *data = NULL;

    /*
     * if command output is requested, print the command that we will
     * execute
     */

    if (output) ui_command_output (op, "executing: '%s'...", cmd);

    /* redirect stderr to stdout */

    if (redirect) {
        cmd2 = nvstrcat(cmd, " 2>&1", NULL);
    } else {
        cmd2 = nvstrdup(cmd);
    }
    
    /*
     * Open a process by creating a pipe, forking, and invoking the
     * command.
     */
    
    if ((stream = popen(cmd2, "r")) == NULL) {
        ui_error(op, "Failure executing command '%s' (%s).",
                 cmd, strerror(errno));
        return errno;
    }
    
    free(cmd2);

    /*
     * read from the stream, filling and growing buf, until we hit
     * EOF.  Send each line to the ui as it is read.
     */
    
    len = 0;    /* length of what has actually been read */
    buflen = 0; /* length of destination buffer */
    buf = NULL;
    n = 0;      /* output line counter */

    while (1) {
        
        if ((buflen - len) < NV_MIN_LINE_LEN) {
            buflen += NV_LINE_LEN;
            tmpbuf = (char *) nvalloc(buflen);
            if (buf) {
                memcpy(tmpbuf, buf, len);
                free(buf);
            }
            buf = tmpbuf;
        }
        
        if (fgets(buf + len, buflen - len, stream) == NULL) break;
        
        if (output) ui_command_output(op, buf + len);
        
        len += strlen(buf + len);

        if (status) {
            n++;
            if (n > status) n = status;
            percent = (float) n / (float) status;
            ui_status_update(op, percent, NULL);
        }
    } /* while (1) */

    /* Close the popen()'ed stream. */

    ret = pclose(stream);

    /* if the last character in the buffer is a newline, null it */
    
    if ((len > 0) && (buf[len-1] == '\n')) buf[len-1] = '\0';
    
    if (data) *data = buf;
    else free(buf);
    
    return ret;
    
} /* run_command() */



/*
 * find_system_utils() - search the $PATH (as well as some common
 * additional directories) for the utilities that the installer will
 * need to use.  Returns TRUE on success and assigns the util fields
 * in the option struct; it returns FALSE on failure.
 *
 * XXX requiring ld may cause problems
 */

#define EXTRA_PATH "/bin:/usr/bin:/sbin:/usr/sbin"

int find_system_utils(Options *op)
{
    /* keep in sync with the SystemUtils enum type */
    const struct { const char *util, *package; } needed_utils[] = {
        { "ldconfig", "glibc" },
        { "ldd",      "glibc" },
        { "ld",       "binutils" },
        { "objcopy",  "binutils" },
        { "grep",     "grep" },
        { "dmesg",    "util-linux" },
        { "tail",     "coreutils" },
        { "cut",      "coreutils" }
    };
    
    int i;

    ui_expert(op, "Searching for system utilities:");

    /* search the PATH for each utility */

    for (i = 0; i < MAX_SYSTEM_UTILS; i++) {
        op->utils[i] = find_system_util(needed_utils[i].util);
        if (!op->utils[i]) {
            ui_error(op, "Unable to find the system utility `%s`; please "
                     "make sure you have the package '%s' installed.  If "
                     "you do have %s installed, then please check that "
                     "`%s` is in your PATH.",
                     needed_utils[i].util, needed_utils[i].package,
                     needed_utils[i].package, needed_utils[i].util);
            return FALSE;
        }
        
        ui_expert(op, "found `%s` : `%s`",
                  needed_utils[i].util, op->utils[i]);
    }
    
    return TRUE;

} /* find_system_utils() */



typedef struct {
    const char *util;
    const char *package;
} Util;

/* keep in sync with the ModuleUtils enum type */
static Util __module_utils[] = {
    { "insmod",   "module-init-tools" },
    { "modprobe", "module-init-tools" },
    { "rmmod",    "module-init-tools" },
    { "lsmod",    "module-init-tools" },
    { "depmod",   "module-init-tools" },
};

/* keep in sync with the ModuleUtils enum type */
static Util __module_utils_linux24[] = {
    { "insmod",   "modutils" },
    { "modprobe", "modutils" },
    { "rmmod",    "modutils" },
    { "lsmod",    "modutils" },
    { "depmod",   "modutils" },
};

/*
 * find_module_utils() - search the $PATH (as well as some common
 * additional directories) for the utilities that the installer will
 * need to use.  Returns TRUE on success and assigns the util fields
 * in the option struct; it returns FALSE on failures.
 */

int find_module_utils(Options *op)
{
    int i, j = 0;
    Util *needed_utils = __module_utils;

    if (strncmp(get_kernel_name(op), "2.4", 3) == 0)
        needed_utils = __module_utils_linux24;

    ui_expert(op, "Searching for module utilities:");

    /* search the PATH for each utility */

    for (i = MAX_SYSTEM_UTILS; i < MAX_UTILS; i++, j++) {
        op->utils[i] = find_system_util(needed_utils[j].util);
        if (!op->utils[i]) {
            ui_error(op, "Unable to find the module utility `%s`; please "
                     "make sure you have the package '%s' installed.  If "
                     "you do have %s installed, then please check that "
                     "`%s` is in your PATH.",
                     needed_utils[j].util, needed_utils[j].package,
                     needed_utils[j].package, needed_utils[j].util);
            return FALSE;
        }

        ui_expert(op, "found `%s` : `%s`",
                  needed_utils[j].util, op->utils[i]);
    };

    return TRUE;

} /* find_module_utils() */


/*
 * check_development_tools() - check if the development tools needed
 * to build custom kernel interfaces are available.
 */

int check_development_tools(Options *op)
{
#define MAX_TOOLS 2
    const struct { char *tool, *package; } needed_tools[] = {
        { "cc",   "gcc"  },
        { "make", "make" }
    };

    int i;
    char *tool;

    ui_expert(op, "Checking development tools:");

    for (i = 0; i < MAX_TOOLS; i++) {
        tool = find_system_util(needed_tools[i].tool);
        if (!tool) {
            ui_error(op, "Unable to find the development tool `%s` in "
                     "your path; please make sure that you have the "
                     "package '%s' installed. If %s is installed on your "
                     "system, then please check that `%s` is in your "
                     "PATH.",
                     needed_tools[i].tool, needed_tools[i].package,
                     needed_tools[i].package, needed_tools[i].tool);
            return FALSE;
        }

        ui_expert(op, "found `%s` : `%s`", needed_tools[i].tool, tool);
    }

    return TRUE;

} /* check_development_tools() */


/*
 * find_system_util() - build a search path and search for the named
 * utility.  If the utility is found, the fully qualified path to the
 * utility is returned.  On failure NULL is returned.
 */

static char *find_system_util(const char *util)
{
    char *buf, *path, *file, *x, *y;
    
    /* build the search path */
    
    buf = getenv("PATH");
    if (buf) {
        path = nvstrcat(buf, ":", EXTRA_PATH, NULL);
    } else {
        path = nvstrdup(EXTRA_PATH);
    }

    /* search the PATH for the utility */

    for (x = y = path; *x; x++) {
        if (*x == ':') {
            *x = '\0';
            file = nvstrcat(y, "/", util, NULL);
            *x = ':';
            if ((access(file, F_OK | X_OK)) == 0) {
                nvfree(path);
                return file;
            }
            nvfree(file);
            y = x + 1;
        }
    }

    nvfree(path);

    return NULL;

} /* find_system_util() */



/*
 * nvid_version() - parse the given nvid string for the version
 * number, and assign major, minor and patch.  Returns TRUE on
 * success, FALSE on failure.
 *
 * The version format is assumed to be: is X.Y-ZZZZ
 */

int nvid_version (const char *str, int *major, int *minor, int *patch)
{
    char *s, *x;
    int ret = FALSE;

    s = nvstrdup(str);
    x = s;

    while (*x) {
        if (((x[0]) && isdigit(x[0])) &&
            ((x[1]) && (x[1] == '.')) &&
            ((x[2]) && isdigit(x[2])) &&
            ((x[3]) && (x[3] == '-')) &&
            ((x[4]) && isdigit(x[4])) &&
            ((x[5]) && isdigit(x[5])) &&
            ((x[6]) && isdigit(x[6])) &&
            ((x[7]) && isdigit(x[7]))) {
            
            x[1] = x[3] = x[8] = '\0';
            
            *major = atoi(&x[0]);
            *minor = atoi(&x[2]);
            *patch = atoi(&x[4]);
            
            ret = TRUE;
            break;
        }
        x++;
    }

    free(s);
    return ret;
    
} /* nvid_version() */



/*
 * continue_after_error() - tell the user that an error has occured,
 * and ask them if they would like to continue.
 *
 * Returns TRUE if the installer should continue.
 */

int continue_after_error(Options *op, const char *fmt, ...)
{
    char *msg;
    int ret;

    NV_VSNPRINTF(msg, fmt);
    
    ret = ui_yes_no(op, TRUE, "The installer has encountered the following "
                    "error during installation: '%s'.  Continue anyway? "
                    "(\"no\" will abort)?", msg);

    return ret;

} /* continue_after_error() */



/*
 * do_install()
 */

int do_install(Options *op, Package *p, CommandList *c)
{
    char *msg;
    int len, ret;

    len = strlen(p->description) + strlen(p->version_string) + 64;
    msg = (char *) nvalloc(len);
    snprintf(msg, len, "Installing '%s' (%s):",
             p->description, p->version_string);
    
    ret = execute_command_list(op, c, msg, "Installing");
    
    free(msg);
    
    if (!ret) return FALSE;
    
    ui_log(op, "Driver file installation is complete.", p->description);

    return TRUE;

} /* do_install() */



/*
 * should_install_opengl_headers() - if in expert mode, ask the user
 * if they want to install OpenGL header files.
 */

void should_install_opengl_headers(Options *op, Package *p)
{
    int i, have_headers = FALSE;
    
    if (!op->expert) return;

    /*
     * first, scan through the package to see if we have any header
     * files to install
     */

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].flags & FILE_TYPE_OPENGL_HEADER) {
            have_headers = TRUE;
            break;
        }
    }

    if (!have_headers) return;
    
    /*
     * If we're to provide more verbose descriptions, we could present
     * something like this:
     *
     * ("The %s provides OpenGL header files; these are used when
     * compiling OpenGL applications.  Most Linux distributions
     * already have OpenGL header files installed (normally in the
     * /usr/include/GL/ directory).  If you don't have OpenGL header
     * files installed and would like to, or if you want to develop
     * OpenGL applications that take advantage of NVIDIA OpenGL
     * extensions, then you can install NVIDIA's OpenGL header files
     * at this time.", p->description);
     */

    op->opengl_headers = ui_yes_no(op, op->opengl_headers,
                                   "Install NVIDIA's OpenGL header files?");
    
    ui_expert(op, "Installation %s install the OpenGL header files.",
              op->opengl_headers ? "will" : "will not");

} /* should_install_opengl_headers() */


/*
 * should_install_compat32_files() - ask the user if he/she wishes to
 * install 32bit compatibily libraries.
 */

void should_install_compat32_files(Options *op, Package *p)
{
#if defined(NV_X86_64)
    int i, have_compat32_files = FALSE;

    /*
     * first, scan through the package to see if we have any
     * 32bit compatibility files to install.
     */

    for (i = 0; i < p->num_entries; i++) {
        if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
            have_compat32_files = TRUE;
            break;
        }
    }

    if (!have_compat32_files)
        return;

    if (!ui_yes_no(op, TRUE, "Install NVIDIA's 32bit compatibility "
                   "OpenGL libraries?")) {
        for (i = 0; i < p->num_entries; i++) {
            if (p->entries[i].flags & FILE_CLASS_COMPAT32) {
                /* invalidate file */
                p->entries[i].flags &= ~FILE_TYPE_MASK;
                p->entries[i].dst = NULL;
            }
        }
    }
#endif /* NV_X86_64 */
}


/*
 * check_installed_files_from_package() - scan through the entries in
 * the package, making sure that all symbolic links and files are
 * properly installed.
 */

void check_installed_files_from_package(Options *op, Package *p)
{
    int i, ret = TRUE;
    float percent;
    unsigned int installable_files;
    
    ui_status_begin(op, "Running post-install sanity check:", "Checking");

    installable_files = get_installable_file_mask(op);
    
    for (i = 0; i < p->num_entries; i++) {
        
        percent = (float) i / (float) p->num_entries;
        ui_status_update(op, percent, p->entries[i].dst);
        
        if (p->entries[i].flags & FILE_TYPE_SYMLINK) {
            if (!check_symlink(op, p->entries[i].target,
                               p->entries[i].dst,
                               p->description)) {
                ret = FALSE;
            }
        } else if (p->entries[i].flags & installable_files) {
            if (!check_file(op, p->entries[i].dst, p->entries[i].mode, 0)) {
                ret = FALSE;
            }
        }
    }

    ui_status_end(op, "done.");
    ui_log(op, "Post-install sanity check %s.", ret ? "passed" : "failed");

} /* check_installed_files_from_package() */



/*
 * check_symlink() - check that the specified symbolic link exists and
 * point to the correct target.  Print descriptive warnings if
 * anything about the symbolic link doesn't appear as it should.
 *
 * Returns FALSE if the symbolic link appeared wrong; returns TRUE if
 * everything appears in order.
 */

static int check_symlink(Options *op, const char *target, const char *link,
                         const char *descr)
{
    char *actual_target;

    actual_target = get_symlink_target(op, link);
    if (!actual_target) {
        ui_warn(op, "The symbolic link '%s' does not exist.  This is "
                "necessary for correct operation of the %s.  You can "
                "create this symbolic link manually by executing "
                "`ln -sf %s %s`.",
                link,
                descr,
                target,
                link);
        return FALSE;
    } 

    if (strcmp(actual_target, target) != 0) {
        ui_warn(op, "The symbolic link '%s' does not point to '%s' "
                "as is necessary for correct operation of the %s.  "
                "It is possible that `ldconfig` has created this "
                "incorrect symbolic link because %s's "
                "\"soname\" conflicts with that of %s.  It is "
                "recommended that you remove or rename the file "
                "'%s' and create the necessary symbolic link by "
                "running `ln -sf %s %s`.",
                link,
                target,
                descr,
                actual_target,
                target,
                actual_target,
                target,
                link);
        free(actual_target);
        return FALSE;
    }
    return TRUE;
    
} /* check_symlink() */



/*
 * check_file() - check that the specified installed file exists, has
 * the correct permissions, and has the correct crc.
 *
 * If anything is incorrect, print a warning and return FALSE,
 * otherwise return TRUE.
 */

static int check_file(Options *op, const char *filename,
                      const mode_t mode, const uint32 crc)
{
    struct stat stat_buf;
    uint32 actual_crc;

    if (lstat(filename, &stat_buf) == -1) {
        ui_warn(op, "Unable to find installed file '%s' (%s).",
                filename, strerror(errno));
        return FALSE;
    }

    if (!S_ISREG(stat_buf.st_mode)) {
        ui_warn(op, "The installed file '%s' is not of the correct filetype.",
                filename);
        return FALSE;
    }

    if ((stat_buf.st_mode & PERM_MASK) != (mode & PERM_MASK)) {
        ui_warn(op, "The installed file '%s' has permissions %04o, but it "
                "was installed with permissions %04o.", filename,
                (stat_buf.st_mode & PERM_MASK),
                (mode & PERM_MASK));
        return FALSE;
    }

    /* only check the crc if we were handed a non-emtpy crc */

    if (crc != 0) {
        actual_crc = compute_crc(op, filename);
        if (crc != actual_crc) {
            ui_warn(op, "The installed file '%s' has a different checksum "
                    "(%ul) than when it was installed (%ul).",
                    filename, actual_crc, crc);
            return FALSE;
        }
    }

    return TRUE;
    
} /* check_file() */



/*
 * get_installable_file_mask() - return the mask of what file types
 * should be considered installable.
 */

unsigned int get_installable_file_mask(Options *op)
{
    unsigned int installable_files = FILE_TYPE_INSTALLABLE_FILE;
    if (!op->opengl_headers) installable_files &= ~FILE_TYPE_OPENGL_HEADER;

    return installable_files;

} /* get_installable_file_mask() */



/*
 * tls_test() - Starting with glibc 2.3, there is a new thread local
 * storage mechanism.  To accomodate this, NVIDIA's OpenGL libraries
 * are built both the "classic" way, and the new way.  To determine
 * which set of OpenGL libraries to install, execute the test program
 * stored in tls_test_array.  If the program returns 0 we should
 * install the new tls libraries; if it returns anything else, we
 * should install the "classic" libraries.
 *
 * So as to avoid any risk of not being able to find the tls_test
 * binary at run time, the test program is stored as static data
 * inside the installer binary (in the same way that the user
 * interface shared libraries are)... see
 * user_interface.c:extract_user_interface() for details.
 *
 * Return TRUE if the new tls libraries should be installed; FALSE if
 * the old libraries should be used.
 */

/* pull in the array and size from g_tls_test.c */

extern const unsigned char tls_test_array[];
extern const int tls_test_array_size;

/* pull in the array and size from g_tls_test_dso.c */

extern const unsigned char tls_test_dso_array[];
extern const int tls_test_dso_array_size;



#if defined(NV_X86_64)

/* pull in the array and size from g_tls_test_32.c */

extern const unsigned char tls_test_array_32[];
extern const int tls_test_array_32_size;

/* pull in the array and size from g_tls_test_dso_32.c */

extern const unsigned char tls_test_dso_array_32[];
extern const int tls_test_dso_array_32_size;

#endif /* NV_X86_64 */


/* forward prototype */

static int tls_test_internal(Options *op, int which_tls,
                             const unsigned char *test_array,
                             const int test_array_size,
                             const unsigned char *dso_test_array,
                             const int dso_test_array_size);



int tls_test(Options *op, int compat_32_libs)
{
    if (compat_32_libs) {
        
#if defined(NV_X86_64)
        return tls_test_internal(op, op->which_tls_compat32,
                                 tls_test_array_32,
                                 tls_test_array_32_size,
                                 tls_test_dso_array_32,
                                 tls_test_dso_array_32_size);
#else
        return FALSE;
#endif /* NV_X86_64 */        
        
    } else {
        return tls_test_internal(op, op->which_tls,
                                 tls_test_array,
                                 tls_test_array_size,
                                 tls_test_dso_array,
                                 tls_test_dso_array_size);
    }
} /* tls_test */



/*
 * tls_test_internal() - this is the routine that does all the work to
 * write the tests to file and execute them; the caller (tls_test())
 * just selects which array data is used as the test.
 */

static int tls_test_internal(Options *op, int which_tls,
                             const unsigned char *test_array,
                             const int test_array_size,
                             const unsigned char *test_dso_array,
                             const int test_dso_array_size)
{
    int ret = FALSE;
    char *tmpfile = NULL, *dso_tmpfile = NULL, *cmd = NULL;
    
    /* allow commandline options to bypass this test */
    
    if (which_tls == FORCE_NEW_TLS) return TRUE;
    if (which_tls == FORCE_CLASSIC_TLS) return FALSE;
    
    /* check that we have the test program */

    if ((test_array == NULL) ||
        (test_array_size == 0) ||
        (test_dso_array == NULL) ||
        (test_dso_array_size == 0)) {
        ui_warn(op, "The thread local storage test program is not "
                "present; assuming classic tls.");
        return FALSE;
    }
    
    /* write the tls_test data to tmp files */
    
    tmpfile = write_temp_file(op, test_array_size, test_array,
                              S_IRUSR|S_IWUSR|S_IXUSR);
    
    if (!tmpfile) {
        ui_warn(op, "Unable to create temporary file for thread local "
                "storage test program (%s); assuming classic tls.",
                strerror(errno));
        goto done;
    }

    dso_tmpfile = write_temp_file(op, test_dso_array_size,
                                  test_dso_array,
                                  S_IRUSR|S_IWUSR|S_IXUSR);
    if (!dso_tmpfile) {
        ui_warn(op, "Unable to create temporary file for thread local "
                "storage test program (%s); assuming classic tls.",
                strerror(errno));
        goto done;
    }
    
    /* run the test */

    cmd = nvstrcat(tmpfile, " ", dso_tmpfile, NULL);
    
    ret = run_command(op, cmd, NULL, FALSE, 0, TRUE);
    
    ret = ((ret == 0) ? TRUE : FALSE);

 done:

    if (tmpfile) {
        unlink(tmpfile);
        nvfree(tmpfile);
    }

    if (dso_tmpfile) {
        unlink(dso_tmpfile);
        nvfree(dso_tmpfile);
    }

    if (cmd) nvfree(cmd);

    return ret;

} /* test_tls_internal() */



/*
 * check_runtime_configuration() - In the past, nvidia-installer has
 * frequently failed to backup/move all conflicting files prior to
 * installing the NVIDIA OpenGL libraries.  Consequently, some of the
 * installations considered successful by the installer didn't work
 * correctly.
 *
 * This sanity check attemps to verify that the correct libraries are
 * picked up by the runtime linker.  It returns TRUE on success and
 * FALSE on failure.
 */

/* pull in the array and size from g_rtld_test.c */

extern const unsigned char rtld_test_array[];
extern const int rtld_test_array_size;

#if defined(NV_X86_64)

/* pull in the array and size from g_rtld_test_32.c */

extern const unsigned char rtld_test_array_32[];
extern const int rtld_test_array_32_size;

#endif /* NV_X86_64 */


/* forward prototype */

static int rtld_test_internal(Options *op, Package *p,
                              int which_tls,
                              const unsigned char *test_array,
                              const int test_array_size,
                              int compat_32_libs);

int check_runtime_configuration(Options *op, Package *p)
{
    int ret = TRUE;

    ui_status_begin(op, "Running runtime sanity check:", "Checking");

#if defined(NV_X86_64)
    ret = rtld_test_internal(op, p, op->which_tls_compat32,
                             rtld_test_array_32,
                             rtld_test_array_32_size,
                             TRUE);
#endif /* NV_X86_64 */

    if (ret == TRUE) {
        ret = rtld_test_internal(op, p, op->which_tls,
                                 rtld_test_array,
                                 rtld_test_array_size,
                                 FALSE);
    }

    ui_status_end(op, "done.");
    ui_log(op, "Runtime sanity check %s.", ret ? "passed" : "failed");

    return ret;

} /* check_runtime_configuration() */


/*
 * collapse_multiple_slashes() - remove any/all occurances of "//" from the
 * argument string.
 */

void collapse_multiple_slashes(char *s)
{
    char *p;
    unsigned int i, len;

    while ((p = strstr(s, "//")) != NULL) {
        p++; /* advance to second '/' */
        while (*p == '/') {
            len = strlen(p);
            for (i = 0; i < len; i++) p[i] = p[i+1];
        }
    }
}


/*
 * rtld_test_internal() - this routine writes the test binaries to a file
 * and performs the test; the caller (rtld_test()) selects which array data
 * is used (native, compat_32).
 */

static int rtld_test_internal(Options *op, Package *p,
                              int which_tls,
                              const unsigned char *test_array,
                              const int test_array_size,
                              int compat_32_libs)
{
    int i, ret = TRUE;
    char *name = NULL, *cmd = NULL, *data = NULL;
    char *tmpfile, *s;
    struct stat stat_buf0, stat_buf1;

    if ((test_array == NULL) || (test_array_size == 0)) {
        ui_warn(op, "The runtime configuration test program is not "
                "present; assuming successful installation.");
        return TRUE;
    }

    /* write the rtld_test data to a temporary file */

    tmpfile = write_temp_file(op, test_array_size, test_array,
                              S_IRUSR|S_IWUSR|S_IXUSR);

    if (!tmpfile) {
        ui_warn(op, "Unable to create a temporary file for the runtime "
                "configuration test program (%s); assuming successful "
                "installation.", strerror(errno));
        goto done;
    }

    /* perform the test(s) */

    for (i = 0; i < p->num_entries; i++) {
        if (!(p->entries[i].flags & FILE_TYPE_RTLD_CHECKED))
            continue;
        else if ((which_tls & TLS_LIB_TYPE_FORCED) &&
                 (p->entries[i].flags & FILE_TYPE_TLS_LIB))
            continue;
#if defined(NV_X86_64)
        else if ((p->entries[i].flags & FILE_CLASS_NATIVE)
                 && compat_32_libs)
            continue;
        else if ((p->entries[i].flags & FILE_CLASS_COMPAT32)
                 && !compat_32_libs)
            continue;
#endif /* NV_X86_64 */
        else if ((which_tls == TLS_LIB_NEW_TLS) &&
                 (p->entries[i].flags & FILE_CLASS_CLASSIC_TLS))
            continue;
        else if ((which_tls == TLS_LIB_CLASSIC_TLS) &&
                 (p->entries[i].flags & FILE_CLASS_NEW_TLS))
            continue;

        name = nvstrdup(p->entries[i].name);
        if (!name) continue;

        s = strstr(name, ".so.1");
        if (!s) goto next;
        *(s + strlen(".so.1")) = '\0';

        cmd = nvstrcat(op->utils[LDD], " ", tmpfile, " | ",
                       op->utils[GREP], " ", name, " | ",
                       op->utils[CUT], " -d \" \" -f 3", NULL);

        if (run_command(op, cmd, &data, FALSE, 0, TRUE)) {
            ui_warn(op, "Unable to perform the runtime configuration "
                    "check (%s); assuming successful installation.",
                    strerror(errno));
            goto done;
        }

        nvfree(name); name = NULL;
        name = nvstrdup(p->entries[i].dst);
        if (!name) goto next;

        collapse_multiple_slashes(data);
        s = strstr(name, ".so.1");
        if (!s) goto next;
        *(s + strlen(".so.1")) = '\0';

        if ((strcmp(data, name) != 0)) {
            /*
             * XXX Handle the case where the same library is
             * referred to, once directly and once via a symbolic
             * link. This check is far from perfect, but should
             * get the job done.
             */

            if ((stat(data, &stat_buf0) == 0) &&
                (stat(name, &stat_buf1) == 0) &&
                (memcmp(&stat_buf0, &stat_buf1, sizeof(struct stat)) == 0))
                goto next;

            ui_error(op, "The runtime configuration check failed for "
                     "library '%s' (expected: '%s', found: '%s').  "
                     "The most likely reason for this is that conflicting "
                     "OpenGL libraries are installed in a location "
                     "not inspected by nvidia-installer.  Please be sure "
                     "you have uninstalled any third-party OpenGL and "
                     "third-party graphics driver packages.",
                     p->entries[i].name, name,
                     (data && strlen(data)) ? data : "(not found)");

            ret = FALSE;
            goto done;
        }

 next:
        nvfree(name); name = NULL;
        nvfree(cmd); cmd = NULL;
        nvfree(data); data = NULL;
    }

 done:
    if (tmpfile) {
        unlink(tmpfile);
        nvfree(tmpfile);
    }

    nvfree(name);
    nvfree(cmd);
    nvfree(data);

    return ret;

} /* rtld_test_internal() */


/*
 * get_distribution() - determine what distribution this is; only used
 * for several bits of distro-specific behavior requested by
 * distribution maintainers.
 *
 * XXX should we provide a commandline option to override this
 * detection?
 */

Distribution get_distribution(Options *op)
{
    if (access("/etc/SuSE-release", F_OK) == 0) return SUSE;
    if (access("/etc/UnitedLinux-release", F_OK) == 0) return UNITED_LINUX;
    if (access("/etc/debian_version", F_OK) == 0) return DEBIAN;

    return OTHER;
    
} /* get_distribution() */



/*
 * check_for_running_x() - running any X server (even with a
 * non-NVIDIA driver) can cause stability problems, so check that
 * there is no X server running.  To do this, scan for any
 * /tmp/.X[n]-lock files, where [n] is the number of the X Display
 * (we'll just check for 0-7).  If any X server is running, print an
 * error message and return FALSE.  If no X server is running, return
 * TRUE.
 */

int check_for_running_x(Options *op)
{
    char path[14];
    int i;

    /*
     * If we are installing for a non-running kernel *and* we are only
     * installing a kernel module, then skip this check.
     */

    if (op->kernel_module_only && op->kernel_name) {
        ui_log(op, "Only installing a kernel module for a non-running "
               "kernel; skipping the \"is an X server running?\" test.");
        return TRUE;
    }
    
    for (i = 0; i < 8; i++) {
        snprintf(path, 14, "/tmp/.X%1d-lock", i);
        if (access(path, R_OK) == 0) {
            ui_log(op, "The file '%s' exists... an X server appears to be "
                   "running", path);
            ui_error(op, "You appear to be running an X server; please exit "
                     "X before installing.  For further details, please see "
                     "the section INSTALLING THE NVIDIA DRIVER in the README "
                     "available on the Linux driver download page at "
                     "www.nvidia.com.");
            return FALSE;
        }
    }
    
    return TRUE;

} /* check_for_running_x() */


/*
 * nv_format_text_rows() - this function breaks the given string str
 * into some number of rows, where each row is not longer than the
 * specified width.
 *
 * If prefix is non-NULL, the first line is prepended with the prefix,
 * and subsequent lines are indented to line up with the prefix.
 *
 * If word_boundary is TRUE, then attempt to only break lines on
 * boundaries between words.
 *
 * XXX Note that we don't use nvalloc() or any of the other wrapper
 * functions from here, so that this function doesn't require any
 * non-c library symbols (so that it can be called from dlopen()'ed
 * user interfaces.
 */

TextRows *nv_format_text_rows(const char *prefix, const char *str,
                              int width, int word_boundary)
{
    int len, prefix_len, z, w, i;
    char *line, *buf, *local_prefix, *a, *b, *c;
    TextRows *t;
    
    /* initialize the TextRows structure */

    t = (TextRows *) malloc(sizeof(TextRows));
    t->t = NULL;
    t->n = 0;
    t->m = 0;

    if (!str) return t;

    buf = strdup(str);

    z = strlen(buf); /* length of entire string */
    a = buf;         /* pointer to the start of the string */

    /* initialize the prefix fields */

    if (prefix) {
        prefix_len = strlen(prefix);
        local_prefix = nvstrdup(prefix);
    } else {
        prefix_len = 0;
        local_prefix = NULL;
    }

    /* adjust the max width for any prefix */

    w = width - prefix_len;

    do {
        /*
         * if the string will fit on one line, point b to the end of the
         * string
         */
        
        if (z < w) b = a + z;

        /* 
         * if the string won't fit on one line, move b to where the
         * end of the line should be, and then move b back until we
         * find a space; if we don't find a space before we back b all
         * the way up to a, just assign b to where the line should end.
         */
        
        else {
            b = a + w;
            
            if (word_boundary) {
                while ((b >= a) && (!isspace(*b))) b--;
                if (b <= a) b = a + w;
            }
        }

        /* look for any newline inbetween a and b, and move b to it */
        
        for (c = a; c < b; c++) if (*c == '\n') { b = c; break; }
        
        /*
         * copy the string that starts at a and ends at b, prepending
         * with a prefix, if present
         */

        len = b-a;
        len += prefix_len;
        line = (char *) malloc(len+1);
        if (local_prefix) strncpy(line, local_prefix, prefix_len);
        strncpy(line + prefix_len, a, len - prefix_len);
        line[len] = '\0';
        
        /* append the new line to the array of text rows */

        t->t = (char **) realloc(t->t, sizeof(char *) * (t->n + 1));
        t->t[t->n] = line;
        t->n++;
        
        if (t->m < len) t->m = len;

        /*
         * adjust the length of the string and move the pointer to the
         * beginning of the new line
         */
        
        z -= (b - a + 1);
        a = b + 1;

        /* move to the first non whitespace character (excluding newlines) */
        
        if (word_boundary && isspace(*b)) {
            while ((z) && (isspace(*a)) && (*a != '\n')) a++, z--;
        } else {
            if (!isspace(*b)) z++, a--;
        }
        
        if (local_prefix) {
            for (i = 0; i < prefix_len; i++) local_prefix[i] = ' ';
        }
        
    } while (z > 0);

    if (local_prefix) free(local_prefix);
    free(buf);
    
    return t;

} /* nv_format_text_rows() */



/*
 * nv_free_text_rows() - free the TextRows data structure allocated by
 * nv_format_text_rows()
 */

void nv_free_text_rows(TextRows *t)
{
    int i;
    
    if (!t) return;
    for (i = 0; i < t->n; i++) free(t->t[i]);
    if (t->t) free(t->t);
    free(t);

} /* nv_free_text_rows() */
