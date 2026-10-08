/****************************************************************/
/* File: lex.c                                                  */
/*                                                              */
/* Lexical analyser                                             */
/*                                                              */
/* Author: NHG 19-Feb-87                                        */
/****************************************************************/
#ifdef __TRAN
static char *RcsId = "$Id: lex.c,v 1.5 1994/03/08 13:05:50 nickc Exp $ Copyright (C) Perihelion Software Ltd.";
#endif
  
#include "ampp.h"
#include <ctype.h>
#include <string.h>	/* Host port: memcpy() */

#define trace if(traceflags&db_lex)_trace

PUBLIC INT symb;
/*
 * Host port: the token grows as needed.  It was a fixed BYTE token[128]
 * filled without a bound: an identifier or number of 128 characters or
 * more overwrote the following globals (on x86-64 the symbol table, and
 * ampp crashed).  Tokens that fit in 128 bytes are handled as before.
 */
PRIVATE BYTE tokinit[128];
PUBLIC BYTE *token = tokinit;
PRIVATE INT toklimit = sizeof tokinit;
PUBLIC struct Def *tokdef;
PUBLIC struct Symbol *toksym;
PUBLIC INT toksize;
PUBLIC INT tokval;

struct Symbol *lookup( BYTE * );
PRIVATE void readnumber(INT ch);

/* append ch to token, keeping room for the terminating NUL */
PRIVATE void tokadd(INT ch)
{
        if ( toksize + 2 > toklimit )
        {
                BYTE *t = (BYTE *)alloc(toklimit * 2);

                memcpy(t, token, (size_t)toksize);
                if ( token != tokinit ) free(token);
                token = t;
                toklimit *= 2;
        }
        token[toksize++] = (int) ch;
}

/********************************************************/
/* nextsym                                              */
/*                                                      */
/* The next token in the input stream is analysed and   */
/* its type returned in symb. The text of the token is  */
/* in token. If it is in the symbol table its Def       */
/* node will be in tokdef and the type will be take from*/
/* it.                                                  */
/*                                                      */
/********************************************************/

PUBLIC void nextsym()
{
        INT ch = rdch();

        toksize = 0;
        if( isalpha(ch) || ch == '_' || 
	    ch == c_dot || ch == '/' )      /* possible first char of a token */
        {
                do {
                        tokadd(ch);
                        ch = rdch();
                } while( isalnum(ch) || ch == '_' || 
			ch == c_dot  || ch == '/' );

                pbchar((UBYTE)ch);
                token[toksize] = '\0';
                toksym = lookup(token);
                if ( toksym != NULL && toksym->definition != NULL )
                {
                        tokdef = toksym->definition;
                        symb = tokdef->type;
                        if( symb == s_var || symb == s_hexvar ) 
                        	tokval = tokdef->Value.value;
                }
                else symb = s_token; /* unknown token, or presently undefined */
                trace("Symbol = '%s' %2x",token,symb);
                return;
        }

        switch ( (int)ch )
        {
        /* this could either be a sign or an operator or a comment */
        case c_minus :
        {
                INT c = rdch();         /* look at next char */
                pbchar((UBYTE)c);       /* put the char right back */

                if( !isdigit(c) )
                {
                        if ( c == c_minus ) {
                                while( (ch = rdch()) != c_nl );
                                symb = s_nl;
                        }
                        else symb = s_minus;
                        break;
                }
                /* otherwise drop through */
        }
        case c_hash:
        case '0': case '1': case '2':
        case '3': case '4': case '5':
        case '6': case '7': case '8':
        case '9':
                readnumber(ch);
                return;
        case c_backslash:
        	symb = s_other;
        	ch = rdch() | 0x80;
        	break;
        case c_lbra  : symb = s_lbra;   break;
        case c_rbra  : symb = s_rbra;   break;
        case c_comma : symb = s_comma;  break;
        case c_space : symb = s_space;  break;
        case c_tab   : symb = s_tab;    break;
        case c_nl    : symb = s_nl;     break;
        case EOF     : symb = s_eof;    break;
        case c_quote : symb = s_quote;  break;
        case c_concat: symb = s_concat; break;
        case c_plus  : symb = s_plus;   break;
        default      : symb = s_other;  break;
        }

        tokadd(ch);
        token[toksize] = '\0';
        trace("Symbol = '%s' %2x",token,symb);
}

/********************************************************/
/* readnumber                                           */
/*                                                      */
/* Read a number from the input. Stops on the first non */
/* digit.                                               */
/*                                                      */
/********************************************************/

PRIVATE void readnumber(INT ch)
{
        INT radix = 10;
        INT val = 0;
        BYTE sign = c_plus;

        if( ch == c_minus )
        {
                sign = (int) ch;
                tokadd(ch);
                ch = rdch();
        }
        else if( ch == c_hash )
        {
                radix = 16;
		tokadd(ch);
                ch = rdch();
        }

        for(;;)
        {
                INT digit;
                INT uch = locase((int)ch);
                if ( '0' <= uch && uch <= '9' ) digit = uch - '0';
                else if (radix == 16 && 'a' <= uch && uch <= 'f') digit = uch - 'a' + 10;
		else if (ch == 'x' && val == 0)
		  {
		    radix = 16;
		    digit = 0;
		  }
                else
		  {
		    break;
		  }
                val = val * radix + digit;
                tokadd(ch);
                ch = rdch();
        }

        pbchar((UBYTE)ch);
        token[toksize] = '\0';

        symb = s_number+radix;
        tokval = (sign==c_minus?-val:val);

        trace("Readnumber %d[%8x]",tokval,tokval);
}

/********************************************************/
/* skipspaces                                           */
/*                                                      */
/* skip over any white space optionally copying it to   */
/* the output.                                          */
/*                                                      */
/********************************************************/

PUBLIC void skipspaces()
{
        do {
                nextsym();
        } while( symb == s_space || symb == s_tab || symb == s_nl );

}

PUBLIC void pbskipspaces()
{
       skipspaces();
       pbstr(token);
}
