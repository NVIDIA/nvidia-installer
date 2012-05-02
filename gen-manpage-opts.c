/*
 * Prints the option help in a form that is suitable to include in the manpage.
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "option_table.h"
#include "gen-manpage-opts-helper.h"

int main(void)
{
    gen_manpage_opts_helper(__options);
    return 0;
}
