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
 * stream_ui.c - implementation of the nvidia-installer ui using printf
 * and friends.  This user interface is compiled into nvidia-installer to
 * ensure that we always have a ui available.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include "nvidia-installer.h"
#include "nvidia-installer-ui.h"
#include "misc.h"
#include "files.h"
#include "format.h"

/* prototypes of each of the stream ui functions */

int   stream_detect              (Options*);
int   stream_init                (Options*, FormatTextRows format_text_rows);
void  stream_set_title           (Options*, const char*);
char *stream_get_input           (Options*, const char*, const char*);
int   stream_display_license     (Options*, const char*);
void  stream_message             (Options*, int level, const char*);
void  stream_command_output      (Options*, const char*);
int   stream_approve_command_list(Options*, CommandList*, const char*);
int   stream_yes_no              (Options*, const int, const char*);
void  stream_status_begin        (Options*, const char*, const char*);
void  stream_status_update       (Options*, const float, const char*);
void  stream_status_end          (Options*, const char*);
void  stream_close               (Options*);

InstallerUI stream_ui_dispatch_table = {
    stream_detect,
    stream_init,
    stream_set_title,
    stream_get_input,
    stream_display_license,
    stream_message,
    stream_command_output,
    stream_approve_command_list,
    stream_yes_no,
    stream_status_begin,
    stream_status_update,
    stream_status_end,
    stream_close
};


typedef struct {
    int status_active;
    char *status_label;
} Data;



#define STATUS_BEGIN 0
#define STATUS_UPDATE 1
#define STATUS_END 2

#define STATUS_BAR_WIDTH 30

/*
 * print_status_bar() -
 */

static void print_status_bar(Data *d, int status, float percent)
{
    int i;
    float val;
    
    if (status != STATUS_BEGIN) printf("\r");
    
    if (d->status_label) {
        printf("  %s: [", d->status_label);
    } else {
        printf("  [");
    }

    val = ((float) STATUS_BAR_WIDTH * percent);

    for (i = 0; i < STATUS_BAR_WIDTH; i++) {
        printf("%c", ((float) i < val) ? '#' : ' ');
    }

    printf("] %3d%%", (int) (percent * 100.0));

    if (status == STATUS_END) printf("\n");
    
    fflush(stdout);

} /* print_status_bar() */



static void sigwinch_handler(int n)
{
    reset_current_terminal_width(0);
}


/*
 * stream_detect - returns TRUE if the user interface is present; the
 * stream_ui is always present, so this always returns TRUE.
 */

int stream_detect(Options *op)
{
    return TRUE;

} /* stream_detect() */



/*
 * stream_init - initialize the ui and print a welcome message.
 */

int stream_init(Options *op, FormatTextRows format_text_rows)
{
    Data *d = nvalloc(sizeof(Data));

    d->status_active = FALSE;
    op->ui_priv = d;

    if (!op->silent) {

        fmtout("");
        fmtout("Welcome to the NVIDIA Software Installer for Unix/Linux");
        fmtout("");
    
        /* register the SIGWINCH signal handler */

        signal(SIGWINCH, sigwinch_handler);
    }

    return TRUE;
    
} /* stream_init() */



/*
 * stream_set_title() - other ui's might use this to update a welcome
 * banner, window title, etc, but it doesn't make sense for this ui.
 */

void stream_set_title(Options *op, const char *title)
{
    return;

} /* stream_set_title() */



/*
 * stream_get_input - prompt for user input with the given msg;
 * returns the user inputed string.
 */

char *stream_get_input(Options *op, const char *def, const char *msg)
{
    char *buf;
    
    format(stdout, NULL, "");
    format(stdout, NULL, msg);
    fprintf(stdout, "  [default: '%s']: ", def);
    fflush(stdout);
    
    buf = fget_next_line(stdin, NULL);
    
    if (!buf || !buf[0]) {
        if (buf) free(buf);
        buf = nvstrdup(def);
    }

    return buf;

} /* stream_get_input() */




/*
 * stream_display_license() - print the text from the license file,
 * prompt for acceptance from the user, and return whether or not the
 * user accepted.
 */

int stream_display_license(Options *op, const char *license)
{
    char *str;
    
    fmtout("");
    fmtout("Please read the following LICENSE and type \""
           "accept\" followed by the Enter key to accept the license, or "
           "type anything else to not accept and exit nvidia-installer.");
    fmtout("");
    
    fmtout("________");
    fmtout("");
    printf("%s", license);
    fmtout("________");
    fmtout("");
    
    printf("Accept? (Type \"Accept\" to accept, or anything else to abort): ");
    fflush(stdout);

    str = fget_next_line(stdin, NULL);
    if (strlen(str) < 6) {
        free(str);
        return FALSE;
    }
    
    str[7] = '\0';
    
    if (strcasecmp(str, "ACCEPT") == 0) {
        free(str);
        return TRUE;
    } else {
        free(str);
        return FALSE;
    }
    
} /* stream_display_license() */



/*
 * stream_message() - print a message
 */

