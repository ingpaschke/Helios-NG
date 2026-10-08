/* Sccs Id: "@(#)ttypes.h	1.3\t18/9/87 Copyright (C) Perihelion Software Ltd.	*/

/* Host port: on Helios (the commands built for the transputer) the	*/
/* system's <helios.h> has these types, with a long as the 32-bit word;	*/
/* it defines __helios_h, so the rest of this file is skipped there.	*/
#ifdef __HELIOS
#include <helios.h>
#endif

#ifndef __helios_h
#define __helios_h

/* standard type definitions */

/* Host port: a WORD is the transputer's 32-bit word, also on LP64 hosts  */
/* where long is 64 bits wide (was: long / unsigned long).               */
#include <stdint.h>
typedef  int32_t        WORD    ;       /* a machine word, 32 bits      */
typedef  uint32_t       UWORD   ;       /* a machine word, 32 bits      */
typedef  WORD           INT     ;       /* a synonym                    */
typedef  WORD           word    ;       /* a synonym                    */
typedef  char           BYTE    ;       /* a byte, used in place of char*/
typedef  char           byte    ;       /* a byte, used in place of char*/
typedef  unsigned char  UBYTE   ;       /* an unsigned byte             */
typedef  char *         STRING  ;       /* character string             */
typedef  void	(*VoidFnPtr)()	;	/* pointer to function		*/
typedef  word	(*WordFnPtr)()	;	/* pointer to function		*/
typedef  char *         APTR    ;       /* a machine pointer            */
typedef  WORD           RPTR    ;       /* a self relative pointer      */
typedef	 int		bool	;	/* boolean value		*/

#define Variable	1

#define PUBLIC
#define PRIVATE		static

#define  TRUE           1l
#define  FALSE          0l

#define RTOA(x) ((APTR)(&(x)+(x)))      /* convert RPTR to APTR         */
#define ATOR(x) ((RPTR)((x)-&(x)))      /* convert APTR to RPTR         */

#define MinInt          0x80000000l

#define TargetBytesPerWord 4

/* Host port: the tools read and write the transputer's little-endian    */
/* words in host byte order (sysbuild, objed, keyform), and the Makefile */
/* sets none of the big-endian defines (BYTE_SEX_ODD, HOSTISBIGENDIAN),  */
/* which are untested in this port. Refuse a big-endian host at compile  */
/* time where the compiler says so, and at run time (HostIsLittleEndian) */
/* in the programs that depend on it.                                    */
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# error "the host tools support little-endian hosts only (see tools/hosttools/README.md)"
#endif

static inline int
HostIsLittleEndian( void )
{
  const UWORD	one = 1;

  return *(const UBYTE *)&one == 1;
}

#endif
