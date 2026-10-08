/****************************************************************/
/* File: objed.c                                                */
/*                                                              */
/*                                                              */
/* Author: NHG 16-May-88                                        */
/*								*/
/****************************************************************/
/* $Id: objed.c,v 1.9 1991/12/02 15:13:43 martyn Exp $ */

/* do NOT include asm.h */

#if defined(SUN4)
#include <stdio.h>
#include "ttypes.h"
#ifndef __helios_h
#define __helios_h 1
#endif
#include "module.h"
#include <sys/file.h>
#ifndef SEEK_SET
#define SEEK_SET L_SET
#endif

/* Host port: C library headers, real prototypes for the message	*/
/* functions of asm.c, and a check that the headers objed reads and	*/
/* writes back have their 32-bit layout on this host.			*/
#include <errno.h>
#include <string.h>
#ifdef __GNUC__
#define PRINTFLIKE(f,a)	__attribute__((format(printf,f,a)))
#else
#define PRINTFLIKE(f,a)
#endif
extern void error(BYTE *str, ...) PRINTFLIKE(1,2);
extern void report(BYTE *str, ...) PRINTFLIKE(1,2);
typedef char ImageHdr_is_12_bytes[sizeof(ImageHdr) == 12 ? 1 : -1];
typedef char Program_is_68_bytes[sizeof(Program) == 68 ? 1 : -1];
#else
#include <stdio.h>
#include <module.h>
#endif

extern FILE *verfd;
extern WORD verbose;

/* Host port: WORD (was long, 64 bits on LP64): the header fields it	*/
/* converts are 32-bit WORDs.						*/
WORD swap(WORD x)

#if defined(HOSTISBIGENDIAN)
{
	WORD r;
	char *a = (char *)&x;
	char *b = (char *)&r;

	b[0] = a[3];
	b[1] = a[2];
	b[2] = a[1];
	b[3] = a[0];

	return r;
}
#else
{ return (x); }
#endif

void objed(fd,name,stacksize,heapsize)
FILE *fd;
char *name;
long stacksize;
long heapsize;
{
	Program prog;
	ImageHdr hdr;
	int info = verbose;
	int size;
	long progtype;
	char *type;

	fseek(fd,0,SEEK_SET);	/* ensure we are at start of file */
	
	size = fread(&hdr,1,sizeof(ImageHdr),fd);

	if( size != sizeof(ImageHdr) )
		error("Read failure: %d",size);


	if( swap(hdr.Magic) != Image_Magic )
		error("File not object image");

	size = fread(&prog,1,sizeof(Program),fd);

	if( size != sizeof(Program) )
		error("Read failure: %d",size);

	progtype = swap(prog.Module.Type);

	if      ( progtype == T_Program ) type = "Program";
	else if ( progtype == T_Module  ) type = "Module";
	else if ( progtype == T_ResRef  ) type = "ResRef"; 
	else if ( progtype == T_Device  ) type = "Device"; 
	else type = "Unknown";

report("Image:           Type   %16s Size           %8d",type,swap(hdr.Size));

report("Header Fields:                       Old                     New");
	
	if( info ) 
report("                 Name %18s      %18s",prog.Module.Name,name?name:"");

	if( name != 0 )
	{
		int i;
		for(i=0; i<32; i++ ) prog.Module.Name[i] = 0;
		strncpy(prog.Module.Name,name,31);
	}

	if( swap(prog.Module.Type) == T_Program )
	{
		if ( info ) fprintf(verfd,"                 Stacksize      %8d",swap(prog.Stacksize));
		if( stacksize != -1 )
		{
			prog.Stacksize = swap(stacksize);
			if ( info ) fprintf(verfd,"                %8ld",stacksize);
		}
		if ( info ) fprintf(verfd,"\n                 Heapsize       %8d",swap(prog.Heapsize));
		if( heapsize != -1 )
		{
			prog.Heapsize = swap(heapsize);
			if ( info ) fprintf(verfd,"                %8ld",heapsize);
		}
		if ( info ) putc('\n',verfd);
	}

	fseek(fd,0,SEEK_SET);	/* ensure we are at start of file */
	
	size = fwrite(&hdr,1,sizeof(ImageHdr),fd);

	if( size != sizeof(ImageHdr) )
		error("Failed to write back image header: %d %d",size,errno);

	size = fwrite(&prog,1,sizeof(Program),fd);

	if( size != sizeof(Program) )
		error("Failed to write back program header: %d %d",size,errno);

}
