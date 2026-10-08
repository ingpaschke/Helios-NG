/*------------------------------------------------------------------------
--                                                                      --
--                     H E L I O S   N U C L E U S                      --
--                     ---------------------------                      --
--                                                                      --
--             Copyright (C) 1987, Perihelion Software Ltd.             --
--                        All Rights Reserved.                          --
--                                                                      --
-- gencodes.c								--
--                                                                      --
--	program to generate header files from fault database		--
--                                                                      --
--	Author:  NHG 7/3/89						--
--                                                                      --
------------------------------------------------------------------------*/
/* SccsId: 	@(#)gencodes.c	1.1	14/3/89 Copyright (C) 1989, Perihelion Software Ltd.*/


/*--------------------------------------------------------
-- 		     Include Files			--
--------------------------------------------------------*/

#include <stdio.h>
/* Host port: the C library headers on every host (the SUN4 branch	*/
/* declared malloc() itself) and <stdarg.h> (<varargs.h> is gone)	*/
#if defined(__HELIOS)
# include <string.h>
# include <stdlib.h>
# include <posix.h>
#else
# include <string.h>
# include <stdlib.h>
# include <unistd.h>
#endif

#include <stdarg.h>

#include <fcntl.h>

#define FDBBUFMAX	2000

typedef struct FDB {
	int	stream;
	int	pos;
	int	upb;
	char	buf[FDBBUFMAX];
} FDB;

/* Host port: these prototypes on every host, tfprintf() with "..." */
static char *skipname(char *p);
static char *skipspace(char *p);
static int strtonum(char *s);
static char *rdline(FDB *fdb);
static int rdch(FDB *fdb);

static void tfprintf(FILE *f, char *fmt, ... );
static void *xrealloc(void *p, size_t size);

/*--------------------------------------------------------
-- fdbopen						--
--							--
-- Open faults database					--
--							--
--------------------------------------------------------*/

extern FDB *fdbopen(name)
char * name;
{
	int s;

	FDB *fdb = (FDB *)malloc(sizeof(FDB));

	if( fdb == NULL ) return NULL;

	fdb->pos = 0;	
	fdb->upb = 0;	
	
	s = open(name,O_RDONLY);
	
	if( s < 0 ) goto fail;
	
	fdb->stream = s;
	
	return fdb;
fail:
	if( fdb != NULL ) free(fdb);
	return NULL;
}

/*--------------------------------------------------------
-- fdbclose						--
--							--
-- Close faults database				--
--							--
--------------------------------------------------------*/

extern void fdbclose(fdb)
FDB *fdb;
{
	close(fdb->stream);
	free(fdb);
}

/*--------------------------------------------------------
-- fdbrewind						--
--							--
-- Rewind faults database				--
--							--
--------------------------------------------------------*/

extern void fdbrewind(fdb)
FDB *fdb;
{
	fdb->pos = 0;
	fdb->upb = 0;
	lseek(fdb->stream,0,0);
}

/*--------------------------------------------------------
-- fdbscan						--
--							--
--							--
--------------------------------------------------------*/

