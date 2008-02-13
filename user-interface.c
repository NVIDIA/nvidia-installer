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
 * user_interface.c - this source file contains an abstraction to the
 * nvidia-installer user interface.
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include "nvidia-installer.h"
#include "nvidia-installer-ui.h"
#include "misc.h"
#include "files.h"


/*
 * global user interface pointer
 */

InstallerUI *__ui = NULL;

/*
 * filename of extracted user interface
 */

char *__extracted_user_interface_filename = NULL;

/* pull in the default stream_ui dispatch table from stream_ui.c */

extern InstallerUI stream_ui_dispatch_table;

/* pull in the user interface data arrays and sizes */

extern const unsigned char ncurses_ui_array[];
extern const int ncurses_ui_array_size;

/* struct describing the ui data */

typedef struct {
    char *descr;
    char *filename;
    const char *data_array;
    const int data_array_size;
} user_interface_attribute_t;


static int extract_user_interface(Options *op, user_interface_attribute_t *ui);
static void ui_signal_handler(int n);

/*
 * ui_init() - initialize the user interface; we start by looping over
 * each of the possible ui shared libs (gtk, ncurses) until we find
 * one that will work; if neither will work, then fall back to the
 * built-in stream ui.  Once we have chosen our ui, call it's init()
 * function and return TRUE.
 */

int ui_init(Options *op)
{
    void *handle;
    int i;
    user_interface_attribute_t ui_list[] = {
        /* { "nvidia-installer GTK+ user interface", NULL, NULL, 0 }, */
        { "nvidia-installer ncurses user interface", NULL,
          ncurses_ui_array, ncurses_ui_array_size },
        { NULL, NULL, NULL, 0 }
    };
    
    /* dlopen() the appropriate ui shared lib */
    
    __ui = NULL;
    
    i = 0;

    if (((op->ui_str) && (strcmp(op->ui_str, "none") == 0)) || (op->silent)) {
        i = 1;
    }
    
    for (; ui_list[i].descr && !__ui; i++) {

        if (!extract_user_interface(op, &ui_list[i])) continue;
        
        handle = dlopen(ui_list[i].filename, RTLD_NOW);

        if (handle) {
            __ui = dlsym(handle, "ui_dispatch_table");
            if (__ui && __ui->detect(op)) {
                log_printf(op, TRUE, NULL, "Using: %s",
                           ui_list[i].descr);
                __extracted_user_interface_filename = ui_list[i].filename;
                break;
            } else {
                log_printf(op, TRUE, NULL, "Unable to initialize: %s",
                           ui_list[i].descr);
                dlclose(handle);
                __ui = NULL;
            }
        } else {
            log_printf(op, TRUE, NULL, "Unable to load: %s",
                       ui_list[i].descr);
            log_printf(op, TRUE, NULL, "");
        }
    }
    
    /* fall back to the always built-in stream ui */

    if (!__ui) {
        __ui = &stream_ui_dispatch_table;
        log_printf(op, TRUE, NULL, "Using built-in stream user interface");
    }
    
    /*
     * init the ui
     *
     * XXX if init() fails, we should try to fall back to the build-in
     * stream ui.
     */
    
    if (!__ui->init(op, nv_format_text_rows)) return FALSE;
    
    /* handle some common signals */

    signal(SIGHUP,  ui_signal_handler);
    signal(SIGALRM, ui_signal_handler);
    signal(SIGABRT, ui_signal_handler);
    signal(SIGSEGV, ui_signal_handler);
    signal(SIGTERM, ui_signal_handler);
    signal(SIGINT,  ui_signal_handler);
    signal(SIGILL,  ui_signal_handler);
    signal(SIGBUS,  ui_signal_handler);

    /* so far, so good */

    return TRUE;

} /* init_ui () */



/*
 * ui_set_title() - 
 */

void ui_set_title(Options *op, const char *fmt, ...)
{
    char *title;
    va_list ap;
    
    if (op->silent) return;

    va_start(ap, fmt);
    title = assemble_string(fmt, ap);
    va_end(ap);

    __ui->set_title(op, title);
    free(title);

} /* ui_set_title() */



/*
 * ui_get_input() - 
 */

char *ui_get_input(Options *op, const char *def, const char *fmt, ...)
{
    char *msg, *tmp = NULL, *ret;
    va_list ap;
    
    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    if (op->no_questions) {
        ret = nvstrdup(def ? def : "");
        tmp = nvstrcat(msg, " (Answer: '", ret, "')", NULL);
        if (!op->silent) {
            __ui->message(op, NV_MSG_LEVEL_LOG, tmp);
        }
    } else {
        ret = __ui->get_input(op, def, msg);
        tmp = nvstrcat(msg, " (Answer: '", ret, "')", NULL);
    }
    log_printf(op, TRUE, NV_BULLET_STR, tmp);
    nvfree(msg);
    nvfree(tmp);

    return ret;

} /* ui_get_input() */



