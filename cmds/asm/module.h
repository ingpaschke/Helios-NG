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
/* RcsId: $Id: module.h,v 1.8 1992/09/16 16:32:31 paul Exp $ */

/* Host port: an excerpt of Helios 1.3 include/module.h, the parts	*/
/* objed.c uses (the image and program headers it rewrites in place).	*/
/* Every field is a 32-bit word: word, WORD and RPTR come from		*/
/* ttypes.h. Distributed under the GPL v3 with the rest of Helios-NG.	*/

#ifndef __module_h
#define __module_h

#ifndef __helios_h
#include "ttypes.h"
#endif

/* Code module header */

typedef struct Module {
	word		Type;		/* module type = T_Module	*/
	word		Size;		/* size of module in bytes	*/
	char		Name[32];	/* module name			*/
	word		Id;		/* module table index		*/
	word		Version;	/* version number of this module*/
	word		MaxData;	/* highest data offset 		*/
	RPTR		Init;		/* root of init chain		*/
} Module;

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

/* Program structure */

typedef struct Program {
	Module		Module;		/* start of module		*/
	WORD		Stacksize;	/* size of initial stack	*/
	WORD		Heapsize;	/* size of program heap		*/
	RPTR		Main;		/* offset of main entry point	*/
} Program;

/* Image file header */

typedef struct ImageHdr {
	word		Magic;		/* = ??????_Magic		*/
	word		Flags;		/* zero for now			*/
	word		Size;		/* image size			*/
} ImageHdr;

#define Image_Magic		0x12345678L	/* simple program	*/

#endif

/* -- End of module.h */
