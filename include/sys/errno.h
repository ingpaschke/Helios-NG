/* sys/errno.h: BSD compatibility header				*/
/* SccsId: %W% %G% (C) Copyright 1990, Perihelion Software Ltd.		*/
/* RcsId: $Id: errno.h,v 1.2 1991/02/13 12:25:33 nick Exp $ */

#ifdef _BSD

#ifndef __errno_h


/* AM: Changed this from absolute to relative path 
#  include "/helios/include/errno.h" */
/* The search path can have include/sys ahead of include/ (tcpip), so	*/
/* <errno.h> may find this file again: then take the errno.h one level	*/
/* up, <../errno.h>. This was the cross-compiler (__CROSSCOMP) branch;	*/
/* it works for every build and needs no fixed location of the tree.	*/
#  ifndef __sys_errno_h
#    define __sys_errno_h
#    include <errno.h>
#  else
#    include <../errno.h>
#  endif

#endif

#else
#error sys/errno.h included without _BSD set
#endif

