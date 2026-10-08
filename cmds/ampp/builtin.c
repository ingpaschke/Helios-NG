/****************************************************************/
/* File: builtin.c                                              */
/*                                                              */
/*                                                              */
/* Author: NHG 19-Feb-87                                        */
/****************************************************************/
#ifdef __TRAN
static char *RcsId = "$Id: builtin.c,v 1.10 1994/03/08 13:05:34 nickc Exp $ Copyright (C) Perihelion Software Ltd.";
#endif
  
#include "ampp.h"
#include <signal.h>	/* Host port: raise() */
#include <string.h>
  
#define trace if(traceflags&db_builtin)_trace
#define file_trace if (traceflags & db_fileop) _trace


PRIVATE struct Symbol *truesym;

#define TRUESTRING "_true"
#define FALSESTRING "_false"

/********************************************************/
/* initbuiltin                                          */
/*                                                      */
/* initialise builtin functions                         */
/*                                                      */
/********************************************************/


/********************************************************/
/* builtin                                              */
/*                                                      */
/* The current symbol has been identified as a builtin  */
/* function, call it.                                   */
/*                                                      */
/********************************************************/

PUBLIC void builtin()
{
        jmp_buf errlev;
        jmp_buf oldreclev;

	memcpy(oldreclev, reclev, sizeof(jmp_buf));

        if( setjmp(errlev) )
        {
                while ( symb != s_nl && symb != s_eof ) nextsym();
        }
        else {
                memcpy(reclev, errlev, sizeof(jmp_buf));
		(*(tokdef->Value.builtin))();
        }
	memcpy(reclev, oldreclev, sizeof(jmp_buf));
}

/********************************************************/
/* getargs                                              */
/*                                                      */
/* Read a formal argument spec, the left bracket has    */
/* been read, we now expect a list of tokens defining   */
/* the argument names.                                  */
/* If a formal argument is preceeded by a number of	*/
/* quotes, this is recorded and when the actual arg is	*/
/* collected during evaluation that number of quotes    */
/* are pushed back before collection. This allows the	*/
/* definition of the macro to control the evaluation of */
/* actual arguments. 					*/
/*                                                      */
/********************************************************/

PRIVATE void getargs(struct Macro *macro)
{
        INT nargs = 0;
        INT quoted;
        struct Arg *arg;
        struct Symbol *sym;

        InitList(&(macro->arglist));

        for(;;) 
        {
                skipspaces();               /* on to next symbol */
                if( symb == s_rbra ) break; /* right bracket ends args */

		quoted = 0;
		while( symb == s_quote )
		{
			quoted++;
			nextsym();
		}

                if( symb > s_token )
                        recover("Invalid symbol in argument list %s",token);


                sym = insert(token);  /* get symbol table node */

                arg = New(struct Arg);

                arg->sym = sym;
                arg->quoted = quoted;

                AddTail(&(macro->arglist),(Node *)arg);

                nargs++;
        }
        macro->nargs = nargs;
}

/********************************************************/
/* getdef                                               */
/*                                                      */
/* get a single item from the input and transfer it into*/
/* a charbuf.                                           */
/* getdef1 gets a def after skipping spaces.            */
/* getdef1q quotes the def after skipping spaces.       */
/*                                                      */
/********************************************************/

PUBLIC void getdef(struct List *queue)
{
        struct List *oldout = outbuf;
        InitList(queue);
        outbuf = queue;
        parse(0l);
        outbuf = oldout;
}

PRIVATE void getdef1(struct List *def)
{
        pbskipspaces();
        getdef(def);
}

PRIVATE void getdef1q(struct List *def)
{
        pbskipspaces();
        pbchar(c_quote);
        getdef(def);
}

/********************************************************/
/* evalexp                                              */
/* evalcond                                             */
/* geneval                                              */
/*                                                      */
/* geneval parses the next item from the input, pushes  */
/* it back and then reads a single token which is the	*/
/* value of the expression. 				*/
/* evalexp checks that the token is a numeral, and 	*/
/* returns its numeric value.				*/
/* evalcond checks for a textual token and returns	*/
/* true if it is TRUESTRING and false otherwise.	*/
/*                                                      */
/********************************************************/

PRIVATE void geneval()
{
        struct List def;

        getdef1(&def);                  /* parse it into def */
        pbdef(&def);                    /* replace it in input */
        skipspaces();			/* throw out white space & get next sym */
        freebuf(&def);                  /* free char buffer */
}

