/*
 * mdutils.c - routines to read a newXrc file
 *
 * Copyright (c) 1990, 1991 by Perihelion Software Ltd.
 * 
 * All rights reserved
 *
 * Author:	N Clifton
 * Version:	0.2
 * Date:	11/7/90
 */


#include <helios.h>
#include <ctype.h>
#include <string.h>


#define MD_BUF_LEN	256

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

#ifndef forever
#define forever	for (;;)
#endif

#define isvalid( c )	(isalnum( c ) || c == '_' || c == '.' || c == '/')


static int
isascii( int c )
{
	if (c >= 0 && c <= 127)
		return 1;
		
	return 0;

} /* isascii */


static int
strieq(
	char *	s1,
	char *	s2 )
/*
 * returns TRUE iff s1 == s2
 * (case insensitive)
 */
{
	while (toupper( *s1++ ) == toupper( *s2 ))
		if (*s2++ == '\0')
			return TRUE;
			
	return FALSE;
	
} /* strieq */


static char
md_get_char(
	 const char *	pbuffer,
	 const char **	newpbuffer,
	 int *		pis_escaped )
/*
 * reads a character from pbuffer in newXrc format
 * (see md_get_var()).  Skips comments, handles
 * \<char>, returns character or \0 or failure.
 * (returns *newpbuffer adjusted to take
 * account of the characters read).
 * (returns *pis_escaped set to TRUE iff the
 * character read was preceeded by a backslash)
 */
{
	char	c;
	

	if (pbuffer     == NULL ||
	    *pbuffer    == '\0' ||
	    newpbuffer  == NULL ||
	    pis_escaped == NULL )
	{
		/* nothing there */

		if (newpbuffer != NULL)
			*newpbuffer = pbuffer;
		
		return '\0';
	}

	c = *pbuffer++;

	if (c == '\\')
	{
		c = *pbuffer++;

		*pis_escaped = TRUE;
	}
	else
	{
		*pis_escaped = FALSE;
		
		if (c == '#')
		{
			/*
			 * start of comment :-
			 *  read until end-of-line,
			 *  do not use get_char() as we must
			 *   prevent recursive comments
			 */

			do
			{
				c = *pbuffer++;

				if (c == '\\')
				{
					c = *pbuffer++;

					if (c != '\0')
						c = *pbuffer++;
				}
			}
			while( c != '\n' && c != '\0' );

			/* all comments are interpreted as new-lines */
		
			c = '\n';
		}
	}
	
	*newpbuffer = pbuffer;

	return c;

} /* md_get_char */


static int
md_get_name(
	 const char *	pbuffer,
	       char *	pdest,
	 const char **	newpbuffer )
/*
 * reads a name from the buffer in newXrc format (see md_get_var())
 * returns the name in pdest, and returns pbuffer in *newpbuffer
 * adjusted to take account of the characters read.
 * At most MD_BUF_LEN characters will be placed into pdest.
 * Returns TRUE upon success, FALSE upon failure
 */
{
	int	is_escaped;
	int	in_quotes;
	int	pos = 0;
	char	c;
	
	
	if (pbuffer    == NULL ||
	    *pbuffer   == '\0' ||
	    pdest      == NULL ||
	    newpbuffer == NULL  )
	{
		if (newpbuffer != NULL)
			*newpbuffer = pbuffer;
		
		return FALSE;
	}

	/* skip leading white space */

	forever
	{
		c = md_get_char( pbuffer, &pbuffer, &is_escaped );

		if (is_escaped || c == '\0')
			break;

		if (isascii( c ) &&
		   (isvalid( c ) || c == '"'))
			break;
	}

	if (c == '\0')
	{
		*newpbuffer = pbuffer;

		return FALSE;
	}

	if (c == '"')
	{
		in_quotes = TRUE;

		c = md_get_char( pbuffer, &pbuffer, &is_escaped );
	}
	else
	{
		in_quotes = FALSE;
	}
	
	/* save first character */

	if (pos < MD_BUF_LEN)
		pdest[ pos++ ] = c;

	/* read characters in the name */

	if (in_quotes)
	{
		forever
		{
			c = md_get_char( pbuffer, &pbuffer, &is_escaped );

			if (c == '\0' || (c == '"' && !is_escaped))
				break;
			
			if (pos < MD_BUF_LEN)
				pdest[ pos++ ] = c;
		}
	}
	else
	{
		forever
		{
			c = md_get_char( pbuffer, &pbuffer, &is_escaped );

			if (c == '\0')
				break;
			
			if (!is_escaped && (!isascii( c ) || !isvalid( c )))
				break;

			if (pos < MD_BUF_LEN)
				pdest[ pos++ ] = c;
		}
	}
	
	*newpbuffer = pbuffer;

	pdest[ pos ] = '\0';

	return TRUE;

} /* md_get_name */


