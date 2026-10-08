/************************************************************************/
/*                                                                      */
/* File: growcode.c                                                     */
/*                                                                      */
/* Changes:                                                             */
/*      V0.0   NHG  16-May-87 : Created                                 */
/*                                                                      */
/* Description:                                                         */
/*      Massages the code which has been built in the code vector to    */
/*      minimise the length of all prefix encodings. This also performs */
/*      some of the semantic checking of the code.                      */
/*                                                                      */
/*                                                                      */
/* Copyright (c) 1987, Perihelion Software Ltd. All Rights Reserved.    */
/************************************************************************/
/* $Id: growcode.c,v 1.5 1991/03/28 17:50:25 nick Exp $ */

#include "asm.h"

#define trace if (traceflags & db_growcode) _trace

#define MaxIterations 1000

PUBLIC WORD 	simPc;
PUBLIC Code *	code;
PUBLIC WORD 	iteration;
PRIVATE int	dataoffset;	/* Host port: was implicit int */
PUBLIC WORD 	oldloc;

WORD eval(ellipsis);
WORD dodyadic(ellipsis);
WORD domonadic(ellipsis);

/****************************************************************/
/* Procedure: growcode                                          */
/* Description:                                                 */
/*      Main code massaging loop                                */
/*                                                              */
/****************************************************************/

