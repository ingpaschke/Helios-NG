/****************************************************************/
/* File: asm.c                                                  */
/*                                                              */
/*                                                              */
/* Author: NHG 19-Feb-87                                        */
/* Modified to compile under Norcroft C: AEv 21-Jan-88		*/
/****************************************************************/
/* $Id: asm.c,v 1.11 1991/12/02 15:13:43 martyn Exp $ */

#include "asm.h"
#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include "getargs.h"

#ifdef __STDC__
#include <stdlib.h>
#endif

#ifdef LATTICE
#include <fcntl.h>
#endif

#ifdef __HELIOS
#include <task.h>
#include <memory.h>
#include <nonansi.h>
#include <module.h>
#endif

PUBLIC  FILE *infd;
PUBLIC  FILE *outfd;
PUBLIC  FILE *verfd;
PUBLIC  jmp_buf error_level;
PRIVATE jmp_buf default_level;
PUBLIC  WORD errors;
PRIVATE WORD phase = 0;
PUBLIC  WORD traceflags;
PUBLIC  WORD warnflags;
PRIVATE WORD maxerr = 0;
PUBLIC  WORD preasm = FALSE;
PUBLIC  WORD hexopt = FALSE;
PRIVATE WORD image  = i_helios;
PUBLIC  WORD defopt = FALSE;
PUBLIC  WORD fastopt = TRUE;
PUBLIC  WORD verbose = FALSE;
PUBLIC  WORD inlib = FALSE;
PUBLIC  FILE *deffd;
PUBLIC  WORD vmpagesize = 8*1024;
PUBLIC  WORD instr_size = 3;

PRIVATE WORD needobjed = FALSE;
PRIVATE WORD noname = FALSE;
PRIVATE char *progname = NULL;
PRIVATE WORD stack_size = -1;
PRIVATE WORD heap_size = -1;

/* Fix: infile and outfile were BYTE [128] arrays filled by strcpy from	*/
/* argv and from stored file names, so a path of 128 characters or more	*/
/* overflowed them. They point at the name now:	*/
/* argv strings, the file names stored in the code, or a malloc'ed copy.*/
PUBLIC  char *infile = "";
PRIVATE char *outfile = "";
PUBLIC  char *curfile;

#ifdef __STDC__
#define BUFSIZE (8*1024)
PRIVATE BYTE inbuf[BUFSIZE];
PRIVATE BYTE outbuf[BUFSIZE];
#endif

WORD filemod;
WORD filepos = -1;

PUBLIC WORD waste;

/* void unload(void); */
PRIVATE WORD parsearg(ellipsis);
/* Fix: @file arguments: the outer argument lists, and their depth.	*/
/* File-level, so that parsearg() can take an option's argument from	*/
/* the outer list when the option is the last word of an @file.	*/
PRIVATE ArgStack *argstack = NULL;
PRIVATE int argdepth = 0;
PRIVATE char **nextword(char **argv);
WORD max(ellipsis);
PRIVATE void tidy1(ellipsis);
PRIVATE void tidyup(ellipsis);

/* Fix: the output (-o) and the .def file (-d) are written to a	*/
/* temporary file beside the target and renamed to it only when the	*/
/* run ends without an error. asm used to create the	*/
/* output while it parsed the arguments, so any error left a 0-byte or	*/
/* truncated file with a fresh time stamp, which make then took as up	*/
/* to date; write errors (a full disk) were not checked.		*/
typedef struct OutFile
{
	char	*name;		/* the target				*/
	/* Fix: volatile, the signal handler reads it; changed only	*/
	/* with the caught signals blocked	*/
	char	*volatile tmp;	/* temporary file, NULL if none		*/
	FILE	*fd;
} OutFile;