extern void fdbscan(name)
char *name;
{
	/* Host port: a line of any length (rdline() grows its buffer;	*/
	/* both were static [1000], filled without a bound)		*/
	char *line;
	static char *classline = NULL;
	char *codename, *codenum, *codemsg, *p;
	/* Host port: a code line before the first class line used	*/
	/* classpfix uninitialised: the i386 reference printed a NULL	*/
	/* there as "(null)", x86-64 -O2 printed stack garbage. Start	*/
	/* with that text, so the output stays that of the reference.	*/
	char *classmask, *classpfix = "(null)";
	int classbits;
	FDB *fdb = fdbopen(name);
	FILE *f = stdout;
	int classshift;
	int copying = 0;
	
	if( fdb == NULL ) 
	{
		fprintf(stderr,"cannot open %s\n",name);
		return;
	}
	
	for(;;)
	{
		line = rdline(fdb);
/*printf("%s",line);*/
		if( copying )
		{
			if( line[0] == '#' && line[1] == '}' )
			{ copying = 0; continue; }
			fprintf(f,"%s",line);
			continue;			
		}
		switch( line[0] )
		{
		case '\0':		/* EOF		*/
			return;
			
		case '\n':		/* blank line 	*/
			fprintf(f,"\n");
			break;
			
		case '#':		/* comment	*/
			switch( line[1] )
			{
			case '#':
				fprintf(f,"%s",line+1);
				break;
			case '!':
				p = line+2;
				while( *p != '\n' && *p != 0 ) p++;	/* Host port: stop at the NUL too */
				*p = 0;
				if( f != stdout ) fclose(f);
				if( (f = fopen(line+2,"w")) == NULL )
				{
					fprintf(stderr,"Cannot open %s\n",line+2);
					return;
				}
				break;
			case '{':
				copying = 1;
				break;
			default:
				p = line+1;
				while( *p != '\n' && *p != 0 ) p++;	/* Host port: stop at the NUL too */
				*p = 0;
				tfprintf(f,"/*%s\t\t\t\t\t*/\n", line+1);
				break;
			}
			break;
		case '!':		/* class name	*/
			if( line[1] == '!' ) continue;

			free(classline);
			classline = xrealloc(NULL, strlen(line) + 1);
			(void)strcpy(classline,line);			
			p = skipspace(classline+1);
			p = skipname(p);
			p = skipspace(p);
			classmask = p;
			p = skipname(p);
			p = skipspace(p);
			classpfix = p;
			p = skipname(p);
			classbits = strtonum(classmask);
			classshift = 0;
			/* Host port: a mask of 0 looped forever */
			if( classbits == 0 )
			{
				fprintf(stderr,"gencodes: class mask 0 in line: %s",line);
				exit(1);
			}
			while(((classbits>>classshift) & 1) == 0) classshift++;

			if( classpfix[0] != 0 )
			{
				tfprintf(f,"#define\t%sMask\t%s\n",classpfix,classmask);
				tfprintf(f,"#define\t%sShift\t%d\n",classpfix,classshift);
			}
			break;
			
		default:		/* code line	*/
			p = skipspace(line);
			codename = p;
			p = skipname(p);
			p = skipspace(p);
			codenum = p;
			p = skipname(p);
			p = skipspace(p);
			codemsg = p;
			while( *p != '\n' && *p != 0 ) p++;
			*p = 0;

			if( *codemsg )
				tfprintf(f,"#define\t%s%s\t%s\t/*\t%s\t*/\n",
					classpfix,codename,codenum,codemsg);
			else tfprintf(f,"#define\t%s%s\t%s\n",
					classpfix,codename,codenum);
		}
	}	

}

static int tabtab[] = { 8, 20, 12, 3, 29, 2, 8 };

/* Host port: a <stdarg.h> function on every host (was <varargs.h>) */
static void tfprintf(FILE *f, char *fmt, ...)
{
	va_list a;
	/* Host port: fbuf grows and every conversion is measured; both	*/
	/* were static [100] without a bound (a message of about 100	*/
	/* characters overflowed pbuf)					*/
	static char *fbuf = NULL;
	static size_t fbufsize = 0;
	int n = 0;
	int pos = 0;
	int tabn = 0;
	int i;
	int percent = 0;

	va_start(a, fmt);	/* Host port: <stdarg.h> */
	
	for(;;)
	{
		char c = *fmt++;
		if( (size_t)n + 2 > fbufsize )
		{
			fbufsize = fbufsize ? 2 * fbufsize : 100;
			fbuf = xrealloc(fbuf, fbufsize);
		}
		if( c == 0 || c == '\t' || c == '%')
		{
			int newpos;
			fbuf[n] = 0;
			if( percent ) 
			{
				char *pbuf;
				int len;
				/* Host port: a %s takes a char *; every argument	*/
				/* was fetched as an int, which truncates a pointer	*/
				/* on LP64. The conversion is the first letter after	*/
				/* the %.						*/
				char *conv = fbuf+1;
				while( *conv != 0 && !((*conv >= 'a' && *conv <= 'z') ||
						       (*conv >= 'A' && *conv <= 'Z')) )
					conv++;
				if( *conv == 's' )
				{
					char *arg = va_arg(a,char *);
					len = snprintf(NULL,0,fbuf,arg);
					pbuf = xrealloc(NULL,(size_t)len + 1);
					snprintf(pbuf,(size_t)len + 1,fbuf,arg);
				}
				else
				{
					int arg = va_arg(a,int);
					len = snprintf(NULL,0,fbuf,arg);
					pbuf = xrealloc(NULL,(size_t)len + 1);
					snprintf(pbuf,(size_t)len + 1,fbuf,arg);
				}
				fputs(pbuf,f);
				pos += strlen(pbuf);
				free(pbuf);
			}
			else { fputs(fbuf,f); pos += n; }
			if( c == '%' )
			{
				n = 1;
				fbuf[0] = c;
				percent = 1;
				continue;
			}
			else percent = 0;
			if( c == 0 ) break;
			newpos = 0;
			for( i = 0; i <= tabn; i++ ) newpos += tabtab[i];
			tabn++;
			while( pos < newpos )
			{
#if 1
				int nexttab = (pos+8)/8;
				nexttab *= 8;
				if( nexttab <= newpos ) 
				{ putc('\t',f); pos = nexttab; }
				else
#endif
				{ putc(' ',f); pos++; }
			}
			n = 0;
		}
		else fbuf[n++] = c;
	}
	va_end(a);
}