PUBLIC void
growcode()
{
  /* Host port: volatile, as the locals a longjmp() back into this	*/
  /* function finds must be.						*/
  volatile WORD	oldchanges = 0;
  volatile bool	growonly = FALSE;
  jmp_buf 	oldreclev;
  volatile WORD	changes;
  asm_Module *	m;
  

  dataoffset = 0;
  iteration  = 1;
  
  memcpy( oldreclev, error_level, sizeof (jmp_buf) );
  
  do
    {
      register Code *	c;
      register WORD 	tag;
#ifdef __GNUC__
      volatile VMRef	curblock;	/* Host port: live across longjmp() */
#else
      VMRef		curblock;	/* Helios C: no volatile struct assignment */
#endif

      
      curmod   = module0;
      
      m        = VMAddr( asm_Module, curmod );
      curblock = m->start;
      
      VMlock( curblock );
      
      VMDirty( curblock );
      
      code = VMAddr( Code, curblock );
      
      changes = 0;
      simPc   = 0;
      
      trace( "Iteration = %d", iteration );
      
      if ( iteration == MaxIterations )
        {
	  /* Fix: an error; the code sizes have not converged, so the	*/
	  /* offsets in the image would be wrong (was a warning).	*/
	  asmerror( "PROBABLE BUG IN GROWCODE, %d iterations", iteration );
	  
	  break;
        }
      
      /* when an error is detected, longjmp here */
      
      /* Fix: recover() has counted the error already; counting it	*/
      /* twice stopped asm at 50 errors (ASM-REVIEW-FIXES A11)	*/
      if ( setjmp( error_level ) != 0 )
	code++;
      
      for (;;)
        {
	  tag = (c = code)->type;
	  
	  oldloc = c->loc;
	  c->loc = simPc;
	  
	  if ( simPc != oldloc )
	    changes++;
	  
	  trace( "code: %5d : %2x %3d %2x %8x", c->loc, tag, (WORD)c->size, c->vtype, c->value.w );
	  
	  if ( tag == s_newseg ) 
	    {
	      VMunlock( curblock );
	      
	      curblock = c->value.v;
	      
	      VMlock( curblock );
	      
	      code = VMAddr( Code, curblock );
	      
	      VMDirty( curblock );
	      
	      continue;
	    }
	  
	  if ( tag == s_end )
	    {
	      VMunlock( curblock );
	      
	      m = VMAddr( asm_Module, curmod );
	      
	      curmod = m->next;
	      
	      if ( NullRef( curmod ) )
		break;
	      
	      m = VMAddr( asm_Module, curmod );
	      
	      if ( m->id < 0 )
		error("Invalid module 1: %x %p %x %d", VMWord(curmod), (void *)m, VMWord(m->next), m->id );
	      
	      curblock = m->start;
	      
	      VMlock( curblock );
	      
	      code = VMAddr( Code, curblock );
	      
	      dataoffset = 0;
	      
	      VMDirty( curblock );
	      
	      trace( "Module %d %d", m->id, iteration );
	      
	      continue;
            }
#ifdef LINENO
	  lineno = c->line;
#endif
	  if ( 0 <= tag && tag <= 15 ) /* its a direct instruction */
            {
	      register WORD 	expr;
	      register WORD 	isize;


	      /* if the value is a code symbol, switch it to a direct	*/
	      /* ref to the code, this eliminates an indirect through 	*/
	      /* the symbol table.					*/
	      
	      if (  c->vtype == s_codesymb ) 
		{
		  Symbol *	s = VMAddr( Symbol, c->value.v );
		  
		  c->vtype   = s_coderef;
		  c->value.v = s->def.v;
		}
	      else if ( c->vtype == s_unbound )
		{
		  Symbol *	s = VMAddr( Symbol, c->value.v );
		  
		  if ( s->type == s_codesymb )
		    {
		      c->vtype   = s_coderef;
		      c->value.v = s->def.v;
		    }
		}
	      else if( c->vtype == s_at ) /* also switch @symbol to a number */
		{
		  Symbol *	s = VMAddr( Symbol, c->value.v );
		  
		  if ( NullRef( s->module ) ) 
		    {
		      /* Fix: an error unless -wu (ASM-REVIEW H3) */
		      if ( warnflags & warn_undef )
		        warn( "DATA label %s undefined - module set to zero", s->name );
		      else
		        linkerror( "DATA label %s undefined", s->name );
		      
		      /* Fix: module 0 from now on, so that the symbol is	*/
		      /* reported once, as in eval() (A12; was once per use)	*/
		      s->module = module0;
		      c->vtype   = s_number;
		      c->value.w = 0;	
		    }
		  else
		    {
		      m = VMAddr( asm_Module, s->module );
		      
		      if ( m->id < 0 )
			{
			  error( "Bad module number for %s m %x (%s) id %x", s->name, VMWord(s->module), m->file, m->id );
			}
		      
		      c->vtype   = s_number;
		      c->value.w = m->id;
		    }
		}
	      
	      /* if the size of the instruction has been fixed, do not 	*/
	      /* re-calculate.					 	*/
	      
	      if ( c->flags & codeflags_fixed ) 
		{
		  simPc += c->size;
		  
		  goto next;
		}
	      
	      expr = eval( (WORD)c->vtype, c->value.w, (WORD)simPc + c->size );
	      
	      if ( etype == s_datadone )
		{
		  c->vtype   = s_number;
		  c->value.w = expr;
		}
	      
	      isize = pfsize( expr );
	      
	      trace( "Etype = %x expr = %x isize = %x", etype, expr, isize );
	      
	      /* For code references. If the offset is -ve then		*/
	      /* repeat the evaluation with the new code size until	*/
	      /* it converges. If the offset is +ve and has increased 	*/
	      /* in size, allow for this in new offset.			*/
	      
	      if ( etype == s_coderef )
		{
		  if ( expr < 0 )
		    {
		      int 	osize = c->size;
		      int 	i;

		      
		      for (i = 0; i < 10 && isize != osize; i++ )
			{
			  expr  = eval( (WORD)c->vtype, c->value.w, (WORD)simPc + isize );
			  osize = isize;
			  isize = pfsize( expr );
			}
		    }
		}

	      /* We only allow the code to grow, never shrink. Occasionally
		 this means generating values in more bytes than necessary,
		 this is kludged up in genimage.
		 */
	      
	      if ( growonly )
		{
		  if ( isize > c->size )
		    c->size = isize;
		  
		  simPc += c->size;
		}
	      else
		simPc += c->size = isize;
	      
	      goto next;
            }
	  
	  switch ( (int)tag )
            {
	      /* straight bytes */
            case s_bss:
            case s_code:
            case s_literal:
	      simPc += c->size;
	      break;
	      
	      /* single words */
            case s_init:
            case s_word:
	      simPc += TargetBytesPerWord;
	      break;
	      
	      /* padding to word boundary */
	      /* aligns are the only things which are allowed to shrink */
	      
            case s_align:
	      simPc += c->size = asize( simPc );
	      break;
	      
	    case s_newfile:
	      infile = c->value.c;	/* Host port: was (char *)value.w */
					/* Fix: was a strcpy into infile[128] */
	      trace( "new file %s", infile );
	      break;
	      
	    case s_data:
	    case s_common:
	    case s_size:
	    case s_module:
	      break;
	      
            } /* end of switch */
	  
	next:
	  code++;
	  
        } /* end of while */
      
      if ( verbose )
	{
	  fprintf( verfd, "Iteration %4d Changes %d     \r", iteration, changes );
	  
	  fflush( verfd );
	}
      
      iteration++;
      
      if ( iteration > 2  || oldchanges == changes )
	growonly = TRUE;
      
      oldchanges = changes;
      
    }
  while( (changes > 0 || iteration <= 2 ) && errors == 0  );
  
  memcpy( error_level, oldreclev, sizeof (jmp_buf) );
  
  /* report( "\n%d Iterations Required", iteration ); */

  return;
}