PRIVATE OutFile outf;
PRIVATE OutFile deff;
#ifndef __HELIOS
PRIVATE sigset_t tmpsigs;	/* Fix: the signals rmtemps_sig() catches */
PRIVATE sigset_t oldsigs;	/* Fix: the signal mask before holdsigs() */
PRIVATE void rmtemps_sig(int sig);
PRIVATE void catchsig(int sig);
PRIVATE void holdsigs(void);
PRIVATE void releasesigs(void);
#endif
PRIVATE FILE *openout(OutFile *o, char *name, char *mode);
PRIVATE void closeout(OutFile *o, int ok);
PRIVATE void rmtemps(void);
PRIVATE void droptmp(OutFile *o, int ok, int *bad);

#ifdef MWC
long _stksize = 20000;
#endif
#ifdef IBMPC
int _stack = 4096;
#endif

PRIVATE void
_asm( argc, argv )		/* NB cannot call the function asm() as this is a reserved word in some C compilers */
  int 		argc;
  char ** 	argv;
{
	/* Host port: volatile, these are live across a longjmp() */
	volatile WORD infiles = 0;
	volatile time_t start, inend, growend, end;	/* Host port: was int */
    

        infd = stdin;
        outfd = stdout;
	
#ifdef LATTICE
        verfd = stdout;
#else
        verfd = stderr;
#endif

	deffd = (FILE *) NULL;
        infile = "";
        memcpy(error_level,default_level,sizeof(jmp_buf));
        errors = 0;

        argv++;
	argc--;

	inend = growend = end = start = time(NULL);
	
        if( setjmp(error_level) == 0 )
        {
	  
                initmem();
                initcode();
                initmodules();
                initsym();
                initasm();

                report("Transputer Assembler V1.9");
                report("Copyright (C) 1987, Perihelion Software Ltd.");

                phase = 1;
                /* Fix: @file arguments, from Helios-NG HEAD's asm:	*/
                /* the words of file (blanks and newlines separate	*/
                /* them, '...' or "..." quote, # starts a comment line)	*/
                /* are read as if they stood here; @ nests.		*/
                while( (*argv != NULL) || (argstack != NULL) )
                {
			if (*argv == NULL)
			{
				popargs(&argstack, &argc, &argv);
				argdepth--;
				continue;
			}

			if ( **argv == '@')
			{
				char **argfile;
				int rest = 0, nargc;

				if( ++argdepth > 32 )
					error("Argument files nested too deeply at %s", *argv);
				argfile = getargs((*argv)+1, &nargc);
				if (argfile == NULL)
					error("Cannot read argument file %s", (*argv)+1);
				/* argv+1 is pushed so we return to the next argv; */
				/* Fix: with the count of that list (was the new	*/
				/* file's count), and out of memory is fatal (the	*/
				/* rest of the outer list was dropped), A12		*/
				while( argv[1+rest] != NULL ) rest++;
				if( !pushargs(&argstack, rest, argv+1) )
					error("Out of memory");
				argv = argfile;
				argc = nargc;
				continue;
			}

                        if ( parsearg(&argv) == TRUE )
                        {
				infiles++; filemod = 0; filepos = -1;
                                assemble();
                                tidy1();
                                tempval++;
                                inlib = FALSE;
                        }
                }

		if( infiles == 0 )	/* if no command line files, use stdin */
		{
			assemble();
		}

                genend();	/* finish off last module */

		inend = growend = end = time(NULL);
				
                report("\nSyntax analysis complete");

		/* Fix: a copy (was strcpy into infile[128]); the program	*/
		/* name below cuts outfile in place.				*/
		infile = strdup(outfile);
		if( infile == NULL ) error("Out of memory");
		if( warnflags & warn_unref ) show_unref();
	
                if( !preasm && errors == 0 )
                {
                        phase = 2;
                        setmodules();
			growdata();
			report("Semantic analysis complete");
                        growcode();
			growend = end = time(NULL);
                        report("\nCode growing complete");

                }

                if( !preasm && errors == 0 )
                {
                        phase = 3;
                        switch( (int)image )
                        {
                        case i_helios: genimage();      break;
                        }

			if( !noname && progname == NULL && outfd != stdout )
			{
				char *p = &outfile[strlen(outfile)];
				/* Host port: test p != outfile first; the old order read	*/
				/* outfile[-1] (same result, but out of bounds).	*/
				while( p != outfile && *(p-1) != '/' ) 
				{
					if( *p == '.' ) *p = '\0';
					p--;
				}
				progname = p;
				needobjed = 1;
			}
			
			fflush(outfd);
			
                        report("Image file generated");
			
                        /* Fix: not after an error, the output is dropped */
                        if( needobjed && maxerr == 0 ) objed(outfd,progname,stack_size,heap_size);
                }

                if( preasm && errors == 0 )
                {
                        phase = 2;
                        genpreasm();

                        report("Object code generated");

                }

        }
                                                               
	end = time(NULL);

report("Code Density:    Percent        %8d Waste          %8d",((simPc+4-waste)*100)/(simPc+4),waste);
report("Times (secs):    Total          %8d Input          %8d",(int)(end-start),(int)(inend-start));
report("                 Growing        %8d Output         %8d",(int)(growend-inend),(int)(end-growend));
report("Memory used:     Code           %8d Local Heap     %8d",codesize,heapsize);
report("                 Symbols        %8d Expressions    %8d",symsize,exprsize);

	VMStats();
	
	modstats();

	if( traceflags&db_refs ) printtab(Symtab);

	if( traceflags&db_vm ) showvm();
			
        tidyup();

        exit(maxerr);

        return;
}