void stream_message(Options *op, const int level, const char *msg)
{
    typedef struct {
        char *prefix;
        FILE *stream;
        int newline;
    } MessageLevelAttributes;

    const MessageLevelAttributes msg_attrs[] = {
        { NULL,        stdout, FALSE }, /* NV_MSG_LEVEL_LOG */
        { NULL,        stdout, TRUE  }, /* NV_MSG_LEVEL_MESSAGE */
        { "WARNING: ", stderr, TRUE  }, /* NV_MSG_LEVEL_WARNING */
        { "ERROR: ",   stderr, TRUE  }  /* NV_MSG_LEVEL_ERROR */
    };
    
    Data *d = op->ui_priv;

    /* don't print log messages if we're currently displaying a status */

    if ((level == NV_MSG_LEVEL_LOG) && (d->status_active)) return;

    if (msg_attrs[level].newline) format(msg_attrs[level].stream, NULL, "");
    format(msg_attrs[level].stream, msg_attrs[level].prefix, msg);
    if (msg_attrs[level].newline) format(msg_attrs[level].stream, NULL, "");

} /* stream_message() */



/* 
 * stream_command_output() - if in expert mode, print output from
 * executing a command.
 *
 * XXX Should this output be formatted?
 */

void stream_command_output(Options *op, const char *msg)
{
    Data *d = op->ui_priv;
    
    if ((!op->expert) || (d->status_active)) return;

    fmtoutp("   ", msg);
    
} /* stream_command_output() */



/*
 * stream_approve_command_list() - list all the commands that will be
 * executed, and ask for approval.
 */

int stream_approve_command_list(Options *op, CommandList *cl,
                                const char *descr)
{
    int i;
    Command *c;
    char *perms;
    const char *prefix = " --> ";

    fmtout("");
    fmtout("The following operations will be performed to install the %s:",
           descr);
    fmtout("");
    
    for (i = 0; i < cl->num; i++) {
        c = &cl->cmds[i];

        switch (c->cmd) {
        
          case INSTALL_CMD:
            perms = mode_to_permission_string(c->mode);
            fmtoutp(prefix, "install the file '%s' as '%s' with "
                    "permissions '%s'", c->s0, c->s1, perms);
            free(perms);
            if (c->s2) {
                fmtoutp(prefix, "execute the command `%s`", c->s2);
            }
            break;
            
          case RUN_CMD:
            fmtoutp(prefix, "execute the command `%s`", c->s0);
            break;
            
          case SYMLINK_CMD:
            fmtoutp(prefix, "create a symbolic link '%s' to '%s'",
                    c->s0, c->s1);
            break;
            
          case BACKUP_CMD:
            fmtoutp(prefix, "back up the file '%s'", c->s0);
            break;

          case DELETE_CMD:
            fmtoutp(prefix, "delete file '%s'", c->s0);
            break;

          default:

            fmterrp("ERROR: ", "Error in CommandList! (cmd: %d; s0: '%s';"
                   "s1: '%s'; s2: '%s'; mode: %04o)",
                   c->cmd, c->s0, c->s1, c->s2, c->mode);
            fmterr("Aborting installation.");
            return FALSE;
            break;
        }
    }
    
    fflush(stdout);
    
    if (!stream_yes_no(op, TRUE, "\nIs this acceptable? (answering 'no' will "
                       "abort installation)")) {
        fmterr("Command list not accepted; exiting installation.");
        return FALSE;
    }

    return TRUE;
    
} /* stream_approve_command_list() */



/*
 * stream_yes_no() - ask the yes/no question 'msg' and return TRUE for
 * yes, and FALSE for no.
 */

int stream_yes_no(Options *op, const int def, const char *msg)
{
    char *buf;
    int eof, ret = def;

    format(stdout, NULL, "");
    format(stdout, NULL, msg);
    if (def) fprintf(stdout, "  [default: (Y)es]: ");
    else fprintf(stdout, "  [default: (N)o]: ");
    fflush(stdout);
    
    buf = fget_next_line(stdin, &eof);
    
    if (!buf) return FALSE;
 
    /* key off the first letter; otherwise, just use the default */

    if (tolower(buf[0]) == 'y') ret = TRUE;
    if (tolower(buf[0]) == 'n') ret = FALSE;
    
    free(buf);
    
    return ret;

} /* stream_yes_no() */



/*
 * stream_status_begin() - we assume that title is always passed, but
 * sometimes msg will be NULL.
 */

void stream_status_begin(Options *op, const char *title, const char *msg)
{
    Data *d = op->ui_priv;
    
    d->status_active = TRUE;
    
    fmtout(title);
    d->status_label = nvstrdup(msg);
    
    print_status_bar(d, STATUS_BEGIN, 0.0);
    
} /* stream_status_begin() */



/*
 * stream_status_update() - 
 */

void stream_status_update(Options *op, const float percent, const char *msg)
{
    print_status_bar(op->ui_priv, STATUS_UPDATE, percent);

} /* stream_status_update() */



/*
 * stream_status_end() - 
 */

void stream_status_end(Options *op, const char *msg)
{
    Data *d = op->ui_priv;
    
    print_status_bar(op->ui_priv, STATUS_END, 1.0);
    
    nvfree(d->status_label);
    d->status_active = FALSE;

} /* stream_status_end() */



/*
 * stream_close() - close the ui
 */

void stream_close(Options *op)
{
    return;
    
} /* stream_close() */
