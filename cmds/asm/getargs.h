/*
 * Author:      Alex Schuilenburg
 * Date:        28 July 1994
 *
 * Copyright 1994 Perihelion Distributed Software Limited
 *
 * $Id: getargs.h,v 1.1 1994/08/03 10:47:31 al Exp $
 */

/* Fix: from Helios-NG HEAD cmds/support/getargs.h (see getargs.c);	*/
/* freeargs() is gone, the old-style declarations too; pushargs()	*/
/* returns 0 when out of memory.					*/

/* The arguments stack */
typedef struct _ArgStack ArgStack;
struct _ArgStack
  {
    char **	argv;
    int         argc;
    ArgStack *	next;
  };


/* Prototypes */
char ** getargs( char * name, int * rargc );
void    popargs(  ArgStack ** argstack, int * argc, char *** argv );
int     pushargs( ArgStack ** argstack, int   argc, char  ** argv );
