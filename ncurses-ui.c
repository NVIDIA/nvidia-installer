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
 * ncurses_ui.c - implementation of the nvidia-installer ui using ncurses.
 */

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "nvidia-installer.h"
#include "nvidia-installer-ui.h"




/* structures */

/*
 * RegionStruct - a region is sort of like an ncurses window, but
 * implemented completely in this source file (ie: doesn't use ncurses
 * windows).  I found enough bugs with ncurses windows that I felt it
 * better to avoid their use altogether.
 */

typedef struct {
    int x, y, w, h; /* position and dimensions relative to stdscr */
    int attr;       /* attributes (to be passed to setattr()) */
    char *line;     /* a string filled with spaces; its length is the
                       width of the region.  This is just a
                       convenience for use when clearing the region */
} RegionStruct;




/*
 * PagerStruct - Pager implements the functionality of `less`
 */

typedef struct {
    TextRows *t;          /* rows of text to be displayed */
    RegionStruct *region; /* region in which the text will be displayed */
    const char *label;    /* label for the left side of the footer */
    int cur;              /* current position in the pager */
    int page;             /* height of a page (for use with pgup/pgdn) */
} PagerStruct;




/*
 * DataStruct - private data structure that gets plugged into
 * Options->ui_priv.
 */

typedef struct {

    RegionStruct *header;  /* header region */
    RegionStruct *footer;  /* footer region */
    RegionStruct *message; /* message region */

    FormatTextRows format_text_rows; /* XXX function pointer from installer */

    bool use_color;        /* should the ncurses ui use color? */

    int width;             /* cached copy of the terminal dimensions */
    int height;

    char *title;           /* cached strings for the header and footer */
    char *footer_left;
    char *footer_right;
    
    char *progress_title;  /* cached string for the title of the
                              progress messages */
    
} DataStruct;




/* constants */

/* default strings for the header and footer */

#define NV_NCURSES_DEFAULT_TITLE "NVIDIA Software Installer for Unix/Linux"
#define NV_NCURSES_DEFAULT_FOOTER_LEFT NV_NCURSES_DEFAULT_TITLE
#define NV_NCURSES_DEFAULT_FOOTER_RIGHT "www.nvidia.com"

/* indices for color pairs */

#define NV_NCURSES_HEADER_COLOR_IDX      1
#define NV_NCURSES_MESSAGE_COLOR_IDX     2
#define NV_NCURSES_BUTTON_COLOR_IDX      3
#define NV_NCURSES_INPUT_COLOR_IDX       4


#define NV_NCURSES_HEADER_COLOR (COLOR_PAIR(NV_NCURSES_HEADER_COLOR_IDX))
#define NV_NCURSES_FOOTER_COLOR A_REVERSE
#define NV_NCURSES_MESSAGE_COLOR (COLOR_PAIR(NV_NCURSES_MESSAGE_COLOR_IDX))
#define NV_NCURSES_BUTTON_COLOR (COLOR_PAIR(NV_NCURSES_BUTTON_COLOR_IDX))
#define NV_NCURSES_INPUT_COLOR (COLOR_PAIR(NV_NCURSES_INPUT_COLOR_IDX))

#define NV_NCURSES_HEADER_NO_COLOR A_REVERSE
#define NV_NCURSES_FOOTER_NO_COLOR A_REVERSE
#define NV_NCURSES_MESSAGE_NO_COLOR A_NORMAL

/* use when animating button presses */

#define NV_NCURSES_BUTTON_PRESS_TIME 125000

#define NV_NCURSES_TAB 9
#define NV_NCURSES_ENTER 10
#define NV_NCURSES_BACKSPACE 8

#define NV_NCURSES_CTRL(x) ((x) & 0x1f)


/*
 * somewhat arbitrary minimum values: if the current window size is
 * smaller than this, don't attempt to use the ncurses ui.
 */

#define NV_NCURSES_MIN_WIDTH 40
#define NV_NCURSES_MIN_HEIGHT 10

#define NV_NCURSES_HLINE    '_'



/* prototypes for ui entry points */

static int   nv_ncurses_detect              (Options*);
static int   nv_ncurses_init                (Options*,
                                             FormatTextRows format_text_rows);
static void  nv_ncurses_set_title           (Options*, const char*);
static char *nv_ncurses_get_input           (Options*, const char*,
                                             const char*);
static int   nv_ncurses_display_license     (Options*, const char*);
static void  nv_ncurses_message             (Options*, const int level,
                                             const char*);
static void  nv_ncurses_command_output      (Options*, const char*);
static int   nv_ncurses_approve_command_list(Options*, CommandList*,
                                             const char*);
static int   nv_ncurses_yes_no              (Options*, const int, const char*);
static void  nv_ncurses_status_begin        (Options*, const char*,
                                             const char*);
static void  nv_ncurses_status_update       (Options*, const float,
                                             const char*);
static void  nv_ncurses_status_end          (Options*, const char*);
static void  nv_ncurses_close               (Options*);



/* helper functions for manipulating the header and footer */

static void nv_ncurses_set_header(DataStruct *, const char *);
static void nv_ncurses_set_footer(DataStruct *, const char *, const char *);


/* helper functions for manipulating RegionStructs */

static RegionStruct *nv_ncurses_create_region(DataStruct *, int, int,
                                              int, int, int, int);
static void nv_ncurses_clear_region(RegionStruct *);
static void nv_ncurses_destroy_region(RegionStruct *);


/* helper functions for drawing buttons */

static void nv_ncurses_draw_button(DataStruct *, RegionStruct *, int, int,
                                   int, int, const char *, bool, bool);
static void nv_ncurses_erase_button(RegionStruct *, int, int, int, int);


/* helper functions for drawing regions and stuff */

static void nv_ncurses_do_message_region(DataStruct *, const char *,
                                         const char *, int, int);
static void nv_ncurses_do_progress_bar_region(DataStruct *);
static void nv_ncurses_do_progress_bar_message(DataStruct *, const char *,
                                               int, int);

/* pager functions */

static PagerStruct *nv_ncurses_create_pager(DataStruct *, int, int, int, int,
                                            TextRows *, const char *, int);
static void nv_ncurses_pager_update(DataStruct *, PagerStruct *);
static void nv_ncurses_pager_handle_events(DataStruct *, PagerStruct *, int);
static void nv_ncurses_destroy_pager(PagerStruct *);
static int nv_ncurses_paged_prompt(Options *, const char*, const char*,
                                   const char*, const char **, int, int);


/* progress bar helper functions */

static int choose_char(int i, int p[4], char v[4], char def);
static void init_percentage_string(char v[4], int n);
static void init_position(int p[4], int w);


/* misc helper functions */

static char *nv_ncurses_create_command_list_text(DataStruct *, CommandList *);
static char *nv_ncurses_mode_to_permission_string(mode_t);

static void nv_ncurses_free_text_rows(TextRows *);

static int nv_ncurses_format_print(DataStruct *, RegionStruct *,
                                   int, int, int, int, TextRows *t);

