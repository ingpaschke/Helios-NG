/* Opt-in diagnostics: send each stage to the I/O server before proceeding. */
#include <stdio.h>
#include <stdlib.h>
#include "trace.h"

char *heliosTracePath;
static int opened;
static unsigned long sequence;

void heliosTraceInit(void)
{
    if (!heliosTracePath || opened) return;
    if (!freopen(heliosTracePath, "w", stderr)) {
        printf("Xhelios: cannot open trace %s\n", heliosTracePath);
        exit(1);
    }
    setvbuf(stderr, NULL, _IONBF, 0);
    opened = 1;
    fprintf(stderr, "Xhelios move trace v2: sequence stage a b c d (hex)\n");
}

void heliosTrace(const char *stage, long a, long b, long c, long d)
{
    if (!opened) return;
    fprintf(stderr, "%lu %s %lx %lx %lx %lx\n", ++sequence, stage, a, b, c, d);
}
