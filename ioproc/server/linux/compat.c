/*
 * compat.c -- small libc compatibility shims for the Linux port of the
 * Helios I/O Server.
 *
 * sys_errlist/sys_nerr are gone from glibc for newly linked programs;
 * sunlocal.h maps them onto linux_errlist()/LINUX_NERR.
 */
#include <string.h>

#define LINUX_NERR 256

const char **linux_errlist(void)
{
    static const char *tab[LINUX_NERR];
    static char text[LINUX_NERR][128];
    static int done = 0;
    int i;

    if (!done) {
        for (i = 0; i < LINUX_NERR; i++) {
            strncpy(text[i], strerror(i), sizeof(text[i]) - 1);
            tab[i] = text[i];
        }
        done = 1;
    }
    return tab;
}