static int nv_ncurses_check_resize(DataStruct *, bool);







/* dispatch table that gets dlsym()'ed by ui_init() */

InstallerUI ui_dispatch_table = {
    nv_ncurses_detect,
    nv_ncurses_init,
    nv_ncurses_set_title,
    nv_ncurses_get_input,
    nv_ncurses_display_license,
    nv_ncurses_message,
    nv_ncurses_command_output,
    nv_ncurses_approve_command_list,
    nv_ncurses_yes_no,
    nv_ncurses_paged_prompt,
    nv_ncurses_status_begin,
    nv_ncurses_status_update,
    nv_ncurses_status_end,
    nv_ncurses_close
};




/*
 * nv_ncurses_detect() - initialize ncurses; return FALSE if
 * initialization fails
 */

static int nv_ncurses_detect(Options *op)
{
    int x, y;

    if (!initscr()) return FALSE;
    
    /*
     * query the current size of the window, and don't try to use the
     * ncurses ui if it's too small.
     */

    getmaxyx(stdscr, y, x);

    if ((x < NV_NCURSES_MIN_WIDTH) || (y < NV_NCURSES_MIN_HEIGHT)) {
        endwin();
        return FALSE;
    }

    return TRUE;

} /* nv_ncurses_detect() */




/*
 * nv_ncurses_init() - initialize the ncurses interface.
 */

static int nv_ncurses_init(Options *op, FormatTextRows format_text_rows)
{
    DataStruct *d;
    
    d = (DataStruct *) malloc(sizeof(DataStruct));
    memset(d, 0, sizeof(DataStruct));
    
    d->format_text_rows = format_text_rows;
    
    /* initialize color */
    
    d->use_color = !op->no_ncurses_color;

    if (d->use_color) {
        if (!has_colors()) {
            d->use_color = FALSE;
        }
    }
    
    if (d->use_color) {
        if (start_color() == ERR) {
            d->use_color = FALSE;
        } else {
            /*                                      foreground    background */
            init_pair(NV_NCURSES_HEADER_COLOR_IDX,  COLOR_BLACK,  COLOR_GREEN);
            init_pair(NV_NCURSES_MESSAGE_COLOR_IDX, COLOR_WHITE,  COLOR_BLUE);
            init_pair(NV_NCURSES_BUTTON_COLOR_IDX,  COLOR_WHITE,  COLOR_RED);
            init_pair(NV_NCURSES_INPUT_COLOR_IDX,   COLOR_GREEN,  COLOR_BLACK);
        }
    }
    
    clear();              /* clear the screen */
    noecho();             /* don't echo input to the screen */
    cbreak();             /* disable line buffering and control characters */
    curs_set(0);          /* make the cursor invisible */
    keypad(stdscr, TRUE); /* enable keypad, function keys, arrow keys, etc */
    
    getmaxyx(stdscr, d->height, d->width); /* get current window dimensions */
    
    /* create the regions */
    
    d->header = nv_ncurses_create_region(d, 1, 0, d->width - 2, 1,
                                         NV_NCURSES_HEADER_COLOR,
                                         NV_NCURSES_HEADER_NO_COLOR);

    d->footer = nv_ncurses_create_region(d, 1, d->height - 2, d->width - 2, 1,
                                         NV_NCURSES_FOOTER_COLOR,
                                         NV_NCURSES_FOOTER_NO_COLOR);
    
    /* plug the DataStruct struct into the ui_priv pointer */
    
    op->ui_priv = (void *) d;
    
    /* set the initial strings in the header and footer */
    
    nv_ncurses_set_header(d, NV_NCURSES_DEFAULT_TITLE);
    
    nv_ncurses_set_footer(d, NV_NCURSES_DEFAULT_FOOTER_LEFT,
                          NV_NCURSES_DEFAULT_FOOTER_RIGHT);
    
    refresh();
    
    return TRUE;
    
} /* nv_ncurses_init() */




/*
 * nv_ncurses_set_title() - update the string in the header region
 */

static void nv_ncurses_set_title(Options *op, const char *title)
{
    DataStruct *d = (DataStruct *) op->ui_priv;

    nv_ncurses_set_header(d, title);

    refresh();

} /* nv_ncurses_set_title() */




/*
 * nv_ncurses_get_input - prompt for user input with the given msg;
 * returns the user inputed string.
 */

#define MIN_INPUT_LEN 32
#define MAX_BUF_LEN 1024
#define BUF_CHAR(c) ((c) ? (c) : ' ')

