/* Default error callback for the scanned regular-expression library. */

#include <stdio.h>
#include "regexp.h"

void regerror(char *message)
{
    fprintf(stderr, "regexp: %s\n", message);
}