PRIVATE INT evalexp()
{
        geneval();
        if( symb < s_number ) recover("value of expression not numerical");
        return tokval;
}

PRIVATE INT evalcond()
{
        geneval();
        if( symb > s_token ) recover("value of expression not a token: %s",token);
        return (toksym==truesym);
}

/********************************************************/
/* define                                               */
/* defq                                                 */
/*                                                      */
/* Define a macro                                       */
/* defq prevents the defining text being parsed for     */
/* macros as it is being read                           */
/*                                                      */
/********************************************************/

PRIVATE void define(INT isdefq )
{
        struct Symbol *sym;
        struct Macro *macro = New(struct Macro);

        geneval();

        if( symb > s_token ) recover("Token expected in 'def'");

        trace("Defining macro '%s'",token);

        sym = insert(token);			/* get symbol table node */

        /* if the name is followed immediately by a bracket it has some args */

        nextsym();
        
        if( symb == s_lbra ) getargs(macro);	/* get any formal arguments */
        else macro->nargs = 0;

        pbskipspaces();				/* hop over spacing */

        if( isdefq ) pbchar(c_quote);		/* if call is defq, quote the defn */

        getdef(&(macro->def));			/* get the following item */

        adddef((WORD)s_macro,sym,(intptr_t)macro); 	/* add it to the definition stack for sym */
						/* Host port: was (INT)macro */
}

PRIVATE void def() { define(FALSE); }

PRIVATE void defq() { define(TRUE); }

/********************************************************/
/* _if                                                  */
/* _test                                                */
/*							*/
/* _if evaluates its first argument, if it evaluates to */
/* false then the second argument is skipped.		*/
/* _test evaluates either its second or third argument	*/
/* depending on the value of the first.			*/
/*                                                      */
/********************************************************/

PRIVATE void ifthen()
{
        if( !evalcond() )             /* dispose of next item */
        {
                struct List def;
                getdef1q(&def);                 /* parse it into def */
                freebuf(&def);                  /* and get rid of it */
        }
        else pbskipspaces();
}

PRIVATE void test()
{
        struct List thendef;
        struct List elsedef;
        
        if( evalcond() )
        {       /* then part - save it and then dispose of else part */
                getdef1q(&thendef);
                getdef1q(&elsedef);
                freebuf(&elsedef);
                pbdef(&thendef);
                freebuf(&thendef);
        }
        else { /* else part - skip over then part */
                getdef1q(&thendef);
                freebuf(&thendef);
                pbskipspaces();
        }
}

/********************************************************/
/* defp                                                 */
/*                                                      */
/* Generates '_true' if the given symbol is currently   */
/* defined, '_false' otherwise.                         */
/*                                                      */
/********************************************************/

PRIVATE void defp()
{
        INT exists;
        geneval();

        if( symb > s_token ) recover("Invalid symbol in '_defp'");

        exists = symb < s_token && tokdef != NULL;

        if( exists ) pbstr(TRUESTRING);
        else pbstr(FALSESTRING);
}

/********************************************************/
/* _compare						*/
/*							*/
/* Compare two items. This is not a straight byte-by-	*/
/* byte comparison. All non-space characters must agree */
/* but any whitespace characters (space,tab,nl) are	*/
/* reduced to a single space. Leading and trailing	*/
/* spaces are ignored.					*/
/* The result of _compare is either -1, 0 or 1 showing	*/
/* whether  l < r , l == r or l > r.			*/
/* This result can be used with the relational ops to	*/
/* test any desired property.				*/
/* e.g.							*/
/*	_if[ _eq 0 _compare x y ] [ 'x == 'y ]		*/
/*	generates "x == y" if x and y are equal		*/
/*							*/
/********************************************************/

typedef struct {
	List		def;
	struct Charbuf	*buf;
	int		pos;
	int		size;
	char		*text;
} defpos;

static WORD nextchar(defpos *x);