/********************************************************/
/* main                                                 */
/*                                                      */
/* Initialise everything and call the assembler         */
/*                                                      */
/********************************************************/

PUBLIC int
main( argc, argv )
  int argc;
  BYTE **argv;
{
#if defined(__HELIOS) && defined(__TRAN)
	int			stack_size = 3000;
	static Carrier *	stack      = NULL;

	
	stack = AllocFast( stack_size , &MyTask->MemPool );

	if (stack)
	  Accelerate( stack, _asm, sizeof (argc) + sizeof (argv), argc, argv );
	else
#endif
	  _asm( argc, argv );

	return 0;
}

/* Fix: the word after *argv, continuing in the outer argument list	*/
/* at the end of an @file						*/
PRIVATE char **nextword(char **argv)
{
	int argc;

	argv++;
	while( *argv == NULL && argstack != NULL )
	{
		popargs(&argstack, &argc, &argv);
		argdepth--;
	}
	return argv;
}

PRIVATE WORD parsearg(aargv)
char ***aargv;
{
	char **argv = *aargv;
	char *arg = *argv;
	char *optword = arg;		/* Fix: for the message of _ARG_ */

/* Fix: an option that takes an argument as the last word of the	*/
/* command line passed NULL on. As the last word of an	*/
/* @file its argument is the next word of the outer list (it was	*/
/* "needs an argument", A12).						*/
#define _ARG_ if( *++arg == '\0' && (arg = *(argv = nextword(argv))) == NULL ) \
		error("Option %s needs an argument", optword);

        if( *arg == '-' )
        {
        	arg++;
                switch( *arg )
                {
                case 'v':
                    verbose = !verbose;
                    if( *++arg != '\0' )
                    {
                        FILE *vfd = fopen(arg,"w");
                        /* Fix: the message printed arg+2 */
                        if( vfd == 0 ) error("Cannot open %s for output",arg);
                        verfd = vfd;
                    }
                    break;


                case 't':
                	arg++;
                        while( *arg != '\0' )
                        {
                                switch( (int)locase(*arg) )
                                {
                                case 'l': traceflags ^= db_lex;      break;
                                case 's': traceflags ^= db_syn;      break;
                                case 'c': traceflags ^= db_gencode;  break;
                                case 'g': traceflags ^= db_growcode; break;
                                case 'o': traceflags ^= db_genimage; break;
                                case 'e': traceflags ^= db_expr;     break;
                                case 'v': traceflags ^= db_eval;     break;
                                case 'p': traceflags ^= db_preasm;   break;
                                case 'z': traceflags ^= db_vm; 	     break;
				case 'k': traceflags ^= db_kludge;   break;
				case 'f': traceflags ^= db_files;    break;
				case 'm': traceflags ^= db_mem;      break;
				case 'y': traceflags ^= db_sym;      break;
				case 'd': traceflags ^= db_modules;  break;
				case 'r': traceflags ^= db_refs;     break;
                                }
                                arg++;
                        }
                        break;

		/* Fix: -w sets a flag (was ^=: "-wu -wu" turned	*/
		/* undefined symbols back into errors), A12		*/
		case 'w':
                	arg++;
                        while( *arg != '\0' )
                        {
                                switch( (int)locase(*arg) )
                                {
                                case 'r': warnflags |= warn_unref;   break;
                                case 'u': warnflags |= warn_undef;   break;
                                case 'd': warnflags |= warn_dup;     break;	/* Fix: new */
                                }
                                arg++;
                        }
			break;			

		case 'I':
			_ARG_
			instr_size = (long)atol(arg);
			break;

                case 'p':
                        preasm = TRUE;
                        break;

                case 'x':
                        hexopt = TRUE;
                        break;

                case 'i':
                        image = *arg;
                        break;

		case 'f':
			fastopt = !fastopt;
			break;

		case 'd':
			defopt = TRUE;
			_ARG_;
#if defined(SUN4)
			deffd = openout(&deff,arg,"w");	/* Fix: see OutFile */
#else
			deffd = fopen(arg,"w");
#endif
			/* Fix: the message printed arg+2 */
			if(deffd == NULL ) error("Cannot open %s for output",arg);
			break;

		case 'n':
			_ARG_
			needobjed = 1;
			progname = arg;
			break;
			
		case 's':
			_ARG_
			needobjed = 1;
			stack_size = (long)atol(arg);
			break;

		case 'h':
			_ARG_;
			needobjed = 1;
			heap_size = (long)atol(arg);
			break;			

		case 'N':
			noname = TRUE;
			break;
			
#ifdef VM
		case 'm':
			_ARG_;
			VMfilename = arg;
			break;
			
		case 'z':
			_ARG_;
			vmpagesize = (long)atol(arg);
			break;
			
		case 'M':
			_ARG_;
			VMLeave = (long)atol(arg);
			break;
#endif		
                case 'o':
                	_ARG_;
                        outfile = strdup(arg);	/* Fix: was strcpy into outfile[128] */
                        if( outfile == NULL ) error("Out of memory");
#if defined(SUN4)
			/* Fix: openout() removes outfile and opens a	*/
			/* temporary file (see OutFile)			*/
			outfd = openout(&outf,outfile,"w+");
#else
                        remove(outfile);
#endif
#ifdef LATTICE
                        if( !hexopt ) 
			{
				_iomode ^= O_RAW;
	                        outfd = fopen(outfile,"w");
				_iomode ^= O_RAW;
			}
#endif
#ifdef MWC
			outfd = fopen(outfile,hexopt?"w+":"wb+");
#endif
#ifdef SUN
			outfd = fopen(outfile,"w+");
#endif
#ifdef IBMPC
			outfd = fopen(outfile,"w+");
#endif
#ifdef __HELIOS
			outfd = fopen(outfile,hexopt?"w+":"wb+");
#endif
                        if( outfd == 0 ) error("Cannot open %s for output",outfile);
#ifdef __STDC__
			setvbuf( outfd, outbuf, _IOFBF, BUFSIZE );
#endif
                        break;


		case 'l':
			_ARG_;
			inlib = TRUE;
			goto inputfile;

		case '\0':
			infile = "stdin";	/* Fix: was strcpy */
			infd   = stdin;
			goto openedfile;
			break;
                }
                *aargv = ++argv;
                return FALSE;
        }
        else
	  {
           inputfile:
	        infile = arg;		/* Fix: was strcpy into infile[128] */
#ifdef LATTICE
                if( !preasm ) 
		{
			_iomode ^= O_RAW;
	                infd = fopen(infile,"r");
                	_iomode ^= O_RAW;
		}
#endif
#ifdef MWC
		infd = fopen(infile,preasm?"r":"rb");
#endif
#if defined(SUN4)
		infd = fopen(infile,"r");
#endif
#ifdef SUN
		infd = fopen(infile,"r");
#endif
#ifdef IBMPC
		infd = fopen(infile,"r");
#endif
#ifdef __HELIOS
		infd = fopen(infile,"rb");
#endif
                if( infd == 0 )
		{
		 error("Cannot open %s for input",infile);
		}
openedfile:		
		if( verbose )
		{
			fprintf(verfd,"Reading %s                                     \r",infile);
			fflush(verfd);
		}
#ifdef __STDC__
		setvbuf( infd, inbuf, _IOFBF, BUFSIZE );
#endif
                lineno = 1;
                *aargv = ++argv;
                return TRUE;
        }
}


