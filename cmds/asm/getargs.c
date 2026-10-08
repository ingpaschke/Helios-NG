/*
 * Author:      Alex Schuilenburg
 * Date:        18 July 1994
 *
 * Copyright 1994 Perihelion Distributed Software Limited
 *
 * $Id: getargs.c,v 1.1 1994/08/03 10:47:31 al Exp $
 */

/* Fix: taken from Helios-NG HEAD cmds/support/getargs.c for the @file	*/
/* arguments of asm (HEAD's asm.c reads them; 1.3 had none), with these	*/
/* changes:								*/
/*  - the file is read in a loop into a growing buffer (it used		*/
/*    fseek/ftell, which fail on a pipe);				*/
/*  - an empty file (or one with only comments) gives no arguments;	*/
/*    it was an error;							*/
/*  - a quoted argument starts after the quote and ends at the closing	*/
/*    one (an empty '' or "" skipped past its closing quote);		*/
/*  - freeargs() and the list of allocations it walked are gone: asm	*/
/*    keeps pointers into the arguments (file names) to the end.	*/

#include <stdio.h>
#include <stdlib.h>
#include "getargs.h"

/*
 * Routine to read in a file contents and convert it to
 * argc, argv style.
 * It is passed the file name and an address of argc.
 * It returns argv or NULL on error.
 */

char **getargs(char *name, int *rargc)
{
    int     argc, argc_size;
    char    **rargv;
    size_t  l, p, size;
    FILE    *f;
    char    *ch;

    /* Default return */
    *rargc = 0;

    /* 1st open the file */
    if ((f = fopen(name, "rb")) == NULL) {
        return NULL;
    }

    /* Read in the entire file */
    size = 4096;
    l = 0;
    if ((ch = (char *)malloc(size + 1)) == NULL) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        size_t n = fread(&ch[l], 1, size - l, f);
        l += n;
        if (l < size) break;
        size *= 2;
        {
            /* Fix: a failed realloc leaked the buffer (A12) */
            char *nch = (char *)realloc(ch, size + 1);
            if (nch == NULL) {
                free(ch);
                fclose(f);
                return NULL;
            }
            ch = nch;
        }
    }
    if (ferror(f)) {
        free(ch);
        fclose(f);
        return NULL;
    }
    fclose(f);
    ch[l] = '\0';

    /* Allocate some buffer space for argv */
    argc_size = 10;
    if ((rargv = (char **)malloc(argc_size * sizeof(char *))) == NULL) {
        free(ch);
        return NULL;
    }

    /* 1st position is the whole array */
    rargv[0] = ch;
    argc = 1;

    /* Parse into the list */
    p = 0;
    while (p < l) {
        if (ch[p] == '#') {
            /* This line is a comment */
            p++;
            while ((p < l) && !((ch[p] == '\r') || (ch[p] == '\n')))
                p++;
        } else if ((ch[p] == '\n') || (ch[p] == '\r') || (ch[p] == '\t') || (ch[p] == ' ')) {
            /* Skip <nl> <cr> and whitespace */
        } else {
            /* We have a new argv */

            /* Enough room ? */
            if ((argc+1) == argc_size) {
                /* We must grow */
                char **newptr;

                argc_size += 10;
                newptr = (char **)realloc(rargv, sizeof(char *) * argc_size);
                if (newptr == NULL) {
                    /* No more memory */
                    free(rargv[0]);
                    free(rargv);
                    return NULL;
                }

                rargv = newptr;
            }

            /* Add the item */
            if ((ch[p] == '\'') || (ch[p] == '"')) {
                /* This is in quotes */
                char q = ch[p++];               /* Skip Quote */
                rargv[argc] = &ch[p];

                /* Skip until quote */
                while ((p < l) && (ch[p] != q)) {
                    p++;
                }

                /* Terminate */
                ch[p] = '\0';
            } else {
                /* Standard argument */
                rargv[argc] = &ch[p++];

                /* Skip until \n, \r or whitespace */
                while ((p < l) && (ch[p] != '\n') && (ch[p] != '\r') &&
                       (ch[p] != '\t') && (ch[p] != ' ')) {
                    p++;
                }

                /* Terminate */
                ch[p] = '\0';
            }

            /* New argc */
            argc++;
        }

        /* Next character */
        p++;
    }

    /* The last one must be a NULL */
    rargv[argc] = NULL;

    /* Advance to the next */
    rargv++;
    *rargc = argc-1;                    /* One less than you think */

    return rargv;
}

/*
 * This routine is used to push the arguments onto an ArgStack
 */
/* Fix: returns 0 if out of memory, so that the caller can stop; the	*/
/* rest of the outer arguments was dropped silently (A12).		*/
int     pushargs(ArgStack **argstack, int argc, char **argv)
{
    ArgStack    *newitem;

    /* Get new */
    newitem = (ArgStack *)malloc(sizeof(ArgStack));

    /* Add to stack */
    if (newitem == NULL)
        return 0;
    newitem->argc = argc;
    newitem->argv = argv;
    newitem->next = *argstack;
    *argstack = newitem;
    return 1;
}

/*
 * This routine is used to pop arguments from an ArgStack
 */
void    popargs(ArgStack **argstack, int *argc, char ***argv)
{
    ArgStack    *olditem;

    /* Catch any funny routines */
    if ((argstack == NULL) || (*argstack == NULL))
        return;

    /* Take old off stack */
    olditem = *argstack;
    *argstack = olditem->next;

    /* Set results */
    *argc = olditem->argc;
    *argv = olditem->argv;

    /* Free */
    free(olditem);
}
