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
 * nv_installer_ui.h
 */

#ifndef __NVIDIA_INSTALLER_UI_H__
#define __NVIDIA_INSTALLER_UI_H__

#include "nvidia-installer.h"
#include "command-list.h"

#define NV_MSG_LEVEL_LOG     0
#define NV_MSG_LEVEL_MESSAGE 1
#define NV_MSG_LEVEL_WARNING 2
#define NV_MSG_LEVEL_ERROR   3

/* 
 * InstallerrUI - this structure defines the dispatch table that each
 * user interface module must provide.
 */

typedef struct __nv_installer_ui {

    /*
     * detect - returns TRUE if the user interface is present (eg for
     * gtk check that the DISPLAY environment variable is set and can
     * be connected to).
     */
    
    int (*detect)(Options *op);

    /*
     * init - initialize the ui and print a welcome message.
     */

    int (*init)(Options *op, FormatTextRows format_text_rows);
               
    /*
     * set_title - set the title to be displayed by the user interface
     */
    
    void (*set_title)(Options *op, const char *title);

    /*
     * get_input - prompt for user input with the given msg; returns
     * the user inputted string.
     */

    char *(*get_input)(Options *op, const char *def, const char *msg);

    /*
     * display_license - given the text from the license file, display
     * the text to the user and prompt the user for acceptance.
     * Returns whether or not the user accepted the license.
     */

    int (*display_license)(Options *op, const char *license);
    
    /* 
     * message - display a message at the specified message level; the
     * possible message levels are:
     *
     * NV_MSG_LEVEL_LOG: only print the message to the ui's message
     * log
     *
     * NV_MSG_LEVEL_MESSAGE: display the message where users will see
     * it; possibly require them to acknowledge that they've seen it
     * (by requiring them to clikc "OK" to continue, for example).
     * This message may also be printed in the ui's message log.
     *
     * NV_MSG_LEVEL_WARNING: display the message where users will see
     * it; possibly require them to acknowledge that they've seen it
     * (by requiring them to clikc "OK" to continue, for example).
     * This message may also be printed in the ui's message log.  The
     * ui should do something to indicate that this message is a
     * warning.
     *
     * NV_MSG_LEVEL_ERROR: display the message where users will see
     * it; possibly require them to acknowledge that they've seen it
     * (by requiring them to clikc "OK" to continue, for example).
     * This message may also be printed in the ui's message log.  The
     * ui should do something to indicate that this message is an
     * error.
     */
    
    void (*message)(Options *op, int level, const char *msg);

    /*
     * command_output - print output from executing a command; this
     * might be stuff like the output from compiling the kernel
     * module; a ui might choose to display this in a special window,
     * or it might ignore it all together.
     */

    void (*command_output)(Options *op, const char *msg);
    
    /*
     * approve_command_list - display the command list to the user;
     * returns TRUE if the user accepts that those commands will be
     * executed, or FALSE if the user does not accept.  This is only
     * done in expert mode.
     */

    int (*approve_command_list)(Options *op, CommandList *c,const char *descr);

    /*
     * yes_no - ask the yes/no question 'msg' and return TRUE for yes,
     * and FALSE for no.
     */

    int (*yes_no)(Options *op, const int def, const char *msg);


    /*
     * multiple_choice - ask the question 'question' with 'num_answers' possible
     * multiple choice answers in 'answers', with 'default_answer' as the
     * default answer. Returns the index into 'answers' of the user-selected
     * answer to 'question'. The names of answers must not begin with digits,
     * and the caller is responsible for enforcing this.
     */

    int (*multiple_choice)(Options *op, const char *question,
                           const char * const *answers, int num_answers,
                           int default_answer);

    /*
     * paged_prompt - ask the question 'question' with 'num_answers' possible
     * multiple choice answers in 'answers', with 'default_answer' as the
     * default answer, and with scrollable 'pager_text' given as supplementary
     * information, described by 'pager_title'. Returns the index into 'answers'
     * of the user-selected answer to 'question'. The names of answers must
     * not begin with digits, and the caller is responsible for enforcing this.
     */

    int (*paged_prompt)(Options *op, const char *question,
                        const char *pager_title, const char *pager_text,
                        const char * const *answers, int num_answers,
                        int default_answer);

    /*
     * status_begin(), status_update(), status_end() - these three
     * functions display the status of some process.  It is expected
     * that status_begin() would be called, followed by some number of
     * status_update() calls, followed by a status_end() call.
     */

    void (*status_begin)(Options *op, const char *title, const char *msg);
    void (*status_update)(Options *op, const float percent, const char *msg);
    void (*status_end)(Options *op, const char *msg);

    /*
     * close - close down the ui.
     */
    
    void (*close)(Options *op);


} InstallerUI;

#endif /* __NVIDIA_INSTALLER_UI_H__ */