static char *nv_ncurses_get_input(Options *op,
                                  const char *def, const char *msg)
{
    DataStruct *d = (DataStruct *) op->ui_priv;
    int msg_len, width, input_len, buf_len, def_len;
    int input_x, input_y, i, w, h, x, y, c, lines, ch, color, redraw;
    char *tmp, buf[MAX_BUF_LEN];
    TextRows *t;

    if (!msg) return NULL;

    nv_ncurses_check_resize(d, FALSE);

    color = d->use_color ? NV_NCURSES_INPUT_COLOR : A_REVERSE;

    /* concatenate ": " to the end of the message */

    msg_len = strlen(msg) + 2;
    tmp = (char *) malloc(msg_len + 1);
    snprintf(tmp, msg_len, "%s: ", msg);

    /* copy the default response into the buffer */

    memset(buf, 0, MAX_BUF_LEN);
    if (def) strncpy(buf, def, MAX_BUF_LEN);
    
 draw_get_input:
    
    /* free any existing message region */

    if (d->message) {
        nv_ncurses_destroy_region(d->message);
        d->message = NULL;
    }

    /*
     * compute the size of the input box: the input box is twice the
     * length of the default string, clamped to MIN_INPUT_LEN
     */

    def_len = def ? strlen(def) : 0;
    input_len = NV_MAX(def_len * 2, MIN_INPUT_LEN);
    width = d->width - 4;

    /* convert the message to text rows */

    t = d->format_text_rows(NULL, tmp, width, TRUE);
    
    /*
     * if the message and input box will fit on one line, do that;
     * otherwise, place them on separate lines
     */

    if ((msg_len + input_len + 1) < width) {
        input_x = 1 + msg_len;
        lines = 1;
    } else {
        input_x = 2;
        lines = t->n + 1;
    }
    
    input_y = lines;

    /*
     * compute the width, height, and starting position of the message
     * region
     */

    w = d->width - 2;
    h = lines + 2;
    x = 1;
    y = ((d->height - (3 + h)) / 3) + 1;

    /* create the message region */

    d->message = nv_ncurses_create_region(d, x, y, w, h,
                                          NV_NCURSES_MESSAGE_COLOR,
                                          NV_NCURSES_MESSAGE_NO_COLOR);

    nv_ncurses_format_print(d, d->message, 1, 1, d->message->w - 2, lines, t);

    /* free the text rows */

    nv_ncurses_free_text_rows(t);

    /* clamp the input box to the width of the region */
    
    input_len = NV_MIN(input_len, width - 2);

    curs_set(1); /* make the cursor visible */

    c = buf_len = strlen(buf);
    
    redraw = TRUE; /* force a redraw the first time through */

    /* offset input_x and input_y by the region offset */

    input_x += d->message->x;
    input_y += d->message->y;

    do {
        x = NV_MAX(c - (input_len - 1), 0);

        /* redraw the input box */
        
        if (redraw) {
            for (i = 0; i < input_len; i++) {
                mvaddch(input_y, input_x + i, BUF_CHAR(buf[i + x]) | color);
            }
        
            /* if we're scrolling, display an arrow */

            if (x > 0) {
                mvaddch(input_y, input_x - 1, '<' | d->message->attr);
            } else {
                mvaddch(input_y, input_x - 1, ' ' | d->message->attr);
            }
        
            if (buf_len > (input_len - 1 + x)) {
                mvaddch(input_y, input_x + input_len, '>' | d->message->attr);
            } else {
                mvaddch(input_y, input_x + input_len, ' ' | d->message->attr);
            }
            
            redraw = FALSE;
        }

        /* position the cursor */

        move(input_y, input_x - x + c);
        refresh();
        
        /* wait for input */

        if (nv_ncurses_check_resize(d, FALSE)) goto draw_get_input;
        ch = getch();

        switch (ch) {
            
        case NV_NCURSES_BACKSPACE:
        case KEY_BACKSPACE:

            /*
             * If there is a character to be deleted, move everything
             * after the character forward, and decrement c and len.
             */
            
            if (c <= 0) break;
            for (i = c; i <= buf_len; i++) buf[i-1] = buf[i];
            c--;
            buf_len--;
            redraw = TRUE;
            break;
            
        case KEY_DC:

            /*
             * If there is a character to be deleted, move everything
             * after the character forward, and decrement len.
             */

            if (c == buf_len) break;
            for (i = c; i < buf_len; i++) buf[i] = buf[i+1];
            buf_len--;
            redraw = TRUE;
            break;

        case KEY_LEFT:
            if (c > 0) {
                c--;
                redraw = TRUE;
            }
            break;

        case KEY_RIGHT:
            if (c < buf_len) {
                c++;
                redraw = TRUE;
            }
            break;

        case NV_NCURSES_CTRL('L'):
            nv_ncurses_check_resize(d, TRUE);
            goto draw_get_input;
            break;
        }
        
        /*
         * If we have a printable character, then move everything
         * after the current location back, and insert the character.
         */

        if (isprint(ch)) {
            if (buf_len < (MAX_BUF_LEN - 1)) {
                for (i = buf_len; i > c; i--) buf[i] = buf[i-1];
                buf[c] = (char) ch;
                buf_len++;
                c++;
                redraw = TRUE;
            }
        }

        if (op->debug) {
            mvprintw(d->message->y, d->message->x,
                     "c: %3d  ch: %04o (%d)", c, ch, ch);
            clrtoeol();
            redraw = TRUE;
        }
        
    } while (ch != NV_NCURSES_ENTER);

    /* free the message region */
    
    nv_ncurses_destroy_region(d->message);
    d->message = NULL;
    
    curs_set(0); /* make the cursor invisible */
    refresh();
    
    free(tmp);
    tmp = strdup(buf);
    return tmp;

} /* nv_ncurses_get_input() */




/*
 * nv_ncurses_display_license() - print the text from the license file,
 * prompt for acceptance from the user, and return whether or not the
 * user accepted.
 */

static int nv_ncurses_display_license(Options *op, const char *license)
{
    static const char *descr = "Please read the following LICENSE "
        "and then select either \"Accept\" to accept the license "
        "and continue with the installation, or select "
        "\"Do Not Accept\" to abort the installation.";

    static const char *buttons[2] = {"Accept", "Do Not Accept"};

    int ret = nv_ncurses_paged_prompt(op, descr, "NVIDIA Software License",
                                      license, buttons, 2, 1);

    return ret == 0;
} /* nv_ncurses_display_license() */




/*
 * nv_ncurses_message() - print a message
 */

static void nv_ncurses_message(Options *op, int level, const char *msg)
{
    DataStruct *d = (DataStruct *) op->ui_priv;
    int w, h, x, y, ch;
    char *prefix;
   
    if (!msg) return;

    /* XXX for now, log messages are ignored by the ncurses ui */

    if (level == NV_MSG_LEVEL_LOG) return;
    
    /* determine the prefix for the message from the message level */

    switch(level) {
    case NV_MSG_LEVEL_MESSAGE:
        prefix = NULL;
        break;
    case NV_MSG_LEVEL_WARNING:
        prefix = "WARNING: ";
        break;
    case NV_MSG_LEVEL_ERROR:
        prefix = "ERROR: ";
        break;
    default:
        return;
    }

 draw_message:

    if (d->message) {
        
        /*
         * XXX we may already have a message region allocated when we
         * enter nv_ncurses_message(): we may have been in the middle
         * of displaying a progress bar when we encountered an error
         * that we need to report.  To deal with this situation, throw
         * out the existing message region; nv_ncurses_status_update()
         * and nv_ncurses_status_end() will have to recreate their
         * message region.
         */
        
        nv_ncurses_destroy_region(d->message);
        d->message = NULL;
    }

    /* create the message region and print msg in it */

    nv_ncurses_do_message_region(d, prefix, msg, FALSE, 2);
    
    /* init the dimensions of the button */

    w = 6;
    h = 1;
    x = (d->message->w - w) / 2;
    y = d->message->h - 2;

    /* draw the OK button */

    nv_ncurses_draw_button(d, d->message, x, y, w, h, "OK",
                           TRUE, FALSE);
    refresh();
    
    /* wait for enter */

    do {
        /* if a resize occurred, jump back to the top and redraw */
        
        if (nv_ncurses_check_resize(d, FALSE)) goto draw_message;
        ch = getch();

        switch (ch) {
        case NV_NCURSES_CTRL('L'):
            nv_ncurses_check_resize(d, TRUE);
            goto draw_message;
            break;
        }
    } while (ch != NV_NCURSES_ENTER);
    
    /* animate the button being pushed down */

    nv_ncurses_erase_button(d->message, x, y, w, h);
    nv_ncurses_draw_button(d, d->message, x, y, w, h, "OK", TRUE, TRUE);
    refresh();
    usleep(NV_NCURSES_BUTTON_PRESS_TIME);

    nv_ncurses_erase_button(d->message, x, y, w, h);
    nv_ncurses_draw_button(d, d->message, x, y, w, h, "OK", TRUE, FALSE);
    refresh();
    usleep(NV_NCURSES_BUTTON_PRESS_TIME);

    /* free the message region */

    nv_ncurses_destroy_region(d->message);
    d->message = NULL;
    refresh();

} /* nv_ncurses_message() */




/* 
 * nv_ncurses_command_output() - drop this on the floor, for now.
 */

static void nv_ncurses_command_output(Options *op, const char *msg)
{
    /* 
     * XXX we don't currently display command output in the ncurses ui
     */

    return;

} /* nv_ncurses_command_output() */




