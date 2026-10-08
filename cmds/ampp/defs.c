/****************************************************************/
/* File: defs.c                                                 */
/*                                                              */
/* Routines to manipulate object defintions                     */
/*                                                              */
/* Author: NHG 19-Feb-87                                        */
/****************************************************************/
#ifdef __TRAN
static char *RcsId = "$Id: defs.c,v 1.2 1993/08/12 16:41:28 nickc Exp $ Copyright (C) Perihelion Software Ltd.";
#endif
  
#include "ampp.h"

static struct Def *freedefs = NULL;

/********************************************************/
/* adddef                                               */
/*                                                      */
/* add a new defintion for the given symbol.            */
/*                                                      */
/********************************************************/

PUBLIC void adddef(
		   INT type,
		   struct Symbol *sym,
		   intptr_t value )	/* Host port: was INT */
{
        struct Def *def = freedefs;
        
        if( def == NULL ) def = New(struct Def); 
        else freedefs = def->prev;

        def->type = type;
        def->sym = sym;
        def->prev = sym->definition;
        /* Host port: store a pointer in the pointer member of the	*/
        /* union, where it is read back (was def->Value.value = value)	*/
        if( type == s_macro ) def->Value.macro = (struct Macro *)value;
        else if( type == s_builtin ) def->Value.builtin = (void (*)())value;
        else def->Value.value = (INT)value;

        sym->definition = def;
}

/********************************************************/
/* unwind                                               */
/*                                                      */
/* Clear the last definition of the given symbol        */
/*                                                      */
/********************************************************/

PUBLIC void unwind(struct Symbol *sym)
{

        struct Def *def = sym->definition;

	if( def == NULL ) return;
        sym->definition = def->prev;
        if( def->type == s_macro ) freebuf(&(def->Value.macro->def));

	def->prev = freedefs;
	freedefs = def;

}

