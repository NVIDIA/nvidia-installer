/*
 * Prints the option help in a form that is suitable to include in the manpage.
 */
#include <stdio.h>
#include <ctype.h>

#include "nvidia-installer.h"
#include "option_table.h"

static void print_option(const NVOption *o)
{
    printf(".TP\n.BI ");
    /* Print the name of the option */
    /* XXX We should backslashify the '-' characters in o->name. */
    if (o->flags & NVOPT_IS_BOOLEAN) {
        /* "\-\-name, \-\-no\-name */
        printf("\"\\-\\-%s, \\-\\-no\\-%s", o->name, o->name);
    } else if (isalnum(o->val)) {
        /* "\-c, \-\-name */
        printf("\"\\-%c, \\-\\-%s", o->val, o->name);
    } else {
        /* "\-\-name */
        printf("\"\\-\\-%s", o->name);
    }

    if (o->flags & NVOPT_HAS_ARGUMENT) {
        printf("=\" \"%s", o->name);
    }

    printf("\"\n");

    /* Print the option description */
    /* XXX Each sentence should be on its own line! */
    /* XXX We need to backslashify the '-' characters here. */
    printf("%s\n", o->description);
}

int main(int argc, char* argv[])
{
    int i;
    const NVOption *o;

    /* Print the "simple" options, i.e. the ones you get by running
     * nvidia-installer --help.
     */
    printf(".SH OPTIONS\n");
    for (i = 0; __options[i].name; i++) {
        o = &__options[i];

        if (!(o->flags & OPTION_HELP_ALWAYS))
            continue;

        if (!o->description)
            continue;

        print_option(o);
    }

    /* Print the advanced options. */
    printf(".SH \"ADVANCED OPTIONS\"\n");
    for (i = 0; __options[i].name; i++) {
        o = &__options[i];

        if (o->flags & OPTION_HELP_ALWAYS)
            continue;

        if (!o->description)
            continue;

        print_option(o);
    }

    return 0;
}
