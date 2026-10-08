#if 0
static char *rcsid = "$Header: /hsrc/cmds/com/RCS/objed.c,v 1.15 1993/08/27 14:11:05 nickc Exp $";
#endif

#include <stdio.h>
#include <module.h>	/* Host port: on the host tools/hosttools/include/module.h, 32-bit fields */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

Program 	prog;
ImageHdr	hdr;
FILE *		fd;


#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#define _ARG_ if( *arg == 0 ) arg = *(++argv);

#ifdef HOSTISBIGENDIAN
int	swapopt = TRUE;	/* default to swapping */
#else
int	swapopt = FALSE;
#endif

word
swapword( word it )
{
  if (!swapopt)	return (it);
    {
      /* Host port: memcpy, was a store and load through (word *) casts */
      byte	from[4];
      byte	to[4];

      memcpy( from, &it, 4 );

      to[0] = from[3];
      to[1] = from[2];
      to[2] = from[1];
      to[3] = from[0];
      
      memcpy( &it, to, 4 );
      return(it);
    }
}

void
putmodules( void )
{
  word 		isize = swapword( hdr.Size );
  word *       	v;
  Module *	m;
  int		rsize;
  word		mtype;
  word		offset = 0;

  
  /* Host port: a header size below 0 made malloc() fail and -m printed	*/
  /* nothing; the module chain was walked without bounds (a module of	*/
  /* size 0 looped for ever, a chain without its terminating 0 word or	*/
  /* a bad size read past the image). The image is followed by zeroed	*/
  /* room for one Module, every step is checked against the size.	*/
  if ( isize < 0 )
   { fprintf(stderr, "Invalid image size in header: %d\n", isize );
     exit(1);
   }
  
  v  = (word *)calloc( 1, (size_t)isize + sizeof (Module) );
  
  if ( v == NULL )
    return;
  
  fseek( fd, sizeof (ImageHdr), SEEK_SET );
  
  rsize = fread( v, 1, (int)isize, fd );
  
  if ( rsize != isize )
   { fprintf(stderr, "Image file smaller than header size\n" );
     exit(1);
   }
  
  m = (Module *)v;
  
  for (;;)
    {
      if ( offset > isize - 4 )
       { fprintf(stderr, "Module chain not terminated by 0 within the image\n" );
         exit(1);
       }
      
      if ((mtype = swapword( m->Type )) == 0 )
        break;
      
    {
      word	msize    = swapword( m->Size    );
      word	mslot    = swapword( m->Id      );
      word	mdsize   = swapword( m->MaxData );
      word	mversion = swapword( m->Version );
      char *	mname    = m->Name;

      
      if ( msize <= 0 || msize > isize - offset )
       { fprintf(stderr, "Bad module size %d at offset %d\n", msize, offset );
         exit(1);
       }

      
      switch ( mtype )
	{		
	case T_Program:
	  /* Host port: %d for the 32-bit words, was %ld (in all printfs) */
	  printf( "Program : %10.32s slot %3d version %4d size %5d datasize %4d\n",
		 mname, mslot, mversion, msize, mdsize );
	  break;
	  
	case T_Module:
	  printf( "Module  : %10.32s slot %3d version %4d size %5d datasize %4d\n",
		 mname, mslot, mversion, msize, mdsize );
	  break;
	  
	case T_ResRef:
	  printf( "ResRef  : %10.32s slot %3d version %4d\n",
		 mname, mslot, mversion );
	  break;
	  
	default:
	  fprintf( stderr, "Unknown module type: %x\n", mtype );
	  exit(1);
	}
      
      m = (Module *)((char *)m + msize);
      offset += msize;
    }
    }
  
  free( v );

  return;
  
} /* putmodules */

/* Host port: atol() of the i386 reference, whose long is 32 bits:	*/
/* out-of-range values saturate at the 32-bit limits			*/
static long
atol32( char * arg )
{
  long	v = atol( arg );

  if (v > 0x7fffffffL)		return 0x7fffffffL;
  if (v < -0x7fffffffL - 1)	return -0x7fffffffL - 1;
  return v;
}

typedef struct Image_Magics
  {
    UWORD		Magic;	/* Host port: 32 bits, was unsigned long */
    char *		Type;	  
  }
Image_Magics;


