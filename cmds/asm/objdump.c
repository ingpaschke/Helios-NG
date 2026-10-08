/* $Id: objdump.c,v 1.4 1990/09/28 14:18:19 nick Exp martyn $ */


#include "asm.h"
#include <ctype.h>
#include <stdarg.h>

#define RB "rb"

char *fns[] = 
{
        "j    ",
        "ldlp ",
        "pfix ",
        "ldnl ",
        "ldc  ",
        "ldnlp",
        "nfix ",
        "ldl  ",
        "adc  ",
        "call ",
        "cj   ",
        "ajw  ",
        "eqc  ",
        "stl  ",
        "stnl ",
        "opr  "
};

/* Fix: ebuf was a char [300] filled without a bound by expr1() and	*/
/* label(); a symbol name (up to 1023 characters) or an expression	*/
/* longer than that overflowed it. It grows now.			*/
static char ebuf0[300];
char *ebuf = ebuf0;
int ebufsize = sizeof(ebuf0);

void ebufput(int i, int c)
{
	if( i >= ebufsize )
	{
		int nsize = ebufsize * 2;
		char *n;
		while( i >= nsize ) nsize *= 2;
		n = malloc(nsize);
		if( n == NULL ) { fprintf(stderr,"Error: out of memory\n"); exit(1); }
		memcpy(n,ebuf,ebufsize);
		if( ebuf != ebuf0 ) free(ebuf);
		ebuf = n; ebufsize = nsize;
	}
	ebuf[i] = c;
}
int ch;
FILE *f;
int modnum;
int pos = -1;
int asmable = FALSE;
int numbers = FALSE;
int printstrings = FALSE;

int rdch()
{
	ch = getc(f);
	pos++;
	return ch;
}

/* Host port: real varargs; "unexpected code %x" passed an int where	*/
/* this took a char *.							*/
void error(BYTE *fmt, ...)
{
	va_list ap;
	fprintf(stderr,"Error: ");
	va_start(ap,fmt);
	vfprintf(stderr,fmt,ap);
	va_end(ap);
	fprintf(stderr,"\n");
	exit(1);
}

void expr1()
{
	int i = 0;
	do
	{
		ebufput(i++,ch);
		rdch();
	} while( ch > ' ' && ch < '~' );
	ebufput(i,0);
}

void expr()
{
	rdch();
	expr1();
}

void label()
{
	int i = 0;
	do {
		ebufput(i++,ch);
		rdch();
	} while( ch < 0x80 && ( isalnum(ch) || ch == '.' || ch == '_' ) );
	ebufput(i,0);
}

void showcode(size)
int size;
{
	int i;
	printf("\t\tbyte\t");
	
	if( printstrings )
	{
		int printable = TRUE;
		
		for( i = 0; i < size; i++ )
		{
			if( (' ' <= ebuf[i] &&  ebuf[i] <= '~') ||
				ebuf[i] == '\t' ) continue;
			if( ebuf[i] == 0 && i+1 == size  ) break;
				printable = FALSE;
		}
			
		if( printable )
		{
			ebuf[size] = 0;
			printf("\"%s\"",ebuf);
			if( size > 0 && ebuf[size-1] == 0 ) printf(",0\n");	/* Fix: size 0 */
			else printf("\n");
			return;
		}
	}
	
	for( i = 0; i < size; i++ )
		printf("#%02x%c",ebuf[i],
			(i+1==size)?'\n':',');
}

void objdump(file)
char *file;
{
	int i;
	int size;
		
	f = fopen(file,RB);
	
	if( f == NULL ) error("cannot open %s",file);
	
	rdch();

	modnum = 0;
	
	while( ch != EOF )
	{
		if( numbers && ch != '\n' ) printf("%07x:",pos);
		
		if( 0x80 <= ch && ch <= 0x8f )
		{
			int op = ch&0x0f;
			expr();
			printf("\t\t%s\t%s\n",fns[op],ebuf);
			continue;
		}
		
		if( ch < 0x80 && (isalpha(ch) || ch == '.' || ch == '_') )
		{
			label();
			if( ch == ':' ) printf("%s:\n",ebuf);
			else error("unknown token %s",ebuf);
			rdch();
			continue;
		}
		
		switch( ch )
		{
		case s_align:
			rdch();
			printf("\t\talign\n");
			break;
			
		case s_module:
			expr();
			printf("\t\tmodule\t%s\t\t-- %d\n",ebuf,modnum++);
			break;
			
		case s_code:
			size = rdch();
			/* Fix: a CODE record cut off at the end of the	*/
			/* file passed size -1 (EOF) on; showcode() read	*/
			/* ebuf[-1] for size 0 or -1. asm never writes an	*/
			/* empty record.					*/
			if( size == EOF ) error("unexpected end of file in a CODE record");
			for( i = 0; i < size ; i++ )
			{
				ebuf[i]	= rdch();
			}
			if( asmable ) showcode(size);
			else printf("\t\tcode\t%d\n",i);
			rdch();
			break;
			
		case s_bss:
			i = rdch();
			printf("\t\tblkb\t%d\n",i);
			rdch();
			break;

		case s_word:
			expr();
			printf("\t\tword\t%s\n",ebuf);
			break;

		case s_init:
			rdch();
			printf("\t\tinit\n");
			break;

		case s_data:
			rdch();
			label();
			printf("\t\tdata\t%s ",ebuf);
			expr();
			printf("%s\n",ebuf);
			break;
						
		case s_common:
			rdch();
			label();
			printf("\t\tcommon\t%s ",ebuf);
			expr();
			printf("%s\n",ebuf);
			break;
					
		case s_global:
			rdch();
			label();
			printf("\t\tglobal\t%s\n",ebuf);
			break;
			
		case s_ref:
			rdch();
			label();
			printf("\t\tref\t%s\n",ebuf);
			break;
			
		case s_size:
			expr();
			printf("\t\tsize\t%s\n",ebuf);
			break;

		case '\n':
			rdch();
			break;
						
		default:
			error("unexpected code %x",ch);
		}
	}
	
	fclose(f);
}


int main(argc,argv)
int argc;
char **argv;
{
	if( argc < 2 )
	{
		fprintf(stderr,"usage: %s file...\n",argv[0]);
		exit(1);
	}

	argv++;
	
	while( *argv )
	{
		char *arg = *argv;
		if( *arg == '-' )
		{
			while( *++arg ) switch( *arg )
			{
			case 'a':
				asmable = TRUE;
				break;

			case 'n':
				numbers = TRUE;
				break;
							
			case 's':
				printstrings = TRUE;
				break;
				
			default: ;
			}
		}
		else objdump(arg);
		argv++;
	}	
	return 0;	/* Host port: was falling off the end */
}