/*
 * nv_ncurses_approve_command_list() - list all the commands that will be
 * executed, and ask for approval.
 */

static int nv_ncurses_approve_command_list(Options *op, CommandList *cl,
                                           const char *descr)
{
    DataStruct *d = (DataStruct *) op->ui_priv;
    char *commandlist, *question;
    int ret, len;
    const char *buttons[2] = {"Yes", "No"};

    /* initialize the question string */

    len = strlen(descr) + 256;
    question = (char *) malloc(len + 1);
    snprintf(question, len, "The following operations will be performed to "
             "install the %s.  Is this acceptable?", descr);
    
    commandlist = nv_ncurses_create_command_list_text(d, cl);

    ret = nv_ncurses_paged_prompt(op, question, "Proposed CommandList",
                                  commandlist, buttons, 2, 0);

    free(question);
    free(commandlist);

    return ret == 0;
} /* nv_ncurses_approve_command_list() */




/*
 * nv_ncurses_yes_no() - ask the yes/no question 'msg' and return TRUE for
 * yes, and FALSE for no.
 */

static int nv_ncurses_yes_no(Options *op, const int def, const char *msg)
{
    DataStruct *d = (DataStruct *) op->ui_priv;
    int yes_x, no_x, x, y, w, h, yes, ch;
    char *str;
   
    if (!msg) return FALSE;

    /* check if the window was resized */

    nv_ncurses_check_resize(d, FALSE);
    yes = def;

 draw_yes_no:

    if (d->message) {
        
        /*
         * XXX we may already have a message region allocated when we
         * enter nv_ncurses_yes_no(): we may have been in the middle
         * of displaying a progress bar when we encountered an error
         * that we need to report.  To deal with this situation, throw
         * out the existing message region; nv_ncurses_status_update()
         * and nv_ncurses_status_end() will have to recreate their
         * message region.
         */
        
        nv_ncurses_destroy_region(d->message);
        d->message = NULL;
    }

    /* create the message region and print the message in it */

    nv_ncurses_do_message_region(d, NULL, msg, FALSE, 2);

    /* draw the yes/no buttons */
    
    w = 7;
    h = 1;
    y = d->message->h - 2;
    yes_x = (d->message->w - w*3)/2 + 0;
    no_x  = (d->message->w - w*3)/2 + 2*w;
    
    nv_ncurses_draw_button(d, d->message, yes_x, y, w, h, "Yes", yes,FALSE);
    nv_ncurses_draw_button(d, d->message, no_x, y, w, h, "No", !yes, FALSE);
    
    refresh();

    /* process key strokes */

    do {
        if (nv_ncurses_check_resize(d, FALSE)) goto draw_yes_no;
        ch = getch();

        switch (ch) {
        case NV_NCURSES_TAB:
        case KEY_LEFT:
        case KEY_RIGHT:
            
            /*
             * any of left, right, and tab will toggle which button is
             * selected
             */

            yes ^= 1;

            nv_ncurses_draw_button(d, d->message, yes_x, y, w, h,
                                   "Yes", yes, FALSE);
            nv_ncurses_draw_button(d, d->message, no_x, y, w, h,
                                   "No", !yes, FALSE);
            refresh();
            break;

        case NV_NCURSES_CTRL('L'):
            nv_ncurses_check_resize(d, TRUE);
            goto draw_yes_no;
            break;

        default:
            break;
        }
    } while (ch != NV_NCURSES_ENTER);
    
    /* animate the button being pushed down */
    
    x = yes ? yes_x : no_x;
    str = yes ? "Yes" : "No";
    
    nv_ncurses_erase_button(d->message, x, y, w, h);
    nv_ncurses_draw_button(d, d->message, x, y, w, 1, str, TRUE, TRUE);
    refresh();
    usleep(NV_NCURSES_BUTTON_PRESS_TIME);
    
    nv_ncurses_erase_button(d->message, x, y, w, h);
    nv_ncurses_draw_button(d, d->message, x, y, w, h, str, TRUE, FALSE);
    refresh();
    usleep(NV_NCURSES_BUTTON_PRESS_TIME);
    
    /* free the message region */

    nv_ncurses_destroy_region(d->message);
    d->message = NULL;
    
    refresh();

    return yes;
   
} /* nv_ncurses_yes_no() */




/*
 * nv_ncurses_status_begin() - initialize a progress bar
 */

static void nv_ncurses_status_begin(Options *op,
                                    const char *title, const char *msg)
{
    DataStruct *d = (DataStruct *) op->ui_priv;
   
    nv_ncurses_check_resize(d, FALSE);

    /* cache the progress bar title */

    d->progress_title = strdup(title);

    /* create the message region for use by the progress bar */

    nv_ncurses_do_progress_bar_region(d);

    /* write the one line msg (truncating it, if need be) */
    
    nv_ncurses_do_progress_bar_message(d, msg, d->message->h - 3,
                                       d->message->w);

    refresh();

} /* nv_ncurses_status_begin() */




/*
 * nv_ncurses_status_update() - update the progress bar
 */

static void nv_ncurses_status_update(Options *op, const float percent,
                                     const char *msg)
{
    int i, n, h, ch;
    int p[4];
    char v[4];
    DataStruct *d = (DataStruct *) op->ui_priv;

    /* 
     * if the message region was deleted or if the window was resized,
     * redraw the entire progress bar region.
     */

    if (nv_ncurses_check_resize(d, FALSE) || !d->message) {
        if (d->message) nv_ncurses_destroy_region(d->message);
        nv_ncurses_do_progress_bar_region(d);
    }

    /* temporarily set getch() to non-blocking mode */

    nodelay(stdscr, TRUE);

    while ((ch = getch()) != ERR) {
        /*
         * if the user explicitely requested that the screen be
         * redrawn by pressing CTRL-L, then also redraw the entire
         * progress bar egion.
         */
        if (ch == NV_NCURSES_CTRL('L')) {
            nv_ncurses_check_resize(d, TRUE);
            if (d->message) nv_ncurses_destroy_region(d->message);
            nv_ncurses_do_progress_bar_region(d);
        }
    }

    /* set getch() back to blocking mode */

    nodelay(stdscr, FALSE);

    /* compute the percentage */

    n = ((int) (percent * (float) (d->message->w - 2)));
    n = NV_MAX(n, 2);
    
    init_position(p, d->message->w);
    init_percentage_string(v, (int) (100.0 * percent));
    
    h = d->message->h;
    
    /* write the one line msg (truncating it, if need be) */

    nv_ncurses_do_progress_bar_message(d, msg, h - 3, d->message->w - 2);

    /* draw the progress bar */

    if (d->use_color) {
        for (i = 1; i <= n; i++) {
            mvaddch(d->message->y + h - 2, d->message->x + i,
                    choose_char(i, p, v,' ') |
                    A_REVERSE | NV_NCURSES_INPUT_COLOR);
        }

        for (i = 0; i < 4; i++) {
            if (p[i] >= (n+1)) {
                mvaddch(d->message->y + h - 2, d->message->x + p[i],
                        (v[i] ? v[i] : ' ') | NV_NCURSES_INPUT_COLOR);
            }
        }
    } else {
        for (i = 2; i < n; i++) {
            mvaddch(d->message->y + h - 2, d->message->x + i,
                    choose_char(i, p, v, ' ') | A_REVERSE);
        }
        
        for (i = 0; i < 4; i++) {
            if (p[i] >= n) {
                mvaddch(d->message->y + h - 2, d->message->x + p[i],
                        v[i] ? v[i] : '-');
            }
        }
    }

    refresh();
    
} /* nv_ncurses_status_update() */