/*
 * ui_display_license()
 */

int ui_display_license (Options *op, const char *license)
{
    if (op->silent) return TRUE;

    return __ui->display_license(op, license);
    
} /* ui_display_license() */



/*
 * ui_error() - have the ui display an error message
 */

void ui_error(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;
    
    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    __ui->message(op, NV_MSG_LEVEL_ERROR, msg);
    log_printf(op, TRUE, "ERROR: ", msg);
    
    free(msg);

} /* ui_error() */



void ui_warn(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    __ui->message(op, NV_MSG_LEVEL_WARNING, msg);
    log_printf(op, TRUE, "WARNING: ", msg);
 
    free(msg);
    
} /* ui_error() */



void ui_message(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    if (!op->silent) __ui->message(op, NV_MSG_LEVEL_MESSAGE, msg);
    
    log_printf(op, TRUE, NV_BULLET_STR, msg);

    free(msg);

} /* ui_message() */


void ui_log(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    if (!op->silent) __ui->message(op, NV_MSG_LEVEL_LOG, msg);
    log_printf(op, TRUE, NV_BULLET_STR, msg);

    free(msg);

} /* ui_message() */


/*
 * ui_expert() - this is essentially the same as ui_log, but the ui is
 * only called to display the message when we are in expert mode.
 */

void ui_expert(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    if (!op->expert) return;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    if (!op->silent) __ui->message(op, NV_MSG_LEVEL_LOG, msg);
    log_printf(op, FALSE, NV_BULLET_STR, msg);

    free (msg);
    
} /* ui_expert() */



void ui_command_output(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    if (!op->silent) __ui->command_output(op, msg);

    log_printf(op, FALSE, NV_CMD_OUT_PREFIX, msg);

    free(msg);

} /* ui_command_output() */



/*
 * ui_approve_command_list()
 */

int ui_approve_command_list(Options *op, CommandList *c, const char *fmt, ...)
{
    char *msg;
    int ret;
    va_list ap;
    
    if (!op->expert || op->no_questions) return TRUE;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    ret = __ui->approve_command_list(op, c, msg);
    free(msg);

    if (ret) __ui->message(op, NV_MSG_LEVEL_LOG, "Commandlist approved.");
    else __ui->message(op, NV_MSG_LEVEL_LOG, "Commandlist rejected.");

    return ret;

} /* ui_approve_command_list() */


/*
 * ui_yes_no()
 */

int ui_yes_no (Options *op, const int def, const char *fmt, ...)
{
    char *msg, *tmp = NULL;
    int ret;
    va_list ap;
    
    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);
    
    if (op->no_questions) {
        ret = def;
        tmp = nvstrcat(msg, " (Answer: ", (ret ? "Yes" : "No"), ")", NULL);
        if (!op->silent) {
            __ui->message(op, NV_MSG_LEVEL_LOG, tmp);
        }
    } else {
        ret = __ui->yes_no(op, def, msg);
        tmp = nvstrcat(msg, " (Answer: ", (ret ? "Yes" : "No"), ")", NULL);
    }
    
    log_printf(op, FALSE, NV_BULLET_STR, tmp);
    nvfree(msg);
    nvfree(tmp);

    return ret;

} /* ui_yes_no() */



void ui_status_begin(Options *op, const char *title, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    log_printf(op, TRUE, NV_BULLET_STR, title);

    if (op->silent) return;
 
    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    __ui->status_begin(op, title, msg);
    free(msg);
}



void ui_status_update(Options *op, const float percent, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    if (op->silent) return;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    __ui->status_update(op, percent, msg);
    free(msg);
}



void ui_status_end(Options *op, const char *fmt, ...)
{
    char *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = assemble_string(fmt, ap);
    va_end(ap);

    if (!op->silent) __ui->status_end(op, msg);
    log_printf(op, TRUE, NV_BULLET_STR, msg);
    free(msg);
}



void ui_close (Options *op)
{
    __ui->close(op);

    if (__extracted_user_interface_filename) {
        unlink(__extracted_user_interface_filename);
    }

    __ui = NULL;

} /* ui_close() */



/*
 * extract_user_interface() - we want the user interfaces to be shared
 * libraries, separate from the main installer binary, to protect the
 * main installer from any broken library dependencies that the user
 * interfaces may introduce.  However, we don't want to have to
 * install the user interface shared libraries on the user's system
 * (and risk not finding them -- or worse: finding the wrong ones --
 * when the installer gets run).
 *
 * The solution to this is to build the ui shared libraries as usual,
 * but to include them INSIDE the installer binary as static data, so
 * that the installer is contained within one single file.
 *
 * The user_interface_attribute_t struct contains everything that is
 * necessary to extract the user interface files, dumping each to a
 * temporary file so that it can be dlopen()ed.
 */

