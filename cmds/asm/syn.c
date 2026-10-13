/****************************************************************/
/* syn.c                                                        */
/*                                                              */
/* Assembler syntax analyser                                    */
/*                                                              */
/****************************************************************/
/* $Id: syn.c,v 1.4 1991/06/07 07:30:14 nickc Exp nickc $ */

#include "asm.h"

#define trace if(traceflags&db_syn)_trace

PUBLIC WORD lineno;
PUBLIC WORD etype;

PRIVATE WORD exp(ellipsis);
PRIVATE WORD exp1(ellipsis);
PRIVATE WORD term(ellipsis);
PRIVATE WORD tertiary(ellipsis);
PRIVATE WORD secondary(ellipsis);
PRIVATE WORD primary(ellipsis);
PRIVATE WORD combine(ellipsis);

/* Fix: a symbol type that a DATA or COMMON directive redefines: a	*/
/* code label, data or common					*/
#define dupsym(t) ((t) == s_codesymb || (t) == s_datasymb || (t) == s_commsymb)

/********************************************************/
/* assemble                                             */
/*                                                      */
/* main assembler loop                                  */
/*                                                      */
/********************************************************/

PUBLIC void initasm()
{
        /* install special symbols */
        toksymref = insert("modnum",FALSE);
        toksym = VMAddr(Symbol,toksymref);
        toksym->type = s_modnum;
        toksym->def.w = 2;                /* allow up to 256 modules */
	toksym->global = 1;
        VMDirty(toksymref);
}

/********************************************************/
/* assemble                                             */
/*                                                      */
/* main assembler loop                                  */
/*                                                      */
/********************************************************/