/*
 * nv_ncurses_status_end() - draw the progress bar at 100%
 */

static void nv_ncurses_status_end(Options *op, const char *msg)
{
    int i, n, h;
    int p[4];
    char v[4];
    DataStruct *d = (DataStruct *) op->ui_priv;  

    /* 
     * if the message region was deleted or if the window was resized,
     * redraw the entire progress bar region.
     */

    if (nv_ncurses_check_resize(d, FALSE) || !d->message) {
        if (d->message) nv_ncurses_destroy_region(d->message);
        nv_ncurses_do_progress_bar_region(d);
    }
    
    n = d->message->w - 2;
    
    init_position(p, d->message->w);
    init_percentage_string(v, 100.0);
    
    h = d->message->h;
    
    /* write the one line msg (truncating it, if need be) */

    nv_ncurses_do_progress_bar_message(d, msg, h - 3, d->message->w - 2);

    /* draw the complete progress bar */

    if (d->use_color) {
        for (i = 1; i < (n+1); i++) {
            mvaddch(d->message->y + h - 2, d->message->x + i,
                    choose_char(i, p, v, ' ') |
                    A_REVERSE | NV_NCURSES_INPUT_COLOR);
        }
    } else {
        for (i = 2; i < (n); i++) {
            mvaddch(d->message->y + h - 2, d->message->x + i,
                    choose_char(i, p, v, ' ') | A_REVERSE);
        }
    }
    
    refresh();

    free(d->progress_title);
    d->progress_title = NULL;
    
    /* XXX don't free the message window, yet... */

} /* nv_ncurses_status_end() */




/*
 * nv_ncurses_close() - close the ui: free any memory we allocated,
 * and end curses mode.
 */

static void nv_ncurses_close(Options *op)
{
    DataStruct *d = NULL;

    if (op) {

        /* XXX op may be NULL if we get called from a signal handler */

        d = (DataStruct *) op->ui_priv;  
        nv_ncurses_destroy_region(d->header);
        nv_ncurses_destroy_region(d->footer);
        free(d);
    }
    
    clear();
    refresh();
    
    endwin();  /* End curses mode */
    
    return;
    
} /* nv_ncurses_close() */



/****************************************************************************/
/*
 * internal helper functions for manipulating the header and footer
 */




/*
 * nv_ncurses_set_header() - write the title to the header region;
 * note that this function does not call refresh()
 */

static void nv_ncurses_set_header(DataStruct *d, const char *title)
{
    int x, y;
    char *tmp;

    tmp = strdup(title);

    if (d->title) free(d->title);
    d->title = tmp;
    
    x = (d->header->w - strlen(d->title)) / 2;
    y = 0;

    attrset(d->header->attr);
    
    nv_ncurses_clear_region(d->header);
    mvaddstr(d->header->y + y, d->header->x + x, (char *) d->title);
    attrset(A_NORMAL);
    
} /* nv_ncurses_set_header() */




/*
 * nv_ncurses_set_footer() - write the left and right text to the
 * footer; note that this function does not call refresh()
 */

static void nv_ncurses_set_footer(DataStruct *d, const char *left,
                                  const char *right)
{
    int x, y;
    char *tmp0, *tmp1;

    tmp0 = strdup(left);
    tmp1 = strdup(right);

    if (d->footer_left) free(d->footer_left);
    if (d->footer_right) free(d->footer_right);
    
    d->footer_left = tmp0;
    d->footer_right = tmp1;

    attrset(d->footer->attr);
    
    nv_ncurses_clear_region(d->footer);
    
    if (d->footer_left) {
        y = 0;
        x = 1;
        mvaddstr(d->footer->y + y, d->footer->x + x, d->footer_left);
    }
    if (d->footer_right) {
        y = 0;
        x = d->footer->w - strlen(d->footer_right) - 1;
        mvaddstr(d->footer->y + y, d->footer->x + x, d->footer_right);
    }

    attrset(A_NORMAL);
    
} /* nv_ncurses_set_footer() */




/****************************************************************************/
/*
 * internal helper functions for manipulating RegionStructs
 */




/*
 * nv_ncurses_create_region() - create a new region at the specified
 * location with the specified dimensions; note that this function
 * does not call refresh()
 */

static RegionStruct *nv_ncurses_create_region(DataStruct *d,
                                              int x, int y, int w, int h,
                                              int color,
                                              int no_color_attr)
{
    RegionStruct *region =
        (RegionStruct *) malloc(sizeof(RegionStruct));
    
    region->x = x;
    region->y = y;
    region->w = w;
    region->h = h;

    if (d->use_color) region->attr = color;
    else              region->attr = no_color_attr;
    
    /* create a single line for use in clearing the region */

    region->line = (char *) malloc(w + 1);
    memset(region->line, ' ', w);
    region->line[w] = '\0';

    /* clear the region */

    attrset(region->attr);
    nv_ncurses_clear_region(region);
    attrset(A_NORMAL);
    
    return region;

} /* nv_ncurses_create_region() */




/*
 * nv_ncurses_clear_region() - clear each line in the region; note
 * that this function does not call refresh(), nor does it explicitly
 * set any attributes.
 */

static void nv_ncurses_clear_region(RegionStruct *region)
{
    int i;

    for (i = region->y; i < (region->y + region->h); i++) {
        mvaddstr(i, region->x, region->line);
    }
} /* nv_ncurses_clear_region() */




/*
 * nv_ncurses_destroy_region() - clear and free the RegionStruct; note
 * that this function does not call refresh()
 */

static void nv_ncurses_destroy_region(RegionStruct *region)
{
    if (!region) return;

    attrset(A_NORMAL);
    nv_ncurses_clear_region(region);
    free(region->line);
    free(region);
    
} /* nv_ncurses_destroy_region() */
    

                                  

/****************************************************************************/
/*
 * internal helper functions for drawing buttons
 */




/*
 * nv_ncurses_draw_button() - draw a button on the specified region,
 * at location x,y with dimensions w,h.  Give the button the label
 * str.
 *
 * The hilite parameter, when TRUE, causes the label to be printed in
 * reverse colors.
 *
 * The down parameter, when TRUE, causes the button to be drawn as if
 * it had been pressed down (ie: shifted down and right).
 */

