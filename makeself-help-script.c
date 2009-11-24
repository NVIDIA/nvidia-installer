/*
 * Implements the '--help-args-only' and '--advanced-options-args-only'
 * nvidia-installer command line options for use by makeself when
 * generating the .run file during the 'dist' step.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "nvidia-installer.h"
#include "help-args.h"

void print_usage(char **argv)
{
    fprintf(stderr, "usage: %s --help-args-only|"
            "--advanced-options-args-only\n", argv[0]);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        print_usage(argv);
        exit(1);
    }

    if (strcmp(argv[1], "--help-args-only") == 0)
        print_help_args_only(TRUE, FALSE);
    else if (strcmp(argv[1], "--advanced-options-args-only") == 0)
        print_help_args_only(TRUE, TRUE);
    else {
        print_usage(argv);
        exit(1);
    }

    return 0;
}
