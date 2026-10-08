/*------------------------------------------------------------------------
--                                                                      --
--                     H E L I O S   N U C L E U S                      --
--                     ---------------------------                      --
--                                                                      --
--             Copyright (C) 1987, Perihelion Software Ltd.             --
--                        All Rights Reserved.                          --
--                                                                      --
-- module.h								--
--                                                                      --
--	Module and Program structures					--
--                                                                      --
--	Author:  NHG 16/8/87						--
--                                                                      --
------------------------------------------------------------------------*/
/* SccsId: %W% %G% Copyright (C) 1987, Perihelion Software Ltd.	*/
/* RcsId: $Id: module.h,v 1.14 1993/12/13 11:48:38 tony Exp $ */

/* Host port: an excerpt of Helios-NG include/module.h (RCS 1.14), the	*/
/* parts sysbuild.c and objed.c use, for a build without the Helios	*/
/* include directory. Every field is a 32-bit word: word, WORD and RPTR	*/
/* come from ttypes.h (in Helios's <helios.h> a word is a long, 64 bits	*/
/* on an LP64 host). Also the few <helios.h> macros objed.c uses.	*/
/* Distributed under the GPL v3 with the rest of Helios-NG.		*/

#ifndef __module_h
#define __module_h

#ifndef __helios_h
#include "ttypes.h"
#endif

/* Host port: from <helios.h> */
#ifndef elif
#define elif(   x )	else if (x)
#endif
#ifndef true
#define true		1
#define false		0
#endif

/* Code module header */

struct Module
{
	word		Type;		/* module type = T_Module	*/
	word		Size;		/* size of module in bytes	*/
	char		Name[32];	/* module name			*/
	word		Id;		/* module table index		*/
	word		Version;	/* version number of this module*/
	word		MaxData;	/* highest data offset 		*/
	RPTR		Init;		/* root of init chain		*/
};

typedef struct Module Module;

/* type codes */

#define T_Mask		0xfff0fff0l	/* mask for constant bits 	*/
#define T_Valid		0x60f060f0l	/* contant bit in type code	*/

#define T_Program	0x60f060f0l
#define	T_Module	0x60f160f1l
#define T_ResRef        0x60f260f2l
#define T_Proc          0x60f360f3l
#define T_Code          0x60f460f4l
#define T_Stack         0x60f560f5l
#define T_Static        0x60f660f6l
#define T_ProcInfo	0x60f760f7l
#define T_Device	0x60f860f8l
#define T_FileName	0x60f960f9l
#define T_DevInfo	0x60fa60fal
#define T_RomDisk	0x60fb60fbl

/* Program structure */

struct Program
{
        Module		Module;		/* start of module		*/
	WORD		Stacksize;	/* size of initial stack	*/
	WORD		Heapsize;	/* size of program heap		*/
	RPTR		Main;		/* offset of main entry point	*/
};

typedef struct Program Program;

/* Image file header */

struct ImageHdr
{
	word		Magic;		/* = ??????_Magic		*/
	word		Flags;		/* zero for now			*/
	word		Size;		/* image size			*/
};

typedef struct ImageHdr ImageHdr;

/* Host port: the on-disk sizes, checked at compile time */
typedef char ModuleSizeCheck[ sizeof(Module) == 56 ? 1 : -1 ];
typedef char ProgramSizeCheck[ sizeof(Program) == 68 ? 1 : -1 ];
typedef char ImageHdrSizeCheck[ sizeof(ImageHdr) == 12 ? 1 : -1 ];

/*
 * Magic Numbers
 *
 * Cannot use #elif as SUN compiler cannot cope!
 */

#ifdef __TRAN
# define Image_Magic		0x12345678L	/* simple program	*/
#else
# if defined(__C40)
#  define Image_Magic		0xc4045601L	/* TMS320C40 program	*/
# else
#  if defined(__ARM)
#   define Image_Magic		0x0a245601L	/* ARM program		*/
#  else
#   if defined(__I860)
#    define Image_Magic		0x86045601L	/* i860 program		*/
#   else
#    if defined(__M68K)
#     define Image_Magic	0x68045601L	/* 68000 program	*/
#    endif
#   endif
#  endif
# endif
#endif

#define TaskForce_Magic 	0x12345677L	/* CDL object file	*/
#define RmLib_Magic		0x12345676L	/* Network object	*/

#endif /* __module_h */

/* -- End of module.h */