static void nv_ncurses_draw_button(DataStruct *d, RegionStruct *region,
                                   int x, int y, int w, int h,
                                   const char *str, bool hilite, bool down)
{
    int i, j, n, attr = 0;
    
    if (down) x++, y++;
    
    n = strlen(str);
    n = (n > w) ? 0 : ((w - n) / 2);

    if (d->use_color) {
        attr = NV_NCURSES_BUTTON_COLOR;
    } else if (hilite) {
        attr = A_REVERSE;
    } else {
        attr = 0;
    }
        
    for (j = y; j < (y + h); j++) {
        for (i = x; i < (x + w); i++) {
            mvaddch(region->y + j, region->x + i, ' ' | attr);
        }
    }
    
    if (hilite) attr |= A_REVERSE;
    
    attron(attr);
    mvaddstr(region->y + y + h/2, region->x + x + n, str);
    attroff(attr);

} /* nv_ncurses_draw_button() */




/*
 * nv_ncurses_erase_button() - erase the button on the specified
 * region at location x,y with dimensions w,h.
 */

static void nv_ncurses_erase_button(RegionStruct *region,
                                    int x, int y, int w, int h)
{
    int i, j;
    
    for (j = y; j <= (y + h); j++) {
        for (i = x; i <= (x + w); i++) {
            mvaddch(region->y + j, region->x + i, ' ' | region->attr);
        }
    }
    
} /* nv_ncurses_erase_button() */




/*****************************************************************************/
/*
 * nv_ncurses_do_message_region(),
 * nv_ncurses_do_progress_bar_region(),
 * nv_ncurses_do_progress_bar_message() - helper functions for drawing
 * regions and stuff.
 */

/*
 * nv_ncurses_do_message_region() - create a new message region
 * containing the string msg.  The "top" argument indicates whether
 * the region should be vertically positioned immediately below the
 * header, or should be positioned 1/3 of the way down the screen.
 * The num_extra_lines argument is used to request extra lines in the
 * message region below the string (to leave room for buttons, for
 * example).
 */

static void nv_ncurses_do_message_region(DataStruct *d, const char *prefix,
                                         const char *msg, int top,
                                         int num_extra_lines)
{
    int w, h, x, y;
    TextRows *t;

    /*
     * compute the width and height that we need (taking into account
     * num_extra_lines that the caller may need for buttons
     */

    w = d->width - 2;
    t = d->format_text_rows(prefix, msg, w - 2, TRUE);
    h = t->n + num_extra_lines + 2;
    
    /*
     * compute the starting position of the message region: either
     * immediately below the header or 1/3 of the way down the screen.
     */

    x = 1;
    if (top) y = 2;
    else     y = ((d->height - (3 + h)) / 3) + 1;
    
    /* create the message region */
    
    d->message = nv_ncurses_create_region(d, x, y, w, h,
                                          NV_NCURSES_MESSAGE_COLOR,
                                          NV_NCURSES_MESSAGE_NO_COLOR);
    
    nv_ncurses_format_print(d, d->message, 1, 1, d->message->w - 2,
                            d->message->h - (1 + num_extra_lines), t);

    /* free the text rows */

    nv_ncurses_free_text_rows(t);
    
} /* nv_ncurses_do_message_region() */




/*
 * nv_ncurses_do_progress_bar_region() - create a message region, draw
 * the progress bar's title, separator, and the empty progress bar.
 */

static void nv_ncurses_do_progress_bar_region(DataStruct *d)
{
    int n, h, i, p[4];
    char v[4];

    /* create the message region and print the title in it */
    
    nv_ncurses_do_message_region(d, NULL, d->progress_title, FALSE, 3);

    n = d->message->w - 2;
    h = d->message->h;
    
    attrset(d->message->attr);

    /* draw the horizontal separator */

    for (i = 1; i <= n; i++) {
        mvaddch(d->message->y + h - 4, d->message->x + i,
                NV_NCURSES_HLINE | d->message->attr);
    }

    /* draw an empty progress bar */

    init_position(p, n + 2);
    init_percentage_string(v, 0);
    v[2] = '0';

    if (d->use_color) {
        for (i = 1; i <= n; i++) {
            mvaddch(d->message->y + h - 2, d->message->x + i,
                    choose_char(i, p, v, ' ') | NV_NCURSES_INPUT_COLOR);
        }
    } else {
        mvaddch(d->message->y + h - 2, d->message->x + 1, '[');
        for (i = 2; i < n; i++)
            mvaddch(d->message->y + h - 2, d->message->x + i,
                    choose_char(i, p, v, '-'));
        mvaddch(d->message->y + h - 2, d->message->x + n, ']');
    }

} /* nv_ncurses_do_progress_bar_region() */



/*
 * nv_ncurses_do_progress_bar_message() - write the one line progress
 * bar message to the message region, truncating the string, if need
 * be.
 */

static void nv_ncurses_do_progress_bar_message(DataStruct *d, const char *str,
                                               int y, int w)
{
    char *tmp;

    /* clear the message line */

    attrset(d->message->attr);
    mvaddstr(d->message->y + y, d->message->x, d->message->line);

    /* write the message string */
    
    if (str) {
        tmp = malloc(w + 1);
        strncpy(tmp, str, w);
        tmp[w] = '\0';
        mvaddstr(d->message->y + y, d->message->x + 1, tmp);
        free(tmp);
    }
    
} /* nv_ncurses_do_progress_bar_message() */




/***************************************************************************/
/*
 * pager functions
 */


/*
 * pager functions -- these functions provide the basic behavior of a
 * text viewer... used to display TextRows.
 *
 *  d      : DataStruct struct
 *  x      : starting x coordinate of the pager
 *  y      : starting y coordinate of the pager
 *  w      : width of the pager
 *  h      : height of the pager
 *  t      : TextRows to be displayed
 *  label  : string to be displayed in the status bar 
 *  cur    : initial current line of the pager
 */

static PagerStruct *nv_ncurses_create_pager(DataStruct *d,
                                            int x, int y, int w, int h,
                                            TextRows *t, const char *label,
                                            int cur)
{
    PagerStruct *p = (PagerStruct *) malloc(sizeof(PagerStruct));
    
    p->t = t;
    p->region = nv_ncurses_create_region(d, x, y, w, h, A_NORMAL, A_NORMAL);
    p->label = label;
    
    p->cur = cur;
    
    p->page = h - 2;

    nv_ncurses_pager_update(d, p);

    return p;

} /* nv_ncurses_create_pager() */




/*
 * nv_ncurses_destroy_pager() - free resources associated with the
 * pager
 */

static void nv_ncurses_destroy_pager(PagerStruct *p)
{
    nv_ncurses_destroy_region(p->region);
    free(p);
    
} /* nv_ncurses_destroy_pager () */



/*
 * nv_ncurses_pager_update() - redraw the text in the pager, and
 * update the information about the pager in the footer.  Note that
 * this function does not call refresh().
 */