/* Fix: the file a message is about: errors raised while the options	*/
/* are read, before any input, printed an empty name ("Fatal Error:  :	*/
/* Cannot open ...").				*/
PRIVATE char *msgfile()
{
	return infile != NULL && *infile != '\0' ? infile : "asm";
}

/* Host port: the message functions below took six (or eleven) INT	*/
/* arguments and passed them on to fprintf, which only works where	*/
/* pointers and ints are passed alike; they are real varargs now.	*/

PUBLIC void error(BYTE *str, ...)
{
	va_list ap;
#ifdef LINENO
        fprintf(verfd,"Fatal Error: %s %d :",msgfile(),lineno);
#else
        fprintf(verfd,"Fatal Error: %s :",msgfile());
#endif
	va_start(ap,str);
        vfprintf(verfd,str,ap);
	va_end(ap);
        putc('\n',verfd);
        maxerr = max(maxerr,20);
#if defined(SUN4)
	{
		float x = 0.0;
		float y = 0.0;
		float z;

		z = x/y;		/* force fpe to get core dump	*/
	}
#endif
	exit(maxerr);
}


/* the body of warn(), shared with recover() */
PRIVATE void vwarn(BYTE *str, va_list ap)
{
#ifdef LINENO
        fprintf(verfd,"Warning: %s %d :",msgfile(),lineno);
#else
        fprintf(verfd,"Warning: %s :",msgfile());
#endif
        vfprintf(verfd,str,ap);
        putc('\n',verfd);
}

