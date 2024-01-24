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
 * misc.c - this source file contains miscellaneous routines for use
 * by the nvidia-installer.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <libgen.h>
#include <pciaccess.h>
#include <elf.h>
#include <link.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "kernel.h"
#include "files.h"
#include "misc.h"
#include "crc.h"
#include "nvLegacy.h"
#include "manifest.h"
#include "nvpci-utils.h"
#include "conflicting-kernel-modules.h"
#include "initramfs.h"
#include "detect-self-hosted.h"

static int check_symlink(Options*, const char*, const char*, const char*);


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
    char *c;
    int success = TRUE;

    /*
     * extract any pathname portion out of the program_name and chdir
     * to it
     */

    c = strrchr(program_name, '/');
    if (c) {
        int len;
        char *path;

        len = c - program_name + 1;
        path = (char *) nvalloc(len + 1);
        strncpy(path, program_name, len);
        path[len] = '\0';
        if (op->expert) log_printf(op, NULL, "chdir(\"%s\")", path);
        if (chdir(path)) {
            fprintf(stderr, "Unable to chdir to %s (%s)",
                    path, strerror(errno));
            success = FALSE;
        }
        free(path);
    }

    return success;
}


/*
 * get_next_line() - this function scans for the next newline,
 * carriage return, NUL terminator, or EOF in buf.  If non-NULL, the
 * passed-by-reference parameter 'end' is set to point to the next
 * printable character in the buffer, or NULL if EOF is encountered.
 *
 * If the parameter 'start' is non-NULL, then that is interpreted as
 * the start of the buffer string, and we check that we never walk
 * 'length' bytes past 'start'.
 * 
 * On success, a newly allocated buffer is allocated containing the
 * next line of text (with a NULL terminator in place of the
 * newline/carriage return).
 *
 * On error, NULL is returned.
 */