/****************************************************************/
/* Procedure: eval                                              */
/* Description:                                                 */
/*      Evaluate an expression and return its value, and its    */
/*      type in etype.                                          */
/*                                                              */
/****************************************************************/

#undef trace
#define trace if(traceflags&db_eval)_trace

PUBLIC WORD eval(type,earg,loc)
WORD type,earg,loc;
{
    Value expr;
    Symbol *s = NULL;
    asm_Module *m = NULL;
    
    expr.w = earg;	/* some compilers can't handle union args!! */

    if( type == s_unbound )
    {
    	s = VMAddr(Symbol,expr.v);
    	etype = s->type;
    }
    else etype = type;

    trace("Eval %2x %8x %8x",etype,earg,loc);

    switch( (int)etype )
    {
    case s_number:      /* just a number - return it */
        return expr.w;

    case s_codesymb:    /* code label - return offset from current loc */
    {
	Code *c;
	WORD labloc;
    	/* Problem here: if this pass has pushed the new location	*/
    	/* of the current instruction past that of the instruction	*/
    	/* referenced, the offset here will be negative. This can lead	*/
    	/* to our generating larger instructions than necessary.	*/
        /* If the offset appears to be negative, but the old offset was	*/
        /* positive, keep to the old estimate for now.			*/
        /* This will sort itself out in a future iteration when the 	*/
        /* code gets closer to its eventual size.			*/

	s = VMAddr(Symbol,expr.v);
	c = VMAddr(Code,s->def.v);

        labloc = c->loc;
/*if(labloc <0 || labloc >1000000 ) _trace("bad labloc c = %x s = %s",c,s->name);*/

        if( (labloc < loc) && (labloc > (oldloc+code->size)) ) 
        {
        	return labloc-oldloc-code->size;
        }
        else return labloc - loc;
    }
    
    case s_coderef:
  	{
  		Code *c = VMAddr(Code,expr.v);
  		WORD labloc = c->loc;

		if( (labloc < loc) && (labloc > (oldloc+code->size)) ) 
		{
			return labloc-oldloc-code->size;
		}
		else return labloc - loc;
  	}

    case s_datasymb:
    case s_commsymb:
/*       	s = VMAddr(Symbol,expr.v); */
       	return 1;
        	
    case s_datadone:
    case s_commdone:
    	s = VMAddr(Symbol,expr.v);
	return s->def.w;

    case s_modnum:
    	m = VMAddr(asm_Module,curmod);
	etype = s_number;
	if( m->id < 0 ) error("Invalid module 2: %x %p %x %d",VMWord(curmod),(void *)m,VMWord(m->next),m->id);
        return m->id;
    
    case s_at:
        etype = s_number;
        s = VMAddr(Symbol,expr.v);
        /* Fix: @ of an undefined symbol inside an expression or a	*/
        /* word read module NullVMRef: "VM Error: invalid VMRef 0"	*/
        /* (ASM-REVIEW M7). Handled as at the top level of an		*/
        /* instruction.							*/
        if( NullRef(s->module) )
        {
            if( warnflags & warn_undef )
                warn("DATA label %s undefined - module set to zero",s->name);
            else
                linkerror("DATA label %s undefined",s->name);
            s->module = module0;	/* module 0 from now on, reported once */
            return 0;
        }
        m = VMAddr(asm_Module,s->module);
       	if( m->id < 0 ) error("Invalid module 3: %x %p %x %d",VMWord(s->module),(void *)m,VMWord(m->next),m->id);
        return m->id;

    case s_unbound:     /* undefined - an error!!        */
        s = VMAddr(Symbol,expr.v);
        /* Fix: an error unless -wu; it was a warning and the image	*/
        /* used 0, e.g. a call to address 0 (ASM-REVIEW H3).		*/
        if( warnflags & warn_undef )
	        warn("Symbol '%s' undefined - set to zero",s->name);
        else
	        linkerror("Symbol '%s' undefined",s->name);
        s->def.w = 0;
        s->type = s_datadone;
        return 0;

    case s_expr:        /* top of an expression tree     */
    {
    	WORD ntype;
    	WORD ltype, rtype;
    	Value lexp, rexp;
    	WORD lvalue, rvalue;
    	
	Bnode *b = VMAddr(Bnode,expr.v);
	
	ntype = b->ntype;
	ltype = b->ltype; rtype = b->rtype;
	lexp = b->lexp; rexp = b->rexp;


	switch( ltype )
	{
		Symbol *s;

	case s_unbound:
		s = VMAddr(Symbol,lexp.v);
		if( s->type != s_codesymb) break;

	case s_codesymb:
		VMlock(expr.v);
		s = VMAddr(Symbol,lexp.v);
		ltype = b->ltype = s_coderef;
		lexp = b->lexp = s->def;
		VMDirty(expr.v);
		VMunlock(expr.v);
		break;
	}

	switch( rtype )
	{
		Symbol *s;

	case s_unbound:
		s = VMAddr(Symbol,rexp.v);
		if( s->type != s_codesymb) break;

	case s_codesymb:
		VMlock(expr.v);
		s = VMAddr(Symbol,rexp.v);
		rtype = b->rtype = s_coderef;
		rexp = b->rexp = s->def;
		VMDirty(expr.v);
		VMunlock(expr.v);
		break;
	}

        lvalue = eval(ltype,lexp.w,loc); ltype = etype;
        
        rvalue = eval(rtype,rexp.w,loc); rtype = etype;


	trace("Expr: L %2x %8x Op %2x R %2x %8x",ltype,lvalue,
		ntype,rtype,rvalue);
		
        if( islabel(ltype) && islabel(rtype) && ltype != rtype )
            recover("Cannot mix Code and Data symbols in same expression");

        /* this ensures that the overall type of an expression reflects any
           label references it contains. */
        etype = ltype<rtype?ltype:rtype;

        return dodyadic(ntype,lvalue,rvalue);
    }

    case s_monadic:
    {
	Unode *u = VMAddr(Unode,expr.v);
	WORD ntype = u->ntype;
	WORD uetype = u->etype;
	Value uexpr;

	uexpr.w = u->expr.w;
	
	switch( uetype )
	{
		Symbol *s;

	case s_unbound:
		s = VMAddr(Symbol,uexpr.v);
		if( s->type != s_codesymb) break;

	case s_codesymb:
		VMlock(expr.v);
		s = VMAddr(Symbol,uexpr.v);
		uetype = u->etype = s_coderef;
		uexpr = u->expr = s->def;
		VMDirty(expr.v);
		VMunlock(expr.v);
		break;
	}

        return domonadic(ntype,eval(uetype,uexpr.w,loc));
    }

    }

    return 0;	/* Host port: was falling off the end */
}