PUBLIC void assemble()
{
        jmp_buf oldreclev;
        Value expr;
	
	newfile();

	/* Fix: a file named *.s is text from its first byte */
	{
		size_t l = strlen(infile);
		textinput = l >= 2 && strcmp(infile + l - 2, ".s") == 0;
	}

        nextsym();

	memcpy(oldreclev,error_level,sizeof(jmp_buf));

        if( setjmp(error_level) != 0 )
        {
		int recovering = TRUE;
		while( recovering )
		{
			nextsym();
			switch( symb )
			{
			case s_direct:
			case s_oper1:
			case s_oper2:
			case s_oper3:
			case s_oper4:
			case s_directive:
			case s_code:
			case s_bss:
			case s_labdef:
			/* Fix: stop at the end of the file; an error in	*/
			/* the last statement made asm loop for ever.		*/
			case s_eof:
 				recovering = FALSE;
#ifdef NEVER
			case s_datasymb:
			case s_commsymb:
			case s_codesymb:
			case s_unbound:
			case s_nl:
			case s_semic:
			case s_comma:
			case s_cr:
#endif
				;
			}
		}
        }

        while( symb != s_eof )
        {
                trace("Main loop symbol = %2x %x",symb,tokval.w);
                switch( (int)symb )
                {
                case s_direct:
                {
                        WORD op = tokval.w;
                        expr.w = exp1();
                        gendirect(op,etype,expr.w);
                        break;
                }

                case s_oper1:
                case s_oper2:
                case s_oper3:
                case s_oper4:
                        gencode((UBYTE *)&tokval.w,symb&15L);
                        nextsym();
                        break;


                case s_datasymb:
                case s_commsymb:
			/* Fix: an error outside a library unless -wd */
			if( inlib || (warnflags & warn_dup) )
				warn("Code symbol re-defined as data symbol '%s' - ignored",token);
			else
				linkerror("Data symbol '%s' re-defined as a code label",token);
			nextsym();		
			/* Fix: skip the label's colon, now that an		*/
			/* unexpected ':' is an error			*/
			if( symb == s_colon ) nextsym();
                        break;

                case s_codesymb:
                case s_unbound:
                        if( tokval.w != 0 )	/* Host port: was NULL */
                        {
			    /* Fix: an error outside a library unless -wd: the	*/
			    /* second module's own references went to the first	*/
			    /* module's label. In a library the	*/
			    /* first definition silently wins, as before.	*/
			    if( !inlib )
			    {
				if( warnflags & warn_dup )
					warn("Duplicate label definition '%s' - ignored",token);
				else
					linkerror("Duplicate label definition '%s'",token);
			    }
                        }
                        else {
                            toksym->def.v = deflabel();
                            toksym->type = s_codesymb;
			    toksym->module = curmod;
			    if (!preasm && toksym->referenced) refsymbol_def(toksymref); 
			    VMDirty(toksymref);
                            if( preasm ) genlabdef(toksymref);
                        }
                        nextsym();
                        if( symb == s_colon ) nextsym();
                        else warn("Colon missing - assumed");
                        break;

                case s_labdef:
                        toksymref = insert(token,TRUE);
                        toksym = VMAddr(Symbol,toksymref);
                        toksym->type = s_codesymb;
                        toksym->def.v = deflabel();
                        toksym->module = curmod;
                        VMDirty(toksymref);
			if( preasm ) genlabdef(toksymref);
                        nextsym();
                        break;

                case s_nl:
                        lineno++;
                case s_semic:           /* for compatability */
                case s_comma:
		case s_cr:		/* ignore any CRs which get this far */
                        nextsym();
                        break;

                case s_code:            /* only appears in pre-asm input */
                {
                    WORD n = asm_rdch();
                    int i;
                    trace("P_Code %d",n);
                    for( i = 0 ; i < n ; i++ ) genbyte(asm_rdch());
                    nextsym();
                    break;
                }

                case s_bss:             /* ditto */
                {
                    WORD n = asm_rdch();
                    trace("P_Bss %d",n);
                    genblkb((WORD)s_number,n);
                    nextsym();
                    break;
                }

                case s_directive:

                    switch( (int)tokval.w )
                    {
    
                    case s_byte:
                            do {
                                    nextsym();
                                    if( symb == s_number )
                                    {
					    Value expr;
                                            expr.w = exp();
                                            /* Fix: errors, see asmerror() (were warnings, the	*/
                                            /* item was dropped or truncated, exit 0)		*/
                                            if( etype != s_number )
                                                asmerror("Only numeric expressions or strings allowed in BYTE directive");
                                            else {
                                                if( expr.w > 255 || expr.w < -128 )
                                                   asmerror("Value #%x out of range for BYTE directive",expr.w);
                                                genbyte(expr.w);
                                            }
                                    }
                                    else
                                    {
                                            if( symb == s_string ) gencode((UBYTE *)token,toksize);
                                            else asmerror("Invalid item '%s' in BYTE directive",token);	/* Fix: was a warning */
                                            nextsym();
                                    }
                            } while ( symb == s_comma );
                            break;
    
                    case s_word:
                            do {
                                    expr.w = exp1();
                                    genword(etype,expr.w);
                            } while( symb == s_comma );
                            break;
    
                    case s_blkb:
                    {
			    WORD n;
			    Value e; e.w = exp1();
                            n = e.w;
                            /* Fix: a negative size gave an empty block.	*/
                            if( etype == s_number && n < 0 )
                                asmerror("Negative size %d in BLKB directive",n);
                            while ( symb == s_comma && n > 0 )
                            {
                                    nextsym();
                                    if( symb == s_number )
                                    {
                                            expr.w = exp();
                                            /* Fix: errors, see asmerror() (were warnings, the	*/
                                            /* item was dropped or truncated, exit 0)		*/
                                            if( etype != s_number )
                                                asmerror("Only numeric expressions or strings allowed in BYTE directive");
                                            else {
                                                if( expr.w > 255 || expr.w < -128 )
                                                   asmerror("Value #%x out of range for BYTE directive",expr.w);
                                                genbyte(expr.w);
                                                n--;
                                            }
                                    }
                                    else
                                    {
                                            /* Fix: a string longer than the rest	*/
                                            /* of the block was cut short, silently;	*/
                                            /* an excess number item was	*/
                                            /* already an error.		*/
                                            if( symb == s_string && toksize > n )
                                                asmerror("String '%s' longer than the rest of the BLKB block (%d)",token,n);
                                            if( symb == s_string ) gencode((UBYTE *)token,min(n,toksize));
                                            else asmerror("Invalid item '%s' in BYTE directive",token);	/* Fix: was a warning */
                                            n -= toksize;
                                            nextsym();
                                    }
                            }
                            if( n > 0 ) genblkb((WORD)s_number,n);
                            break;
                    }
    
                    case s_blkw:
                    {
			    WORD n;
			    Value e; e.w = exp1();
                            n = e.w;
                            if( etype == s_number && n < 0 )	/* Fix: A9 */
                                asmerror("Negative size %d in BLKW directive",n);
                            while( symb == s_comma && n > 0)
                            {
                                    expr.w = exp1();
                                    genword(etype,expr.w);
                                    n--;
                            }
                            if( n > 0 ) genblkw((WORD)s_number,n);
                            break;
                    }
    
                    case s_init:
                            geninit();
                            nextsym();
                            break;
    
                    case s_align:
                            genalign();
                            nextsym();
                            break;

                    case s_global:
                            nextsym();
                            if( NullRef(toksymref) )
                            {
                                toksymref = insert(token,FALSE);
                                toksym = VMAddr(Symbol,toksymref);
                                toksym->type = s_unbound;
				VMDirty(toksymref);
                            }
                            else movesym(toksymref);
                            genglobal(toksymref);
                            nextsym();
                            break;

                    case s_data:
                    {
                            VMRef sym;
                            nextsym();
                            if( NullRef(toksymref) )
                            {
                                toksymref = insert(token,TRUE);
                                toksym = VMAddr(Symbol,toksymref);
                                toksym->type = s_datasymb;
                                toksym->module = curmod;
                                VMDirty(toksymref);
                            }
                            else /* it has already been referenced */
                                if ( toksym->type == s_unbound ) {
                                        toksym->type = s_datasymb;
					toksym->module = curmod;
					if (!preasm && toksym->referenced) refsymbol_def(toksymref);
					VMDirty(toksymref);
				}
                                else { 
					if( toksym->type == s_datasymb )
					{
					     /* Fix: recover() is an error now; -wd	*/
					     /* makes it a warning again		*/
					     if( !inlib && (warnflags & warn_dup) )
					      recoverwarn("Duplicate definition of symbol '%s' - ignored",token);
					     else if( !inlib ) 
					      recover("Duplicate definition of symbol '%s'",token);
					     else
					     {
					     	/* if already defined, re-define locally */
					     	/* to hold space in table	*/
					     	toksymref = insert(token,TRUE);
		                                toksym = VMAddr(Symbol,toksymref);
                	                	toksym->type = s_datasymb;
                        		        toksym->module = curmod;
		                                VMDirty(toksymref);
					     }
					}
					/* Fix: a code label or common of this name	*/
					/* is a duplicate	*/
					/* definition like the others: a warning in	*/
					/* a library or with -wd (as 1.3 reported	*/
					/* it), else an error. -wd did not cover a	*/
					/* label followed by data of the same name.	*/
					else if( dupsym(toksym->type) && (inlib || (warnflags & warn_dup)) )
					    recoverwarn("Illegal symbol in DATA directive: '%s' - ignored",token);
					else recover("Illegal symbol in DATA directive: '%s'",token);
				}
                            sym = toksymref;
                            expr.w = exp1();
                            if( etype != s_number )
                                recover("only numeric expressions allowed in DATA directive");
                            gendata(sym,expr.w);
                            break;
                    }

                    case s_common:
                    {
                            VMRef sym;
                            nextsym();
                            if( NullRef(toksymref) )
                            {
                                toksymref = insert(token,TRUE);
                                toksym = VMAddr(Symbol,toksymref);
                                toksym->type = s_commsymb;
                                toksym->module = curmod;
                                VMDirty(toksymref);
                            }
                            else
                                if ( toksym->type == s_unbound ) {
                                        toksym->type = s_commsymb;
					toksym->module = curmod;
					if (!preasm && toksym->referenced) refsymbol_def(toksymref);
					VMDirty(toksymref);
				}
                                else { 
					/* Fix: as for DATA */
					if( toksym->type == s_commsymb )
						;
					else if( dupsym(toksym->type) && (inlib || (warnflags & warn_dup)) )
						recoverwarn("illegal symbol in COMMON directive: '%s' - ignored",token);
					else
						recover("illegal symbol in COMMON directive: '%s'",token);
				}
                            sym = toksymref;
                            expr.w = exp1();
                            if( etype != s_number )
				recover("only numeric expressions allowed in COMMON directive");
                            gencommon(sym,expr.w);
                            break;
                    }

                    case s_module:
                        expr.w = exp1();
                        if( etype != s_number )
                                recover("Numerical expressions only allowed in MODULE directive");
                        genmodule(expr.w);
                        break;

		    case s_size:
			expr.w = exp1();
                        if( etype != s_number )
	                       recover("Numerical expressions only allowed in SIZE directive");
                        /* Fix: the size is stored in a byte: size 300	*/
                        /* became 44, size -1 was ignored and size -2	*/
                        /* became 254, silently; size 0 failed only at	*/
                        /* link time.		*/
                        if( expr.w < 1 || expr.w > 255 )
                               asmerror("SIZE %d out of range 1..255",expr.w);
                        gensize(expr.w);
                        break;
                        
                    case s_ref:
			nextsym();
                    	if( symb > s_token ) recover("Illegal operand to REF directive");
			if( symb == s_token )
			{
				toksymref = insert(token,FALSE);
				toksym = VMAddr(Symbol,toksymref);
				toksym->type = s_unbound;
				/*toksym->module = NullVMRef;*/
				VMDirty(toksymref);
			}
			else movesym(toksymref);

			if (!preasm)
			  refsymbol_nondef(toksymref);
			genref(toksymref);
			nextsym();			
			break;

		    /* Fix: an unknown directive code (only possible from	*/
		    /* a byte in a .p file) did not advance: endless loop	*/
		    default:
			recover("Unknown directive #%02x",(int)tokval.w);
                }
                        break;
                default:
                        /* Fix: an error. It was a	*/
                        /* warning, and the token was dropped: "ldc 0x10"	*/
                        /* assembled as "ldc 0" (x10 ignored), "ldc 1.5" as	*/
                        /* "ldc 1", a misspelt instruction vanished, exit 0.	*/
                        /* recover() skips to the next statement.		*/
                        recover("Unexpected symbol: '%s' (%02x)",token,symb);

                }
        }
        memcpy(error_level,oldreclev,sizeof(jmp_buf));
}

