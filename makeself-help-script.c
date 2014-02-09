/*
 * Implements the '--help-args-only' and '--advanced-options-args-only'
 * nvidia-installer command line options for use by makeself when
 * generating the .run file during the 'dist' step.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "nvidia-installer.h"
#include "nvgetopt.h"
#include "option_table.h"
#include "msg.h"

static void print_usage(char **argv)
{
    fprintf(stderr, "usage: %s --help-args-only|"
            "--advanced-options-args-only\n", argv[0]);
}

static void print_help_helper(const char *name, const char *description)
{
    nv_info_msg(TAB, name);
    nv_info_msg(BIGTAB, description);
    nv_info_msg(NULL, "");
}

int main(int argc, char **argv)
{
    unsigned int include_mask = 0;

    if (argc != 2) {
        print_usage(argv);
        exit(1);
    }

    /*
     * We are printing help text for use by makeself.sh; we do not
     * want this formatted to the width of the current terminal, so
     * hardcode the width used by nv_info_msg() to 65.
     */
    reset_current_terminal_width(65);

    if (strcmp(argv[1], "--help-args-only") == 0) {
        /* only print options with the ALWAYS flag */
        include_mask = NVGETOPT_HELP_ALWAYS;
    } else if (strcmp(argv[1], "--advanced-options-args-only") == 0) {
        /* print all options */
        include_mask = 0;
    } else {
        print_usage(argv);
        exit(1);
    }

    nvgetopt_print_help(__options, include_mask, print_help_helper);

    return 0;
}
