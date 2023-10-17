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
#include "user-interface.h"
#include "ui-status-indeterminate.h"

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

extern const char ncurses_ui_array[];
extern const int ncurses_ui_array_size;
#if defined(NV_INSTALLER_NCURSES6)
extern const char ncurses6_ui_array[];
extern const int ncurses6_ui_array_size;
#endif
#if defined(NV_INSTALLER_NCURSESW6)
extern const char ncursesw6_ui_array[];
extern const int ncursesw6_ui_array_size;
#endif

/* struct describing the ui data */

typedef struct {
    char *name;
    char *descr;
    char *filename;
    const char *data_array;
    const int data_array_size;
} user_interface_attribute_t;


static int extract_user_interface(Options *op, user_interface_attribute_t *ui);
static void ui_signal_handler(int n);

/*
 * Definitions of very common answer choices used by callers of
 * ui_multiple_choice() or ui_paged_prompt()
 */

const char * const CONTINUE_ABORT_CHOICES[] = {
    [CONTINUE_CHOICE] = "Continue installation",
    [ABORT_CHOICE]    = "Abort installation"
};

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
#if defined(NV_INSTALLER_NCURSES6)
        { "ncurses6", "nvidia-installer ncurses v6 user interface", NULL,
          ncurses6_ui_array, ncurses6_ui_array_size },
#endif
        { "ncurses", "nvidia-installer ncurses user interface", NULL,
          ncurses_ui_array, ncurses_ui_array_size },
#if defined(NV_INSTALLER_NCURSESW6)
        { "ncursesw6", "nvidia-installer ncurses v6 user interface (widechar)",
          NULL, ncursesw6_ui_array, ncursesw6_ui_array_size },
#endif
        { "none", NULL, NULL, NULL, 0 }
    };

    /* dlopen() the appropriate ui shared lib */

    __ui = NULL;

    if (!op->silent) {
        if (op->ui.name) {
            for (i = 0; i < ARRAY_LEN(ui_list); i++) {
                if (strcmp(op->ui.name, ui_list[i].name) == 0) {
                    break;
                }
            }

            if (i == ARRAY_LEN(ui_list)) {
                log_printf(op, NULL, "Invalid \"ui\" option: %s", op->ui.name);
                i = 0;
            }
        } else {
            i = 0;
        }

        for (; i < ARRAY_LEN(ui_list) && ui_list[i].descr && !__ui; i++) {

            if (!extract_user_interface(op, &ui_list[i])) continue;

            handle = dlopen(ui_list[i].filename, RTLD_NOW);

            if (handle) {
                __ui = dlsym(handle, "ui_dispatch_table");
                if (__ui && __ui->detect(op)) {
                    log_printf(op, NULL, "Using: %s", ui_list[i].descr);
                    __extracted_user_interface_filename = ui_list[i].filename;
                    break;
                } else {
                    log_printf(op, NULL, "Unable to initialize: %s",
                               ui_list[i].descr);
                    dlclose(handle);
                    __ui = NULL;
                }
            } else {
                log_printf(op, NULL, "Unable to load: %s", ui_list[i].descr);
                log_printf(op, NULL, "");
            }
        }
    }

    /* fall back to the always built-in stream ui */

    if (!__ui) {
        __ui = &stream_ui_dispatch_table;
        log_printf(op, NULL, "Using built-in stream user interface");
    }

    /*
     * init the ui
     *
     * XXX if init() fails, we should try to fall back to the built-in
     * stream ui.
     */

    if (!__ui->init(op, nv_format_text_rows)) return FALSE;

    op->ui.indeterminate_data = indeterminate_init();

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
    
    if (op->silent) return;

    NV_VSNPRINTF(title, fmt);

    __ui->set_title(op, title);
    free(title);

} /* ui_set_title() */



/*
 * ui_get_input() - 
 */

char *ui_get_input(Options *op, const char *def, const char *fmt, ...)
{
    char *msg, *tmp = NULL, *ret;
    
    NV_VSNPRINTF(msg, fmt);

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
    log_printf(op, NV_BULLET_STR, "%s", tmp);
    nvfree(msg);
    nvfree(tmp);

    return ret;

} /* ui_get_input() */



/*
 * ui_error() - have the ui display an error message
 */

void ui_error(Options *op, const char *fmt, ...)
{
    char *msg;
    
    NV_VSNPRINTF(msg, fmt);

    __ui->message(op, NV_MSG_LEVEL_ERROR, msg);
    log_printf(op, "ERROR: ", "%s", msg);
    
    free(msg);

} /* ui_error() */