PUBLIC WORD dodyadic(op,lexp,rexp)
WORD op,lexp,rexp;
{

    switch( (int)op )
    {
        case s_plus : return lexp  +  rexp;
        case s_minus: return lexp  -  rexp;
        case s_mul  : return lexp  *  rexp;
        /* Host port: x86 traps on a division by zero and on		*/
        /* MinInt / -1, AArch64 returns 0 and MinInt: give the same	*/
        /* answers everywhere. The shift counts are taken modulo 32,	*/
        /* as the i386 shift instructions did.				*/
        case s_div  : if( rexp == 0 ) error("Division by zero");
                      if( rexp == -1 ) return (WORD)(0 - (UWORD)lexp);
                      return lexp  /  rexp;
        case s_rem  : if( rexp == 0 ) error("Division by zero");
                      if( rexp == -1 ) return 0;
                      return lexp  %  rexp;
        case s_shl  : return (WORD)((UWORD)lexp << (rexp & 31));
        case s_shr  : return lexp  >> (rexp & 31);
        case s_and  : return lexp  &  rexp;
        case s_or   : return lexp  |  rexp;
        case s_xor  : return lexp  ^  rexp;
    }
    return 0;	/* Host port: was falling off the end */
}

PUBLIC WORD domonadic(op,value)
WORD op,value;
{
    switch ( (int)op )
    {
        case s_minus: return (WORD)(0 - (UWORD)value);
        case s_not  : return ~value;
    }
    return 0;	/* Host port: was falling off the end */
}

/*  -- End of growcode.c -- */