/********************************************************/
/* exp                                                  */
/*                                                      */
/* expression parser, builds a tree corresponding to    */
/* the input expression.                                */
/*                                                      */
/********************************************************/

#undef trace
#define trace if(traceflags&db_expr)_trace

PRIVATE WORD exp1()
{
        nextsym();
        return exp();
}

PRIVATE WORD exp()
{
        Value lexp; lexp.w = term();

        trace("exp symb = %2x '%s'",symb,token);
        while ( symb == s_shl ||
                symb == s_shr )
        {
                WORD ltype = etype;
                WORD op = symb;
                Value rexp; nextsym(); rexp.w = term();
                lexp.w = combine(op,ltype,etype,lexp.w,rexp.w);
        }
        return lexp.w;
}

PRIVATE WORD term()
{
        Value lexp; lexp.w = tertiary();

        trace("term symb = %2x '%s'",symb,token);
        while ( symb == s_and ||
                symb == s_or )
        {
                WORD ltype = etype;
                WORD op = symb;
                Value rexp; nextsym(); rexp.w = tertiary();
                lexp.w = combine(op,ltype,etype,lexp.w,rexp.w);
        }
        return lexp.w;
}

PRIVATE WORD tertiary()
{
        Value lexp; lexp.w = secondary();

        trace("tertiary symb = %2x '%s'",symb,token);
        while ( symb == s_plus ||
                symb == s_minus )
        {
                WORD ltype = etype;
                WORD op = symb;
                Value rexp; nextsym(); rexp.w = secondary();
                lexp.w = combine(op,ltype,etype,lexp.w,rexp.w);
        }
        return lexp.w;
}