void ui_warn(Options *op, const char *fmt, ...)
{
    char *msg;

    NV_VSNPRINTF(msg, fmt);

    __ui->message(op, NV_MSG_LEVEL_WARNING, msg);
    log_printf(op, "WARNING: ", "%s", msg);
 
    free(msg);
    
} /* ui_error() */



void ui_message(Options *op, const char *fmt, ...)
{
    char *msg;

    NV_VSNPRINTF(msg, fmt);

    if (!op->silent) __ui->message(op, NV_MSG_LEVEL_MESSAGE, msg);
    
    log_printf(op, NV_BULLET_STR, "%s", msg);

    free(msg);

} /* ui_message() */


void ui_log(Options *op, const char *fmt, ...)
{
    char *msg;

    NV_VSNPRINTF(msg, fmt);

    if (!op->silent) __ui->message(op, NV_MSG_LEVEL_LOG, msg);
    log_printf(op, NV_BULLET_STR, "%s", msg);

    free(msg);

} /* ui_message() */


/*
 * ui_expert() - this is essentially the same as ui_log, but the ui is
 * only called to display the message when we are in expert mode.
 */

void ui_expert(Options *op, const char *fmt, ...)
{
    char *msg;

    if (!op->expert) return;

    NV_VSNPRINTF(msg, fmt);

    if (!op->silent) __ui->message(op, NV_MSG_LEVEL_LOG, msg);
    log_printf(op, NV_BULLET_STR, "%s", msg);

    free (msg);
    
} /* ui_expert() */



void ui_command_output(Options *op, const char *fmt, ...)
{
    char *msg;

    NV_VSNPRINTF(msg, fmt);

    if (!op->silent) __ui->command_output(op, msg);

    log_printf(op, NV_CMD_OUT_PREFIX, "%s", msg);

    free(msg);

} /* ui_command_output() */



/*
 * ui_approve_command_list()
 */

int ui_approve_command_list(Options *op, CommandList *c, const char *fmt, ...)
{
    char *msg;
    int ret;
    
    if (!op->expert || op->no_questions) return TRUE;

    NV_VSNPRINTF(msg, fmt);

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
    
    NV_VSNPRINTF(msg, fmt);
    
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
    
    log_printf(op, NV_BULLET_STR, "%s", tmp);
    nvfree(msg);
    nvfree(tmp);

    return ret;

} /* ui_yes_no() */


int ui_multiple_choice (Options *op, const char * const *answers,
                        int num_answers, int default_answer,
                        const char *fmt, ...)
{
    char *question, *tmp = NULL;
    int ret;

    NV_VSNPRINTF(question, fmt);

    if (op->no_questions) {
        ret = default_answer;
    } else {
        ret = __ui->multiple_choice(op, question, answers, num_answers,
                                    default_answer);
    }

    tmp = nvstrcat(question, " (Answer: ", answers[ret], ")", NULL);

    if (!op->silent) {
        __ui->message(op, NV_MSG_LEVEL_LOG, tmp);
    }

    log_printf(op, NV_BULLET_STR, "%s", tmp);
    nvfree(question);
    nvfree(tmp);

    return ret;

} /* ui_multiple_choice() */


int ui_paged_prompt (Options *op, const char *question, const char *pager_title,
                     const char *pager_text, const char * const *answers,
                     int num_answers, int default_answer)
{
    char *tmp;
    int ret;

    if (op->no_questions) {
        ret = default_answer;
    } else {
        ret = __ui->paged_prompt(op, question, pager_title, pager_text, answers,
                                 num_answers, default_answer);
    }

    tmp = nvstrcat(question, "\n\n", pager_text,
                   "\n(Answer: ", answers[ret], ")", NULL);

    if (!op->silent) {
        __ui->message(op, NV_MSG_LEVEL_LOG, tmp);
    }

    log_printf(op, NV_BULLET_STR, "%s", tmp);
    nvfree(tmp);

    return ret;
}


/*
 * ui_status_begin(): create a new status indicator and displays it immediately
 *
 * title: a title that will be displayed for the life of the status indicator.
 * fmt, ...: a printf(3)-style message which may be replaced by other messages
 *          during the life of the status indicator. This argument is optional
 *          and may be passed NULL.
 */

void ui_status_begin(Options *op, const char *title, const char *fmt, ...)
{
    char *msg;

    log_printf(op, NV_BULLET_STR, "%s", title);

    if (op->silent) return;
 
    NV_VSNPRINTF(msg, fmt);

    op->ui.status_active = TRUE;

    __ui->status_begin(op, title, msg);
    free(msg);
}