PRIVATE void compare()
{
	defpos left, right;
	INT result = 0;
	char lch = c_space, rch = c_space;

	getdef1(&left.def);
	getdef1(&right.def);

	left.buf = (struct Charbuf *)(&left.def);
	right.buf = (struct Charbuf *)(&right.def);
	left.pos = left.size = 0;
	right.pos = right.size = 0;

	while( result == 0 && lch != 0 && rch != 0 )
	{
		/* for both sides, if the last char we saw was a space,	*/
		/* chuck out spaces until we get something else.	*/
		/* Intialising lch and rch to c_space allows us to kill	*/
		/* leading spaces here.					*/
		if( lch == c_space )
			while( lch == c_space ) lch = (char) nextchar(&left);
		else lch = (char) nextchar(&left);
		
		if( rch == c_space )
			while( rch == c_space ) rch = (char) nextchar(&right);
		else rch = (char) nextchar(&right);

		result = (INT)lch - (INT)rch;
	}

	/* if one side ran out of chars while the other was in whitespace */
	/* see if these are trailing spaces, if so let the compare	  */
	/* succeed.							  */
	if( lch == 0 && rch == c_space )
	{
		while ( rch == c_space ) rch = (int)nextchar(&right);
		result = (INT)lch - (INT)rch;
	}
	else if( rch == 0 && lch == c_space )
	{
		while( lch == c_space ) lch = (int)nextchar(&left);
		result = (INT)lch - (INT)rch;
	}
	
	/* force the result to -1, 0, or 1				*/
	result = (result<0)?-1:((result>0)?1:0);
	
	pbnum(result);
	
	freebuf(&left.def);
	freebuf(&right.def);
}

static WORD nextchar(defpos *x)
{
	char c;
	if( x->pos == x->size )
	{
		x->buf = (struct Charbuf *)x->buf->node.Next;
		if( x->buf->node.Next == NULL ) return 0;
		x->pos = 0;
		x->text = x->buf->text;
		x->size = (int) x->buf->size;
	}
	c = x->text[x->pos++];
	if( c == c_space || c == c_tab || c == c_nl ) c = c_space;
	return c;
}	

/********************************************************/
/* include                                              */
/*                                                      */
/* Include the given file into the text at this point.  */
/* The name of the current input file is available as	*/
/* _file.						*/
/*                                                      */
/********************************************************/

/*
 * Host port: the text of a def as one malloc'd string.  A def is a stack
 * of Charbufs of charbuf_max (256) bytes, the newest at the head; include
 * and _report used the head buffer only and wrote the NUL one byte past
 * it when it was full.  For a text of fewer than 256 bytes the string is
 * the same.
 */
PRIVATE char *defstring(struct List *def)
{
        struct Charbuf *buf;
        size_t len = 0;
        char *str, *p;

        for( buf = (struct Charbuf *)(def->Tail); buf->node.Prev != NULL;
             buf = (struct Charbuf *)(buf->node.Prev) )
                len += buf->size;

        p = str = alloc((INT)len + 1);

        for( buf = (struct Charbuf *)(def->Tail); buf->node.Prev != NULL;
             buf = (struct Charbuf *)(buf->node.Prev) )
        {
                memcpy(p, buf->text, (size_t)buf->size);
                p += buf->size;
        }
        *p = '\0';
        return str;
}

/*
 * Host port: incdir + "/" + name in a malloc'd string, was a char[128]
 * (an include directory and name of 127 characters or more overflowed
 * it).  The '/' is always added, as before: the old test
 * path[strlen(path)] != '/' looked at the NUL.
 */
PUBLIC char *incpath(char *dir, char *name)
{
        char *path = alloc((INT)(strlen(dir) + strlen(name) + 2));

        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, name);
        return path;
}

PRIVATE void include()
{
        jmp_buf errlev;
        jmp_buf oldreclev;

        struct List		filebuf;
        char *			fname;	/* Host port: defstring(), was the head Charbuf */
        FILE *			oldin   = infd;
	INT			oldline = in_line;
	char *			oldfile = infile;

	memcpy(oldreclev, reclev, sizeof(jmp_buf));

        getdef1(&filebuf);
        fname = defstring(&filebuf);

	file_trace("Include: %s",fname);

        infd = fopen(fname,"r");
	
	if( infd == NULL)
	{
	  int	i;
	  
	  for (i = MAX_INCLUDE_PATHS; i--;)
	    {
	      char *path;

	      if (incpaths[ i ] == NULL)
		continue;
	      
	      path = incpath(incpaths[i], fname);
	      file_trace("FAILED trying: %s", path);
	      infd = fopen(path, "r");
	      free(path);

	      if (infd != NULL)
		break;
	    }
	}

        if ( infd == NULL ) error( "Cannot open %s for input", fname );
        else
	  {
	    /* Host port: volatile, it is live across the longjmp to errlev */
	    BYTE * volatile	pbsave = NULL;

	    
	    file_trace( "Succeded" );
	    
	    if ( setjmp(errlev) == 0 )
	      {
		WORD nesting = 0;

		infile  = fname;
		in_line = 1;
		pbsave  = savepb();
		
		memcpy (reclev, errlev, sizeof(jmp_buf));

		while( symb != s_eof ) nesting = parse(nesting);
		
		if( nesting != 0 ) warn("Mismatched brackets at EOF");
	      }

	    setpb(pbsave);
	    memcpy(reclev, oldreclev, sizeof(jmp_buf));
	    /* file_trace("Close %d", infd); */
	    fclose(infd);
	  }

        freebuf(&filebuf);

	infd    = oldin;
	infile  = oldfile;
	in_line = oldline;
        symb    = 0;
	free(fname);
}