static void nv_ncurses_pager_update(DataStruct *d, PagerStruct *p)
{
    int i, maxy, percent, denom;
    char tmp[10];

    if (!p) return;

    /* determine the maximum y value for the text */

    maxy = (p->cur + (p->region->h - 1));
    if (maxy > p->t->n) maxy = p->t->n;

    /* draw the text */

    attrset(p->region->attr);

    for (i = p->cur; i < maxy; i++) {
        mvaddstr(p->region->y + i - p->cur, p->region->x, p->region->line);
        if (p->t->t[i]) {
            mvaddstr(p->region->y + i - p->cur, p->region->x, p->t->t[i]);
        }
    }

    /* compute the percentage */
    
    denom = p->t->n - (p->region->h - 1);
    if (denom < 1) percent = 100;
    else percent = ((100.0 * (float) (p->cur)) / (float) denom);
    
    /* create the percentage string */
    
    if (p->t->n <= (p->region->h - 1)) snprintf(tmp, 10, "All");
    else if (percent <= 0)          snprintf(tmp, 10, "Top");
    else if (percent >= 100)        snprintf(tmp, 10, "Bot");
    else                            snprintf(tmp, 10, "%3d%%", percent);

    /* update the status in the footer */

    nv_ncurses_set_footer(d, p->label, tmp);
    
} /* nv_ncurses_pager_update() */



/*
 * nv_ncurses_pager_handle_events() - process any keys that affect the
 * pager.
 */

static void nv_ncurses_pager_handle_events(DataStruct *d,
                                           PagerStruct *p, int ch)
{
    int n;
    
    if (!p) return;
    n = p->t->n - (p->region->h - 1);

    switch (ch) {
    case KEY_UP:
        if (p->cur > 0) {
            p->cur--;
            nv_ncurses_pager_update(d, p);
            refresh();
        }
        break;

    case KEY_DOWN:
        if (p->cur < n) {
            p->cur++;
            nv_ncurses_pager_update(d, p);
            refresh();
        }
        break;

    case KEY_PPAGE:
        if (p->cur > 0) {
            p->cur -= p->page;
            if (p->cur < 0) p->cur = 0;
            nv_ncurses_pager_update(d, p);
            refresh();
        }
        break;

    case KEY_NPAGE:
        if (p->cur < n) {
            p->cur += p->page;
            if (p->cur > n) p->cur = n;
            nv_ncurses_pager_update(d, p);
            refresh();
        }
        break;
    }
} /* nv_ncurses_pager_handle_events() */

static void draw_buttons(DataStruct *d, const char **buttons, int num_buttons,
                         int button, int button_w, const int *buttons_x,
                         int button_y)
{
    int i;

    for (i = 0; i < num_buttons; i++) {
        nv_ncurses_draw_button(d, d->message, buttons_x[i], button_y,
                               button_w, 1, buttons[i], button == i, FALSE);
    }
}

/*
 * nv_ncurses_paged_prompt() - display a question with multiple-choice buttons
 * along with a scrollable paged text area, allowing the user to review the
 * pageable text and select a button corresponding to the desired response.
 * Returns the index of the button selected from the passed-in buttons array.
 */

static int nv_ncurses_paged_prompt(Options *op, const char *question,
                                   const char *pager_title,
                                   const char *pager_text, const char **buttons,
                                   int num_buttons, int default_button)
{
    DataStruct *d = (DataStruct *) op->ui_priv;
    TextRows *t_pager = NULL;
    int ch, cur = 0;
    int i, button_w = 0, button_y, button = default_button;
    int buttons_x[num_buttons];
    PagerStruct *p = NULL;

    /* check if the window was resized */

    nv_ncurses_check_resize(d, FALSE);

    for (i = 0; i < num_buttons; i++) {
        int len = strlen(buttons[i]);
        if (len > button_w) {
            button_w = len;
        }
    }
    button_w += 4;

  print_message:

    /* free any existing message region, pager, and pager textrows */

    if (d->message) {
        nv_ncurses_destroy_region(d->message);
        d->message = NULL;
    }

    if (p) {
        nv_ncurses_destroy_pager(p);
        p = NULL;
    }
    if (t_pager) {
        nv_ncurses_free_text_rows(t_pager);
        t_pager = NULL;
    }

    /* create the message region and print the question in it */

    nv_ncurses_do_message_region(d, NULL, question, TRUE, 2);

    button_y = d->message->h - 2;

    for (i = 0; i < num_buttons; i++) {
        buttons_x[i] = (i + 1) * (d->message->w / (num_buttons + 1)) -
                       button_w / 2;
    }

    draw_buttons(d, buttons, num_buttons, button, button_w, buttons_x,
                 button_y);

    /* draw the paged text */

    t_pager = d->format_text_rows(NULL, pager_text, d->message->w, TRUE);
    p = nv_ncurses_create_pager(d, 1, d->message->h + 2, d->message->w,
                                d->height - d->message->h - 4, t_pager,
                                pager_title, cur);
    refresh();

    /* process key strokes */

    do {
        /* if a resize occurred, jump back to the top and redraw */
            if (nv_ncurses_check_resize(d, FALSE)) {
                cur = p->cur;
                goto print_message;
        }

        ch = getch();

        switch (ch) {
            case NV_NCURSES_TAB:
            case KEY_RIGHT:
                button = (button + 1) % num_buttons;
                draw_buttons(d, buttons, num_buttons, button, button_w,
                             buttons_x, button_y);
                refresh();
                break;

            case KEY_LEFT:
                button = (button + num_buttons - 1) % num_buttons;
                draw_buttons(d, buttons, num_buttons, button, button_w,
                             buttons_x, button_y);
                refresh();
                break;

            case NV_NCURSES_CTRL('L'):
                nv_ncurses_check_resize(d, TRUE);
                cur = p->cur;
                goto print_message;
                break;

            default:
                break;
        }

        nv_ncurses_pager_handle_events(d, p, ch);
    } while (ch != NV_NCURSES_ENTER);

    /* animate the button being pushed down */

    nv_ncurses_erase_button(d->message, buttons_x[button], button_y,
                            button_w, 1);
    nv_ncurses_draw_button(d, d->message, buttons_x[button], button_y,
                           button_w, 1, buttons[button], TRUE, TRUE);
    refresh();
    usleep(NV_NCURSES_BUTTON_PRESS_TIME);

    nv_ncurses_erase_button(d->message, buttons_x[button], button_y,
                            button_w, 1);
    nv_ncurses_draw_button(d, d->message, buttons_x[button], button_y,
                           button_w, 1, buttons[button], TRUE, FALSE);
    refresh();
    usleep(NV_NCURSES_BUTTON_PRESS_TIME);

    /* restore the footer */

    nv_ncurses_set_footer(d, NV_NCURSES_DEFAULT_FOOTER_LEFT,
                          NV_NCURSES_DEFAULT_FOOTER_RIGHT);

    /* clean up */

    nv_ncurses_free_text_rows(t_pager);
    nv_ncurses_destroy_pager(p);
    nv_ncurses_destroy_region(d->message);
    d->message = NULL;

    refresh();

    return button;
}





/*****************************************************************************/
/*
 * helper functions for the progress bar
 */