/*
 * ui_status_update(): update the position of the status indicator
 *
 * percent: a number from 0.0 to 1.0 indicating the level of completion
 * fmt, ...: replaces any previously displayed status message; note that some
 *           UI implementations (e.g. stream) may only display the initial
 *           message, if any, that was provided with ui_status_begin().
 */

void ui_status_update(Options *op, const float percent, const char *fmt, ...)
{
    char *msg;

    if (op->silent) return;

    NV_VSNPRINTF(msg, fmt);

    __ui->status_update(op, percent, msg);
    free(msg);
}

struct indeterminate_args {
    Options *op;
    char *msg;
};

static void *indeterminate_worker(void *p)
{
    struct indeterminate_args *args = p;
    Options *op = args->op;
    char *msg = nvstrdup(args->msg);
    IndeterminateData *id = op->ui.indeterminate_data;

    while (indeterminate_get(id) == INDETERMINATE_ACTIVE) {
        __ui->update_indeterminate(op, msg);
    }

    nvfree(msg);
    return NULL;
}


/*
 * ui_indeterminate_begin(): display an "indeterminate" status indicator for
 * a task with an unknown completion point. This indicator will be shown until
 * it is ended with ui_indeterminate_end();
 *
 * fmt, ...: same as ui_status_update()
 */

void ui_indeterminate_begin(Options *op, const char *fmt, ...)
{
    IndeterminateData *id = op->ui.indeterminate_data;
    static struct indeterminate_args args;
    char *msg;

    if (!op->silent && fmt != NULL) {
        NV_VSNPRINTF(msg, fmt);
        args.op = op;
        args.msg = msg;

        indeterminate_begin(id, indeterminate_worker, &args);
    }
}

/*
 * ui_indeterminate_end(): terminate an indeterminate status indicator
 */

void ui_indeterminate_end(Options *op)
{
    indeterminate_end(op->ui.indeterminate_data);
}

/*
 * ui_status_end(): finish the progress indicator created with ui_status_begin()
 */
void ui_status_end(Options *op, const char *fmt, ...)
{
    char *msg;

    NV_VSNPRINTF(msg, fmt);

    if (!op->silent) __ui->status_end(op, msg);
    log_printf(op, NV_BULLET_STR, "%s", msg);
    free(msg);

    op->ui.status_active = FALSE;
}



void ui_close (Options *op)
{
    if (__ui) __ui->close(op);
                       
    if (__extracted_user_interface_filename) {
        unlink(__extracted_user_interface_filename);
    }

    __ui = NULL;

    indeterminate_destroy(op->ui.indeterminate_data);
    op->ui.indeterminate_data = NULL;
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
        log_printf(op, NULL, "%s: not present.", ui->descr);
        return FALSE;
    }

    /* create a temporary file */

    ui->filename = nvstrcat(op->tmpdir, "/nv-XXXXXX", NULL);
    
    fd = mkstemp(ui->filename);
    if (fd == -1) {
        log_printf(op, NULL, "unable to create temporary file (%s)",
                   strerror(errno));
        goto failed;
    }
    
    /* set the temporary file's size */

    if (lseek(fd, ui->data_array_size - 1, SEEK_SET) == -1) {
        log_printf(op, NULL, "Unable to set file size for '%s' (%s)",
                   ui->filename, strerror(errno));
        goto failed;
    }
    if (write(fd, "", 1) != 1) {
        log_printf(op, NULL, "Unable to write file size for '%s' (%s)",
                   ui->filename, strerror(errno));
        goto failed;
    }
    
    /* mmap the temporary file */

    if ((dst = mmap(0, ui->data_array_size, PROT_READ | PROT_WRITE,
                    MAP_FILE | MAP_SHARED, fd, 0)) == (void *) -1) {
        log_printf(op, NULL, "Unable to map destination file '%s' "
                   "for copying (%s)", ui->filename, strerror(errno));
        goto failed;
    }

    /* copy the data out to the file */

    memcpy(dst, ui->data_array, ui->data_array_size);

    /* unmap the temporary file */

    if (munmap(dst, ui->data_array_size) == -1) {
        log_printf(op, NULL, "Unable to unmap destination file '%s' "
                   "(%s)", ui->filename, strerror(errno));
        goto failed;
    }

    /* close the file */

    close(fd);
    
    return TRUE;

 failed:

    if (dst != (void *) -1) munmap(dst, ui->data_array_size);
    if (fd != -1) { close(fd); unlink(ui->filename); }
    free(ui->filename);

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
    
    ui_close(NULL); /* 
                     * XXX don't have an Options struct to
                     * pass to ui_close()
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
