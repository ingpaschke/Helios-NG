/************************************************************************/
/*                                                                      */
/* File: preasm.c                                                       */
/*                                                                      */
/* Changes:                                                             */
/*      NHG  20-Jun-87  : Created                                       */
/*                                                                      */
/* Description:                                                         */
/*      Generate pre-assembly output from the code vector               */
/*                                                                      */
/*                                                                      */
/* Copyright (c) 1987, Perihelion Software Ltd. All Rights Reserved.    */
/************************************************************************/
/* $Id: preasm.c,v 1.3 90/09/28 13:20:54 nick Exp $ */

#include "asm.h"

#define trace if(traceflags&db_preasm)_trace

extern WORD etype;
extern WORD DataSize;
extern UBYTE *codevec;
extern WORD codepos;
extern WORD symb;
extern Value tokval;
extern WORD lineno;

static void wrch(ellipsis);

/* Fix: > 0 while preval() prints the operands of an expression node */
static int inexpr = 0;
static void preval(ellipsis);
static void predyadic(ellipsis);
static void premonadic(ellipsis);

/****************************************************************/
/* parsepreasm                                                  */
/*                                                              */
/* convert preasm bytes into lexical tokens                     */
/*                                                              */
/****************************************************************/

PUBLIC void parsepreasm(ch)
WORD ch;
{
        trace("Parsepreasm %2x",ch);
        /* Fix: a byte >= 0x80 outside a string or comment is a .p	*/
        /* token. Only the codes below occur in .p files: direct	*/
        /* functions, CODE and BSS records, and the directives. Any	*/
        /* other byte (binary garbage, or a stray non-ASCII character	*/
        /* in a .s file) became a symbol type that the parser did not	*/
        /* expect: directive codes 0xe0 and 0xed-0xff made asm loop	*/
        /* for ever, symbol codes used stale token	*/
        /* data. It is an error now.					*/
        /* Fix: and in text input (.s) no byte >= 0x80 is a token:	*/
        /* a stray UTF-8 character such as the euro sign (e2 82 ac)	*/
        /* was read as a directive with binary operands.		*/
        if( textinput ||
            !( (s_direct <= ch && ch <= (s_direct+15)) ||
               ch == s_code || ch == s_bss ||
               (s_blkb <= ch && ch <= s_ref) ) )
        {
                symb = s_other;
                token[0] = '\0';
                recover("Illegal character #%02x in input",(int)ch);
        }
        if( s_direct <= ch && ch <= (s_direct+15) )
        {
                symb = s_direct;
                tokval.w = (ch & 0x0f)<<4;
                return;
        }
        if( ch < s_firstdir ) symb = ch;
        else { symb = s_directive; tokval.w = ch; }
}

/****************************************************************/
/* Procedure: genpreasm                                         */
/* Description:                                                 */
/*      pre-assembler code generator                            */
/*                                                              */
/****************************************************************/

PUBLIC void genpreasm()
{
        WORD pc = 0;
        int i;
	Code *c;
	Symbol *s;
	asm_Module *m;
	VMRef curblock;

	curmod = module0;
	m = VMAddr(asm_Module,curmod);
	curblock = m->start;
	
	VMlock(curblock);

	code = VMAddr(Code,curblock);

        lineno = 1;
        codepos = 0;

	for(;;)
        {
                WORD tag = (c = code)->type;

trace("preasm c = %p %x %x %x",(void *)c,tag,c->vtype,VMWord(c->value.v));
		if( tag == s_newseg ) 
		{
	    		VMunlock(curblock);
			curblock = c->value.v;
		    	VMlock(curblock);
	    		code = VMAddr(Code,curblock);
		    	continue;
		}

	        if( tag == s_end )
		{
			VMunlock(curblock);
			m = VMAddr(asm_Module,curmod);
	                curmod = m->next;
	                if( NullRef(curmod) ) break;

			m = VMAddr(asm_Module,curmod);
			curblock = m->start;
			VMlock(curblock);
			code = VMAddr(Code,curblock);

	                continue;
		}

#ifdef LINENO
                while( lineno < c->line ) /* preserve line numbering */
                {
                        wrch('\n');
                        lineno++;
                }
#endif
/*                if( pc != c->loc ) error("Phase error in Preasm");*/

                if( 0 <= tag && tag <= 15 )     /* a direct operation */
                {
                        trace("%x8: Direct %2x",pc,tag);
                        wrch((UBYTE)tag+s_direct);
                        preval((WORD)c->vtype,c->value.w);
                        goto next;
                }

                switch( (int)tag )
                {
                case s_module:
                        trace("%8x: MODULE",pc);
                        wrch((UBYTE)s_module);
                        m = VMAddr(asm_Module,curmod);
                        fprintf(outfd,"%d",m->id);
			break;
			
                case s_labdef:
			trace("%8x: LABDEF %x",pc,c->value.w);
                        wrch('\n');
                        preval((WORD)s_codesymb,c->value.w);
                        wrch(':');
                        break;

                case s_bss:
                        trace("%8x: BSS %d",pc,c->size);
                        wrch((UBYTE)s_bss); wrch((UBYTE)c->size);
                        break;

                case s_literal:
                        trace("%8x: CODE %d",pc,c->size);
                        wrch((UBYTE)s_code); wrch((UBYTE)c->size);
                        for( i = 0; i < c->size ; i++ )
                                wrch(((UBYTE *)(&c->value.w))[i]);
                        break;

                case s_code:
                {
                	UBYTE *v = VMAddr(UBYTE,c->value.v);
                        trace("%8x: CODE %d",pc,c->size);
                        wrch((UBYTE)s_code); wrch((UBYTE)c->size);
                        for( i = 0; i < c->size ; i++ ) wrch(v[i]);
                        break;
		}
		
                case s_init:
                        trace("%8x: INIT",pc);
                        wrch((UBYTE)s_init);
                        break;

                case s_word:
                {
                        wrch((UBYTE)s_word);
                        trace("%8x: WORD",pc);
                        preval((WORD)c->vtype,c->value.w);
                        break;

                case s_align:
                        wrch((UBYTE)s_align);
                        trace("%8x: ALIGN",pc);
                        break;

                case s_global:
			trace("%8x: GLOBAL",pc);
                        wrch((UBYTE)s_global);
                        preval((WORD)c->vtype,c->value.w);
                        break;

                case s_data:
			trace("%8x: DATA",pc);
                        wrch((UBYTE)s_data);
                        s = VMAddr(Symbol,c->value.v);
                        preval((WORD)c->vtype,c->value.w);
                        fprintf(outfd," %d",s->def.w);
                        break;

                case s_common:
			trace("%8x: COMMON",pc);
                        wrch((UBYTE)s_common);
                        s = VMAddr(Symbol,c->value.v);
                        preval((WORD)c->vtype,c->value.w);
                        fprintf(outfd," %d",s->def.w);
                        break;
                }
		case s_size:
			trace("%8x: SIZE",pc);
			wrch((UBYTE)s_size);
			fprintf(outfd,"%d",c->value.w);
			break;

                case s_ref:
			trace("%8x: REF",pc);
                        wrch((UBYTE)s_ref);
                        preval((WORD)c->vtype,c->value.w);
                        break;

                }

        next:
                pc += c->size;
                code++;
        }
}