static int extract_user_interface(Options *op, user_interface_attribute_t *ui)
{
    unsigned char *dst = (void *) -1;
    int fd = -1;

    /* check that this ui is present in the binary */

    if ((ui->data_array == NULL) || (ui->data_array_size == 0)) {
        log_printf(op, TRUE, NULL, "%s: not present.", ui->descr);
        return FALSE;
    }

    /* create a temporary file */

    ui->filename = nvstrcat(op->tmpdir, "/nv-XXXXXX", NULL);
    
    fd = mkstemp(ui->filename);
    if (fd == -1) {
        log_printf(op, TRUE, NULL, "unable to create temporary file (%s)",
                   strerror(errno));
        goto failed;
    }
    
    /* set the temporary file's size */

    if (lseek(fd, ui->data_array_size - 1, SEEK_SET) == -1) {
        log_printf(op, TRUE, NULL, "Unable to set file size for '%s' (%s)",
                   ui->filename, strerror(errno));
        goto failed;
    }
    if (write(fd, "", 1) != 1) {
        log_printf(op, TRUE, NULL, "Unable to write file size for '%s' (%s)",
                   ui->filename, strerror(errno));
        goto failed;
    }
    
    /* mmap the temporary file */

    if ((dst = mmap(0, ui->data_array_size, PROT_READ | PROT_WRITE,
                    MAP_FILE | MAP_SHARED, fd, 0)) == (void *) -1) {
        log_printf(op, TRUE, NULL, "Unable to map destination file '%s' "
                   "for copying (%s)", ui->filename, strerror(errno));
        goto failed;
    }

    /* copy the data out to the file */

    memcpy(dst, ui->data_array, ui->data_array_size);

    /* unmap the temporary file */

    if (munmap(dst, ui->data_array_size) == -1) {
        log_printf(op, TRUE, NULL, "Unable to unmap destination file '%s' "
                   "(%s)", ui->filename, strerror(errno));
        goto failed;
    }

    /* close the file */

    close(fd);
    
    return TRUE;

 failed:

    if (dst != (void *) -1) munmap(dst, ui->data_array_size);
    if (fd != -1) { close(fd); unlink(ui->filename); }
    if (ui->filename) free(ui->filename);

    return FALSE;

} /* extract_user_interface() */


/*
 * ui_signal_handler() - if a signal goes off that is going to
 * terminate the process, call the ui to close cleanly; then print a
 * message to stderr.
 */

static void ui_signal_handler(int n)
{
    const char *sig_names[] = {
        "UNKNOWN",   /* 0 */
        "SIGHUP",    /* 1 */
        "SIGINT",    /* 2 */
        "SIGQUIT",   /* 3 */
        "SIGILL",    /* 4 */
        "SIGTRAP",   /* 5 */
        "SIGABRT",   /* 6 */
        "SIGBUS",    /* 7 */
        "SIGFPE",    /* 8 */
        "SIGKILL",   /* 9 */
        "SIGUSR1",   /* 10 */
        "SIGSEGV",   /* 11 */
        "SIGUSR2",   /* 12 */
        "SIGPIPE",   /* 13 */
        "SIGALRM",   /* 14 */
        "SIGTERM",   /* 15 */
        "SIGSTKFLT", /* 16 */
        "SIGCHLD",   /* 17 */
        "SIGCONT",   /* 18 */
        "SIGSTOP",   /* 19 */
        "SIGTSTP",   /* 20 */
        "SIGTTIN",   /* 21 */
        "SIGTTOU",   /* 22 */
        "SIGURG",    /* 23 */
        "SIGXCPU",   /* 24 */
        "SIGXFSZ",   /* 25 */
        "SIGVTALRM", /* 26 */
        "SIGPROF",   /* 27 */
        "SIGWINCH",  /* 28 */
        "SIGIO",     /* 29 */
        "SIGPWR",    /* 30 */
        "SIGSYS",    /* 31 */
        "SIGUNUSED", /* 31 */
    };
    
    const char *s;

    if (__ui) __ui->close(NULL); /* 
                                  * XXX don't have an Options struct to
                                  * pass to close()
                                  */
    /*
     * print to stderr with write(2) rather than fprintf(3), since
     * fprintf isn't guaranteed to be reentrant
     */
    
    s = (n < 32) ? sig_names[n] : "UNKNOWN";
    
    write(2, "Received signal ", 16);
    write(2, s, strlen(s));
    write(2, "; aborting.\n", 12);
    
    exit(128 + n);

} /* ui_signal_handler() */
