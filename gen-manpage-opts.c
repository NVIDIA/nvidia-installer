/*
 * Prints the option help in a form that is suitable to include in the manpage.
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "nvidia-installer.h"
#include "option_table.h"

static void print_option(const NVGetoptOption *o)
{
    char scratch[64], *s;
    int j, len;
    
    int omitWhiteSpace;

    printf(".TP\n.BI ");
    /* Print the name of the option */
    /* XXX We should backslashify the '-' characters in o->name. */
    if (o->flags & NVGETOPT_IS_BOOLEAN) {
        /* "\-\-name, \-\-no\-name */
        printf("\"\\-\\-%s, \\-\\-no\\-%s", o->name, o->name);
    } else if (isalnum(o->val)) {
        /* "\-c, \-\-name */
        printf("\"\\-%c, \\-\\-%s", o->val, o->name);
    } else {
        /* "\-\-name */
        printf("\"\\-\\-%s", o->name);
    }

    if (o->flags & NVGETOPT_HAS_ARGUMENT) {
        len = strlen(o->name);
        for (j = 0; j < len; j++) scratch[j] = toupper(o->name[j]);
        scratch[len] = '\0';
        printf("=\" \"%s", scratch);
    }

    printf("\"\n");

    /*
     * Print the option description:  write each character one at a
     * time (ugh) so that we can special-case a few characters:
     *
     * "[" --> "\n.I "
     * "]" --> "\n"
     * "-" --> "\-"
     *
     * Brackets are used to mark the text inbetween as italics.
     * '-' is special cased so that we can backslashify it.
     *
     * XXX Each sentence should be on its own line!
     */
    
    omitWhiteSpace = 0;
    
    for (s = o->description; s && *s; s++) {
        
        switch (*s) {
          case '[':
              printf("\n.I ");
              omitWhiteSpace = 0;
              break;
          case ']':
              printf("\n");
              omitWhiteSpace = 1;
              break;
          case '-':
              printf("\\-");
              omitWhiteSpace = 0;
              break;
          case ' ':
              if (!omitWhiteSpace) {
                  printf("%c", *s);
              }
              break;
          default:
              printf("%c", *s);
              omitWhiteSpace = 0;
              break;
        }
    }
    
    printf("\n");
}

int main(int argc, char* argv[])
{
    int i;
    const NVGetoptOption *o;

    /* Print the "simple" options, i.e. the ones you get by running
     * nvidia-installer --help.
     */
    printf(".SH OPTIONS\n");
    for (i = 0; __options[i].name; i++) {
        o = &__options[i];

        if (!(o->flags & NVGETOPT_HELP_ALWAYS))
            continue;

        if (!o->description)
            continue;

        print_option(o);
    }

    /* Print the advanced options. */
    printf(".SH \"ADVANCED OPTIONS\"\n");
    for (i = 0; __options[i].name; i++) {
        o = &__options[i];

        if (o->flags & NVGETOPT_HELP_ALWAYS)
            continue;

        if (!o->description)
            continue;

        print_option(o);
    }

    return 0;
}