PUBLIC void warn(BYTE *str, ...)
{
	va_list ap;
	va_start(ap,str);
	vwarn(str,ap);
	va_end(ap);
}

PUBLIC void warn_error(BYTE *str, ...)
{
	va_list ap;
#ifdef LINENO
        fprintf(verfd,"Warning: %s %d :",msgfile(),lineno);
#else
        fprintf(verfd,"Warning: %s :",msgfile());
#endif
	va_start(ap,str);
        vfprintf(verfd,str,ap);
	va_end(ap);
        putc('\n',verfd);

        errors++;
        maxerr = max(maxerr,10);
}


PUBLIC void report(BYTE *str, ...)
{
	va_list ap;
	if( !verbose ) return;
	va_start(ap,str);
        vfprintf(verfd,str,ap);
	va_end(ap);
        putc('\n',verfd);
}


PUBLIC void _trace(BYTE *str, ...)
{
       va_list ap;
       fprintf(verfd,"%x: ",filepos);
       va_start(ap,str);
       vfprintf(verfd,str,ap);
       va_end(ap);
       putc('\n',verfd);
}


PUBLIC void recover(BYTE *str, ...)
{
	va_list ap;
	/* Fix: an error, so say so (was printed as a warning) */
#ifdef LINENO
        fprintf(verfd,"Error: %s %d :",msgfile(),lineno);
#else
        fprintf(verfd,"Error: %s :",msgfile());
#endif
	va_start(ap,str);
        vfprintf(verfd,str,ap);
	va_end(ap);
        putc('\n',verfd);
	/* Fix: recover() reports an error: the statement is dropped, so	*/
	/* no output may be written and the exit status must say so. It	*/
	/* only warned, and asm exited 0.			*/
	errors++;
	maxerr = max(maxerr,10);
	/* Fix: a binary file would give one message per byte */
	if( errors >= 100 ) error("Too many errors");
        longjmp(error_level,1);
}