static int
md_get_val(
	 const char *	pbuffer,
	       char *	pdest,
	 const char **	newpbuffer )
/*
 * reads a value from the buffer in newXrc format (see md_get_var())
 * returns the name in pdest, and returns pbuffer in *newpbuffer
 * adjusted to take account of the characters read.
 * Returns TRUE upon success, FALSE upon failure
 */
{
	int	is_escaped;
	char	c;
	
	
	if (pbuffer    == NULL ||
	    *pbuffer   == '\0' ||
	    pdest      == NULL ||
	    newpbuffer == NULL  )
	{
		if (newpbuffer != NULL)
			*newpbuffer = pbuffer;
		
		return FALSE;
	}

	/* skip leading white space */

	forever
	{
		c = md_get_char( pbuffer, &pbuffer, &is_escaped );

		if (is_escaped || c == '\0')
			break;

		if (isascii( c ) && (isvalid( c ) || c == '"' || c == '='))
			break;
	}

	if (c == '\0')
	{
		*newpbuffer = pbuffer;

		return FALSE;
	}

	if (c != '=')
	{
		/*
		 * we must have reached the start of a new variable,
		 * rather than the value of the current variable.
		 * unread the first character of the variable's name
		 * and return FALSE
		 */

		*newpbuffer = pbuffer - 1;

		return FALSE;
	}

	/* got the equality sign - now read value */

	return md_get_name( pbuffer, pdest, newpbuffer );
	
} /* md_get_val */


const char *
md_get_var(
	const char *	pbuffer,
	const char *	pvar )
/*
 * searches 'pbuffer' for a variable called 'pvar' and
 * returns its value.  (Boolean variables return the string
 * "TRUE" for on and "FALSE" for off).  Returns NULL if the
 * variable does not exist, or upon error.  The contents of
 * the buffer are expected to be in newXrc format, vis :-
 *
 *    everything from a hash (#) to end-of-line is a comment
 *      (white space preceeding a comment is also considered
 *       to be part of the comment).
 *
 *    variables are not case sensitive, and only the
 *      first occurance of a variable in the buffer is
 *      significant.
 *
 *    variables take the form <name> = <value>
 *      where <name> and <value> are sequences of
 *      alpha-numerics, underscores, periods and forward slashes.
 *	If other characters are required in either field they
 *	should be enclosed between double quotes.  The sequence
 *	'\<char>' is treated as '<char>' except that '<char>' is
 *      treated as if it were an alpha-numeric.  (Note the
 *      sequence \# is treated as an alpha-numeric, not the
 *      start of a comment).  White space between <name> and
 *      the equality sign, and white space between the
 *      equality sign and <value> is ignored.  An alternative
 *      form of just <name> with no equality sign is also accepted.
 *	This form specifies a boolean variable (switched on).  The
 *	form !<name> specifies a boolean variable (switched off).  
 */
{
	static char	name_buf[ MD_BUF_LEN + 1 ];	 /* XXX */
	static char	val_buf[  MD_BUF_LEN + 1 ];	 /* XXX */
	const char *	ptr;
	const char *	newptr;
	
	
	if (pbuffer  == NULL ||
	    *pbuffer == '\0' ||
	    pvar     == NULL ||
	    *pvar    == '\0'  )
	{
		/* nothing there */

		return NULL;
	}

	ptr = pbuffer;

	while (md_get_name( ptr, name_buf, &newptr ))
	{
		int	is_bool;


		ptr = newptr - strlen( name_buf ) - 1;
		
		is_bool = !md_get_val( newptr, val_buf, &newptr );

		if (strieq( name_buf, (char *)pvar ))
		{
			if (is_bool)
			{
				if (md_get_char( ptr - 1, &ptr, &is_bool ) == '!')
				{
					return "FALSE";
				}

				return "TRUE";
			}
			else
			{
				return val_buf;
			}
		}

		ptr = newptr;
	}

	return NULL;
	
} /* md_get_var */