static int choose_char(int i, int p[4], char v[4], char def)
{
    if (p[0] == i) return v[0] ? v[0] : def;
    if (p[1] == i) return v[1] ? v[1] : def;
    if (p[2] == i) return v[2] ? v[2] : def;
    if (p[3] == i) return v[3] ? v[3] : def;
    return def;
}

static void init_percentage_string(char v[4], int n)
{
    int j;

    n = NV_MAX(n, 1);

    v[0] = (n/100);
    v[1] = (n/10) - (v[0]*10);
    v[2] = (n - (v[0]*100) - (v[1]*10));
    v[0] += '0';
    v[1] += '0';
    v[2] += '0';
    v[3] = '%';

    for (j = 0; j < 3; j++) {
        if (v[j] == '0') v[j] = 0;
        else break;
    }
}

static void init_position(int p[4], int w)
{
    p[0] = 0 + (w - 4)/2;
    p[1] = 1 + (w - 4)/2;
    p[2] = 2 + (w - 4)/2;
    p[3] = 3 + (w - 4)/2;
}



/*****************************************************************************/
/*
 * misc helper functions
 */


/*
 * nv_ncurses_create_command_list_text() - build a string from the command list
 */

static char *nv_ncurses_create_command_list_text(DataStruct *d, CommandList *cl)
{
    int i, len;
    Command *c;
    char *str, *perms, *ret = strdup("");

    for (i = 0; i < cl->num; i++) {
        c = &cl->cmds[i];
        
        str = NULL;

        switch (c->cmd) {
            
        case INSTALL_CMD:
            perms = nv_ncurses_mode_to_permission_string(c->mode);
            len = strlen(c->s0) + strlen(c->s1) + strlen(perms) + 64;
            if (c->s2) {
                len += strlen(c->s2) + 64;
            }
            str = (char *) malloc(len + 1);
            snprintf(str, len, "Install the file '%s' as '%s' with "
                     "permissions '%s'", c->s0, c->s1, perms);
            free(perms);
            if (c->s2) {
                len = strlen(c->s2) + 64;
                snprintf(str + strlen(str), len,
                         " then execute the command `%s`", c->s2);
            }
            break;
            
        case RUN_CMD:
            len = strlen(c->s0) + 64;
            str = (char *) malloc(len + 1);
            snprintf(str, len, "Execute the command `%s`", c->s0);
            break;
            
        case SYMLINK_CMD:
            len = strlen(c->s0) + strlen(c->s1) + 64;
            str = (char *) malloc(len + 1);
            snprintf(str, len, "Create a symbolic link '%s' to '%s'",
                     c->s0, c->s1);
            break;
            
        case BACKUP_CMD:
            len = strlen(c->s0) + 64;
            str = (char *) malloc(len + 1);
            snprintf(str, len, "Backup the file '%s'", c->s0);
            break;

        case DELETE_CMD:
            len = strlen(c->s0) + 64;
            str = (char *) malloc(len + 1);
            snprintf(str, len, "Delete the file '%s'", c->s0);
            break;

        default:
            /* XXX should not get here */
            break;
        }

        if (str) {
            int lenret, lenstr;
            char *tmp;

            lenret = strlen(ret);
            lenstr = strlen(str);
            tmp = malloc(lenret + lenstr + 2 /* "\n\0" */);

            tmp[0] = 0;

            strncat(tmp, ret, lenret);
            strncat(tmp, str, lenstr);

            tmp[lenret + lenstr] = '\n';
            tmp[lenret + lenstr + 1] = 0;

            free(str);
            free(ret);
            ret = tmp;
        }
    }

    return ret;

}


/*
 * mode_to_permission_string() - given a mode bitmask, allocate and
 * write a permission string.
 */

static char *nv_ncurses_mode_to_permission_string(mode_t mode)
{
    char *s = (char *) malloc(10);
    memset (s, '-', 9);
    
    if (mode & (1 << 8)) s[0] = 'r';
    if (mode & (1 << 7)) s[1] = 'w';
    if (mode & (1 << 6)) s[2] = 'x';
    
    if (mode & (1 << 5)) s[3] = 'r';
    if (mode & (1 << 4)) s[4] = 'w';
    if (mode & (1 << 3)) s[5] = 'x';
    
    if (mode & (1 << 2)) s[6] = 'r';
    if (mode & (1 << 1)) s[7] = 'w';
    if (mode & (1 << 0)) s[8] = 'x';
    
    s[9] = '\0';
    return s;

} /* mode_to_permission_string() */




/*
 * nv_free_text_rows() - free the TextRows data structure allocated by
 * nv_format_text_rows()
 */

static void nv_ncurses_free_text_rows(TextRows *t)
{
    int i;
    
    if (!t) return;
    for (i = 0; i < t->n; i++) free(t->t[i]);
    if (t->t) free(t->t);
    free(t);

} /* nv_free_text_rows() */



/*
 * nv_ncurses_formatted_printw() - this function formats the string
 * str on the region region, wrapping to the next line as necessary,
 * using the bounding box specified by x, y, w, and h.
 *
 * The number of lines printed are returned.
 */

static int nv_ncurses_format_print(DataStruct *d, RegionStruct *region,
                                   int x, int y, int w, int h,
                                   TextRows *t)
{
    int i, n;
    
    n = NV_MIN(t->n, h);
    
    attrset(region->attr);

    for (i = 0; i < n; i++) {
        mvaddstr(region->y + y + i, region->x + x, t->t[i]);
    }
    
    attrset(A_NORMAL);
    return n;

} /* nv_ncurses_format_printw() */



/*
 * nv_ncurses_check_resize() - check if the dimensions of the screen
 * have changed; if so, update the old header and footer regions,
 * clear everything, update our cached dimensions, and recreate the
 * header and footer.
 *
 * XXX we could catch the SIGWINCH (window resize) signal, but that's
 * asynchronous... it's safer to only attempt to handle a resize when
 * we know we can.
 */

static int nv_ncurses_check_resize(DataStruct *d, bool force)
{
    int x, y;

    getmaxyx(stdscr, y, x);

    if (!force) {
        if ((x == d->width) && (y == d->height)) {
            /* no resize detected... just return */
            return FALSE;
        }
    }

    /* we have been resized */

    /* destroy the old header and footer */

    nv_ncurses_destroy_region(d->header);
    nv_ncurses_destroy_region(d->footer);

    clear();
    
    /* update our cached copy of the dimensions */

    d->height = y;
    d->width = x;

    /* recreate the header and footer */
    
    d->header = nv_ncurses_create_region(d, 1, 0, d->width - 2, 1,
                                         NV_NCURSES_HEADER_COLOR,
                                         NV_NCURSES_HEADER_NO_COLOR);

    d->footer = nv_ncurses_create_region(d, 1, d->height - 2, d->width - 2, 1,
                                         NV_NCURSES_FOOTER_COLOR,
                                         NV_NCURSES_FOOTER_NO_COLOR);
    
    nv_ncurses_set_header(d, d->title);
    nv_ncurses_set_footer(d, d->footer_left, d->footer_right);
    
    return TRUE;

} /* nv_ncurses_check_resize() */