int
main(
     int argc,
     char **argv )
{
  long 		info      = false;
  int		size;
  long		changed   = false;
  WORD		progtype;	/* Host port: 32 bits, was long */
  char *	file      = NULL;
  char *	type 	  = NULL;
  char *	name	  = 0;
  long		stacksize = -1;
  long		heapsize  = -1;
  long		modules	  = false;

  
#ifndef __HELIOS
  if ( !HostIsLittleEndian() )	/* Host port: see ttypes.h */
    {
      fprintf( stderr, "objed: big-endian hosts are not supported\n" );
      exit(1);
    }
#endif
  
  argv++;
  
  while ( *argv != 0 )
    {
      char *arg = *argv;
      
      if ( *arg++ == '-' )
	{
	  switch ( *arg++ )
	    {
	    case 'n':
	      _ARG_;
	      name = arg;
	      break;
	      
	    case 's':
	      _ARG_;
	      stacksize = atol32( arg );	/* Host port: was (long)atol( arg ) */
	      break;
	      
	    case 'h':
	      _ARG_;
	      heapsize = atol32( arg );	/* Host port: was (long)atol( arg ) */
	      break;			
	      
	    case 'i':
	      info = true;
	      break;	
	      
	    case 'm':
	      modules = true;
	      break;
	      
	    }
	}
      else
	{
	  file = *argv;
	}
      
      argv++;
    }

  if (file == NULL)
    {
      fprintf( stderr, "objed: must have name of file to examine\n" );
      exit(1);
    }
  
  if (name == 0 && stacksize == -1 && heapsize == -1) /* nowt to write */
    {
      fd = fopen(file,"rb");
    }
  else
    {
#if defined(__TRAN) || defined(__ARM) || defined(R140) || defined(__C40) || defined (__HELIOS)
      fd = fopen( file, "r+b" );
#else
      fd = fopen( file, "rwb" );
#endif
    }
  
  if ( fd == 0 ) 
    { fprintf(stderr, "Cannot open %s\n",file );
      exit(1);
    }
  
  size = fread( &hdr, 1, sizeof (ImageHdr), fd );
  
  if ( size != sizeof(ImageHdr) )
   { fprintf( stderr, "Read failure: %d\n", size );
     exit(1);
   }
  
    {
      static Image_Magics	Values[] =
	{
	  /*
	   * XXX - the following have been extracted from /hsrc/include/module.h
	   *       make sure that they are kept up to date
	   */

	  { 0x12345678L,	"Transputer Helios Executable\n" },
	  { 0xc4045601L,	"Helios-C40 Executable\n" },
	  { 0x0a245601L,	"Helios-ARM Executable\n" },
	  { 0x86045601L,	"Helios-I860 Executable\n" },
#ifdef HOSTISBIGENDIAN
	  { 0x01560468L,	"Helios-M68K Executable\n" },
#else
	  { 0x68045601L,	"Helios-M68K Executable\n" },
#endif
	  { TaskForce_Magic,	"Task Force Binary\n" },
	  { RmLib_Magic,	"Resource Management Library Binary\n" }
	};
      UWORD		value = swapword( hdr.Magic );	/* Host port: was unsigned long */
      int		i;


      for (i = sizeof (Values) / sizeof (Values[0]); i--;)
	{
	  if (value == Values[ i ].Magic)
	    {
	      printf( "%s", Values[ i ].Type );	/* Host port: not as a format */
	      break;	      
	    }
	}
      
      if (i < 0)
	{
	  fprintf( stderr,  "File not object image\n" );
	  exit(1);
	}

      if (Values[i].Magic == 0x01560468)
	{
	  swapopt = FALSE;
	}      
    }
  
  if ( info )
    printf( "Image size = %d bytes\n", swapword( hdr.Size ) );

  size = fread( &prog, 1, sizeof (Program), fd );
  
  if ( size != sizeof (Program) )
   { fprintf(stderr, "Read failure: %d\n", size );
     exit(1);
   }
   
  progtype = swapword( prog.Module.Type );
  
  if   ( progtype == T_Program ) type = "Program";
  elif ( progtype == T_Module  ) type = "Module";
  elif ( progtype == T_ResRef  ) type = "ResRef"; 
  
  if ( type == NULL )
   { fprintf(stderr, "Invalid Module type: %x\n",progtype );
     exit(1);
   }
  
  if ( info )
    {
      printf( "Object type is %s\n",type );
  
      printf( "Name is '%.32s' ",prog.Module.Name );	/* Host port: %.32s, the name need not end in a NUL */
    }
  
  if ( name != 0 )
    {
      int i;

      
      changed = true;
      
      for (i = 0; i < 32; i++ )
	prog.Module.Name[ i ] = 0;
      
      strncpy( prog.Module.Name, name, 31 );
      
      if ( info )
	printf( "New = '%s'", prog.Module.Name );
    }
  
  if ( info )
    putchar('\n');
  
  if ( swapword( prog.Module.Type ) == T_Program )
    {
      if ( info )
	printf( "Stacksize = %d ", swapword( prog.Stacksize ) );
      
      if ( stacksize != -1 )
	{
	  changed = true;
	  
	  prog.Stacksize = swapword( stacksize );
	  
	  if ( info )
	    printf( "New = %ld", stacksize );
	}
      
      if ( info )
	printf( "\nHeapsize = %d ", swapword( prog.Heapsize ) );
      
      if ( heapsize != -1 )
	{
	  changed = true;
	  
	  prog.Heapsize = swapword( heapsize );
	  
	  if ( info )
	    printf( "New = %ld", heapsize );
	}
      
      if ( info )
	putchar('\n');
    }
  else if (stacksize != -1 || heapsize != -1)
    {
      printf( "Cannot set stacksize/heapsize - image does not include a program stucture\n" );
    }
  
  if ( modules )
    putmodules();
  
  if ( changed )
    {
      
      fseek( fd, 0L, SEEK_SET );
      
      size = fwrite( &hdr, 1, sizeof (ImageHdr), fd );
      
      if ( size != sizeof (ImageHdr) )
	{ fprintf(stderr,  "Write failure writing header: wrote %d bytes, errno = %d\n",
	      size, errno);
	  exit(1);
	}
      
      size = fwrite( &prog, 1, sizeof (Program), fd );
      
      if ( size != sizeof (Program) )
	{ fprintf( stderr, "Write failure writing program: wrote %d bytes, errno = %d\n",
	      size, errno);
	  exit(1);
	}	      
    }
  
  fclose( fd );
  
  return 0;

} /* main */