static char *skipname(p)
char *p;
{
	while( *p != ' ' && *p != '\t' && *p != '\n' && *p != 0 ) p++;
	if( *p == 0 ) return p;
	*p++ = 0;
	return p;
}

static char *skipspace(p)
char *p;
{
	while( *p == ' ' || *p == '\t' ) p++;
	return p;
}

static int strtonum(s)
char *s;
{
	int radix = 10;
	int val = 0;
	/* Host port: declared outside the loop and set to 0: a		*/
	/* character that is no digit (an upper case hex digit but F,	*/
	/* see below) repeats the previous digit (as the i386 -O0 build	*/
	/* did) or adds 0, instead of using an uninitialised value	*/
	int digit = 0;
	
	while( *s )
	{
		char c = *s++;
		
		if( c == 'x' ) { radix = 16; continue; }
		
		if( '0' <= c && c <= '9' ) digit = c - '0';
		else if( radix == 16 && ('a' <= c && c <= 'f') ) digit = c - 'a' + 10;
		else if( radix == 16 && ('F' <= c && c <= 'F') ) digit = c - 'A' + 10;

		val = val * radix + digit;
	}
	
	return val;
}

/* Host port: returns the line in a buffer that grows as needed	*/
/* (the caller's was a char[1000] filled without a bound)		*/
static char *rdline(fdb)
FDB *fdb;
{
	static char *buf = NULL;
	static size_t size = 0;
	int pos = 0;
	int c = 0;

	for(;;)
	{
		/* room for a tab's 8 spaces and the NUL */
		if( (size_t)pos + 9 > size )
		{
			size = size ? 2 * size : 1000;
			buf = xrealloc(buf, size);
		}
		c = rdch(fdb);
		switch( c )
		{
		case -1: goto done;
		case '\r': continue;
		case '\t':
			do { buf[pos++] = ' '; } while(pos % 8);
			break;
		default:
			buf[pos++] = c; 
		}
		if( c == '\n' ) break;
	}
done:
	buf[pos] = 0;
	return buf;
}

/* Host port: realloc() or fail */
static void *xrealloc(void *p, size_t size)
{
	void *q = realloc(p, size);

	if( q == NULL )
	{
		fprintf(stderr,"gencodes: out of memory\n");
		exit(1);
	}
	return q;
}

static int rdch(fdb)
FDB *fdb;
{
	if( fdb->upb == -1 ) return -1;
	if( fdb->pos == fdb->upb )
	{
		int size = read(fdb->stream,fdb->buf,FDBBUFMAX);
		if( size <= 0 ) 
		{
			fdb->upb = -1;
			return -1;
		}
		fdb->upb = size;
		fdb->pos = 0;
	}
	return fdb->buf[fdb->pos++];
}


int main(argc,argv)
int argc; char **argv;
{
	if( argc < 2 ) 
	{
		printf("usage: gencodes fdb\n");
		exit(1);
	}
	
	fdbscan(argv[1]);
	return 0;
}


/* -- End of gencodes.c */