/* Fix: recover() as it was in 1.3, a warning: for the duplicate data	*/
/* definitions that -wd turns back into warnings.			*/
PUBLIC void recoverwarn(BYTE *str, ...)
{
	va_list ap;
	va_start(ap,str);
        vwarn(str,ap);
	va_end(ap);
        longjmp(error_level,1);
}

/* Fix: an error that does not stop the link (an undefined symbol, a	*/
/* duplicate definition): asm carries on, so that every such error is	*/
/* reported, but writes no output and exits with status 10.		*/
PUBLIC void linkerror(BYTE *str, ...)
{
	va_list ap;
#ifdef LINENO
        fprintf(verfd,"Error: %s %d :",msgfile(),lineno);
#else
        fprintf(verfd,"Error: %s :",msgfile());
#endif
	va_start(ap,str);
        vfprintf(verfd,str,ap);
	va_end(ap);
        putc('\n',verfd);
        maxerr = max(maxerr,10);
}

/* Fix: an error that drops or alters part of a statement, where the	*/
/* parser can go on with the next token (an item of a BYTE directive):	*/
/* counted like recover(), so that no output is written and the exit	*/
/* status is 10, but without skipping the rest of the statement. These	*/
/* were warnings in 1.3, and asm wrote the altered code with exit 0.	*/
PUBLIC void asmerror(BYTE *str, ...)
{
	va_list ap;
#ifdef LINENO
        fprintf(verfd,"Error: %s %d :",msgfile(),lineno);
#else
        fprintf(verfd,"Error: %s :",msgfile());
#endif
	va_start(ap,str);
        vfprintf(verfd,str,ap);
	va_end(ap);
        putc('\n',verfd);
	errors++;
	maxerr = max(maxerr,10);
	if( errors >= 100 ) error("Too many errors");
}

PRIVATE void tidyup()
{
        tidy1();
        /* Fix: keep the output only if there was no error (see OutFile) */
        if(deffd != NULL) { closeout(&deff, maxerr == 0); deffd = NULL; }
        if(outfd != stdout && outfd != NULL) { closeout(&outf, maxerr == 0); outfd = stdout; }
        if(verfd != stderr && verfd != NULL) { fclose(verfd); verfd = stderr; }
        VMTidy();
}

#ifndef __HELIOS

/* Fix: remove the temporary files on sig, unless sig is ignored	*/
/* under nohup, or as a background job of a	*/
/* shell without job control (SIGINT, SIGQUIT ignored), the handler	*/
/* made asm die on a signal that the i386 build ignored.		*/
PRIVATE void catchsig(int sig)
{
	struct sigaction sa;

	if( sigaction(sig,NULL,&sa) != 0 || sa.sa_handler == SIG_IGN ) return;
	signal(sig,rmtemps_sig);
	sigaddset(&tmpsigs,sig);
}

/* Fix: the handler rmtemps_sig() unlinks outf.tmp and deff.tmp. They	*/
/* are set and cleared with the caught signals blocked, so that the	*/
/* handler never sees a name that is being freed (a signal between	*/
/* free() and "= NULL" unlinked a garbage path) or	*/
/* misses a temporary file that mkstemp() has just created.		*/
PRIVATE void holdsigs()
{
	sigprocmask(SIG_BLOCK,&tmpsigs,&oldsigs);
}