PRIVATE WORD secondary()
{
        Value lexp; lexp.w = primary();

        trace("secondary symb = %2x '%s'",symb,token);
        while ( symb == s_mul ||
                symb == s_div ||
                symb == s_rem )
        {
                WORD ltype = etype;
                WORD op = symb;
                Value rexp; nextsym(); rexp.w = primary();
                lexp.w = combine(op,ltype,etype,lexp.w,rexp.w);
        }
        return lexp.w;
}

PRIVATE WORD primary()
{
        WORD op = symb;
        Value res;

        trace("primary symb = %2x '%s'",symb,token);
        switch( (int)op )
        {
        case s_lbra:
                res.w = exp1();
                if ( symb != s_rbra ) recover("Malformed expression");
                nextsym();
                break;

        case s_number:
                etype = s_number;
                res.w = tokval.w;
                nextsym();
                break;

        case s_token:
                toksymref = insert(token,TRUE);
		toksym = VMAddr(Symbol,toksymref);
                op = toksym->type = s_unbound;
                VMDirty(toksymref);
        case s_unbound:
                etype = op;
                res.v = toksymref;
		if (!preasm)
		  refsymbol_nondef(toksymref);                
                nextsym();
                break;

        case s_modnum:
                etype = op;
                res.v = toksymref;
                nextsym();
                break;

        case s_commsymb:
        case s_datasymb:
		etype = s_unbound;
		res.v = toksymref;
		if (!preasm)
		  refsymbol_nondef(toksymref);
		nextsym();
		break;
		
        case s_codesymb:
        	if( preasm )
		{
			etype = op;
	                res.v = toksymref;
	        }
	        else {
			etype = s_coderef;
			res = toksym->def;
		}
		if (!preasm)
		  refsymbol_nondef(toksymref);
		nextsym();
		break;

        case s_at:
                nextsym();
                if( symb > s_token ) recover("'@' operator may only be applied to labels");
		if( symb == s_token )
		{
			toksymref = insert(token,FALSE);
			toksym = VMAddr(Symbol,toksymref);
			toksym->type = s_unbound;
			/*toksym->module = NullVMRef;*/
			VMDirty(toksymref);
		}
		else movesym(toksymref);

		if (!preasm)
		  refsymbol_nondef(toksymref);
                etype = op;
		res.v = toksymref;
		nextsym();
                break;

        case s_minus:
        case s_not:
        {
                /* Fix: the number in -N must be at most 0x80000000;	*/
                /* -2147483649 became 2147483647 and -4294967295 1,	*/
                /* silently. Checked when the	*/
                /* operand is the number alone: an expression such as	*/
                /* -(#ffffffff+2) is 32-bit arithmetic as before.	*/
                WORD n, v;
                bool big;
                nextsym();
                n = tokcount; v = tokval.w;
                big = op == s_minus && symb == s_number && toknegovf;
                res.w = exp();
                if( big && tokcount == n + 1 )
                        asmerror("Number -%u does not fit in 32 bits",(unsigned)v);
                if( etype == s_number ) res.w = domonadic(op,res.w);
                else {
                        res.v = unary( op, etype, res.w );
                        etype = s_monadic;
                }
                break;
        }

        default:
                recover("Unexpected symbol '%s' in expression",token);

        }
        return res.w;
}

PRIVATE WORD combine( op, ltype, rtype, lexp, rexp )
WORD op, ltype, rtype;
WORD lexp, rexp;
{
        Value res;

        if( ltype == s_number && rtype == s_number )
        {
                res.w = dodyadic(op,lexp,rexp);
                etype = s_number;
        }
        else {
                res.v = binary(op,ltype,rtype,lexp,rexp);
                etype = s_expr;
        }
        return res.w;
}

/* End of syn.c */