PRIVATE void wrch(ch)
char ch;
{
#ifdef MWC
        binputc(ch,outfd);
#else
        putc(ch,outfd);
#endif
}

PRIVATE void preval(type,earg)
WORD type,earg;
{
    Value expr;
    Symbol *s;
        
    expr.w = earg;

    if( type == s_unbound )
    {
	s = VMAddr(Symbol,expr.v);
	etype = s->type;
    }
    else etype = type;
    
    trace("PrEval %2x %8x",etype,earg);

    switch( (int)etype )
    {
    case s_number:      /* just a number */
        /* Fix: a negative number inside an expression is printed as	*/
        /* (0-N): "x - -4" came out as (x--4), where -- starts a	*/
        /* comment, and "(-4+x)" read back as -(4+x), as unary minus	*/
        /* takes a whole expression. The reader folds	*/
        /* (0-N) back into the number -N. At the top level of an	*/
        /* operand -N reads back correctly and is kept as it was.	*/
        if( inexpr && expr.w < 0 )
            fprintf(outfd,"(0-%u)",(unsigned)(0 - (UWORD)expr.w));
        else
            fprintf(outfd,"%d",expr.w);
        return;

    case s_at:
        wrch('@');

    case s_modnum:
    case s_unbound:
    case s_label:
    case s_datasymb:
    case s_commsymb:
    case s_coderef:
    case s_codesymb:    /* label  */
	s = VMAddr(Symbol,expr.v);
        fprintf(outfd,"%s",s->name);
        return;

    case s_expr:        /* top of an expression tree     */
    {
    	Bnode *b = VMAddr(Bnode,expr.v);
    	WORD ntype = b->ntype;
    	WORD rtype = b->rtype;
    	WORD rexp = b->rexp.w;
trace("expr l %x %x r %x %x",b->ltype,b->lexp.w,rtype,rexp);
        wrch('(');
        inexpr++;		/* Fix: see s_number */
        preval((WORD)b->ltype,b->lexp.w);
        predyadic(ntype);
        preval(rtype,rexp);
        inexpr--;
        wrch(')');
        return;
    }

    case s_monadic:
    {
    	Unode *u = VMAddr(Unode,expr.v);
    	WORD uetype = u->etype;
    	WORD uexpr = u->expr.w;
        wrch('(');
        premonadic((WORD)u->ntype);
        inexpr++;		/* Fix: see s_number */
        preval(uetype,uexpr);
        inexpr--;
        wrch(')');
        return;
    }
    
    default: recover("Unexpected symbol during tokenisation %x",etype);
    
    }

}

PRIVATE void predyadic(op)
WORD op;
{
    switch( (int)op )
    {
        case s_plus : wrch('+'); return;
        case s_minus: wrch('-'); return;
        case s_mul  : wrch('*'); return;
        case s_div  : wrch('/'); return;
        case s_rem  : wrch('%'); return;
        case s_shl  : wrch('<'); return;
        case s_shr  : wrch('>'); return;
        case s_and  : wrch('&'); return;
        case s_or   : wrch('|'); return;
        case s_xor  : wrch('^'); return;
    }
}

PRIVATE void premonadic(op)
WORD op;
{
    switch ( (int)op )
    {
        case s_minus: wrch('-'); return;
        case s_not  : wrch('~'); return;
    }
}


/*  -- End of preasm.c -- */