PRIVATE void file()
{
	pbstr(infile);
}

/********************************************************/
/* undefine                                             */
/*                                                      */
/* Remove the last definition of the given symbol to    */
/* reveal its previous def, if any.                     */
/*                                                      */
/********************************************************/

PRIVATE void undefine()
{
        geneval();

        if( symb >= s_token )
                recover("Invalid or undefined symbol in _undef: '%s'",token);

        unwind(toksym);
}

/********************************************************/
/* evaluate						*/
/*							*/
/* Force evaluation of the argument.			*/
/* This causes argument to be passed through the parser */
/* one more time than it would if it were not preceeded */
/* by _eval. This is of use in some macros where macro	*/
/* names have been constructed and need to be evaluated.*/
/* This used to be a macro, but it is so useful that it	*/
/* has been made a buitin.				*/
/*							*/
/********************************************************/

PRIVATE void evaluate()
{
	struct List def;
	
	getdef1(&def);	/* this evaluates the argument once */
	
	pbdef(&def);	/* and put it back to evaluate again */
	
	freebuf(&def);
}

/********************************************************/
/* rep                                                  */
/* repdef                                               */
/*                                                      */
/* Debugging aids.                                      */
/* _report simply output the next item to the error	*/
/* channel.						*/
/* _repdef outputs the defining text of the supplied	*/
/* macro name to the error channel.			*/
/*                                                      */
/********************************************************/

PRIVATE void rep()
{
        struct List textbuf;

        getdef1(&textbuf);

	if (textbuf.Head == (Node *)(&(textbuf.Earth)))
	  {
	    warn( "_report encountered with no text to report" );
	  }
	else
	  {
	    char *text = defstring(&textbuf);	/* Host port: was the head Charbuf */

	    report("%s", text);	/* Host port: "%s", the text was the format */
	    free(text);
	  }
	
        freebuf(&textbuf);
}

PRIVATE void repdef()
{
        struct List *deflist;
        struct Charbuf *buf;
        geneval();

        if( symb != s_macro ) recover("Macro name required");

        deflist = &(((toksym->definition)->Value.macro)->def);
        buf = (struct Charbuf *)(deflist->Tail);

        report("Text of macro '%s' is:",token);
        while( buf->node.Prev != NULL )
        {
                int i;
                for( i = 0; i < buf->size ; i++ ) putc(buf->text[i],verfd);
                buf = (struct Charbuf *)(buf->node.Prev);
        }
        report("\n...");

}

/********************************************************/
/* set                                                  */
/*                                                      */
/* Assign the value of a numeric variable.		*/
/* Variables are either s_var or s_hexvar, they both	*/
/* behave the same in expressions, but print in either  */
/* decimal or hex respectively when output. A variable  */
/* is set to hex by assigning a hex number to it, and	*/
/* may only revert to decimal if it is 'undef'ed and	*/
/* re-assigned.						*/
/*                                                      */
/********************************************************/

PRIVATE void set()
{
        struct Symbol *sym;
        struct Def *def;
	WORD vartype = s_var;
	
        geneval();

        if( symb > s_token ) recover("Variable name required");

        sym = insert(token);
        def = tokdef;

        evalexp();                      /* should leave a value in tokval */

	if( symb == s_number+16 ) vartype = s_hexvar;
	
        if( def != NULL && (def->type == s_var || def->type == s_hexvar) )
        {
                def->Value.value = tokval;
                def->type = max(def->type,vartype);
        }
        else adddef(vartype,sym,tokval);
}