PRIVATE void releasesigs()
{
	sigprocmask(SIG_SETMASK,&oldsigs,NULL);
}

/* Fix: finish with o's temporary file: rename it to the target if ok	*/
/* and nothing went wrong so far (*bad), else remove it (A6: the name	*/
/* is cleared before it is freed, see holdsigs).			*/
PRIVATE void droptmp(OutFile *o, int ok, int *bad)
{
	char *t;

	holdsigs();
	t = o->tmp;
	if( ok && !*bad && rename(t,o->name) != 0 ) *bad = TRUE;
	if( !ok || *bad ) unlink(t);
	o->tmp = NULL;
	releasesigs();
	free(t);
}

/* Fix: open name for output through a temporary file in the same	*/
/* directory (see OutFile). Anything that is not a regular file, such	*/
/* as /dev/null, is opened directly as before.				*/
PRIVATE FILE *openout(OutFile *o, char *name, char *mode)
{
	static int atexit_done = 0;
	struct stat st;
	mode_t mask;
	int fd;
	char *t;

	if( !atexit_done )
	{
		atexit(rmtemps);
		sigemptyset(&tmpsigs);
		catchsig(SIGINT);
		catchsig(SIGTERM);
		catchsig(SIGHUP);
		/* Fix: these left the temporary file behind too	*/
		/* SIGQUIT (^\), SIGPIPE		*/
		/* (asm -v ... | head) and SIGXFSZ (ulimit -f).		*/
		catchsig(SIGQUIT);
		catchsig(SIGPIPE);
		catchsig(SIGXFSZ);
		atexit_done = 1;
	}

	closeout(o, FALSE);		/* a second -o or -d replaces the first */
	/* a copy: the automatic program name cuts outfile in place */
	o->name = strdup(name);
	if( o->name == NULL ) error("Out of memory");

	/* Fix: lstat, not stat. stat follows a	*/
	/* symbolic link: with stdout redirected to a regular file,	*/
	/* /dev/stdout looked like a regular file, so asm unlinked the	*/
	/* name (as root: the /dev/stdout link) and created its		*/
	/* temporary file in /dev ("Cannot open"). A symbolic link, a	*/
	/* pipe, a device or anything else that is not a regular file is	*/
	/* opened and written directly and never removed, as the i386	*/
	/* build did; only a regular file goes through a temporary file.	*/
	if( lstat(name,&st) == 0 && !S_ISREG(st.st_mode) )
		return o->fd = fopen(name,mode);

	unlink(name);

	/* Fix: the temporary file is the target's name plus ".XXXXXX";	*/
	/* for a file name (the last component) of 249-255 characters	*/
	/* that was longer than NAME_MAX, and the output failed ("Cannot	*/
	/* open"). The name is shortened to fit.	*/
	{
		char *base = strrchr(name,'/');
		size_t dl = base == NULL ? 0 : (size_t)(base + 1 - name);
		size_t bl = strlen(name + dl);
		long nmax = 255;
		char *dir = dl == 0 ? strdup(".") : strndup(name,dl);

		if( dir != NULL )
		{
			long v = pathconf(dir,_PC_NAME_MAX);
			if( v > 8 ) nmax = v;
			free(dir);
		}
		if( bl > (size_t)(nmax - 7) ) bl = nmax - 7;
		t = malloc(dl + bl + 8);
		if( t == NULL ) error("Out of memory");
		memcpy(t,name,dl + bl);
		strcpy(t + dl + bl,".XXXXXX");
	}
	holdsigs();			/* Fix: A6, see holdsigs() */
	fd = mkstemp(t);
	if( fd >= 0 ) o->tmp = t;
	releasesigs();
	if( fd < 0 ) { free(t); return NULL; }
	/* the permissions fopen() would have given the file */
	mask = umask(0); umask(mask);
	fchmod(fd, 0666 & ~mask);
	o->fd = fdopen(fd,mode);
	if( o->fd == NULL ) { int bad = TRUE; close(fd); droptmp(o,FALSE,&bad); }
	return o->fd;
}