char *get_next_line(char *buf, char **end, char *start, int length)
{
    char *c, *retbuf;
    int len;

    if (start && (length < 1)) return NULL;

#define __AT_END(_start, _current, _length) \
    ((_start) && (((_current) - (_start)) >= (_length)))
    
    if (end) *end = NULL;
    
    // Cast all char comparisons to EOF to signed char in order to
    // allow proper sign extension on platforms like GCC ARM where
    // char is unsigned char
    if ((!buf) ||
        __AT_END(start, buf, length) ||
        (*buf == '\0') ||
        (((signed char)*buf) == EOF)) return NULL;
    
    c = buf;
    
    while ((!__AT_END(start, c, length)) &&
           (*c != '\0') &&
           (((signed char)*c) != EOF) &&
           (*c != '\n') &&
           (*c != '\r')) c++;

    len = c - buf;
    retbuf = nvalloc(len + 1);
    strncpy(retbuf, buf, len);
    retbuf[len] = '\0';
    
    if (end) {
        while ((!__AT_END(start, c, length)) &&
               (*c != '\0') &&
               (((signed char)*c) != EOF) &&
               (!isprint(*c))) c++;
        
        if (__AT_END(start, c, length) ||
            (*c == '\0') ||
            (((signed char)*c) == EOF)) *end = NULL;
        else *end = c;
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
 * If the output_match parameter is set to a { 0 }-terminated array of
 * RunCommandOutputMatch records, it is interpreted as a rough estimate of how
 * many lines of output (optionally requiring an initial substring match) will
 * be generated by the command.  This is used to compute the value that should
 * be passed to ui_status_update() as lines of output are received. This may be
 * set to NULL to skip the ui_status_update() updates.
 *
 * The redirect argument tells run_command() to redirect stderr to
 * stdout so that all output is collected, or just stdout.
 *
 * XXX maybe we should do something to cap the time we allow the
 * command to run?
 */

int run_command(Options *op, char **data, int output,
                const RunCommandOutputMatch *output_match, int redirect,
                const char *cmd_start, ...)
{
    int n = 0; /* output line counter */
    int len = 0; /* length of what has actually been read */
    int buflen = 0; /* length of destination buffer */
    int ret, total_lines;
    char *cmd, *buf = NULL;
    FILE *stream = NULL;
    struct sigaction act, old_act;
    float percent;
    int *match_sizes = NULL;
    va_list ap;

    va_start(ap, cmd_start);
    cmd = nvvstrcat(cmd_start, ap);
    va_end(ap);

    if (!cmd) {
        return 1;
    }

    if (data) *data = NULL;

    /*
     * if command output is requested, print the command that we will
     * execute
     */

    if (output) ui_command_output (op, "executing: '%s'...", cmd);

    /* redirect stderr to stdout */

    if (redirect) {
        char *tmp = cmd;
        cmd = nvstrcat(cmd, " 2>&1", NULL);
        nvfree(tmp);
    }
    
    /*
     * XXX: temporarily ignore SIGWINCH; our child process inherits
     * this disposition and will likewise ignore it (by default).
     * This fixes cases where child processes abort after receiving
     * SIGWINCH when its caught in the parent process.
     */
    if (op->sigwinch_workaround) {
        act.sa_handler = SIG_IGN;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;

        if (sigaction(SIGWINCH, &act, &old_act) < 0)
            old_act.sa_handler = NULL;
    }

    /*
     * Clear LANG and LC_ALL before running the command, to make sure
     * command output that might need to be parsed doesn't vary based
     * on system locale settings.
     */

    unsetenv("LANG");
    unsetenv("LC_ALL");


    /*
     * Open a process by creating a pipe, forking, and invoking the
     * command.
     */
    
    stream = popen(cmd, "r");

    if (stream == NULL) {
        ret = errno;
        ui_error(op, "Failure executing command '%s' (%s).",
                 cmd, strerror(errno));
    }

    nvfree(cmd);

    if (stream == NULL) {
        return ret;
    }

    /*
     * read from the stream, filling and growing buf, until we hit
     * EOF.  Send each line to the ui as it is read.
     */

    if (output_match) {
        int match_count, i;

        /* Determine the total number of lines to match */
        for (total_lines = i = 0; output_match[i].lines; i++) {
            total_lines += output_match[i].lines;
        }

        match_count = i;

        /* Cache the lengths of the .initial_match strings */
        match_sizes = nvalloc(match_count * sizeof(*match_sizes));

        for (i = 0; i < match_count; i++) {
            if (output_match[i].initial_match) {
                match_sizes[i] = strlen(output_match[i].initial_match);
            }
        }

        /* Don't divide by zero */
        if (total_lines == 0) total_lines = 1;

        /*
         * Note: 'match_sizes' and 'total_lines' must only be used when
         * output_match != NULL; otherwise, 'match_sizes' cannot be safely
         * dereferenced, and 'total_lines' is uninitialized.
         */
    }

    while (1) {
        
        if ((buflen - len) < NV_MIN_LINE_LEN) {
            buflen += NV_LINE_LEN;
            buf = nvrealloc(buf, buflen);
        }
        
        if (fgets(buf + len, buflen - len, stream) == NULL) break;
        
        if (output) ui_command_output(op, "%s", buf + len);

        if (output_match) {
            int i;

            for (i = 0; output_match[i].lines; i++) {
                const char *s = output_match[i].initial_match;
                if (s == NULL || strncmp(buf + len, s, match_sizes[i]) == 0) {
                    n++;
                    break;
                }
            }

            if (n > total_lines) n = total_lines;
            percent = (float) n / (float) total_lines;

            /*
             * XXX: manually call the SIGWINCH handler, if set, to
             * handle window resizes while we ignore the signal.
             */
            if (op->sigwinch_workaround) {
                /* Only call into the handler if it isn't one of the special
                 * pointer values from bits/signum-generic.h */
                if (old_act.sa_handler != NULL &&
                    old_act.sa_handler != SIG_DFL &&
                    old_act.sa_handler != SIG_IGN &&
                    old_act.sa_handler != SIG_ERR) {
                    old_act.sa_handler(SIGWINCH);
                }
            }

            ui_status_update(op, percent, NULL);
        }

        len += strlen(buf + len);
    } /* while (1) */

    nvfree(match_sizes);

    /* Close the popen()'ed stream. */

    ret = pclose(stream);

    /*
     * Restore the SIGWINCH signal disposition and handler, if any,
     * to their original values.
     */
    if (op->sigwinch_workaround)
        sigaction(SIGWINCH, &old_act, NULL);

    /* if the last character in the buffer is a newline, null it */
    
    if ((len > 0) && (buf[len-1] == '\n')) buf[len-1] = '\0';
    
    if (data) *data = buf;
    else free(buf);
    
    return ret;
    
} /* run_command() */



/*
 * read_text_file() - open a text file, read its contents and return
 * them to the caller in a newly allocated buffer.  Returns TRUE on
 * success and FALSE on failure.
 */

int read_text_file(const char *filename, char **buf)
{
    FILE *fp;
    int index = 0, buflen = 0;
    int eof = FALSE;
    char *line, *tmpbuf;

    *buf = NULL;

    fp = fopen(filename, "r");
    if (!fp)
        return FALSE;

    while (((line = fget_next_line(fp, &eof)) != NULL)) {
        if ((index + strlen(line) + 2) > buflen) {
            buflen = 2 * (index + strlen(line) + 2);
            tmpbuf = (char *)nvalloc(buflen);
            if (!tmpbuf) {
                if (*buf) nvfree(*buf);
                fclose(fp);
                return FALSE;
            }
            if (*buf) {
                memcpy(tmpbuf, *buf, index);
                nvfree(*buf);
            }
            *buf = tmpbuf;
        }

        index += sprintf(*buf + index, "%s\n", line);
        nvfree(line);

        if (eof) {
            break;
        }
    }

    fclose(fp);

    return TRUE;

} /* read_text_file() */



/*
 * find_system_utils() - search the $PATH (as well as some common
 * additional directories) for the utilities that the installer will
 * need to use.  Returns TRUE on success and assigns the util fields
 * in the option struct; it returns FALSE on failure.
 */

#define EXTRA_PATH "/bin:/usr/bin:/sbin:/usr/sbin:/usr/X11R6/bin:/usr/bin/X11"

/*
 * Utils list; keep in sync with SystemUtils, SystemOptionalUtils, ModuleUtils
 * and DevelopUtils enum types
 */

typedef struct {
    const char *util;
    const char *package;
} Util;

static const Util __utils[] = {

    /* SystemUtils */
    [LDCONFIG] = { "ldconfig", "glibc" },
    [GREP]     = { "grep",     "grep" },
    [DMESG]    = { "dmesg",    "util-linux" },
    [TAIL]     = { "tail",     "coreutils" },

    /* SystemOptionalUtils */
    [OBJCOPY]         = { "objcopy",        "binutils" },
    [CHCON]           = { "chcon",          "selinux" },
    [SELINUX_ENABLED] = { "selinuxenabled", "selinux" },
    [GETENFORCE]      = { "getenforce",     "selinux" },
    [EXECSTACK]       = { "execstack",      "selinux" },
    [PKG_CONFIG]      = { "pkg-config",     "pkg-config" },
    [XSERVER]         = { "X",              "xserver" },
    [OPENSSL]         = { "openssl",        "openssl" },
    [DKMS]            = { "dkms",           "dkms"    },
    [SYSTEMCTL]       = { "systemctl",      "systemd" },
    [TAR]             = { "tar",            "tar"     },

    /* ModuleUtils */
    [MODPROBE] = { "modprobe", "module-init-tools' or 'kmod" },
    [RMMOD]    = { "rmmod",    "module-init-tools' or 'kmod" },
    [LSMOD]    = { "lsmod",    "module-init-tools' or 'kmod" },
    [DEPMOD]   = { "depmod",   "module-init-tools' or 'kmod" },

    /* DevelopUtils */
    [CC]   = { "cc",   "gcc"  },
    [MAKE] = { "make", "make" },
    [LD]   = { "ld",   "binutils" },
    [TR]   = { "tr",   "coreutils" },
    [SED]  = { "sed",  "sed" },

};

int find_system_utils(Options *op)
{
    int i;

    ui_expert(op, "Searching for system utilities:");

    /* search the PATH for each utility */

    for (i = MIN_SYSTEM_UTILS; i < MAX_SYSTEM_UTILS; i++) {
        op->utils[i] = find_system_util(__utils[i].util);
        if (!op->utils[i]) {
            ui_error(op, "Unable to find the system utility `%s`; please "
                     "make sure you have the package '%s' installed.  If "
                     "you do have %s installed, then please check that "
                     "`%s` is in your PATH.",
                     __utils[i].util, __utils[i].package,
                     __utils[i].package, __utils[i].util);
            return FALSE;
        }

        ui_expert(op, "found `%s` : `%s`", __utils[i].util, op->utils[i]);
    }

    for (i = MIN_SYSTEM_OPTIONAL_UTILS; i < MAX_SYSTEM_OPTIONAL_UTILS; i++) {

        op->utils[i] = find_system_util(__utils[i].util);
        if (op->utils[i]) {
            ui_expert(op, "found `%s` : `%s`", __utils[i].util, op->utils[i]);
        }
    }

    /* If no program called `X` is found; try searching for Xorg */
    if (op->utils[XSERVER] == NULL) {
        op->utils[XSERVER] = find_system_util("Xorg");
        if (op->utils[XSERVER]) {
            ui_expert(op, "found `%s` : `%s`",
                      "Xorg", op->utils[XSERVER]);
        }
    }

    return TRUE;

} /* find_system_utils() */


/*
 * find_module_utils() - search the $PATH (as well as some common
 * additional directories) for the utilities that the installer will
 * need to use.  Returns TRUE on success and assigns the util fields
 * in the option struct; it returns FALSE on failures.
 */

int find_module_utils(Options *op)
{
    int i;

    ui_expert(op, "Searching for module utilities:");

    /* search the PATH for each utility */

    for (i = MIN_MODULE_UTILS; i < MAX_MODULE_UTILS; i++) {
        op->utils[i] = find_system_util(__utils[i].util);
        if (!op->utils[i]) {
            ui_error(op, "Unable to find the module utility `%s`; please "
                     "make sure you have the package '%s' installed.  If "
                     "you do have '%s' installed, then please check that "
                     "`%s` is in your PATH.",
                     __utils[i].util, __utils[i].package,
                     __utils[i].package, __utils[i].util);
            return FALSE;
        }

        ui_expert(op, "found `%s` : `%s`", __utils[i].util, op->utils[i]);
    };

    return TRUE;

} /* find_module_utils() */


/*
 * check_proc_modprobe_path() - check if the modprobe path reported
 * via /proc matches the one determined earlier; also check if it can
 * be accessed/executed.
 */

#define PROC_MODPROBE_PATH_FILE "/proc/sys/kernel/modprobe"
#define DEFAULT_MODPROBE "/sbin/modprobe"

int check_proc_modprobe_path(Options *op)
{
    FILE *fp;
    char *proc_modprobe = NULL, *found_modprobe;
    struct stat st;
    int ret, success = FALSE;

    found_modprobe = op->utils[MODPROBE];

    fp = fopen(PROC_MODPROBE_PATH_FILE, "r");
    if (fp) {
        proc_modprobe = fget_next_line(fp, NULL);
        fclose(fp);
    }

    /* If the modprobe found by find_system_utils() is a symlink, resolve it */

    ret = lstat(found_modprobe, &st);

    if (ret == 0 && S_ISLNK(st.st_mode)) {
        char *target = get_resolved_symlink_target(op, found_modprobe);
        if (target && access(target, F_OK | X_OK) == 0) {
            found_modprobe = target;
        } else {
            nvfree(target);
        }
    }

    if (proc_modprobe) {

        /* If the modprobe reported by the kernel is a symlink, resolve it */

        ret = lstat(proc_modprobe, &st);

        if (ret == 0 && S_ISLNK(st.st_mode)) {
            char *target = get_resolved_symlink_target(op, proc_modprobe);
            if (target && access(target, F_OK | X_OK) == 0) {
                nvfree(proc_modprobe);
                proc_modprobe = target;
            } else {
                nvfree(target);
            }
        }

        /* Check to see if the modprobe reported by the kernel and the
         * modprobe found by nvidia-installer match. */

        if (strcmp(proc_modprobe, found_modprobe) == 0) {
            success = TRUE;
        } else {
            if (access(proc_modprobe, F_OK | X_OK) == 0) {
                ui_warn(op, "The path to the `modprobe` utility reported by "
                        "'%s', `%s`, differs from the path determined by "
                        "`nvidia-installer`, `%s`.  Please verify that `%s` "
                        "works correctly and correct the path in '%s' if "
                        "it does not.",
                        PROC_MODPROBE_PATH_FILE, proc_modprobe, found_modprobe,
                        proc_modprobe, PROC_MODPROBE_PATH_FILE);
                success = TRUE;
            } else {
               ui_error(op, "The path to the `modprobe` utility reported by "
                        "'%s', `%s`, differs from the path determined by "
                        "`nvidia-installer`, `%s`, and does not appear to "
                        "point to a valid `modprobe` binary.  Please correct "
                        "the path in '%s'.",
                        PROC_MODPROBE_PATH_FILE, proc_modprobe, found_modprobe,
                        PROC_MODPROBE_PATH_FILE);
            }
        }
    } else {
        /* We failed to read from /proc/sys/kernel/modprobe, possibly because
         * it doesn't exist or /proc isn't mounted. Assume a default modprobe
         * path of /sbin/modprobe. */

        char * found_mismatch;

        if (strcmp(DEFAULT_MODPROBE, found_modprobe) == 0) {
            found_mismatch = nvstrdup("");
        } else {
            found_mismatch = nvstrcat("This path differs from the one "
                                      "determined by `nvidia-installer`, ",
                                      found_modprobe, ".  ", NULL);
        }

        if (access(DEFAULT_MODPROBE, F_OK | X_OK) == 0) {
            ui_warn(op, "The file '%s' is unavailable; the X server will "
                    "use `" DEFAULT_MODPROBE "` as the path to the `modprobe` "
                    "utility.  %sPlease verify that `" DEFAULT_MODPROBE
                    "` works correctly or mount the /proc file system and "
                    "verify that '%s' reports the correct path.",
                    PROC_MODPROBE_PATH_FILE, found_mismatch,
                    PROC_MODPROBE_PATH_FILE);
            success = TRUE;
        } else {
           ui_error(op, "The file '%s' is unavailable; the X server will "
                    "use `" DEFAULT_MODPROBE "` as the path to the `modprobe` "
                    "utility.  %s`" DEFAULT_MODPROBE "` does not appear to "
                    "point to a valid `modprobe` binary.  Please create a "
                    "symbolic link from `" DEFAULT_MODPROBE "` to `%s` or "
                    "mount the /proc file system and verify that '%s' reports "
                    "the correct path.",
                    PROC_MODPROBE_PATH_FILE, found_mismatch,
                    found_modprobe, PROC_MODPROBE_PATH_FILE);
        }

        nvfree(found_mismatch);
    }

    nvfree(proc_modprobe);
    if (found_modprobe != op->utils[MODPROBE]) {
        nvfree(found_modprobe);
    }

    return success;

} /* check_proc_modprobe_path() */


/*
 * check_development_tools() - check if the development tools needed
 * to build custom kernel interfaces are available.
 */

static int check_development_tool(Options *op, int idx)
{
    if (!op->utils[idx]) {
        ui_error(op, "Unable to find the development tool `%s` in "
                 "your path; please make sure that you have the "
                 "package '%s' installed.  If %s is installed on your "
                 "system, then please check that `%s` is in your "
                 "PATH.",
                 __utils[idx].util, __utils[idx].package,
                 __utils[idx].package, __utils[idx].util);
        return FALSE;
    }

    ui_expert(op, "found `%s` : `%s`", __utils[idx].util, op->utils[idx]);

    return TRUE;
}

int check_development_tools(Options *op, Package *p)
{

    int i, ret;

    op->utils[CC] = getenv("CC");

    ui_expert(op, "Checking development tools:");

    /*
     * Check if the required toolchain components are installed on
     * the system.  Note that we skip the check for `cc` if the
     * user specified the CC environment variable; we do this because
     * `cc` may not be present in the path, nor the compiler named
     * in $CC, but the installation may still succeed. $CC is sanity
     * checked below.
     */

    for (i = (op->utils[CC] != NULL) ? MIN_DEVELOP_UTILS + 1 : MIN_DEVELOP_UTILS;
         i < MAX_DEVELOP_UTILS; i++) {

        op->utils[i] = find_system_util(__utils[i].util);
        if (!check_development_tool(op, i)) {
            return FALSE;
        }
    }

    /*
     * Check if the libc development headers are installed; we need
     * these to build the CC version check utility.
     */
    if (access("/usr/include/stdio.h", F_OK) == -1) {
        ui_error(op, "You do not appear to have libc header files "
                 "installed on your system.  Please install your "
                 "distribution's libc development package.");
        return FALSE;
    }

    if (!op->utils[CC]) op->utils[CC] = "cc";

    ui_log(op, "Performing CC sanity check with CC=\"%s\".", op->utils[CC]);

    ret = conftest_sanity_check(op, p->kernel_module_build_directory,
                                "CC", "cc_sanity_check");

    if (ret) return TRUE;

    return FALSE;

} /* check_development_tools() */


/*
 * check_precompiled_kernel_interface_tools() - check if the development tools
 * needed to link precompiled kernel interfaces are available.
 */

int check_precompiled_kernel_interface_tools(Options *op)
{
    /*
     * If precompiled info has been found we only need to check for
     * a linker
     */
    op->utils[LD] = find_system_util(__utils[LD].util);
    return check_development_tool(op, LD);

} /* check_precompiled_kernel_interface_tools() */


/*
 * find_system_util() - build a search path and search for the named
 * utility.  If the utility is found, the fully qualified path to the
 * utility is returned.  On failure NULL is returned.
 */

char *find_system_util(const char *util)
{
    char *buf, *path, *file, *x, *y, c;
    
    /* build the search path */
    
    buf = getenv("PATH");
    if (buf) {
        path = nvstrcat(buf, ":", EXTRA_PATH, NULL);
    } else {
        path = nvstrdup(EXTRA_PATH);
    }

    /* search the PATH for the utility */

    for (x = y = path; ; x++) {
        if (*x == ':' || *x == '\0') {
            struct stat st;

            c = *x;
            *x = '\0';
            file = nvstrcat(y, "/", util, NULL);
            *x = c;
            if (stat(file, &st) == 0 && S_ISREG(st.st_mode) &&
                (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
                /* If the path points to a regular file (or a symbolic link to a
                 * regular file), and it is executable by at least one of user,
                 * group, or other, use this path for the relevant utility. */
                nvfree(path);
                return file;
            }
            nvfree(file);
            y = x + 1;
            if (*x == '\0') break;
        }
    }

    nvfree(path);

    return NULL;

} /* find_system_util() */



/*
 * continue_after_error() - tell the user that an error has occurred,
 * and ask them if they would like to continue.
 *
 * Returns TRUE if the installer should continue.
 */

int continue_after_error(Options *op, const char *fmt, ...)
{
    char *msg;
    int ret;

    NV_VSNPRINTF(msg, fmt);
    
    ret = (ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
                              NUM_CONTINUE_ABORT_CHOICES,
                              CONTINUE_CHOICE, /* Default choice */
                              "The installer has encountered the following "
                              "error during installation: '%s'.  Would you "
                              "like to continue installation anyway?",
                              msg) == CONTINUE_CHOICE);

    nvfree(msg);

    return ret;

} /* continue_after_error() */



/*
 * do_install()
 */

int do_install(Options *op, Package *p, CommandList *c)
{
    char *msg;
    int len, ret;

    len = strlen(p->description) + strlen(p->version) + 64;
    msg = (char *) nvalloc(len);
    snprintf(msg, len, "Installing '%s' (%s):",
             p->description, p->version);
    
    ret = execute_command_list(op, c, msg, "Installing");
    
    free(msg);
    
    if (!ret) return FALSE;
    
    ui_log(op, "Driver file installation is complete.");

    return TRUE;

} /* do_install() */



/*
 * extract_version_string() - extract the NVIDIA driver version string
 * from the given string.  On failure, return NULL; on success, return
 * a malloced string containing just the version string.
 *
 * The version string can have one of two forms: either the old
 * "X.Y.ZZZZ" format (e.g., "1.0-9742"), or the new format where it is
 * just a collection of period-separated numbers (e.g., "105.17.2").
 * The length and number of periods in the newer format is arbitrary.
 *
 * Furthermore, we expect the new version format to be enclosed either
 * in parenthesis or whitespace (or be at the start or end of the
 * input string) and be at least 5 characters long.  This allows us to
 * distinguish the version string from other numbers such as the year
 * or the old version format in input strings like this:
 *
 *  "NVIDIA UNIX x86 Kernel Module  105.17.2  Fri Dec 15 09:54:45 PST 2006"
 *  "1.0-105917 (105.9.17)"
 */

char *extract_version_string(const char *str)
{
    char c, *copiedString, *start, *end, *x, *version = NULL;
    int state;

    if (!str) return NULL;

    copiedString = strdup(str);
    x = copiedString;
    
    /*
     * look for a block of only numbers and periods; the version
     * string must be surrounded by either whitespace, or the
     * start/end of the string; we use a small state machine to parse
     * the string
     */
    
    start = NULL;
    end = NULL;

#define STATE_IN_VERSION          0
#define STATE_NOT_IN_VERSION      1
#define STATE_LOOKING_FOR_VERSION 2
#define STATE_FOUND_VERSION       3

    state = STATE_LOOKING_FOR_VERSION;

    while (*x) {
        
        c = *x;
        
        switch (state) {
        
            /*
             * if we are LOOKING_FOR_VERSION, then finding a digit
             * will put us inside the version, whitespace (or open
             * parenthesis) will allow us to continue to look for the
             * version, and any other character will cause us to stop
             * looking for the version string
             */
    
        case STATE_LOOKING_FOR_VERSION:
            if (isdigit(c)) {
                start = x;
                state = STATE_IN_VERSION;
            } else if (isspace(c) || (c == '(')) {
                state = STATE_LOOKING_FOR_VERSION;
            } else {
                state = STATE_NOT_IN_VERSION;
            }
            break;
            
            /*
             * if we are IN_VERSION, then a digit or period will keep
             * us in the version, space (or close parenthesis) and
             * more than 4 characters of version means we found the
             * entire version string.  If we find any other character,
             * then what we thought was the version string wasn't, so
             * move to NOT_IN_VERSION.
             */

        case STATE_IN_VERSION:
            if (isdigit(c) || (c == '.')) {
                state = STATE_IN_VERSION;
            } else if ((isspace(c) || (c == ')')) && ((x - start) >= 5)) {
                end = x;
                state = STATE_FOUND_VERSION;
                goto exit_while_loop;
            } else {
                state = STATE_NOT_IN_VERSION;
            }
            break;
            
            /*
             * if we are NOT_IN_VERSION, then space or open
             * parenthesis will make us start looking for the version,
             * and any other character just leaves us in the
             * NOT_IN_VERSION state
             */

        case STATE_NOT_IN_VERSION:
            if (isspace(c) || (c == '(')) {
                state = STATE_LOOKING_FOR_VERSION;
            } else {
                state = STATE_NOT_IN_VERSION;
            }
            break;
        }

        x++;
    }

    /*
     * the NULL terminator that broke us out of the while loop could
     * be the end of the version string
     */
    
    if ((state == STATE_IN_VERSION) && ((x - start) >= 5)) {
        end = x;
        state = STATE_FOUND_VERSION;
    }
    
 exit_while_loop:
    
    /* if we found a version string above, copy it */

    if (state == STATE_FOUND_VERSION) {
        *end = '\0';
        version = strdup(start);
        goto done;
    }
    
    
    
    /*
     * we did not find a version string with the new format; look for
     * a version of the old X.Y-ZZZZ format
     */
    
    x = copiedString;

    while (*x) {
        if (((x[0]) && isdigit(x[0])) &&
            ((x[1]) && (x[1] == '.')) &&
            ((x[2]) && isdigit(x[2])) &&
            ((x[3]) && (x[3] == '-')) &&
            ((x[4]) && isdigit(x[4])) &&
            ((x[5]) && isdigit(x[5])) &&
            ((x[6]) && isdigit(x[6])) &&
            ((x[7]) && isdigit(x[7]))) {
            
            x[8] = '\0';
            
            version = strdup(x);
            goto done;
        }
        x++;
    }

 done:

    free(copiedString);

    return version;

} /* extract_version_string() */


/*
 * should_install_compat32_files() - ask the user if he/she wishes to
 * install 32bit compatibility libraries.
 */

void should_install_compat32_files(Options *op, Package *p)
{
#if defined(NV_X86_64)

    /* If there are no compat32 files, there is nothing to do */
    if (!op->compat32_files_packaged) {
        op->install_compat32_libs = NV_OPTIONAL_BOOL_FALSE;
        return;
    }

    /* Determine where the compatibility libraries should be installed */
    get_compat32_path(op);

    /*
     * If the user hasn't explicitly specified whether to install compat32
     * files, ask if the 32-bit compatibility libraries are to be installed.
     * If yes, check if the chosen prefix exists. If not, notify the user and
     * ask him/her if the files are to be installed anyway.
     */
    if (op->install_compat32_libs == NV_OPTIONAL_BOOL_DEFAULT) {
        int ret;

        ret = ui_yes_no(op, TRUE,
                        "Install NVIDIA's 32-bit compatibility libraries?");

        op->install_compat32_libs = ret ? NV_OPTIONAL_BOOL_TRUE :
                                          NV_OPTIONAL_BOOL_FALSE;
    }

    if (op->install_compat32_libs == NV_OPTIONAL_BOOL_FALSE) {
        int i;

        for (i = 0; i < p->num_entries; i++) {
            if (p->entries[i].compat_arch == FILE_COMPAT_ARCH_COMPAT32) {
                /* invalidate file */
                invalidate_package_entry(&(p->entries[i]));
            }
        }
    }
#endif /* NV_X86_64 */
}


#define member_at_offset(base, offset, target_type) \
    ((target_type *) ((char *) base + offset))


static void set_optional_module_install(Options *op, int offset, int val) {
    *member_at_offset(op, offset, int) = val;
}

static int get_optional_module_install(Options *op, int offset) {
    return *member_at_offset(op, offset, int);
}

/*
 * should_install_optional_modules() - ask the user if he/she wishes to install
 * optional kernel modules
 */

void should_install_optional_modules(Options *op, Package *p,
                                     const KernelModuleInfo* optional_modules,
                                     int num_optional_modules)
{
    int i;

    for (i = 0; i < num_optional_modules; i++) {
        int install = get_optional_module_install(op,
                          optional_modules[i].option_offset);

        /* if the package doesn't include the module, it can't be installed. */

        if (!package_includes_kernel_module(p,
                                            optional_modules[i].module_name)) {
            set_optional_module_install(op, optional_modules[i].option_offset,
                                        FALSE);
            continue;
        }

        /* ask expert users whether they want to install the module */

        if (op->expert) {
            int default_value = install;
            install = ui_yes_no(op, default_value, "Would you like to install "
                                "the %s kernel module? You must install "
                                "this module in order to use %s.",
                                optional_modules[i].module_name,
                                optional_modules[i].optional_module_dependee);
            if (install != default_value) {
                set_optional_module_install(op,
                                            optional_modules[i].option_offset,
                                            install);
            }
        }

        if (!install) {
            ui_warn(op, "The %s module will not be installed. As a result, %s "
                    "will not function with this installation of the NVIDIA "
                    "driver.", optional_modules[i].module_name,
                    optional_modules[i].optional_module_dependee);

            remove_kernel_module_from_package(p,
                                              optional_modules[i].module_name);
        }
    }
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
    PackageEntryFileTypeList installable_files;

    ui_status_begin(op, "Running post-install sanity check:", "Checking");

    get_installable_file_type_list(op, &installable_files);

    for (i = 0; i < p->num_entries; i++) {
        
        percent = (float) i / (float) p->num_entries;
        ui_status_update(op, percent, "%s", p->entries[i].dst);
        
        if (p->entries[i].caps.is_symlink) {
            if (!check_symlink(op, p->entries[i].target,
                               p->entries[i].dst,
                               p->description)) {
                ret = FALSE;
            }
        } else if (installable_files.types[p->entries[i].type]) {
            if (!check_installed_file(op, p->entries[i].dst,
                                      p->entries[i].mode, 0, ui_warn)) {
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
    int success = TRUE;
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
        success = FALSE;
    }

    nvfree(actual_target);
    return success;
}



/*
 * unprelink() - attempt to run `prelink -u` on a file to restore it to
 * its pre-prelinked state.
 */
static int unprelink(Options *op, const char *filename)
{
    char *cmd;
    int ret = ENOENT;

    cmd = find_system_util("prelink");
    if (cmd) {
        ret = run_command(op, NULL, FALSE, NULL, TRUE, cmd, " -u ", filename, NULL);
        nvfree(cmd);
    }
    return ret;
} /* unprelink() */



/*
 * verify_crc() - Compute the CRC of a file and compare it against an
 * expected value. Returns TRUE if the values match, FALSE otherwise.
 * 
 */
int verify_crc(Options *op, const char *filename, unsigned int crc,
                      unsigned int *actual_crc)
{
    /* only check the crc if we were handed a non-emtpy crc */
    if (crc == 0) {
        return TRUE;
    }
    *actual_crc = compute_crc(op, filename);
    return crc == *actual_crc;
} /* verify_crc() */



/*
 * check_installed_file() - check that the specified installed file exists,
 * has the correct permissions, and has the correct crc. Takes a function
 * pointer to either ui_log() or ui_warn() depending on how errors should
 * be reported.
 *
 * If anything is incorrect, print a warning and return FALSE,
 * otherwise return TRUE.
 */

int check_installed_file(Options *op, const char *filename,
                         const mode_t mode, const uint32 crc,
                         ui_message_func *logwarn)
{
    struct stat stat_buf;
    uint32 actual_crc;

    if (lstat(filename, &stat_buf) == -1) {
        logwarn(op, "Unable to find installed file '%s' (%s).",
                filename, strerror(errno));
        return FALSE;
    }

    if (!S_ISREG(stat_buf.st_mode)) {
        logwarn(op, "The installed file '%s' is not of the correct filetype.",
                filename);
        return FALSE;
    }

    /* Don't check the mode if we don't have one: backup log entries for
       installed files don't preserve the mode. */

    if (mode && ((stat_buf.st_mode & PERM_MASK) != (mode & PERM_MASK))) {
        logwarn(op, "The installed file '%s' has permissions %04o, but it "
                "was installed with permissions %04o.", filename,
                (stat_buf.st_mode & PERM_MASK),
                (mode & PERM_MASK));
        return FALSE;
    }


    if (!verify_crc(op, filename, crc, &actual_crc)) {
        int ret;

        /* If this is not an ELF file, we should not try to unprelink it. */

        if (get_elf_architecture(filename) == ELF_INVALID_FILE) {
            logwarn(op, "The installed file '%s' has a different checksum "
                    "(%ul) than when it was installed (%ul).", filename,
                    actual_crc, crc);
            return FALSE;
        }

        /* Otherwise, unprelinking may be able to restore the original file. */

        ui_expert(op, "The installed file '%s' has a different checksum (%ul) "
                  "than when it was installed (%ul). This may be due to "
                  "prelinking; attempting `prelink -u %s` to restore the file.",
                  filename, actual_crc, crc, filename);

        ret = unprelink(op, filename);
        if (ret != 0) {
            logwarn(op, "The installed file '%s' seems to have changed, but "
                    "`prelink -u` failed; unable to restore '%s' to an "
                    "un-prelinked state.", filename, filename);
            return FALSE;
        }

        if (!verify_crc(op, filename, crc, &actual_crc)) {
            logwarn(op, "The installed file '%s' has a different checksum "
                    "(%ul) after running `prelink -u` than when it was "
                    "installed (%ul).",
                    filename, actual_crc, crc);
            return FALSE;
        }

        ui_expert(op, "Un-prelinking successful: %s was restored to its "
                  "original state.", filename);
    }

    return TRUE;
    
}



/*
 * get_xserver_information() - parse the versionString (from `X
 * -version`) and assign relevant information that we infer from the X
 * server version.
 */

static int get_xserver_information(Options *op,
                                   const char *versionString,
                                   int *isModular,
                                   int *supportsOutputClassSection)
{
#define XSERVER_VERSION_FORMAT_1 "X Window System Version"
#define XSERVER_VERSION_FORMAT_2 "X.Org X Server"

    int major, minor, found;
    const char *ptr;

    /* check if this is an XFree86 X server */

    if (strstr(versionString, "XFree86 Version")) {
        ui_error(op, "XFree86 is not supported.");
        return FALSE;
    }


    /*
     * This must be an X.Org X server.  Attempt to parse the major.minor version
     * out of the string
     */

    found = FALSE;

    if (((ptr = strstr(versionString, XSERVER_VERSION_FORMAT_1)) != NULL) &&
        (sscanf(ptr, XSERVER_VERSION_FORMAT_1 " %d.%d", &major, &minor) == 2)) {
        found = TRUE;
    }

    if (!found &&
        ((ptr = strstr(versionString, XSERVER_VERSION_FORMAT_2)) != NULL) &&
        (sscanf(ptr, XSERVER_VERSION_FORMAT_2 " %d.%d", &major, &minor) == 2)) {
        found = TRUE;
    }

    /* if we can't parse the version, give up */

    if (!found) return FALSE;

    /*
     * isModular: X.Org X11R6.x X servers are monolithic, all others
     * are modular
     */

    if (major == 6) {
        *isModular = FALSE;
    } else {
        *isModular = TRUE;
    }

    /*
     * support for using OutputClass sections to automatically match drivers to
     * platform devices was added in X.Org xserver 1.16.
     */
    if ((major == 6) || (major == 7) || ((major == 1) && (minor < 16))) {
        *supportsOutputClassSection = FALSE;
    } else {
        *supportsOutputClassSection = TRUE;
    }

    return TRUE;

} /* get_xserver_information() */



/*
 * query_xorg_version() - run the X binary with the '-version'
 * command line option and extract the version.
 *
 * Using the version, try to infer if it's part of a modular Xorg release. If
 * the version can't be determined, we assume it's not.
 *
 * This function assigns the following fields:
 *      op->modular_xorg
 */

#define OLD_VERSION_FORMAT "(protocol Version %d, revision %d, vendor release %d)"
#define NEW_VERSION_FORMAT "X Protocol Version %d, Revision %d, Release %d."

void query_xorg_version(Options *op)
{
    char *data = NULL;
    int ret = FALSE;

    if (!op->utils[XSERVER])
        goto done;

    if (run_command(op, &data, FALSE, NULL, TRUE,
                    op->utils[XSERVER], " -version", NULL) ||
        (data == NULL)) {
        goto done;
    }

    /*
     * process the `X -version` output to infer if this X server is
     * modular
     */

    ret = get_xserver_information(op, data, &op->modular_xorg,
                                  &op->xorg_supports_output_class);

    /* fall through */

done:

    /*
     * if no X server was found, or querying the version on the command line
     * failed, or get_xserver_information() failed, assume the X server is
     * modular, but does not support OutputClass sections
     */

    if (!ret) {
        op->modular_xorg = TRUE;
        op->xorg_supports_output_class = FALSE;
    }

    nvfree(data);
}


/*
 * check_for_running_x() - running any X server (even with a
 * non-NVIDIA driver) can cause stability problems, so check that
 * there is no X server running.  To do this, scan for any
 * /tmp/.X[n]-lock files, where [n] is the number of the X Display
 * (we'll just check for 0-7). Get the pid contained in this X lock file,
 * this is the pid of the running X server. If any X server is running, 
 * print a warning message and set op->running_x_server_detected. If X is
 * detected, give the user a choice whether to continue installing (return TRUE)
 * or abort (return FALSE).
 */

int check_for_running_x(Options *op)
{
    char path[14], *buf;
    char procpath[17]; /* contains /proc/%d, accounts for 32-bit values of pid */
    int i, pid;

    /*
     * If we are installing for a non-running kernel *and* we are only
     * installing kernel modules, then skip this check.
     */

    if (op->kernel_modules_only && op->kernel_name) {
        ui_log(op, "Only installing kernel modules for a non-running "
               "kernel; skipping the \"is an X server running?\" test.");
        return TRUE;
    }
    
    for (i = 0; i < 8; i++) {
        int ret;
        ret = snprintf(path, 14, "/tmp/.X%1d-lock", i);
        if (ret < 0)
        {
            ui_warn(op, "Failed to determine presence of X lock file");
            return TRUE;
        }

        if (read_text_file(path, &buf) == TRUE) {
            int num = sscanf(buf, "%d", &pid);
            nvfree(buf);
            if (num != 1) {
                ui_warn(op, "Failed to read a pid from X lock file '%s'", path);
                return TRUE;
            }
            snprintf(procpath, 17, "/proc/%d", pid);
            if (access(procpath, F_OK) == 0) {
                ui_log(op, "The file '%s' exists and appears to contain the "
                           "process ID '%d' of a running X server.", path, pid);
                if (op->no_x_check) {
                    ui_log(op, "Continuing per the '--no-x-check' option.");
                } else {
                    int choice = ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
                        NUM_CONTINUE_ABORT_CHOICES, ABORT_CHOICE,
                        "You appear to be running an X server.  Installing the "
                        "NVIDIA driver while X is running is not recommended, "
                        "as doing so may prevent the installer from detecting "
                        "some potential installation problems, and it may not "
                        "be possible to start new graphics applications after "
                        "a new driver is installed.  If you choose to continue "
                        "installation, it is highly recommended that you "
                        "reboot your computer after installation to use the "
                        "newly installed driver.");
                    if (choice == CONTINUE_CHOICE) {
                        op->running_x_server_detected = TRUE;
                    } else {
                        return FALSE;
                    }
                }

                /* We found a running X server; no need to check for others. */
                break;
            }
        }
    }
    
    return TRUE;

} /* check_for_running_x() */

/*
 * nvpci_dev_is_vgpu_gsp() - Check if 'is_vgpu_host_package.txt' file is present
 * in the package. File 'is_vgpu_host_package.txt' is present in vGPU host
 * packages only. Environment variables VGX_BUILD and VGX_KVM_BUILD are used to
 * install vGPU host driver using *-internal.run on Xenserver and KVM
 * respectively.
 * If device_id is present in the list of devIDs of pGPUs that don't support GSP
 * on vGPU then return FALSE, else return TRUE.
 */
static int nvpci_dev_is_vgpu_gsp(Package *p, unsigned int device_id)
{
    unsigned short vgpu_non_gsp_dev_ids[] = {
        0x13bd, // Tesla M10,
        0x13f2, // Tesla M60
        0x13f3, // Tesla M6
        0x15f7, // Tesla P100-PCIE-12GB
        0x15f8, // Tesla P100-PCIE-16GB
        0x15f9, // Tesla P100-SXM2-16GB
        0x1b38, // Tesla P40
        0x1bb3, // Tesla P4
        0x1bb4, // Tesla P6
        0x1db1, // Tesla V100-SXM2-16GB
        0x1db3, // Tesla V100-FHHL-16GB
        0x1db4, // Tesla V100-PCIE-16GB
        0x1db5, // Tesla V100-SXM2-32GB
        0x1db6, // Tesla V100-PCIE-32GB
        0x1df6, // Tesla V100S-PCIE-32GB,
        0x1e30, // Quadro RTX 8000, Quadro RTX 6000,
        0x1e37, // PG150 SKU220, PG150 SKU215,
        0x1e78, // Quadro RTX 8000, Quadro RTX 6000,
        0x1eb8, // Tesla T4
        0x20b0, // NVIDIA A100-SXM4-40GB
        0x20b2, // NVIDIA A100-SXM4-80GB
        0x20b5, // NVIDIA A100-PCIE-80GB, A100-PCIe-80GB LC,
        0x20b7, // NVIDIA A30
        0x20b8, // NVIDIA A100X,
        0x20b9, // NVIDIA A30X,
        0x20f1, // NVIDIA A100-PCIE-40GB
        0x20f3, // NVIDIA A800-SXM4-80GB
        0x20f5, // NVIDIA A800 80GB PCIe
        0x20f6, // NVIDIA A800 PCIe 40GB Active,
        0x20fd, // NVIDIA AX800,
        0x2230, // NVIDIA RTX A6000
        0x2231, // NVIDIA RTX A5000
        0x2233, // NVIDIA RTX A5500,
        0x2235, // NVIDIA A40
        0x2236, // NVIDIA A10
        0x2237, // NVIDIA A10G
        0x2238, // NVIDIA A10M,
        0x25b6, // NVIDIA A16, NVIDIA A2
    };

    int i, is_vgx_kvm_build = 0, is_vgx_build = 0;
    const char *vgx_build = getenv("VGX_BUILD");
    const char *vgx_kvm_build = getenv("VGX_KVM_BUILD");

    if (vgx_build != NULL) {
        is_vgx_build = 1;
    }

    if (vgx_kvm_build != NULL) {
        is_vgx_kvm_build = 1;
    }

    /* Check if this is vGPU host package */
    if ((access("./is_vgpu_host_package.txt", F_OK) == 0) || (is_vgx_build == 1) ||
       (is_vgx_kvm_build == 1)) {

        /* If device_id is present in the non-gsp devId list, return FALSE */
        for (i = 0; i < ARRAY_LEN(vgpu_non_gsp_dev_ids); i++) {
            if (device_id == vgpu_non_gsp_dev_ids[i]) {
                return FALSE;
            }
        }
        return TRUE;
    }
    return FALSE;
}

/*
 * check_for_nvidia_graphics_devices() - check if there are supported
 * NVIDIA graphics devices installed in this system. If no supported devices
 * are found, a warning message is printed. If legacy devices are detected
 * in the system, a warning message is printed for each one.
 * Other special requirements (e.g. defaulting to "-m kernel-open" for self-
 * hosted Hopper) are handled here as well.
 */

void check_for_nvidia_graphics_devices(Options *op, Package *p)
{
    struct pci_device_iterator *iter;
    struct pci_device *dev;
    int i, found_supported_device = FALSE, found_self_hosted = FALSE;
    int found_vga_device = FALSE, found_vgpu_gsp = FALSE, count = 0;

    if (pci_system_init()) {
        return;
    }

    iter = nvpci_find_gpu_by_vendor(NV_PCI_VENDOR_ID);

    for (dev = pci_device_next(iter); dev; dev = pci_device_next(iter), count++) {
        if (dev->device_id >= 0x0020 /* TNT or later */) {
            /*
             * First check if this GPU is a "legacy" GPU; if it is, print a
             * warning message and point the user to the NVIDIA Linux
             * driver download page for.
             *
             * LegacyList only contains a row with a full 4-part ID (including
             * subdevice and subvendor IDs) if its name differs from other
             * devices with the same devid. For all other devices with the same
             * devid and name, there is only one row, with subdevice and
             * subvendor IDs set to 0.
             *
             * This loop finds the name for the matching devid, but continues
             * searching for a matching 4-part ID with a different name, and
             * breaks if it finds one.
             */
            int found_legacy_device = FALSE;
            unsigned int branch = 0;
            const char *dev_name = NULL;

            for (i = 0; i < sizeof(LegacyList) / sizeof(LEGACY_INFO); i++) {
                if (dev->device_id == LegacyList[i].uiDevId) {
                    int found_specific =
                        (dev->subvendor_id == LegacyList[i].uiSubVendorId &&
                         dev->subdevice_id == LegacyList[i].uiSubDevId);

                    if (found_specific || LegacyList[i].uiSubDevId == 0) {
                        branch = LegacyList[i].branch;
                        dev_name = LegacyList[i].AdapterString;
                        found_legacy_device = TRUE;
                    }

                    if (found_specific) {
                        break;
                    }
                }
            }

            if (found_legacy_device) {
                int j, nstrings;
                const char *branch_string = "";
                nstrings = sizeof(LegacyStrings) / sizeof(LEGACY_STRINGS);
                for (j = 0; j < nstrings; j++) {
                    if (LegacyStrings[j].branch == branch) {
                        branch_string = LegacyStrings[j].description;
                        break;
                    }
                }

                ui_warn(op, "The NVIDIA %s GPU installed in this system is supported "
                        "through the NVIDIA %s legacy Linux graphics drivers.  Please "
                        "visit http://www.nvidia.com/object/unix.html for more "
                        "information.  The %s NVIDIA Linux graphics driver will "
                        "ignore this GPU.",
                        dev_name,
                        branch_string,
                        p->version);
            } else {
                found_supported_device = TRUE;

                if (nvpci_dev_is_vga(dev)) {
                    found_vga_device = TRUE;
                }

                if (pci_devid_is_self_hosted(dev->device_id)) {
                    found_self_hosted = TRUE;
                }

                /* Check the first device in the system is vGPU GSP supported device */
                if ((count == 0) && nvpci_dev_is_vgpu_gsp(p, dev->device_id)) {
                    found_vgpu_gsp = TRUE;
                }
            }
        }
    }

    pci_system_cleanup();

    if (!found_supported_device) {
        /* Don't test-load the modules on a system with no supported devices */
        op->skip_module_load = TRUE;

        ui_warn(op, "You do not appear to have an NVIDIA GPU supported by the "
                 "%s NVIDIA Linux graphics driver installed in this system.  "
                 "For further details, please see the appendix SUPPORTED "
                 "NVIDIA GRAPHICS CHIPS in the README available on the Linux "
                 "driver download page at www.nvidia.com.", p->version);
    }

    if (!found_vga_device)
        op->no_nvidia_xconfig_question = TRUE;

    if ((found_self_hosted || found_vgpu_gsp) && !op->kernel_module_build_directory_override) {
        ui_log(op, "This system requires use of the NVIDIA open kernel "
               "modules; these will be selected by default.");
        nvfree(p->kernel_module_build_directory);
        p->kernel_module_build_directory = nvstrdup("kernel-open");
    }
} /* check_for_nvidia_graphics_devices() */


/*
 * check_selinux() - check if selinux is available.
 * Sets the variable op->selinux_enabled.
 * Returns TRUE on success, FALSE otherwise.
 */
int check_selinux(Options *op)
{
    int selinux_available = TRUE;
    
    if (op->utils[CHCON] == NULL ||
        op->utils[SELINUX_ENABLED] == NULL || 
        op->utils[GETENFORCE] == NULL) {
        selinux_available = FALSE;
    }
    
    switch (op->selinux_option) {
    case SELINUX_FORCE_YES:
        if (selinux_available == FALSE) {
            /* We have set the option --force-selinux=yes but SELinux 
             * is not available on this system */
            ui_error(op, "Invalid option '--force-selinux=yes'; "
                        "SELinux is not available on this system");
            return FALSE;
        }
        op->selinux_enabled = TRUE;
        break;
        
    case SELINUX_FORCE_NO:
        if (selinux_available == TRUE) {
            char *data = NULL;
            int ret = run_command(op, &data, FALSE, NULL, TRUE,
                                  op->utils[GETENFORCE], NULL);

            if ((ret != 0) || (!data)) {
                ui_warn(op, "Cannot check the current mode of SELinux; "
                             "Command getenforce() failed"); 
            } else if (!strcmp(data, "Enforcing")) {
                /* We have set the option --force-selinux=no but SELinux 
                 * is enforced on this system */
                ui_warn(op, "The option '--force-selinux' has been set to 'no', "
                            "but SELinux is enforced on this system; "
                            "The X server may not start correctly ");
            }
            nvfree(data);
        }        
        op->selinux_enabled = FALSE;
        break;
        
    case SELINUX_DEFAULT:
        op->selinux_enabled = FALSE;
        if (selinux_available == TRUE) {
            int ret = run_command(op, NULL, FALSE, NULL, TRUE,
                                  op->utils[SELINUX_ENABLED], NULL);
            if (ret == 0) {
                op->selinux_enabled = TRUE;
            }
        }
        break;
    }                 

    /* Figure out which chcon type we need if the user didn't supply one. */
    if (op->selinux_enabled && !op->selinux_chcon_type) {
        unsigned char foo = 0;
        char *tmpfile;
        static const char* chcon_types[] = {
            "textrel_shlib_t",    /* Shared library with text relocations */
            "texrel_shlib_t",     /* Obsolete synonym for the above */
            "shlib_t",            /* Generic shared library */
            NULL
        };

        /* Create a temporary file */
        tmpfile = write_temp_file(op, 1, &foo, S_IRUSR);
        if (!tmpfile) {
            ui_warn(op, "Couldn't test chcon.  Assuming shlib_t.");
            op->selinux_chcon_type = "shlib_t";
        } else {
            int i;

            /* Try each chcon command */
            for (i = 0; chcon_types[i]; i++) {
                if (set_security_context(op, tmpfile, chcon_types[i])) {
                    break;
                }
            }

            if (!chcon_types[i]) {
                /* None of them work! */
                ui_warn(op, "Couldn't find a working chcon argument.  "
                            "Defaulting to shlib_t.");
                op->selinux_chcon_type = "shlib_t";
            } else {
                op->selinux_chcon_type = chcon_types[i];
            }

            unlink(tmpfile);
            nvfree(tmpfile);
        }
    }

    if (op->selinux_enabled) {
        ui_log(op, "Tagging shared libraries with chcon -t %s.",
               op->selinux_chcon_type);
    }

    return TRUE;
} /* check_selinux */

/*
 * run_nvidia_xconfig() - run the `nvidia-xconfig` utility.  Without
 * any options, this will just make sure the X config file uses the
 * NVIDIA driver by default.
 *
 * Parameters:
 *
 *     restore:  controls whether the --restore-original-backup option is added,
 *               which attempts to restore the original backed up X config file.
 *     question: if this is non-NULL, the user will be asked 'question' as a
 *               yes or no question, to determine whether to run nvidia-xconfig.
 *     answer:   the default answer to 'question'.
 *
 * Returns TRUE if nvidia-xconfig ran successfully; returns FALSE if
 * nvidia-xconfig ran unsuccessfully, or did not run at all.
 */

int run_nvidia_xconfig(Options *op, int restore, const char *question,
                       int default_answer)
{
    int ret = FALSE;
    char *nvidia_xconfig;

    nvidia_xconfig = find_system_util("nvidia-xconfig");

    if (nvidia_xconfig == NULL) {
        /* nvidia-xconfig not found: don't run it or ask any questions */
        goto done;
    }

    ret = question ? ui_yes_no(op, default_answer, "%s", question) : TRUE;

    if (ret) {
        int cmd_ret;
        char *data, *cmd, *args;

        args = restore ? " --restore-original-backup" : "";

        cmd = nvstrcat(nvidia_xconfig, args, NULL);

        cmd_ret = run_command(op, &data, FALSE, NULL, TRUE, cmd, NULL);

        if (cmd_ret != 0) {
            ui_error(op, "Failed to run `%s`:\n%s", cmd, data);
            ret = FALSE;
        }

        nvfree(cmd);
        nvfree(data);
    }

done:
    nvfree(nvidia_xconfig);

    return ret;
    
} /* run_nvidia_xconfig() */



#define DISTRO_HOOK_DIRECTORY "/usr/lib/nvidia/"

/*
 * run_distro_hook() - run a distribution-provided hook script
 */

HookScriptStatus run_distro_hook(Options *op, const char *hook)
{
    int ret, status, shouldrun = op->run_distro_scripts;
    char *cmd = nvstrcat(DISTRO_HOOK_DIRECTORY, hook, NULL);

    if (op->kernel_modules_only) {
        ui_expert(op,
                  "Not running distribution-provided %s script %s because "
                  "--kernel-modules-only was specified.",
                  hook, cmd);
        ret = HOOK_SCRIPT_NO_RUN;
        goto done;
    }

    if (access(cmd, X_OK) < 0) {
        ui_expert(op, "No distribution %s script found.", hook);
        ret = HOOK_SCRIPT_NO_RUN;
        goto done;
    }

    /* in expert mode, ask before running distro hooks */
    if (op->expert) {
        shouldrun = ui_yes_no(op, shouldrun,
                              "Run distribution-provided %s script %s?",
                              hook, cmd);
    }

    if (!shouldrun) {
        ui_expert(op,
                  "Not running distribution-provided %s script %s",
                  hook, cmd);
        ret = HOOK_SCRIPT_NO_RUN;
        goto done;
    }

    ui_status_begin(op, "Running distribution scripts", "Executing %s", cmd);
    status = run_command(op, NULL, TRUE, NULL, TRUE, cmd, NULL);
    ui_status_end(op, "done.");

    ret = (status == 0) ? HOOK_SCRIPT_SUCCESS : HOOK_SCRIPT_FAIL;

done:
    nvfree(cmd);
    return ret;
}


/*
 * prompt_for_user_cancel() - print a caller-supplied message and ask the
 * user whether to cancel the installation. If the file at the caller-supplied
 * path is readable, include any text from that file as additional detail for
 * the message. Returns TRUE if the user decides to cancel the installation;
 * returns FALSE if the user decides not to cancel.
 */
static int prompt_for_user_cancel(Options *op, const char *file,
                                  int default_choice, const char *text)
{
    int ret, file_read, msglen;
    char *message = NULL, *prompt;

    file_read = read_text_file(file, &message);

    if (!file_read || !message) {
        message = nvstrdup("");
    }

    msglen = strlen(message);

    prompt = nvstrcat(text, msglen > 0 ? "\n\nPlease review the message "
                      "provided by the maintainer of this alternate "
                      "installation method and decide how to proceed:" : NULL,
                      NULL);

    ret = ui_paged_prompt(op, prompt, msglen > 0 ? "Information about the "
                          "alternate installation method" : "", message,
                          CONTINUE_ABORT_CHOICES, NUM_CONTINUE_ABORT_CHOICES,
                          default_choice);

    nvfree(message);
    nvfree(prompt);

    if (ret == ABORT_CHOICE) {
        ui_error(op, "The installation was canceled due to the availability "
                 "or presence of an alternate driver installation. Please "
                 "see %s for more details.", op->log_file_name);
        return TRUE;
    }

    return FALSE;
}

#define INSTALL_PRESENT_FILE "alternate-install-present"
#define INSTALL_AVAILABLE_FILE "alternate-install-available"

/*
 * check_for_alternate_install() - check to see if an alternate install is
 * available or present. If present, recommend updating via the alternate
 * mechanism or uninstalling first before proceeding with an nvidia-installer
 * installation; if available, but not present, inform the user about it.
 * Returns TRUE if no alternate installation is available or present, or if
 * checking for alternate installs is skipped, or if the user decides not to
 * cancel the installation. Returns FALSE if the user decides to cancel the
 * installation.
 */

int check_for_alternate_install(Options *op)
{
    int shouldcheck = op->check_for_alternate_installs;
    const char *alt_inst_present = DISTRO_HOOK_DIRECTORY INSTALL_PRESENT_FILE;
    const char *alt_inst_avail = DISTRO_HOOK_DIRECTORY INSTALL_AVAILABLE_FILE;

    if (op->expert) {
        shouldcheck = ui_yes_no(op, shouldcheck,
                                "Check for the availability or presence of "
                                "alternate driver installs?");
    }

    if (!shouldcheck) {
        return TRUE;
    }

    if (access(alt_inst_present, F_OK) == 0) {
        const char *msg;

        msg = "The NVIDIA driver appears to have been installed previously "
              "using a different installer. To prevent potential conflicts, it "
              "is recommended either to update the existing installation using "
              "the same mechanism by which it was originally installed, or to "
              "uninstall the existing installation before installing this "
              "driver.";

        return !prompt_for_user_cancel(op, alt_inst_present, ABORT_CHOICE, msg);
    }

    if (access(alt_inst_avail, F_OK) == 0) {
        const char *msg;

        msg = "An alternate method of installing the NVIDIA driver was "
              "detected. (This is usually a package provided by your "
              "distributor.) A driver installed via that method may integrate "
              "better with your system than a driver installed by "
              "nvidia-installer.";

        return !prompt_for_user_cancel(op, alt_inst_avail, CONTINUE_CHOICE, msg);
    }

    return TRUE;
}



/*
 * Determine if the nouveau driver is currently in use.  We do the
 * equivalent of:
 *
 *   ls -l /sys/bus/pci/devices/ /driver | grep nouveau
 *
 * The directory structure under /sys/bus/pci/devices/ should contain
 * a directory for each PCI device, and for those devices with a
 * kernel driver there will be a "driver" symlink.
 *
 * This appears to be consistent with how libpciaccess works.
 *
 * Returns TRUE if nouveau is found; returns FALSE if not.
 */

#define SYSFS_DEVICES_PATH "/sys/bus/pci/devices"

int nouveau_is_present(void)
{
    DIR *dir;
    struct dirent * ent;
    int found = FALSE;

    dir = opendir(SYSFS_DEVICES_PATH);

    if (!dir) {
        return FALSE;
    }

    while ((ent = readdir(dir)) != NULL) {

        char driver_path[PATH_MAX];
        char symlink_target[PATH_MAX];
        char *name;
        ssize_t ret;

        if ((strcmp(ent->d_name, ".") == 0) ||
            (strcmp(ent->d_name, "..") == 0)) {
            continue;
        }

        snprintf(driver_path, PATH_MAX,
                 SYSFS_DEVICES_PATH "/%s/driver", ent->d_name);

        driver_path[PATH_MAX - 1] = '\0';

        ret = readlink(driver_path, symlink_target, PATH_MAX);
        if (ret < 0) {
            continue;
        }

        /* readlink(3) does not nul-terminate its returned string */

        ret = NV_MIN(ret, PATH_MAX - 1);

        symlink_target[ret] = '\0';

        name = basename(symlink_target);

        if (strcmp(name, "nouveau") == 0) {
            found = TRUE;
            break;
        }
    }

    closedir(dir);

    return found;
}



static const char* modprobe_directories[] = { "/etc/modprobe.d",
                                              "/usr/lib/modprobe.d" };
#define DISABLE_NOUVEAU_FILE "/nvidia-installer-disable-nouveau.conf"

/*
 * this checksum is the result of compute_crc() for the file contents
 * written in disable_nouveau()
 */

#define DISABLE_NOUVEAU_FILE_CKSUM 3728279991U

/*
 * disable_nouveau_filename() - generate the filename of a configuration file.
 * The caller should ensure that the directory exists, or be able to handle
 * failures correctly if the directory does not exist.
 */
static char *disable_nouveau_filename(const char *directory)
{
    return nvstrcat(directory, DISABLE_NOUVEAU_FILE, NULL);
}

static char *write_disable_nouveau_file(const char *directory)
{
    int ret;
    struct stat stat_buf;
    FILE *file;
    char *filename;

    ret = stat(directory, &stat_buf);

    if (ret != 0 || !S_ISDIR(stat_buf.st_mode)) {
        return NULL;
    }

    filename = disable_nouveau_filename(directory);
    file = fopen(filename, "w+");

    if (!file) {
        nvfree(filename);
        return NULL;
    }

    fprintf(file, "# generated by nvidia-installer\n");
    fprintf(file, "blacklist nouveau\n");
    fprintf(file, "options nouveau modeset=0\n");

    ret = fclose(file);

    if (ret != 0) {
        nvfree(filename);
        return NULL;
    }

    return filename;
}


/*
 * Write modprobe configuration fragments to disable loading of
 * nouveau.
 *
 * Returns a list of written configuration files if successful; 
 * returns NULL if there was a failure.
 */

static char *disable_nouveau(void)
{
    int i;
    char *filelist = NULL;

    for (i = 0; i < ARRAY_LEN(modprobe_directories); i++) {
        char *filename = write_disable_nouveau_file(modprobe_directories[i]);
        if (filename) {
            filelist = nv_prepend_to_string_list(filelist, filename, ", ");
            nvfree(filename);
        }
    }

    return filelist;
}



/*
 * Check if any disable nouveau file is already present with the
 * contents that we expect, and return the paths to any found files,
 * or NULL if no matching files were found
 */

static char *disable_nouveau_file_is_present(Options *op,
                                             int *present_at_all_paths)
{
    int i, directory_count = 0, file_count = 0;
    char *filelist = NULL;

    for (i = 0; i < ARRAY_LEN(modprobe_directories); i++) {
        char *filename = disable_nouveau_filename(modprobe_directories[i]);

        if (directory_exists(modprobe_directories[i])) {
            directory_count++;
        }

        if ((access(filename, R_OK) == 0) &&
            (compute_crc(op, filename) == DISABLE_NOUVEAU_FILE_CKSUM)) {
            file_count++;
            filelist = nv_prepend_to_string_list(filelist, filename, ", ");
        }
        nvfree(filename);
    }

    *present_at_all_paths = directory_count == file_count;
    return filelist;
}



/*
 * Check if the nouveau kernel driver is in use.  If it is, provide an
 * appropriate error message and offer to try to disable nouveau.
 *
 * Returns FALSE if the user chooses to abort the installation due to
 * the presence of Nouveau; TRUE otherwise.
 */

int check_for_nouveau(Options *op)
{
    int ret, nouveau_detected, all_files_written;
    char *disable_files;

#define NOUVEAU_POINTER_MESSAGE                                         \
    "Please consult the NVIDIA driver README and your Linux "           \
        "distribution's documentation for details on how to correctly " \
        "disable the Nouveau kernel driver."

    if (op->no_nouveau_check) return TRUE;

    nouveau_detected = nouveau_is_present();

    if (nouveau_detected) {
        ui_warn(op, "The Nouveau kernel driver is currently in use "
                "by your system.  This driver is incompatible with the NVIDIA "
                "driver, and must be disabled before proceeding.");
    } else {
        return TRUE;
    }

    disable_files = disable_nouveau_file_is_present(op, &all_files_written);

    if (disable_files) {
        ui_warn(op, "One or more modprobe configuration files to disable "
                "Nouveau are already present at: %s.  Please be "
                "sure you have rebooted your system since these files were "
                "written.  If you have rebooted, then Nouveau may be enabled "
                "for other reasons, such as being included in the system "
                "initial ramdisk or in your X configuration file.  "
                NOUVEAU_POINTER_MESSAGE, disable_files);
        nvfree(disable_files);
        if (all_files_written) {
            /* If all of the possible disable files are already present,
             * don't offer to write any more. */
            goto continue_or_abort;
        }
    }

    /* Disable files were missing from at least one of the expected locations:
     * offer to create additional ones. */
    ret = ui_yes_no(op, op->disable_nouveau,
                    "Nouveau can usually be disabled by adding files "
                    "to the modprobe configuration directories and rebuilding "
                    "the initramfs.\n\n"
                    "Would you like nvidia-installer to attempt to create "
                    "these modprobe configuration files for you?");

    if (ret) {
        disable_files = disable_nouveau();

        if (disable_files) {
            ui_message(op, "One or more modprobe configuration files to "
                       "disable Nouveau have been written.  You will need "
                       "to reboot your system and possibly rebuild the initramfs "
                       "before these changes can take effect.  Note if you "
                       "later wish to reenable Nouveau, you will need to "
                       "delete these files: %s",
                       disable_files);
            nvfree(disable_files);
        } else {
            ui_warn(op, "Unable to alter the nouveau modprobe configuration.  "
                    NOUVEAU_POINTER_MESSAGE);
        }
    } else {
        ui_message(op, "Please disable Nouveau manually and attempt to install "
                   "the NVIDIA driver again later.");
        return FALSE;
    }

continue_or_abort:

    ret = ui_multiple_choice(op, CONTINUE_ABORT_CHOICES,
        NUM_CONTINUE_ABORT_CHOICES,
        op->allow_installation_with_running_driver ?
        CONTINUE_CHOICE : ABORT_CHOICE,
        "nvidia-installer is not able to perform some of the sanity checks "
        "which detect potential installation problems while Nouveau is loaded. "
        "Would you like to continue installation without these sanity "
        "checks, or abort installation, confirm that Nouveau has been "
        "properly disabled, and attempt installation again later?");

    if (ret == ABORT_CHOICE) {
        /* Offer to update the initramfs: normally, this happens before
         * installing files, but the user is explicitly bailing out. */
        update_initramfs(op);
        return FALSE;
    }

    op->skip_module_load = TRUE;
    ui_log(op, "Proceeding with installation despite the presence of Nouveau. "
               "Kernel module load tests will be skipped.");

    return TRUE;
}

#define DKMS_STATUS  " status"
#define DKMS_INSTALL " install"
#define DKMS_REMOVE  " remove"

/*
 * Run the DKMS tool with the provided arguments. The following operations
 * are supported:
 *
 *     DKMS_STATUS: 
 *         Check the status of the specified module.
 *     DKMS_INSTALL: requires version
 *         Installs the module for the currently running kernel.
 *     DKMS_REMOVE: reqires version
 *         Removes the module from all kernels.
 *
 * run_dkms returns TRUE if dkms is found and exits with status 0 when run;
 * FALSE if dkms can't be found, or exits with non-0 status.
 */
static int run_dkms(Options *op, const char* verb, const char *version,
                    const char *kernel, char** out)
{
    char *cmdline, *veropt, *kernopt = NULL;
    const char *modopt = " -m nvidia"; /* XXX real name is in the Package */
    const char *kernopt_all = "", *depmod_opt = "";
    char *output;
    int ret;

    /* Fail if DKMS not found */
    if (!op->utils[DKMS]) {
        if (strcmp(verb, DKMS_STATUS) != 0) {
            ui_error(op, "Failed to find dkms on the system!");
        }
        return FALSE;
    }

    /*
     * Skip depmod when installing or removing, if the --no-depmod option is
     * available. nvidia-installer already runs depmod(8) as part of the kernel
     * module installation process.
     */
    if ((strcmp(verb, DKMS_REMOVE) == 0 || strcmp(verb, DKMS_INSTALL) == 0) &&
        option_is_supported(op, op->utils[DKMS], "--help", "--no-depmod")) {
        depmod_opt = " --no-depmod";
    }

    /* Convert function parameters into commandline arguments. Optional
     * arguments may be NULL, in which case nvstrcat() will end early. */
    veropt = version ? nvstrcat(" -v ", version, NULL) : NULL;

    if (strcmp(verb, DKMS_REMOVE) == 0) {
        /* Always remove DKMS modules from all kernels to avoid confusion. */
        kernopt_all = " --all";
    } else {
        kernopt = kernel ? nvstrcat(" -k ", kernel, NULL) : NULL;
    }

    cmdline = nvstrcat(op->utils[DKMS], verb, depmod_opt, modopt, veropt,
                       kernopt_all, kernopt, NULL);

    /* Run DKMS */
    ret = run_command(op, &output, FALSE, NULL, TRUE, cmdline, NULL);
    if (ret != 0) {
        ui_error(op, "Failed to run `%s`: %s", cmdline, output);
    }

    nvfree(cmdline);
    nvfree(veropt);
    nvfree(kernopt);
    if (out) {
        *out = output;
    } else {
        nvfree(output);
    }

    return ret == 0;
}

/*
 * Check to see whether the module is installed via DKMS.
 * (The driver and kernel version parameters are optional: if NULL, check for
 * any version; if non-NULL, check for the specified version only.)
 *
 * Returns TRUE if DKMS is found, and a matching installed driver is detected.
 * Returns FALSE if DKMS not found, or no matching driver detected.
 */
int dkms_module_installed(Options* op, const char *driver, const char *kernel)
{
    int ret, matched = FALSE;
    char *output = NULL;

    ret = run_dkms(op, DKMS_STATUS, driver, kernel, &output);

    if (!ret || output == NULL) {
        return FALSE;
    }

    if (driver != NULL && kernel != NULL) {
        /*
         * If a module and kernel version were specified, only match actually
         * installed modules.
         */
        if (strstr(output, ": installed") != NULL) {
            matched = TRUE;
        }
    } else {
        /*
         * Otherwise, just match any non-empty output to detect modules in any
         * state (added, built, installed).
         */
        matched = output[0] != '\0';
    }

    nvfree(output);

    return matched;
}


/*
 * Generate a tar archive conforming to the `dkms mktarball` export format.
 */
static char *dkms_gen_tarball(Options *op, Package *p, const char *kernel)
{
    char *tmpdir, *sourcedir, *treedir, *builddir, *logdir, *moduledir, *dst;
    char *tarball = NULL;
    const char *log;
    int ret, i;

    tmpdir = make_tmpdir(op);
    if (!tmpdir) return NULL;

    sourcedir = nvdircat(tmpdir, "dkms_source_tree", NULL);
    treedir = nvdircat(tmpdir, "dkms_main_tree", NULL);
    builddir = nvdircat(treedir, kernel, get_machine_arch(op), NULL);
    logdir = nvdircat(builddir, "log", NULL);
    moduledir = nvdircat(builddir, "module", NULL);

    /*
     * DKMS 2.x checks for dkms_dbversion with a major version of 2.
     * DKMS 3.x ignores dkms_dbversion. Write a dkms_dbversion file
     * for compatibility with DKMS 2.x.
     */
    dst = nvdircat(treedir, "dkms_dbversion", NULL);
    ret = nv_string_to_file(dst, "2.0.0");
    nvfree(dst);
    if (!ret) goto done;

    /* Write the build log to the tarball staging directory */
    dst = nvdircat(logdir, "make.log", NULL);

    if (p->kernel_make_logs) {
        log = p->kernel_make_logs;
    } else {
        log = "This driver was linked from precompiled interfaces and directly "
              "registered with DKMS. This process did not preserve build logs.";
    }

    ret = nv_string_to_file(dst, log);
    nvfree(dst);
    if (!ret) goto done;

    /* Copy the module sources and dkms.conf to the staging directory */
    ret = FALSE;
    for (i = 0; i < p->num_entries; i++) {
        char *dst_copy, *dstdir;
        char *dkms_dstdir, *dkms_srcdir;

        switch (p->entries[i].type) {
        case FILE_TYPE_DKMS_CONF:
        case FILE_TYPE_KERNEL_MODULE_SRC:
            dst = nvdircat(sourcedir, p->entries[i].path, p->entries[i].name,
                           NULL);
            dst_copy = nvstrdup(dst);
            dstdir = dirname(dst_copy);

            if (!directory_exists(dstdir)) {
                ret = mkdir_recursive(op, dstdir, 0755, FALSE);
            }

            dkms_srcdir = nvstrcat("/usr/src/nvidia-", p->version, NULL);
            dkms_dstdir = nvdircat(dkms_srcdir, p->entries[i].path, NULL);
            nvfree(dkms_srcdir);

            /*
             * Create any missing directories which will contain the kernel
             * module sources once the modules are installed via DKMS. This
             * is done ahead of time, with mkdir logging enabled, so these
             * directories can be removed upon uninstallation.
             */
            if (!directory_exists(dkms_dstdir)) {
                mkdir_recursive(op, dkms_dstdir, 0755, TRUE);
            }

            nvfree(dkms_dstdir);

            ret = ret && copy_file(op, p->entries[i].file, dst, 0644);
            nvfree(dst);
            nvfree(dst_copy);
            if (!ret) goto done;
            break;
        default:
            break;
        }
    }

    /* If ret wasn't set to TRUE above, there are no source files */
    if (!ret) goto done;

    ret = mkdir_recursive(op, moduledir, 0755, FALSE);
    if (!ret) goto done;

    /* Copy the (already built) kernel modules */
    for (i = 0; i < p->num_kernel_modules; i++) {
        char *src = nvdircat(p->kernel_module_build_directory,
                             p->kernel_modules[i].module_filename, NULL);

        dst = nvdircat(moduledir, p->kernel_modules[i].module_filename, NULL);
        ret = copy_file(op, src, dst, 0644);
        nvfree(src);
        nvfree(dst);

        if (!ret) goto done;
    }

    /* Reserve a name for a temporary file and run tar(1) to create a tarball */
    tarball = write_temp_file(op, 0, NULL, 0644);
    if (tarball) {
        char *output;

        ret = run_command(op, &output, FALSE, 0, TRUE,
                          op->utils[TAR], " -C ", tmpdir, " -cf ", tarball,
                          " .", NULL);

        if (ret != 0) {
            ui_error(op, "Failed to create a DKMS tarball: %s", output);
        }
        nvfree(output);

        if (ret != 0) {
            unlink(tarball);
            nvfree(tarball);
            tarball = NULL;
            goto done;
        }
    }

done:
    remove_directory(op, tmpdir);
    nvfree(tmpdir);
    nvfree(sourcedir);
    nvfree(treedir);
    nvfree(builddir);
    nvfree(logdir);
    nvfree(moduledir);

    return tarball;
}


/*
 * Register the given version of the modules with DKMS. This is done by first
 * generating and importing a tarball containing the (already built and
 * installed) kernel modules and build log using `dkms ldtarball`, then creating
 * a kernel-$kernel_version-$arch symbolic link in the DKMS tree to mark them
 * as installed in the DKMS database.
 */
void dkms_register_module(Options *op, Package *p, const char *kernel)
{
    char *tarball;
    int ret = FALSE;

    /* If dkms(8) or tar(1) are missing, there is nothing to do here. */

    if (!op->utils[DKMS] || !op->utils[TAR]) return;

    /*
     * Offer the DKMS option if DKMS exists and the kernel modules and their
     * sources were installed somewhere.
     */
    if (op->no_kernel_modules || op->no_kernel_module_source) return;

    op->dkms = ui_yes_no(op, op->dkms,
                         "Would you like to register the kernel module sources "
                         "with DKMS? This will allow DKMS to automatically "
                         "build a new module, if your kernel changes later.");

    /* If the user decided not to use DKMS, there is nothing to do here. */
    if (!op->dkms) return;

    ui_status_begin(op, "Registering the kernel modules with DKMS:",
                    "Generating DKMS tarball");

    /* Create a DKMS tarball */
    tarball = dkms_gen_tarball(op, p, kernel);
    if (tarball) {
        char *output;

        /* Load the tarball into the DKMS tree */
        ui_status_update(op, .5, "Importing DKMS tarball");
        ret = run_command(op, &output, FALSE, 0, TRUE,
                          op->utils[DKMS], " ldtarball ", tarball, NULL);

        if (ret != 0) {
            ui_error(op, "Failed to load DKMS tarball: %s", output);
        }

        unlink(tarball);
        nvfree(tarball);
        nvfree(output);

        if (ret != 0) {
            goto done;
        }
    } else {
        ui_error(op, "Failed to create a DKMS tarball");
        goto done;
    }

    /*
     * After loading the tarball, the modules should be in the "built" state.
     * Run `dkms install` so that DKMS can recognize the (already installed)
     * modules as installed, and do other actions such as updating weak-modules.
     */
    ui_status_update(op, .75, "Marking modules as installed");
    ret = run_dkms(op, DKMS_INSTALL, p->version, kernel, NULL);
    if (!ret) goto done;

    /*
     * As a final sanity check, make sure that `dkms status` shows that the
     * kernel modules were successfully "installed".
     */
    ret = dkms_module_installed(op, p->version, kernel);

done:
    if (ret) {
        ui_status_end(op, "done.");
    } else {
        ui_status_end(op, "Error.");
        ui_warn(op, "Failed to register the NVIDIA kernel modules with DKMS. "
                    "The NVIDIA kernel modules will be installed, but will not "
                    "be automatically rebuilt if you change your kernel.");
    }

    op->dkms_registered = ret;
}

/*
 * Remove the given version of the module on all available kernels.
 */
int dkms_remove_module(Options *op, const char *version)
{
    return run_dkms(op, DKMS_REMOVE, version, NULL, NULL);
}

/*
 * Test the last bit of the given file. Return 1 if the bit is set, 0 if it is
 * not set, and < 0 on error.
 *
 */
static int test_last_bit(const char *file) {
    char buf;
    int ret, data_read = FALSE;
    FILE *fp = fopen(file, "r");

    if (!fp) {
        return -errno;
    }

    /* XXX Using fseek(3) could make this more efficient for larger files, but
     * trying to read after an fseek(stream, -1, SEEK_END) call on a UEFI
     * variable file in sysfs hits a premature EOF. */

    while (fread(&buf, 1, 1, fp)) {
        data_read = TRUE;
    }

    if (ferror(fp)) {
        ret = -ferror(fp);
    } else if (data_read) {
        ret = buf & 1;
    } else {
        ret = -EIO;
    }

    fclose(fp);
    return ret;
}

static const char* secure_boot_files[] = {
    "/sys/firmware/efi/vars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c/data",
    "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c",
};

/*
 * secure_boot_enabled() - Check the known paths where secure boot status is
 * exposed. If secure boot is enabled, return 1. If secure boot is disabled,
 * return 0. On failure to detect whether secure boot is enabled, return < 0.
 */
int secure_boot_enabled(void) {
    int i, ret = -ENOENT;

    for (i = 0; i < ARRAY_LEN(secure_boot_files); i++) {
        if (access(secure_boot_files[i], R_OK) == 0) {
            ret = test_last_bit(secure_boot_files[i]);
            if (ret >= 0) {
                break;
            }
        }
    }

    return ret;
}



/*
 * get_elf_architecture() - attempt to read an ELF header from the given file;
 * returns ELF_ARCHITECTURE_{32,64,UNKNOWN} if the architecture could be parsed,
 * ELF_INVALID_FILE on error, or if the file is not valid ELF.
 */

ElfFileType get_elf_architecture(const char *filename)
{
    FILE *fp;
    ElfW(Ehdr) header;

    fp = fopen(filename, "r");

    /* Read the ELF header */

    if (fp) {
        int ret = fread(&header, sizeof(header), 1, fp);
        fclose(fp);

        if (ret != 1) {
            return ELF_INVALID_FILE;
        }
    } else {
        return ELF_INVALID_FILE;
    }

    /* Verify the magic number */

    if (strncmp((char *) header.e_ident, "\177ELF", 4) != 0) {
        return ELF_INVALID_FILE;
    }

    /* Parse the architecture from the ELF header */

    switch(header.e_ident[EI_CLASS]) {
        case ELFCLASS32:   return ELF_ARCHITECTURE_32;
        case ELFCLASS64:   return ELF_ARCHITECTURE_64;
        case ELFCLASSNONE: return ELF_ARCHITECTURE_UNKNOWN;
        default:           return ELF_INVALID_FILE;
    }
}



/*
 * set_concurrency_level() - automatically determine the concurrency level,
 * if the user has not specified it.
 */

void set_concurrency_level(Options *op)
{
    int detected_cpus;

    if (op->concurrency_level) {
        ui_log(op, "Concurrency level set to %d on the command line.",
               op->concurrency_level);
    } else {
        /* Systems with very high CPU counts may hit the max tasks limit. */
        static const int max_default_cpus = 32;
        int default_concurrency;
#if defined _SC_NPROCESSORS_ONLN
        detected_cpus = sysconf(_SC_NPROCESSORS_ONLN);

        if (detected_cpus < max_default_cpus) {
            default_concurrency = detected_cpus;
        } else {
            default_concurrency = max_default_cpus;
        }

        if (detected_cpus >= 1) {
            ui_log(op, "Detected %d CPUs online; setting concurrency level "
                   "to %d.", detected_cpus, default_concurrency);
        } else
#else
#warning _SC_NPROCESSORS_ONLN not defined; nvidia-installer will not be able \
to detect the number of processors.
#endif
        {
            ui_log(op, "Unable to detect the number of processors: setting "
                   "concurrency level to 1.");
            default_concurrency = 1;
        }
        op->concurrency_level = default_concurrency;
    }

    if (op->expert) {
        int val = op->concurrency_level;
        do {
           char *strval = nvasprintf("%d", val);
           val = atoi(ui_get_input(op, strval, "Concurrency level"));
           nvfree(strval);
        } while (val < 1);
        op->concurrency_level = val;
    }
}

/*
 * get_pkg_config_variable() - call pkg-config to query the value of the given
 *                             variable.
 *
 * Invokes `pkg-config --variable <VARIABLE> <PKG>` and returns a malloced
 * string containing the returned value of the variable, if any.  NULL is
 * returned if the variable could not be found or some error occurred.
 */
char *
get_pkg_config_variable(Options *op,
                        const char *pkg, const char *variable)
{
    char *prefix = NULL;
    int ret;

    if (!op->utils[PKG_CONFIG]) {
        return NULL;
    }

    ret = run_command(op, &prefix, FALSE, NULL, TRUE,
                      op->utils[PKG_CONFIG],
                      " --variable=", variable, " ", pkg,
                      NULL);

    if (ret != 0 ||
        /*
         * Rather than returning an error if a package exists but doen't have a
         * particular variable, pkg-config will return success and write a blank
         * line to stdout.
         *
         * If the path is empty, return NULL to fall back to the defaults.
         */
        (prefix && prefix[0] == '\0')) {
        nvfree(prefix);
        prefix = NULL;
    }

    return prefix;
}

/*
 * check_systemd() - check if systemd is available.
 *
 * If op->use_systemd is NV_OPTIONAL_BOOL_DEFAULT, this sets it to _TRUE or
 * _FALSE depending on whether systemctl is available.
 *
 * Also assigns op->systemd_unit_prefix, op->systemd_sleep_prefix, and
 * op->systemd_sysconf_prefix if pkg-config is available.
 *
 * Returns TRUE on success, FALSE otherwise.
 */
int check_systemd(Options *op)
{
    /*
     * If the user specified --no-systemd, skip everything else.
     */
    if (op->use_systemd == NV_OPTIONAL_BOOL_FALSE) {
        return TRUE;
    }

    if (op->utils[SYSTEMCTL] == NULL) {
        if (op->use_systemd == NV_OPTIONAL_BOOL_TRUE) {
            ui_error(op, "Option '--systemd' was specified but systemctl was "
                     "not found on this system");
            return FALSE;
        }

        op->use_systemd = NV_OPTIONAL_BOOL_FALSE;
        return TRUE;
    }

    op->use_systemd = NV_OPTIONAL_BOOL_TRUE;

    /*
     * Determine the path for unit files and systemd-sleep scripts if pkg-config
     * and systemd.pc are available.
     */
    if (op->systemd_unit_prefix == NULL) {
        op->systemd_unit_prefix =
            get_pkg_config_variable(op, "systemd", "systemdsystemunitdir");
    }

    if (op->systemd_sleep_prefix == NULL) {
        op->systemd_sleep_prefix =
            get_pkg_config_variable(op, "systemd", "systemdsleepdir");
    }

    if (op->systemd_sysconf_prefix == NULL) {
        op->systemd_sysconf_prefix =
            get_pkg_config_variable(op, "systemd", "systemdsystemconfdir");
    }

    return TRUE;
}


/*
 * Run `$cmd $help` to print the help text for "cmd", and examine it for the
 * presence of "option". Returns TRUE if "option" was found.
 */
int option_is_supported(Options *op, const char *cmd, const char *help,
                        const char *option)
{
    int option_found = FALSE, option_len = strlen(option);
    char *helptext, *match;

    /* Ignore the return value: some programs lack a dedicated "help" option
     * and will simply print a help message and return failure when an invalid
     * command line option is specified. */
    run_command(op, &helptext, FALSE, 0, TRUE,
                cmd, " ", help, NULL);

    for (match = helptext; match && *match; match = strstr(match + 1, option)) {
        int before_clear;

        /*
         * Look for "option": we want to make sure it is surrounded on both
         * sides by whitespace, brackets, etc., or the beginning or end of the
         * help text, to ignore substring matches.
         */
        if (match == helptext) {
            if (strncmp(match, option, option_len)) continue;

            before_clear = TRUE;
        } else {
            const char *before = match - 1;

            if (isspace(*before)) {
                before_clear = TRUE;
            } else {
                switch (*before) {
                case '[': case '<': case '|': case '-':
                    before_clear = TRUE; break;
                default:;
                }
            }
        }

        if (before_clear) {
            const char *after = match + option_len;
            int after_clear;

            if (isspace(*after)) {
                after_clear = TRUE;
            } else {
                switch (*after) {
                case '\0': case ']': case '>': case '|':
                    after_clear = TRUE; break;
                default:;
                }
            }

            if (after_clear) {
                option_found = TRUE;
                break;
            }
        }
    }

    nvfree(helptext);
    return option_found;
}


/* Attempt to dlopen(3) a DSO to detect its availability. */
static int detect_library(const char *library)
{
    void *handle = dlopen(library, RTLD_NOW);

    if (handle) {
        dlclose(handle);
        return TRUE;
    }

    return FALSE;
}


/* Test if the system has a Vulkan loader; warn if none is detected */
void check_for_vulkan_loader(Options *op)
{
    if (!op->vulkan_icd_json_packaged) {
        /*
         * Don't check for a Vulkan loader if the package does not include
         * the Vulkan ICD.
         */
        return;
    }

    if (!detect_library("libvulkan.so.1")) {
        ui_warn(op, "This NVIDIA driver package includes Vulkan components, "
                "but no Vulkan ICD loader was detected on this system. "
                "The NVIDIA Vulkan ICD will not function without the loader. "
                "Most distributions package the Vulkan loader; try installing "
                "the \"vulkan-loader\", \"vulkan-icd-loader\", or "
                "\"libvulkan1\" package.");
    }
}


void add_bullet_list_item(const char *new, char **orig)
{
    char *tmp = *orig;

    *orig = nvstrcat(*orig, "  * ", new, "\n", NULL);
    nvfree(tmp);
}


/*
 * suggest_reboot() - Give the user a suggestion to reboot the computer if
 * the conditions during installation call for one.
 */

void suggest_reboot(Options *op)
{
    char *reason = nvstrdup("");

    if (op->loaded_kernel_module_detected) {
        add_bullet_list_item("Existing NVIDIA kernel modules were loaded "
                             "during installation, and are likely still "
                             "loaded.", &reason);
    }

    if (op->running_x_server_detected) {
        add_bullet_list_item("A running X server was detected during "
                             "installation.", &reason);
    }

    if (nouveau_is_present()) {
        add_bullet_list_item("Nouveau is running: any attempt to disable it "
                             "will not take effect until after a reboot.",
                             &reason);
    }

    if (reason[0]) {
        ui_warn(op, "It is strongly recommended that you reboot your computer "
                    "after exiting the installer, due to the following "
                    "condition(s) which the installer detected: \n\n%s\n"
                    "If you continue to use the computer without rebooting, "
                    "you may not be able to start new programs which use the "
                    "NVIDIA GPU(s) until after you reboot or reload the NVIDIA "
                    "kernel modules.", reason);
    }

    nvfree(reason);
}