/********************************************************/
/* add                                                  */
/* sub                                                  */
/* mul                                                  */
/* div                                                  */
/* mod                                                  */
/*                                                      */
/* Arithmetic functions.                                */
/* These function implement a simple arithmetic 	*/
/* capability. Each operator consumes the next two items*/
/* from the input and pushes back their combined value.	*/
/* The effect of this is that expressions are in	*/
/* forward polish notation:				*/
/* e.g. a*(b+c) -> _mul a _add b c			*/
/*      (a*b)+c -> _add _mul a b c			*/
/*                                                      */
/********************************************************/

PRIVATE void arith(int op )			/* note: this is a native integer */
{
        INT result = 0;
        INT left, right;
	INT ltype, rtype;
	
        left  = evalexp(); ltype = symb;
        right = evalexp(); rtype = symb;

	/* Host port: x86 (the i386 reference) traps on a division by	*/
	/* zero and on MinInt / -1, AArch64 does not: raise the signal	*/
	/* on every host							*/
	if( (op == s_div || op == s_mod) &&
	    (right == 0 || (right == -1 && left == (INT)MinInt)) )
		raise(SIGFPE);

        switch( op )
        {
        case s_plus : result = left + right; break;
        case s_minus: result = left - right; break;
        case s_mul  : result = left * right; break;
        case s_div  : result = left / right; break;
        case s_mod  : result = left % right; break;
        }

	if( (ltype==rtype) && ltype==s_number+16 ) pbhex(result);
        else pbnum(result);
}

PRIVATE void add() { arith(s_plus); }
PRIVATE void sub() { arith(s_minus); }
PRIVATE void mul() { arith(s_mul); }
PRIVATE void div2() { arith(s_div); }
PRIVATE void mod() { arith(s_mod); }

/********************************************************/
/* gt, lt, ge, le, eq, ne				*/
/*							*/
/* Relational operators, take two numeric items and	*/
/* push back either TRUESTRING or FALSESTRING.		*/
/*							*/
/********************************************************/

PRIVATE void relation(int op)
{
        INT result = 0;
        INT left, right;

        left = evalexp();
        right = evalexp();

        switch( op )
	{
	case 0	: result = left > right; break;
	case 1	: result = left < right; break; 
	case 2	: result = left >= right; break; 
	case 3	: result = left <= right; break; 
	case 4	: result = left == right; break; 
	case 5	: result = left != right; break; 
	}
	
	pbstr(result ? TRUESTRING : FALSESTRING);
}

PRIVATE void gt() { relation(0); }
PRIVATE void lt() { relation(1); }
PRIVATE void ge() { relation(2); }
PRIVATE void le() { relation(3); }
PRIVATE void eq() { relation(4); }
PRIVATE void ne() { relation(5); }

PRIVATE void hex()
{
        pbhex(evalexp());
}


PRIVATE void strsize()
{
        struct List textbuf;
        struct Charbuf *text;
        INT size = 0;
	
        getdef1(&textbuf);
        /* Host port: the size of every Charbuf, was the head one's only  */
        /* (a longer text gave the size of its last 256-byte piece), 0 for */
        /* an empty text, where the list header was read as a Charbuf     */
        for( text = (struct Charbuf *)(textbuf.Tail); text->node.Prev != NULL;
             text = (struct Charbuf *)(text->node.Prev) )
                size += text->size;
	pbnum(size);

        freebuf(&textbuf);
}

/********************************************************/
/* not, and, or						*/
/*							*/
/* Boolean operators. 					*/
/*							*/
/********************************************************/

PRIVATE void not()
{
        pbstr( evalcond() ? FALSESTRING : TRUESTRING );
}

PRIVATE void and()
{
	WORD left = evalcond();	
	WORD right = evalcond();

	pbstr( left && right ? TRUESTRING : FALSESTRING );
}

PRIVATE void or()
{
	WORD left = evalcond();	
	WORD right = evalcond();
	
	pbstr( left || right ? TRUESTRING : FALSESTRING );
}

/********************************************************/
/* car							*/
/* cdr							*/
/*							*/
/* _car replaces the next input item with the first 	*/
/* component of it.					*/
/* _cdr is replaced by the rest of the item after the	*/
/* first is removed.					*/
/* Just like the LISP primitives.			*/
/* e.g. _car [ a b c d ]		-> a		*/
/*      _cdr [ a b c d ]		-> [b c d]	*/
/*      _car [ [a b] [b c] [d e] ]      -> [a b]	*/
/* 	_cdr [ [a b] [b c] [d e] ]      -> [b c] [d e]	*/
/*	_car _cdr [ [a b] [b c] [d e] ] -> [b c]	*/
/*							*/
/********************************************************/