#else /* __HELIOS */

/* Helios (the asm command on the transputer): the C library has no	*/
/* mkstemp(), sigprocmask() or lstat(), so the target is written	*/
/* directly, as by the 1.3 asm, and removed again at the end of a run	*/
/* with an error: o->tmp is the target itself.				*/
PRIVATE void droptmp(OutFile *o, int ok, int *bad)
{
	if( !ok || *bad ) unlink(o->tmp);
	o->tmp = NULL;
}

PRIVATE FILE *openout(OutFile *o, char *name, char *mode)
{
	static int atexit_done = 0;

	if( !atexit_done )
	{
		atexit(rmtemps);
		atexit_done = 1;
	}

	closeout(o, FALSE);		/* a second -o or -d replaces the first */
	/* a copy: the automatic program name cuts outfile in place */
	o->name = strdup(name);
	if( o->name == NULL ) error("Out of memory");

	unlink(name);
	o->fd = fopen(name,mode);
	if( o->fd != NULL ) o->tmp = o->name;
	return o->fd;
}

PUBLIC char *helios_strdup(const char *s)
{
	char *p = malloc(strlen(s) + 1);

	if( p != NULL ) strcpy(p,s);
	return p;
}

#endif /* __HELIOS */

/* Fix: close an output file; if ok, check that every byte was written	*/
/* and rename the temporary file to the target, else remove it.	*/
PRIVATE void closeout(OutFile *o, int ok)
{
	int bad = FALSE;

	if( o->fd != NULL )
	{
		if( ok && (fflush(o->fd) != 0 || ferror(o->fd)) ) bad = TRUE;
		if( fclose(o->fd) != 0 ) bad = TRUE;
		o->fd = NULL;
	}
	if( o->tmp != NULL ) droptmp(o,ok,&bad);
	if( ok && bad )
	{
		fprintf(verfd,"Fatal Error: %s :Cannot write %s\n",o->name,o->name);
		maxerr = max(maxerr,20);
	}
}

/* Fix: called by exit(): remove the temporary files of a run that	*/
/* stopped with a fatal error.						*/
PRIVATE void rmtemps()
{
	if( outf.tmp != NULL ) unlink(outf.tmp);
	if( deff.tmp != NULL ) unlink(deff.tmp);
}

#ifndef __HELIOS
/* Fix: the same when asm is interrupted (make on ^C). Only		*/
/* async-signal-safe calls: unlink, signal, raise.			*/
PRIVATE void rmtemps_sig(int sig)
{
	rmtemps();
	signal(sig,SIG_DFL);
	raise(sig);
}
#endif

PRIVATE void tidy1()
{
        if(infd != stdin && infd != NULL) 
        {
        	fclose(infd); 
        	infd = stdin; 
        }
}

PUBLIC WORD uch = -2;
PUBLIC WORD unreadready = FALSE;

#ifdef RDCH_FN

PUBLIC WORD asm_rdch()
{
   WORD c = uch;

   if( c == -2 ){
#ifdef MWC
	c = bingetc(infd);
	if( c == 0xffffL ) c = EOF;
#else
        c = getc(infd);
#endif
   }
   else uch = -2;
   filepos++;
   return c;
}


PUBLIC WORD unrdch(c)
WORD c;
{
   filepos--;
   uch = c;
}
#endif
#ifndef LATTICE
PUBLIC WORD min(a,b)
WORD a,b;
{
        return a<b ? a : b;
}

PUBLIC WORD max(a,b)
WORD a,b;
{
        return a>b ? a : b;
}
#endif