PRIVATE void carcdr(int iscar )
{
	List def;
	List cardef;
	
	getdef1(&def);		/* parse next item into def */
	
	pbchar(c_rbra);		/* push a terminating bracket	*/
	pbdef(&def);		/* place parsed def back on input */
	
	getdef1q(&cardef);	/* get the car without parsing it */
	
	pbchar(c_lbra);		/* close the brackets around cdr */
	
	freebuf(&def);		/* reuse this */
	getdef1q(&def);		/* get cdr without parsing it */

	if( iscar)
	{	
		freebuf(&def);		/* junk cdr */
		pbdef(&cardef);		/* push car back */
	}
	else {
		freebuf(&cardef);	/* junk car */
		pbdef(&def);		/* push cdr back */
	}
}

PRIVATE void car() { carcdr(TRUE); }

PRIVATE void cdr() { carcdr(FALSE); }

PUBLIC void initbuiltin()
{
	/* Host port: the builtin functions are passed as (intptr_t), not	*/
	/* (INT), which is too narrow for a pointer on LP64			*/
	/* Macro definition and evaluation */
        adddef((WORD)s_builtin,insert("_def"),  (intptr_t) def);
        adddef((WORD)s_builtin,insert("_defq"), (intptr_t) defq);
        adddef((WORD)s_builtin,insert("_undef"),(intptr_t) undefine);
        adddef((WORD)s_builtin,insert("_eval"), (intptr_t) evaluate);
        adddef((WORD)s_builtin,insert("_set"),  (intptr_t) set);

	/* conditionals */
        adddef((WORD)s_builtin,insert("_if"),  (intptr_t) ifthen);
        adddef((WORD)s_builtin,insert("_test"),(intptr_t) test);
        adddef((WORD)s_builtin,insert("_defp"),(intptr_t) defp);
        adddef((WORD)s_builtin,insert("_compare"), (intptr_t) compare);
	
	/* file inclusion */
        adddef((WORD)s_builtin,insert("_include"),(intptr_t) include);
	adddef((WORD)s_builtin,insert("_file"),(intptr_t) file);

	/* arithmetic */
        adddef((WORD)s_builtin,insert("_add"),(intptr_t) add);
        adddef((WORD)s_builtin,insert("_sub"),(intptr_t) sub);
        adddef((WORD)s_builtin,insert("_mul"),(intptr_t) mul);
        adddef((WORD)s_builtin,insert("_div"),(intptr_t) div2);
        adddef((WORD)s_builtin,insert("_mod"),(intptr_t) mod);
        
       	/* radix conversion */
        adddef((WORD)s_builtin,insert("_hex"),(intptr_t) hex);

       	/* string processing */
        adddef((WORD)s_builtin,insert("_strsize"),(intptr_t) strsize);

	/* relationals */
	adddef((WORD)s_builtin,insert("_gt"),(intptr_t) gt);
	adddef((WORD)s_builtin,insert("_lt"),(intptr_t) lt);
	adddef((WORD)s_builtin,insert("_ge"),(intptr_t) ge);
	adddef((WORD)s_builtin,insert("_le"),(intptr_t) le);
	adddef((WORD)s_builtin,insert("_eq"),(intptr_t) eq);
	adddef((WORD)s_builtin,insert("_ne"),(intptr_t) ne);

	/* booleans */
        adddef((WORD)s_builtin,insert("_not"),(intptr_t) not);
	adddef((WORD)s_builtin,insert("_and"),(intptr_t) and);
	adddef((WORD)s_builtin,insert("_or"), (intptr_t) or);
	
	/* list manipulation */
	adddef((WORD)s_builtin,insert("_car"),(intptr_t) car);
	adddef((WORD)s_builtin,insert("_cdr"),(intptr_t) cdr);
	
	/* debugging aids */
	adddef((WORD)s_builtin,insert("_report"),(intptr_t) rep);
	adddef((WORD)s_builtin,insert("_repdef"),(intptr_t) repdef);

	/* standard tokens */
        truesym = insert(TRUESTRING);
        insert(FALSESTRING);
}
