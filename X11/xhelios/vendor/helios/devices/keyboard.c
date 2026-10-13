/*
 * keyboard device driver code
 *
 * Copyright (c) 1990, 1991 by Perihelion Software Ltd.
 *
 * All rights reserved
 *
 * Author:	N Clifton
 * Version:	0.5
 * Date:	$Date: 1991/11/15 17:17:41 $
 * RCS:         $Id: keyboard.c,v 1.14 1991/11/15 17:17:41 nickc Exp $
 */
 
#include <helios.h>
#include <device.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <nonansi.h>
#include <attrib.h>
#include <ctype.h>
#include <syslib.h>
#include <X11/Xmd.h>

#include "mdkeyboard.h"

#ifndef BYTE
#define BYTE char
#endif

extern Port		EnableEvents(   Stream *, WORD );
extern void 		Acknowledge(    Stream *, word );
extern void 		NegAcknowledge( Stream *, word );

/*
 * define FAKE if you want the driver to get its input from stdin, not /keyboard
 */

/*
#define FAKE
*/

/*
 * define DEBUG if you want the driver to tell you what is going on
 */

/*
#define DEBUG
*/

#define	PROGNAME	"kbd"

#ifdef FAKE
#define FAKE_SHIFT	0x1F	/* ASCII code of fake shift key */
#endif

void *
create_array(
	     int	elem_size,
	     int 	num_elem,
	     ... )
/*
 * allocates space for and initialises an array.
 * (note elem_size must not be > sizeof( int )
 */
{
  int *	parray;
  va_list	args;
	

  va_start( args, num_elem );
	
  parray = malloc( num_elem * elem_size );

  if (elem_size == sizeof (int) )
    {
      memcpy( parray, args[ 0 ], num_elem * elem_size ); /* urg! */
    }
  else if (elem_size > sizeof (int) )
    {
      IOdebug( "%s: Fatal Error: bad size passed to create_array", PROGNAME );
    }
  else
    {
      bytblt( args[ 0 ], (char *)parray, 0, 0, sizeof( int ), elem_size, num_elem * elem_size, 1, BYTBLT_ALL );
    }
	
  va_end( args );
	
  return (void *)parray;

} /* create_array */

typedef enum table_type
  {
    PC_GERMAN,
    PC,
    PC_UK,
    ST
  }
table_type;

void
build_translation_tables(
			 table_type	type,
			 KeySymsRec **	ppKeySyms,
			 CARD8 **	ppModMap )
/*
 * creates the keyboard translation tables.
 * Note, I have to use this method because device
 * drivers are not allowed to have a static data area
 */
{
  KeySymsRec *	pKeySyms;
  KeySym *	pkey_map;
  
  

#ifdef FAKE
  /*
   * With FAKE keyboard processing the HELIOS IO server
   * has already done the translation for us, so we only
   * need a table to translate between ASCII and keysyms
   */

#ifdef DEBUG
  IOdebug( "%s: creating FAKE keyboard mapping table", PROGNAME );
#endif
  
      pkey_map = (KeySym *)create_array( sizeof( *pkey_map ), (127 * 2),
/*	 normal		*/
			
	NoSymbol,	NoSymbol,	
	NoSymbol,	NoSymbol,	
	NoSymbol,	NoSymbol,	
	NoSymbol,	NoSymbol,	
	NoSymbol,	NoSymbol,	
	NoSymbol,	NoSymbol,	
	NoSymbol,	NoSymbol,	

/*  8*/ XK_BackSpace,	XK_BackSpace,	
	XK_Tab,		XK_Tab,		
/* 10*/	XK_Linefeed,	XK_Linefeed,	
	XK_Clear,	XK_Clear,	
	NoSymbol,	NoSymbol,	
	XK_Return,	XK_Return,	
	NoSymbol,	XK_F1,		/* 					*/
	NoSymbol,	XK_F2,		/* XXX					*/
	NoSymbol,	XK_F3,		/*					*/
	NoSymbol,	XK_F4,		/* In order for the MOTIF window	*/
	NoSymbol,	XK_F5,		/* manager to work with the fake	*/
	XK_Pause,	XK_Pause,	/* keyboard driver I am defining	*/
/* 20*/	XK_Scroll_Lock,	XK_Scroll_Lock,	/* the keys XK_F1 - XK_F12 (even	*/
	NoSymbol,	XK_F6,		/* though they will never be produced).	*/
	NoSymbol,	XK_F7,		/* This keeps the window manager	*/
	NoSymbol,	XK_F8,		/* happy and allows work to continue.	*/
	NoSymbol,	XK_F9,		/*					*/
	NoSymbol,	XK_F10,		/*					*/
	NoSymbol,	XK_F11,		/*					*/
	XK_Escape,	XK_Escape,	/*					*/
	NoSymbol,	XK_F12,		/*					*/
	NoSymbol,	NoSymbol,	
/* 30*/	NoSymbol,	NoSymbol,	
	XK_Shift_L,	XK_Shift_L,	/* NB/ this should be the ASCII code of FAKE_SHIFT */	

/* 32*/	XK_space,	XK_space,	
	XK_exclam,	XK_exclam,	
	XK_quotedbl,	XK_quotedbl,	
	XK_numbersign,	XK_numbersign,	
	XK_dollar,	XK_dollar,	
	XK_percent,	XK_percent,	
	XK_ampersand,	XK_ampersand,	
	XK_apostrophe,	XK_apostrophe,	
/* 40*/	XK_parenleft,	XK_parenleft,	
	XK_parenright,	XK_parenright,	
	XK_asterisk,	XK_asterisk,	
	XK_plus,	XK_plus,	
	XK_comma,	XK_comma,	
	XK_minus,	XK_minus,	
	XK_period,	XK_period,	
	XK_slash,	XK_slash,	

/* 48*/	XK_0,		XK_0,		
	XK_1,		XK_1,		
/* 50*/ XK_2,		XK_2,		
	XK_3,		XK_3,		
	XK_4,		XK_4,		
	XK_5,		XK_5,		
	XK_6,		XK_6,		
	XK_7,		XK_7,		
	XK_8,		XK_8,		
	XK_9,		XK_9,		

/* 58*/	XK_colon,	XK_colon,	
	XK_semicolon,	XK_semicolon,	
/* 60*/	XK_less,	XK_less,	
	XK_equal,	XK_equal,	
	XK_greater,	XK_greater,	
	XK_question,	XK_question,	
	XK_at,		XK_at,		

/* 65*/	XK_A,		XK_A,		
	XK_B,		XK_B,		
	XK_C,		XK_C,		
	XK_D,		XK_D,		
	XK_E,		XK_E,		
/* 70*/	XK_F,		XK_F,		
	XK_G,		XK_G,		
	XK_H,		XK_H,		
	XK_I,		XK_I,		
	XK_J,		XK_J,		
	XK_K,		XK_K,		
	XK_L,		XK_L,		
	XK_M,		XK_M,		
	XK_N,		XK_N,		
	XK_O,		XK_O,		
/* 80*/	XK_P,		XK_P,		
	XK_Q,		XK_Q,		
	XK_R,		XK_R,		
	XK_S,		XK_S,		
	XK_T,		XK_T,		
	XK_U,		XK_U,		
	XK_V,		XK_V,		
	XK_W,		XK_W,		
	XK_X,		XK_X,		
	XK_Y,		XK_Y,		
/* 90*/	XK_Z,		XK_Z,		

	XK_bracketleft,	XK_bracketleft,	
	XK_backslash,	XK_backslash,	
	XK_bracketright,	XK_bracketright,	
	XK_asciicircum,	XK_asciicircum,	
	XK_underscore,	XK_underscore,	
	XK_quoteleft,	XK_quoteleft,	

/* 97*/	XK_a,		XK_a,		
	XK_b,		XK_b,		
	XK_c,		XK_c,		
/*100*/	XK_d,		XK_d,		
	XK_e,		XK_e,		
	XK_f,		XK_f,		
	XK_g,		XK_g,		
	XK_h,		XK_h,		
	XK_i,		XK_i,		
	XK_j,		XK_j,		
	XK_k,		XK_k,		
	XK_l,		XK_l,		
	XK_m,		XK_m,		
/*110*/	XK_n,		XK_n,		
	XK_o,		XK_o,		
	XK_p,		XK_p,		
	XK_q,		XK_q,		
	XK_r,		XK_r,		
	XK_s,		XK_s,		
	XK_t,		XK_t,		
	XK_u,		XK_u,		
	XK_v,		XK_v,		
	XK_w,		XK_w,		
/*120*/	XK_x,		XK_x,		
	XK_y,		XK_y,		
	XK_z,		XK_z,				

/*123*/	XK_braceleft,	XK_braceleft,	
	XK_bar,		XK_bar,		
	XK_braceright,	XK_braceright,	
	XK_asciitilde,	XK_asciitilde,	
/*127*/	XK_Delete,	XK_Delete
	);

  pKeySyms = (KeySymsRec *)malloc( sizeof( KeySymsRec ) );
  
  pKeySyms->map        = pkey_map;
  pKeySyms->minKeyCode = 1;
  pKeySyms->maxKeyCode = 0x7F;
  pKeySyms->mapWidth   = 2;
  
  *ppKeySyms = pKeySyms;
  
#else /* !FAKE */
  int	width = 6;
  
  switch (type)
    {
    case PC_GERMAN:
#ifdef DEBUG
  IOdebug( "%s: creating GERMAN keyboard mapping table", PROGNAME );
#endif
  
      pkey_map = (KeySym *)create_array( sizeof( *pkey_map ), ((7 + 114) * width),
/*	 normal		shifted		 lock		control		 alt gr			  alt */
			
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
/*  1*/	XK_Escape,	XK_Escape,	XK_Escape,	XK_Escape, 	XK_Escape,		XK_Escape,
	XK_1,		XK_exclam,	XK_1,		XK_1,	   	XK_1,			XK_1,
	XK_2,		XK_quotedbl,	XK_2,		XK_2,	   	XK_twosuperior,		XK_twosuperior,
	XK_3,		XK_section,	XK_3,		XK_3,	   	XK_threesuperior,	XK_threesuperior,
	XK_4,		XK_dollar,	XK_4,		XK_4,	   	XK_4,			XK_4,
	XK_5,		XK_percent,     XK_5,		XK_5, 	   	XK_5,			XK_5,
	XK_6,		XK_ampersand,	XK_6,		XK_6,	   	XK_6,			XK_6,
	XK_7,		XK_slash,	XK_7,		XK_7,	   	XK_braceleft,		XK_braceleft,
	XK_8,		XK_parenleft,	XK_8,		XK_8,		XK_bracketleft,		XK_bracketleft,
/* 10*/	XK_9,		XK_parenright,	XK_9,		XK_9,		XK_bracketright,	XK_bracketright,
	XK_0,		XK_equal,	XK_0,		XK_0,	   	XK_braceright,		XK_braceright,
	XK_Greek_BETA,	XK_question,  	XK_Greek_BETA,	XK_Greek_BETA,  XK_backslash,		XK_backslash,
	XK_quoteright,	XK_quoteleft,	XK_quoteright,	XK_quoteright,  XK_quoteright,		XK_quoteright,
	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,		XK_BackSpace,
	XK_Tab,		XK_Tab,		XK_Tab,		XK_Tab,    	XK_Tab,			XK_Tab,
	XK_q,		XK_Q,		XK_Q,		NoSymbol,  	XK_at,			XK_at,
	XK_w,		XK_W,		XK_W,		NoSymbol,  	XK_w,			XK_w,
	XK_e,		XK_E,		XK_E,		NoSymbol,  	XK_e,			XK_e,
	XK_r,		XK_R,		XK_R,		NoSymbol,  	XK_r,			XK_r,
/* 20*/	XK_t,		XK_T,		XK_T,		NoSymbol,  	XK_t,			XK_t,
	XK_z,		XK_Z,		XK_Z,		NoSymbol,  	XK_z,			XK_z,
	XK_u,		XK_U,		XK_U,		NoSymbol,  	XK_u,			XK_u,
	XK_i,		XK_I,		XK_I,		NoSymbol /* XK_Tab */,  XK_i,		XK_i,
	XK_o,		XK_O,		XK_O,		NoSymbol,  	XK_o,			XK_o,
	XK_p,		XK_P,		XK_P,		NoSymbol,  	XK_p,			XK_p,
	XK_udiaeresis,	XK_Udiaeresis,	XK_Udiaeresis,	XK_udiaeresis,	XK_udiaeresis,		XK_udiaeresis,
	XK_plus,	XK_asterisk,	XK_plus,	XK_plus,	XK_asciitilde,		XK_asciitilde,
	XK_Return,	XK_Return,	XK_Return,	XK_Return, 	XK_Return,		XK_Return,
	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,		XK_Control_L,
/* 30*/	XK_a,		XK_A,		XK_A,		NoSymbol,  	XK_a,			XK_a,
	XK_s,		XK_S,		XK_S,		NoSymbol /* XK_Pause */,  XK_s,		XK_s,
	XK_d,		XK_D,		XK_D,		NoSymbol,  	XK_d,			XK_d,
	XK_f,		XK_F,		XK_F,		NoSymbol,  	XK_f,			XK_f,
	XK_g,		XK_G,		XK_G,		NoSymbol,  	XK_g,			XK_g,
	XK_h,		XK_H,		XK_H,		NoSymbol /* XK_BackSpace */,  XK_h,	XK_h,
	XK_j,		XK_J,		XK_J,		NoSymbol /* XK_Linefeed */,  XK_j,	XK_j,
	XK_k,		XK_K,		XK_K,		NoSymbol /* XK_Clear */, XK_k,		XK_k,
	XK_l,		XK_L,		XK_L,		NoSymbol,  	XK_l,			XK_l,
	XK_odiaeresis,	XK_Odiaeresis,	XK_Odiaeresis,	XK_odiaeresis,	XK_odiaeresis,		XK_odiaeresis,
/* 40*/	XK_adiaeresis,	XK_Adiaeresis,	XK_Adiaeresis,	XK_adiaeresis,	XK_adiaeresis,		XK_adiaeresis,
	XK_asciicircum,	XK_degree,	XK_asciicircum,	XK_asciicircum,	XK_asciicircum,		XK_asciicircum,
	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,		XK_Shift_L,
	XK_numbersign,	XK_quoteright,	XK_numbersign,	XK_numbersign,	XK_numbersign,		XK_numbersign,
	XK_y,		XK_Y,		XK_Y,		NoSymbol,  	XK_y,			XK_y,
	XK_x,		XK_X,		XK_X,		NoSymbol,  	XK_x,			XK_x,
	XK_c,		XK_C,		XK_C,		NoSymbol,  	XK_c,			XK_c,
	XK_v,		XK_V,		XK_V,		NoSymbol,  	XK_v,			XK_v,
	XK_b,		XK_B,		XK_B,		NoSymbol,  	XK_b,			XK_b,
	XK_n,		XK_N,		XK_N,		NoSymbol,  	XK_n,			XK_n,
/* 50*/	XK_m,		XK_M,		XK_M,		NoSymbol /* XK_Return */,  XK_mu,	XK_mu,
	XK_comma,	XK_semicolon,	XK_comma,	XK_comma,  	XK_comma,		XK_comma,
	XK_period,	XK_colon,	XK_period,	XK_period, 	XK_period,		XK_period,
	XK_minus,	XK_underscore,	XK_minus,	XK_minus,	XK_minus,		XK_minus,
	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,		XK_Shift_R,
	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,		XK_KP_Multiply,
	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,  	XK_Alt_L,		XK_Alt_L,
	XK_space,	XK_space,	XK_space,	XK_space,  	XK_space,		XK_space,
	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,		XK_Caps_Lock,
	XK_F1,		XK_F1,		XK_F1,		XK_F1,	   	XK_F1,			XK_F1,
/* 60*/	XK_F2,		XK_F2,		XK_F2,		XK_F2,	   	XK_F2,			XK_F2,
	XK_F3,		XK_F3,		XK_F3,		XK_F3,	   	XK_F3,			XK_F3,
	XK_F4,		XK_F4,		XK_F4,		XK_F4,	   	XK_F4,			XK_F4,
	XK_F5,		XK_F5,		XK_F5,		XK_F5,	   	XK_F5,			XK_F5,
	XK_F6,		XK_F6,		XK_F6,		XK_F6,	   	XK_F6,			XK_F6,
	XK_F7,		XK_F7,		XK_F7,		XK_F7,	   	XK_F7,			XK_F7,
	XK_F8,		XK_F8,		XK_F8,		XK_F8,	   	XK_F8,			XK_F8,
	XK_F9,		XK_F9,		XK_F9,		XK_F9,	   	XK_F9,			XK_F9,
	XK_F10,		XK_F10,		XK_F10,		XK_F10,	   	XK_F10,			XK_F10,
	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,		XK_Num_Lock,
/* 70*/	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,		XK_Pause,
	XK_KP_7,	XK_KP_7,	XK_KP_7,	XK_KP_7,	XK_KP_7,		XK_KP_7,
	XK_KP_8,	XK_KP_8,	XK_KP_8,	XK_KP_8,	XK_KP_8,		XK_KP_8,
	XK_KP_9,	XK_KP_9,	XK_KP_9,	XK_KP_9,	XK_KP_9,		XK_KP_9,
	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,		XK_KP_Subtract,
	XK_KP_4,	XK_KP_4,	XK_KP_4,	XK_KP_4,	XK_KP_4,		XK_KP_4,
	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,		XK_KP_5,
	XK_KP_6,	XK_KP_6,	XK_KP_6,	XK_KP_6,	XK_KP_6,		XK_KP_6,
	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,		XK_KP_Add,
	XK_KP_1,	XK_KP_1,	XK_KP_1,	XK_KP_1,	XK_KP_1,		XK_KP_1,
/* 80*/	XK_KP_2,	XK_KP_2,	XK_KP_2,	XK_KP_2,	XK_KP_2,		XK_KP_2,
	XK_KP_3,	XK_KP_3,	XK_KP_3,	XK_KP_3,	XK_KP_3,		XK_KP_3,
	XK_KP_0,	XK_KP_0,	XK_KP_0,	XK_KP_0,	XK_KP_0,		XK_KP_0,
	XK_KP_Decimal,	XK_KP_Decimal,	XK_KP_Decimal,	XK_KP_Decimal,	XK_KP_Decimal,		XK_KP_Decimal,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	XK_less,	XK_greater,	XK_less,	XK_less,	XK_bar,			XK_bar,
	XK_F11,		XK_F11,		XK_F11,		XK_F11,		XK_F11,			XK_F11,
	XK_F12,		XK_F12,		XK_F12,		XK_F12,		XK_F12,			XK_F12,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
/* 90*/	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,		XK_KP_Enter,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol,
	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,		XK_KP_Divide,
/*100*/	XK_Print,	XK_Print,	XK_Print,	XK_Print,	XK_Print,		XK_Print,
	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,		XK_Alt_R,
	XK_Home,	XK_Clear,	XK_Home,	XK_Home,   	XK_Home,		XK_Home,
	XK_Up,		XK_Up,		XK_Up,		XK_Up,	   	XK_Up,			XK_Up,
	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,		XK_Prior,
	XK_Left,	XK_Left,	XK_Left,	XK_Left,   	XK_Left,		XK_Left,
	XK_Right,	XK_Right,	XK_Right,	XK_Right,  	XK_Right,		XK_Right,
	XK_End,		XK_End,		XK_End,		XK_End,		XK_End,			XK_End,
	XK_Down,	XK_Down,	XK_Down,	XK_Down,   	XK_Down,		XK_Down,
	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,		XK_Next,
/*110*/	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert, 	XK_Insert,		XK_Insert,
	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,		XK_Pause,
	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,		XK_Delete,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,		NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,		NoSymbol
	);
      break;
      
    case PC:
#ifdef DEBUG
  IOdebug( "%s: creating PC keyboard mapping table", PROGNAME );
#endif
  
      pkey_map = (KeySym *)create_array( sizeof( *pkey_map ), ((7 + 114) * width),
					
/*	 normal		shifted		 lock		control		 alt 		NumLock */
					
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
/*  1*/	XK_Escape,	XK_Escape,	XK_Escape,	XK_Escape, 	XK_Escape, 	XK_Escape,
	XK_1,		XK_exclam,	XK_1,		XK_1,	   	XK_1,		XK_1,
	XK_2,		XK_at,		XK_2,		XK_2,	   	XK_2,		XK_2,
	XK_3,		XK_numbersign,	XK_3,		XK_3,	   	XK_3,	   	XK_3,
	XK_4,		XK_dollar,	XK_4,		XK_4,	   	XK_4,	   	XK_4,
	XK_5,		XK_percent,     XK_5,		XK_5, 	   	XK_5, 	   	XK_5,
	XK_6,		XK_asciicircum,	XK_6,		XK_6,	   	XK_6,	   	XK_6,
	XK_7,		XK_ampersand,	XK_7,		XK_7,	   	XK_7,	   	XK_7,
	XK_8,		XK_asterisk,	XK_8,		XK_8,	   	XK_8,	   	XK_8,
/* 10*/	XK_9,		XK_parenleft,	XK_9,		XK_9,	   	XK_9,	   	XK_9,
	XK_0,		XK_parenright,	XK_0,		XK_0,	   	XK_0,	   	XK_0,
	XK_minus,	XK_underscore,  XK_minus,	XK_minus,  	XK_minus,  	XK_minus,
	XK_equal,	XK_plus,	XK_equal,	XK_equal,  	XK_equal,  	XK_equal,
	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,
	XK_Tab,		XK_Tab,		XK_Tab,		XK_Tab,    	XK_Tab,	    	XK_Tab,
	XK_q,		XK_Q,		XK_Q,		NoSymbol,  	XK_q,  		XK_q,
	XK_w,		XK_W,		XK_W,		NoSymbol,  	XK_w,	  	XK_w,
	XK_e,		XK_E,		XK_E,		NoSymbol,  	XK_e,	  	XK_e,
	XK_r,		XK_R,		XK_R,		NoSymbol,  	XK_r,  		XK_r,
/* 20*/	XK_t,		XK_T,		XK_T,		NoSymbol,  	XK_t,	  	XK_t,
	XK_y,		XK_Y,		XK_Y,		NoSymbol,  	XK_y,	  	XK_y,
	XK_u,		XK_U,		XK_U,		NoSymbol,  	XK_u,	  	XK_u,
	XK_i,		XK_I,		XK_I,		NoSymbol /* XK_Tab */,  	XK_i,  		XK_i,
	XK_o,		XK_O,		XK_O,		NoSymbol,  	XK_o,	  	XK_o,
	XK_p,		XK_P,		XK_P,		NoSymbol,  	XK_p,	  	XK_p,
	XK_bracketleft,	XK_braceleft,	XK_bracketleft,	XK_bracketleft,	XK_bracketleft,	XK_bracketleft,
	XK_bracketright,XK_braceright,	XK_bracketright,XK_bracketright,XK_bracketright,XK_bracketright,
	XK_Return,	XK_Return,	XK_Return,	XK_Return, 	XK_Return, 	XK_Return,
	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,
/* 30*/	XK_a,		XK_A,		XK_A,		NoSymbol,  	XK_a,  		XK_a,
	XK_s,		XK_S,		XK_S,		NoSymbol /* XK_Pause */,  	XK_s,	  	XK_s,
	XK_d,		XK_D,		XK_D,		NoSymbol,  	XK_d,	  	XK_d,
	XK_f,		XK_F,		XK_F,		NoSymbol,  	XK_f,	  	XK_f,
	XK_g,		XK_G,		XK_G,		NoSymbol,  	XK_g,	  	XK_g,
	XK_h,		XK_H,		XK_H,		NoSymbol /* XK_BackSpace */,  	XK_h,	  	XK_h,
	XK_j,		XK_J,		XK_J,		NoSymbol /* XK_Linefeed */,  	XK_j,	  	XK_j,
	XK_k,		XK_K,		XK_K,		NoSymbol /* XK_Clear */,  	XK_k,  		XK_k,
	XK_l,		XK_L,		XK_L,		NoSymbol,  	XK_l,	  	XK_l,
	XK_semicolon,	XK_colon,	XK_semicolon,	XK_semicolon,	XK_semicolon,	XK_semicolon,
/* 40*/	XK_quoteright,	XK_quotedbl,	XK_quoteright,	XK_quoteright,	XK_quoteright,	XK_quoteright,
	XK_quoteleft,	XK_asciitilde,	XK_numbersign,	XK_bar,		XK_notsign,	XK_notsign,
	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,
	XK_backslash,	XK_bar,		XK_backslash,	XK_backslash,	XK_backslash,	XK_backslash,
	XK_z,		XK_Z,		XK_Z,		NoSymbol,  	XK_z,  		XK_z,
	XK_x,		XK_X,		XK_X,		NoSymbol,  	XK_x,	  	XK_x,
	XK_c,		XK_C,		XK_C,		NoSymbol,  	XK_c,	  	XK_c,
	XK_v,		XK_V,		XK_V,		NoSymbol,  	XK_v,	  	XK_v,
	XK_b,		XK_B,		XK_B,		NoSymbol,  	XK_b,	  	XK_b,
	XK_n,		XK_N,		XK_N,		NoSymbol,  	XK_n,	  	XK_n,
/* 50*/	XK_m,		XK_M,		XK_M,		NoSymbol /* XK_Return */,  	XK_m,	  	XK_m,
	XK_comma,	XK_less,	XK_comma,	XK_comma,  	XK_comma,	XK_comma,
	XK_period,	XK_greater,	XK_period,	XK_period, 	XK_period, 	XK_period,
	XK_slash,	XK_question,	XK_slash,	XK_slash,  	XK_slash,  	XK_slash,
	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,
	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,
	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,  	XK_Alt_L,  	XK_Alt_L,
	XK_space,	XK_space,	XK_space,	XK_space,  	XK_space,  	XK_space,
	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,
	XK_F1,		XK_F1,		XK_F1,		XK_F1,	   	XK_F1,	   	XK_F1,
/* 60*/	XK_F2,		XK_F2,		XK_F2,		XK_F2,	   	XK_F2,	   	XK_F2,
	XK_F3,		XK_F3,		XK_F3,		XK_F3,	   	XK_F3,	   	XK_F3,
	XK_F4,		XK_F4,		XK_F4,		XK_F4,	   	XK_F4,	   	XK_F4,
	XK_F5,		XK_F5,		XK_F5,		XK_F5,	   	XK_F5,	   	XK_F5,
	XK_F6,		XK_F6,		XK_F6,		XK_F6,	   	XK_F6,	   	XK_F6,
	XK_F7,		XK_F7,		XK_F7,		XK_F7,	   	XK_F7,	   	XK_F7,
	XK_F8,		XK_F8,		XK_F8,		XK_F8,	   	XK_F8,	   	XK_F8,
	XK_F9,		XK_F9,		XK_F9,		XK_F9,	   	XK_F9,	   	XK_F9,
	XK_F10,		XK_F10,		XK_F10,		XK_F10,	   	XK_F10,	   	XK_F10,
	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,
/* 70*/	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,
	XK_Home,	XK_Home,	XK_Home,	XK_Home,	XK_Home,	XK_KP_7,
	XK_Up,		XK_Up,		XK_Up,		XK_Up,		XK_Up,		XK_KP_8,
	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_KP_9,
	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,
	XK_Left,	XK_Left,	XK_Left,	XK_Left,	XK_Left,	XK_KP_4,
	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,
	XK_Right,	XK_Right,	XK_Right,	XK_Right,	XK_Right,	XK_KP_6,
	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,
	XK_End,		XK_End,		XK_End,		XK_End,		XK_End,		XK_KP_1,
/* 80*/	XK_Down,	XK_Down,	XK_Down,	XK_Down,	XK_Down,	XK_KP_2,
	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_KP_3,
	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert,	XK_KP_0,
	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_KP_Decimal,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	XK_numbersign,	XK_asciitilde,	XK_numbersign,	XK_numbersign,	XK_numbersign,	XK_numbersign,
	XK_F11,		XK_F11,		XK_F11,		XK_F11,		XK_F11,		XK_F11,
	XK_F12,		XK_F12,		XK_F12,		XK_F12,		XK_F12,		XK_F12,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
/* 90*/	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,
/*100*/	XK_Print,	XK_Print,	XK_Print,	XK_Print,	XK_Print,	XK_Print,
	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,
	XK_Home,	XK_Clear,	XK_Home,	XK_Home,   	XK_Home,   	XK_Home,
	XK_Up,		XK_Up,		XK_Up,		XK_Up,	   	XK_Up,	   	XK_Up,
	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,
	XK_Left,	XK_Left,	XK_Left,	XK_Left,   	XK_Left,   	XK_Left,
	XK_Right,	XK_Right,	XK_Right,	XK_Right,  	XK_Right,	XK_Right,
	XK_End,		XK_End,		XK_End,		XK_End,		XK_End,		XK_End,
	XK_Down,	XK_Down,	XK_Down,	XK_Down,   	XK_Down,   	XK_Down,
	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,
/*110*/	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert, 	XK_Insert, 	XK_Insert,
	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,
	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol
	);
      break;
      
    case PC_UK:
#ifdef DEBUG
  IOdebug( "%s: creating UK style PC keyboard mapping", PROGNAME );
#endif
  
      pkey_map = (KeySym *)create_array( sizeof( *pkey_map ), ((7 + 114) * width),
					
/*	 normal		shifted		 lock		control		 alt 		NumLock */
					
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
/*  1*/	XK_Escape,	XK_Escape,	XK_Escape,	XK_Escape, 	XK_Escape, 	XK_Escape,
	XK_1,		XK_exclam,	XK_1,		XK_1,	   	XK_1,		XK_1,
	XK_2,		XK_quotedbl,	XK_2,		XK_2,	   	XK_2,		XK_2,
	XK_3,		XK_sterling,	XK_3,		XK_3,	   	XK_3,	   	XK_3,
	XK_4,		XK_dollar,	XK_4,		XK_4,	   	XK_4,	   	XK_4,
	XK_5,		XK_percent,     XK_5,		XK_5, 	   	XK_5, 	   	XK_5,
	XK_6,		XK_asciicircum,	XK_6,		XK_6,	   	XK_6,	   	XK_6,
	XK_7,		XK_ampersand,	XK_7,		XK_7,	   	XK_7,	   	XK_7,
	XK_8,		XK_asterisk,	XK_8,		XK_8,	   	XK_8,	   	XK_8,
/* 10*/	XK_9,		XK_parenleft,	XK_9,		XK_9,	   	XK_9,	   	XK_9,
	XK_0,		XK_parenright,	XK_0,		XK_0,	   	XK_0,	   	XK_0,
	XK_minus,	XK_underscore,  XK_minus,	XK_minus,  	XK_minus,  	XK_minus,
	XK_equal,	XK_plus,	XK_equal,	XK_equal,  	XK_equal,  	XK_equal,
	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,
	XK_Tab,		XK_Tab,		XK_Tab,		XK_Tab,    	XK_Tab,	    	XK_Tab,
	XK_q,		XK_Q,		XK_Q,		NoSymbol,  	XK_q,  		XK_q,
	XK_w,		XK_W,		XK_W,		NoSymbol,  	XK_w,	  	XK_w,
	XK_e,		XK_E,		XK_E,		NoSymbol,  	XK_e,	  	XK_e,
	XK_r,		XK_R,		XK_R,		NoSymbol,  	XK_r,  		XK_r,
/* 20*/	XK_t,		XK_T,		XK_T,		NoSymbol,  	XK_t,	  	XK_t,
	XK_y,		XK_Y,		XK_Y,		NoSymbol,  	XK_y,	  	XK_y,
	XK_u,		XK_U,		XK_U,		NoSymbol,  	XK_u,	  	XK_u,
	XK_i,		XK_I,		XK_I,		NoSymbol /* XK_Tab */,  	XK_i,  		XK_i,
	XK_o,		XK_O,		XK_O,		NoSymbol,  	XK_o,	  	XK_o,
	XK_p,		XK_P,		XK_P,		NoSymbol,  	XK_p,	  	XK_p,
	XK_bracketleft,	XK_braceleft,	XK_bracketleft,	XK_bracketleft,	XK_bracketleft,	XK_bracketleft,
	XK_bracketright,XK_braceright,	XK_bracketright,XK_bracketright,XK_bracketright,XK_bracketright,
	XK_Return,	XK_Return,	XK_Return,	XK_Return, 	XK_Return, 	XK_Return,
	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,
/* 30*/	XK_a,		XK_A,		XK_A,		NoSymbol,  	XK_a,  		XK_a,
	XK_s,		XK_S,		XK_S,		NoSymbol /* XK_Pause */,  	XK_s,	  	XK_s,
	XK_d,		XK_D,		XK_D,		NoSymbol,  	XK_d,	  	XK_d,
	XK_f,		XK_F,		XK_F,		NoSymbol,  	XK_f,	  	XK_f,
	XK_g,		XK_G,		XK_G,		NoSymbol,  	XK_g,	  	XK_g,
	XK_h,		XK_H,		XK_H,		NoSymbol /* XK_BackSpace */,  	XK_h,	  	XK_h,
	XK_j,		XK_J,		XK_J,		NoSymbol /* XK_Linefeed */,  	XK_j,	  	XK_j,
	XK_k,		XK_K,		XK_K,		NoSymbol /* XK_Clear */,  	XK_k,  		XK_k,
	XK_l,		XK_L,		XK_L,		NoSymbol,  	XK_l,	  	XK_l,
	XK_semicolon,	XK_colon,	XK_semicolon,	XK_semicolon,	XK_semicolon,	XK_semicolon,
/* 40*/	XK_quoteright,	XK_at,		XK_quoteright,	XK_quoteright,	XK_quoteright,	XK_quoteright,
	XK_quoteleft,	XK_asciitilde,	XK_numbersign,	XK_bar,		XK_notsign,	XK_notsign,
	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,
	XK_numbersign,	XK_asciitilde,	XK_numbersign,	XK_numbersign,	XK_numbersign,	XK_numbersign,
	XK_z,		XK_Z,		XK_Z,		NoSymbol,  	XK_z,  		XK_z,
	XK_x,		XK_X,		XK_X,		NoSymbol,  	XK_x,	  	XK_x,
	XK_c,		XK_C,		XK_C,		NoSymbol,  	XK_c,	  	XK_c,
	XK_v,		XK_V,		XK_V,		NoSymbol,  	XK_v,	  	XK_v,
	XK_b,		XK_B,		XK_B,		NoSymbol,  	XK_b,	  	XK_b,
	XK_n,		XK_N,		XK_N,		NoSymbol,  	XK_n,	  	XK_n,
/* 50*/	XK_m,		XK_M,		XK_M,		NoSymbol /*XK_Return */,  	XK_m,	  	XK_m,
	XK_comma,	XK_less,	XK_comma,	XK_comma,  	XK_comma,	XK_comma,
	XK_period,	XK_greater,	XK_period,	XK_period, 	XK_period, 	XK_period,
	XK_slash,	XK_question,	XK_slash,	XK_slash,  	XK_slash,  	XK_slash,
	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,
	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,
	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,  	XK_Alt_L,  	XK_Alt_L,
	XK_space,	XK_space,	XK_space,	XK_space,  	XK_space,  	XK_space,
	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,
	XK_F1,		XK_F1,		XK_F1,		XK_F1,	   	XK_F1,	   	XK_F1,
/* 60*/	XK_F2,		XK_F2,		XK_F2,		XK_F2,	   	XK_F2,	   	XK_F2,
	XK_F3,		XK_F3,		XK_F3,		XK_F3,	   	XK_F3,	   	XK_F3,
	XK_F4,		XK_F4,		XK_F4,		XK_F4,	   	XK_F4,	   	XK_F4,
	XK_F5,		XK_F5,		XK_F5,		XK_F5,	   	XK_F5,	   	XK_F5,
	XK_F6,		XK_F6,		XK_F6,		XK_F6,	   	XK_F6,	   	XK_F6,
	XK_F7,		XK_F7,		XK_F7,		XK_F7,	   	XK_F7,	   	XK_F7,
	XK_F8,		XK_F8,		XK_F8,		XK_F8,	   	XK_F8,	   	XK_F8,
	XK_F9,		XK_F9,		XK_F9,		XK_F9,	   	XK_F9,	   	XK_F9,
	XK_F10,		XK_F10,		XK_F10,		XK_F10,	   	XK_F10,	   	XK_F10,
	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,	XK_Num_Lock,
/* 70*/	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,
	XK_Home,	XK_Home,	XK_Home,	XK_Home,	XK_Home,	XK_KP_7,
	XK_Up,		XK_Up,		XK_Up,		XK_Up,		XK_Up,		XK_KP_8,
	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_KP_9,
	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,
	XK_Left,	XK_Left,	XK_Left,	XK_Left,	XK_Left,	XK_KP_4,
	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,
	XK_Right,	XK_Right,	XK_Right,	XK_Right,	XK_Right,	XK_KP_6,
	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,
	XK_End,		XK_End,		XK_End,		XK_End,		XK_End,		XK_KP_1,
/* 80*/	XK_Down,	XK_Down,	XK_Down,	XK_Down,	XK_Down,	XK_KP_2,
	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_KP_3,
	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert,	XK_KP_0,
	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_KP_Decimal,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	XK_backslash,	XK_bar,		XK_backslash,	XK_backslash,	XK_backslash,	XK_backslash,
	XK_F11,		XK_F11,		XK_F11,		XK_F11,		XK_F11,		XK_F11,
	XK_F12,		XK_F12,		XK_F12,		XK_F12,		XK_F12,		XK_F12,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
/* 90*/	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol,
	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,
/*100*/	XK_Print,	XK_Print,	XK_Print,	XK_Print,	XK_Print,	XK_Print,
	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,	XK_Alt_R,
	XK_Home,	XK_Clear,	XK_Home,	XK_Home,   	XK_Home,   	XK_Home,
	XK_Up,		XK_Up,		XK_Up,		XK_Up,	   	XK_Up,	   	XK_Up,
	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,	XK_Prior,
	XK_Left,	XK_Left,	XK_Left,	XK_Left,   	XK_Left,   	XK_Left,
	XK_Right,	XK_Right,	XK_Right,	XK_Right,  	XK_Right,	XK_Right,
	XK_End,		XK_End,		XK_End,		XK_End,		XK_End,		XK_End,
	XK_Down,	XK_Down,	XK_Down,	XK_Down,   	XK_Down,   	XK_Down,
	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,	XK_Next,
/*110*/	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert, 	XK_Insert, 	XK_Insert,
	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,	XK_Pause,
	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,  	NoSymbol
	);
      break;
      
    case ST:
#ifdef DEBUG
  IOdebug( "%s: creating ATW keyboard mapping", PROGNAME );
#endif
      width = 5;
  
      pkey_map = (KeySym *)create_array( sizeof( *pkey_map ), ((7 + 114) * width),
					
/*	 normal		shifted		 lock		control		 alt */
					
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
/*  1*/	XK_Escape,	XK_Escape,	XK_Escape,	XK_Escape, 	XK_Escape,
	XK_1,		XK_exclam,	XK_1,		XK_1,	   	XK_1,	
	XK_2,		XK_quotedbl,	XK_2,		XK_2,	   	XK_2,	
	XK_3,		XK_sterling,	XK_3,		XK_3,	   	XK_3,	
	XK_4,		XK_dollar,	XK_4,		XK_4,	   	XK_4,	
	XK_5,		XK_percent,     XK_5,		XK_5, 	   	XK_5,	
	XK_6,		XK_asciicircum,	XK_6,		XK_6,	   	XK_6,	
	XK_7,		XK_ampersand,	XK_7,		XK_7,	   	XK_7,	
	XK_8,		XK_asterisk,	XK_8,		XK_8,	   	XK_8,	
/* 10*/	XK_9,		XK_parenleft,	XK_9,		XK_9,	   	XK_9,	
	XK_0,		XK_parenright,	XK_0,		XK_0,	   	XK_0,	
	XK_minus,	XK_underscore,  XK_minus,	XK_minus,  	XK_minus,
	XK_equal,	XK_plus,	XK_equal,	XK_equal,  	XK_equal,
	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,	XK_BackSpace,
	XK_Tab,		XK_Tab,		XK_Tab,		XK_Tab,    	XK_Tab,	
	XK_q,		XK_Q,		XK_Q,		NoSymbol,  	XK_q,	
	XK_w,		XK_W,		XK_W,		NoSymbol,  	XK_w,	
	XK_e,		XK_E,		XK_E,		NoSymbol,  	XK_e,	
	XK_r,		XK_R,		XK_R,		NoSymbol,  	XK_r,	
/* 20*/	XK_t,		XK_T,		XK_T,		NoSymbol,  	XK_t,	
	XK_y,		XK_Y,		XK_Y,		NoSymbol,  	XK_y,	
	XK_u,		XK_U,		XK_U,		NoSymbol,  	XK_u,	
	XK_i,		XK_I,		XK_I,		XK_Tab,  	XK_i,	
	XK_o,		XK_O,		XK_O,		NoSymbol,  	XK_o,	
	XK_p,		XK_P,		XK_P,		NoSymbol,  	XK_p,	
	XK_bracketleft,	XK_braceleft,	XK_bracketleft,	XK_bracketleft,	XK_bracketleft,
	XK_bracketright,XK_braceright,	XK_bracketright,XK_bracketright,XK_bracketright,
	XK_Return,	XK_Return,	XK_Return,	XK_Return, 	XK_Return,	
	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	XK_Control_L,	
/* 30*/	XK_a,		XK_A,		XK_A,		NoSymbol,  	XK_a,		
	XK_s,		XK_S,		XK_S,		XK_Pause,  	XK_s,		
	XK_d,		XK_D,		XK_D,		NoSymbol,  	XK_d,		
	XK_f,		XK_F,		XK_F,		NoSymbol,  	XK_f,		
	XK_g,		XK_G,		XK_G,		NoSymbol,  	XK_g,		
	XK_h,		XK_H,		XK_H,		XK_BackSpace,  	XK_h,		
	XK_j,		XK_J,		XK_J,		XK_Linefeed,  	XK_j,		
	XK_k,		XK_K,		XK_K,		XK_Clear,  	XK_k,		
	XK_l,		XK_L,		XK_L,		NoSymbol,  	XK_l,		
	XK_semicolon,	XK_colon,	XK_semicolon,	XK_semicolon,	XK_semicolon,	
/* 40*/	XK_quoteright,	XK_at,		XK_quoteright,	XK_quoteright,	XK_quoteright,	
	XK_quoteleft,	XK_underscore,	XK_quoteleft,	XK_quoteleft,	XK_quoteleft,	
	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	XK_Shift_L,	
	XK_numbersign,	XK_asciitilde,	XK_numbersign,	XK_numbersign,	XK_numbersign,	
	XK_z,		XK_Z,		XK_Z,		NoSymbol,  	XK_z,		
	XK_x,		XK_X,		XK_X,		NoSymbol,  	XK_x,		
	XK_c,		XK_C,		XK_C,		NoSymbol,  	XK_c,		
	XK_v,		XK_V,		XK_V,		NoSymbol,  	XK_v,		
	XK_b,		XK_B,		XK_B,		NoSymbol,  	XK_b,		
	XK_n,		XK_N,		XK_N,		NoSymbol,  	XK_n,		
/* 50*/	XK_m,		XK_M,		XK_M,		XK_Return,  	XK_m,		
	XK_comma,	XK_less,	XK_comma,	XK_comma,  	XK_comma,	
	XK_period,	XK_greater,	XK_period,	XK_period, 	XK_period,	
	XK_slash,	XK_question,	XK_slash,	XK_slash,  	XK_slash,	
	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	XK_Shift_R,	
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,	
	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,	XK_Alt_L,  	XK_Alt_L,	
	XK_space,	XK_space,	XK_space,	XK_space,  	XK_space,	
	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	XK_Caps_Lock,	
	XK_F1,		XK_F1,		XK_F1,		XK_F1,	   	XK_F1,		
/* 60*/	XK_F2,		XK_F2,		XK_F2,		XK_F2,	   	XK_F2,		
	XK_F3,		XK_F3,		XK_F3,		XK_F3,	   	XK_F3,		
	XK_F4,		XK_F4,		XK_F4,		XK_F4,	   	XK_F4,		
	XK_F5,		XK_F5,		XK_F5,		XK_F5,	   	XK_F5,		
	XK_F6,		XK_F6,		XK_F6,		XK_F6,	   	XK_F6,		
	XK_F7,		XK_F7,		XK_F7,		XK_F7,	   	XK_F7,		
	XK_F8,		XK_F8,		XK_F8,		XK_F8,	   	XK_F8,		
	XK_F9,		XK_F9,		XK_F9,		XK_F9,	   	XK_F9,		
	XK_F10,		XK_F10,		XK_F10,		XK_F10,	   	XK_F10,		
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,	
/* 70*/	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,	
	XK_Home,	XK_Clear,	XK_Home,	XK_Home,   	XK_Home,	
	XK_Up,		XK_Up,		XK_Up,		XK_Up,	   	XK_Up,		
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,	
	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,	XK_KP_Subtract,
	XK_Left,	XK_Left,	XK_Left,	XK_Left,   	XK_Left,	
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,	
	XK_Right,	XK_Right,	XK_Right,	XK_Right,  	XK_Right,	
	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,	XK_KP_Add,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
/* 80*/	XK_Down,	XK_Down,	XK_Down,	XK_Down,   	XK_Down,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	XK_Insert,	XK_Insert,	XK_Insert,	XK_Insert, 	XK_Insert,
	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,	XK_Delete,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
/* 90*/	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,  	NoSymbol,
	XK_backslash,	XK_bar,		XK_backslash,	XK_backslash,	XK_backslash,	
	XK_Undo,	XK_Undo,	XK_Undo,	XK_Undo,   	XK_Undo,
	XK_Help,	XK_Help,	XK_Help,	XK_Help,   	XK_Help,
	XK_parenleft,	XK_parenleft,	XK_parenleft,	XK_parenleft,	XK_parenleft,
/*100*/	XK_parenright,	XK_parenright,	XK_parenright,	XK_parenright,	XK_parenright,
	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,	XK_KP_Divide,
	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,	XK_KP_Multiply,
	XK_KP_7,	XK_KP_7,	XK_KP_7,	XK_KP_7,	XK_KP_7,
	XK_KP_8,	XK_KP_8,	XK_KP_8,	XK_KP_8,	XK_KP_8,
	XK_KP_9,	XK_KP_9,	XK_KP_9,	XK_KP_9,	XK_KP_9,
	XK_KP_4,	XK_KP_4,	XK_KP_4,	XK_KP_4,	XK_KP_4,
	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,	XK_KP_5,
	XK_KP_6,	XK_KP_6,	XK_KP_6,	XK_KP_6,	XK_KP_6,
	XK_KP_1,	XK_KP_1,	XK_KP_1,	XK_KP_1,	XK_KP_1,
/*110*/	XK_KP_2,	XK_KP_2,	XK_KP_2,	XK_KP_2,	XK_KP_2,
	XK_KP_3,	XK_KP_3,	XK_KP_3,	XK_KP_3,	XK_KP_3,
	XK_KP_0,	XK_KP_0,	XK_KP_0,	XK_KP_0,	XK_KP_0,
	XK_KP_Decimal,	XK_KP_Decimal,	XK_KP_Decimal,	XK_KP_Decimal,	XK_KP_Decimal,
	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter,	XK_KP_Enter
	);
      break;
      
    default:
      IOdebug( "%s: Unknown keyboard type", PROGNAME );
      width = 0;
      break;
    }

  pKeySyms = (KeySymsRec *)malloc( sizeof( KeySymsRec ) );
  
  pKeySyms->map        = pkey_map;
  pKeySyms->minKeyCode = 1;
  pKeySyms->maxKeyCode = 0x7a;
  pKeySyms->mapWidth   = width;
  
  *ppKeySyms = pKeySyms;

#endif /* FAKE */
    
#define	cT	(ControlMask)
#define	sH	(ShiftMask)
#define	lK	(LockMask)
#define	aL	(Mod1Mask)
#define nL	(Mod2Mask)

#ifdef FAKE
      *ppModMap = (CARD8 *) create_array( sizeof( CARD8 ), 256,
					 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 00-0f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  sH,/* 10-1f */	/* NB/ should = FAKE_SHIFT */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 20-2f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 30-3f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 40-4f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 50-5f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 60-6f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70-7f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80-8f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90-9f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* a0-af */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* b0-bf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* c0-cf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* d0-df */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* e0-ef */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0  /* f0-ff */
	 );

#else /* !FAKE */
  
  switch (type)
    {
    case PC_GERMAN:
      *ppModMap = (CARD8 *) create_array( sizeof( CARD8 ), 256,
					 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 00-0f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 10-1f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  cT, 0,  0,  0,  0, /* 20-2f */
	 0,  0,  0,  0,  0,  0,  0,  0,  sH, 0,  0,  0,  0,  0,  0,  0, /* 30-3f */
	 0,  0,  0,  0,  sH, 0,  nL, 0,  lK, 0,  0,  0,  0,  0,  0,  0, /* 40-4f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 50-5f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 60-6f */
	 0,  0,  0,  aL, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70-7f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80-8f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90-9f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* a0-af */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* b0-bf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* c0-cf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* d0-df */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* e0-ef */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0  /* f0-ff */
	 );
      break;   

    case PC:
      *ppModMap = (CARD8 *) create_array( sizeof( CARD8 ), 256,
					 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 00-0f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 10-1f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  cT, 0,  0,  0,  0, /* 20-2f */
	 0,  0,  0,  0,  0,  0,  0,  0,  sH, 0,  0,  0,  0,  0,  0,  0, /* 30-3f */
	 0,  0,  0,  0,  sH, 0,  aL, 0,  lK, 0,  0,  0,  0,  0,  0,  0, /* 40-4f */
	 0,  0,  0,  nL, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 50-5f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 60-6f */
	 0,  0,  0,  aL, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70-7f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80-8f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90-9f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* a0-af */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* b0-bf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* c0-cf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* d0-df */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* e0-ef */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0  /* f0-ff */
	 );
      break;
      
    case PC_UK:
      *ppModMap = (CARD8 *) create_array( sizeof( CARD8 ), 256,
					 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 00-0f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 10-1f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  cT, 0,  0,  0,  0, /* 20-2f */
	 0,  0,  0,  0,  0,  0,  0,  0,  sH, 0,  0,  0,  0,  0,  0,  0, /* 30-3f */
	 0,  0,  0,  0,  sH, 0,  aL, 0,  lK, 0,  0,  0,  0,  0,  0,  0, /* 40-4f */
	 0,  0,  0,  nL, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 50-5f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 60-6f */
	 0,  0,  0,  aL, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70-7f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80-8f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90-9f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* a0-af */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* b0-bf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* c0-cf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* d0-df */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* e0-ef */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0  /* f0-ff */
	 );
      break;
      
    case ST:
      *ppModMap = (CARD8 *) create_array( sizeof( CARD8 ), 256,
					 
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 00-0f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 10-1f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  cT, 0,  0,  0,  0, /* 20-2f */
	 0,  0,  0,  0,  0,  0,  0,  0,  sH, 0,  0,  0,  0,  0,  0,  0, /* 30-3f */
	 0,  0,  0,  0,  sH, 0,  aL, 0,  lK, 0,  0,  0,  0,  0,  0,  0, /* 40-4f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 50-5f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 60-6f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70-7f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80-8f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90-9f */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* a0-af */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* b0-bf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* c0-cf */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* d0-df */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* e0-ef */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0  /* f0-ff */
	 );
      break;
      
    default:
      IOdebug( "%s: unknown keyboard type", PROGNAME );
      break;
    }
#endif /* FAKE */

#ifdef DEBUG
  IOdebug( "%s: keyboard tables created", PROGNAME );
#endif
  
  return;

#ifdef FAKE
  type = type;
#endif
  
} /* build_translation_tables */

word
DevClose( DCB * pdcb )
/*
 * close down the device driver
 *
 * returns non-zero upon success, 0 upon failure
 */
{
#ifdef DEBUG
  IOdebug( "%s: closing down", PROGNAME );
#endif
  
  if (pdcb)
    Free( pdcb );
  
  /* finished */
  
  return 1;

} /* DevClose */


void
DevOperate(
  DCB *		pdcb,
  DevReq *	preq )
/*
 * not currently called
 */
{
  /* finished */
  
#ifdef DEBUG
  IOdebug( "%s: DevOperate called!", PROGNAME );
#endif
  
  return;

  pdcb = pdcb;
  preq = preq;

} /* DevOperate */


#ifndef FAKE

#define TIMER_STACK	1000
#define TIMER_PERIOD	100


void
timer(
      Port 		port,
      Semaphore *	lock,
      int *		pticks )
/*
 * decrements 'ticks' and aborts iff 'ticks' reaches zero
 * only active when the lock is released
 * NB/ Warning: code will only work on a transputer
 */
{
  forever
    {
      Wait( lock );

      /* IOdebug( "timer: starting, ticks = %d (100ths of a second)", *pticks ); */

      while (--*pticks > 0)
	{
	  Delay( OneSec / TIMER_PERIOD );
	}
		
      if (*pticks == 0)
	{
	  /* IOdebug( "timer: ticks reached zero, aborting port" ); */

	  AbortPort( port, EC_Error | EG_Exception | EE_Abort );
	}

      /* else IOdebug( "timer: ticks reset externally, aborting count-down" ); */
    }
	
} /* timer */	

#endif /* !FAKE */

#ifdef FAKE

#define build_event( c )			\
  i++;						\
  event.hdr.Type             = Event_Keyboard;	\
  event.hdr.Counter          = i;		\
  event.hdr.Stamp            = i;		\
  event.Device.Keyboard.Key  = c;		\
  event.Device.Keyboard.What = Keys_KeyDown;

#endif /* FAKE */

void
send_event(
	   Firm_Event * pevent,
	   mdKbPrivPtr	pPriv,
	   KbPrivPtr	pKbd  )
/*
 * send the indicated event to the X server
 */
{
  /* put event into event buffer */
	
  memcpy( (byte *)&(pPriv->buffer[ pPriv->Kbd_Head ]), (byte *)pevent, sizeof( Firm_Event ) );

  /* advance buffer pointer */
      
  pPriv->Kbd_Head = (pPriv->Kbd_Head + 1) % KBD_BUF_SIZE;

  /* tell X server that an event has arrived */
      
  if (pKbd->enabled)
    {
      char 	data_here = 'k';
	            
	           
      /* NB/ write will block */
	            
      write( pPriv->pipe, &data_here, 1 );
    }

  /* finished */
  
  return;
  
} /* send_event */


#define KBD_UNCHNGED	0
#define KBD_KEY_UP	1
#define KBD_KEY_DOWN	2

static void
get_kbd( KbdInfoPtr pinfo )
/*
 * keyboard monitoring process
 */
{
#ifdef FAKE
  Attributes	attr;
  int		res;
  Stream *	newstream;
#else
  MCB		mcb;
  BYTE		data[ IOCDataMax ];
  int		ticks 		 = -1; /* shared variable */
  int		expected_counter = -1;
  word		fnrc;
  Firm_Event *	current_event   = NULL;
  int		last_event_down = 0;
  Semaphore	lock;  
  KeyClassPtr	pkey        = pinfo->pkey;
  Stream *	pstream;
#endif /* !FAKE */
  KbPrivPtr	pKbd        = pinfo->pKbPriv;
  mdKbPrivPtr	pPriv       = (mdKbPrivPtr)(pKbd->devPrivate);
  int		i;
  
#ifdef DEBUG
  IOdebug( "%s: initialising", PROGNAME );
#endif
  
#ifdef FAKE
  /*
   * for fake processing we read from stdin (one character at a time)
   * and pass the ASCII code straight on to the X server, which
   * will use our special translation table to generate keysyms.
   * Using this method we can run the X server from an ordinary
   * Helios session, but we will be unable to do auto-repeat or
   * modifier processing. *sigh*
   */

  /*
   * copy stdin.
   *
   * We have to do this becuase Helios commands like 'run'
   * only create one stream and use it for both input and output.
   * This means that when we start reading characters in our
   * forever loop below, any attempts by the X server to write
   * to stdout (or stderr) will be blocked until a character
   * is read.
   */
  
  newstream = CopyStream( Heliosno( stdin ) );

  ReOpen( newstream );

  if ((res = GetAttributes( newstream, &attr )) < 0)
    {
      IOdebug( "%s: failed to get stdin attributes", PROGNAME );

      goto fail;
    }

  AddAttribute( &attr, ConsoleRawInput );
  RemoveAttribute( &attr, ConsolePause );
  RemoveAttribute( &attr, ConsoleEcho  );

  if ((res = SetAttributes( newstream, &attr )) < 0)
    {
      IOdebug( "%s: failed to set attributes for stdin", PROGNAME );

      goto fail;
    }

#else /* !FAKE */

  /*
   * connect to the keyboard device
   */
  
  if (pKbd->replyport == NullPort)
    {
      Object *	 pobj;
      
      if ((pobj = Locate( NULL, "/keyboard" )) == NULL)
        {
	  IOdebug( "%s: failed to locate keyboard service", PROGNAME );
	  
	  goto fail;
        }
      else if ((pstream = Open( pobj, "/keyboard", O_ReadOnly )) == NULL)
        {
	  IOdebug( "%s: failed to open keyboard service", PROGNAME );
	  
	  goto fail;
        }
      else if ((pKbd->replyport = EnableEvents( pstream, Event_Keyboard )) == NullPort)
        {
	  IOdebug( "%s: failed to enable events on keyboard service", PROGNAME );
	  
	  goto fail;
        }

      Close( pobj );
    }

  /*
   * Initialise the lock used by the countdown timer process
   */
  
  InitSemaphore( &lock, 0 );

  /*
   * start the countdown timer process
   */
  
  Fork( TIMER_STACK, timer, sizeof( pKbd->replyport ) + sizeof( &lock ) + sizeof( &ticks ), pKbd->replyport, &lock, &ticks );
  
  mcb.Data          = (byte *)data;

#endif /* !FAKE */

  pPriv->Kbd_Head = pPriv->Kbd_Tail = 0;	/* initialise buffer pointers */

  /*
   * write to pipe to signal readiness and initialise pipe connections
   */
  
#ifdef DEBUG
  IOdebug( "%s: informing X server of readiness", PROGNAME );
#endif
  
  i = 'I';

  /* IOdebug( "%s: writing to pipe %d", PROGNAME, pPriv->pipe ); */

  write( pPriv->pipe, &i, 1 );
  
  /* IOdebug( "%s: written", PROGNAME ); */

  i = 0;

#ifdef DEBUG
  IOdebug( "%s: starting forever loop", PROGNAME );
#endif
  
  forever
    {
#ifdef FAKE
      int		c;
      Firm_Event	event;
      
      
      /* wait for an event */

      if (Read( newstream, &c, 1, -1 ) == -1)
	{
	  /* urg! */

	  IOdebug( "%s: received EOF! - aborting", PROGNAME );

	  return;
	}
      else if (isupper( c ))
	{
	  /*
	   * lots of thins tend to fall apart if we do not
	   * fake a shift press for upper case letters
	   */

	  build_event( FAKE_SHIFT );
	  
	  send_event( &event, pPriv, pKbd );

	  build_event( c );
	  
	  send_event( &event, pPriv, pKbd );

	  build_event( FAKE_SHIFT );

	  event.Device.Keyboard.What = Keys_KeyUp;

	  send_event( &event, pPriv, pKbd );
	}
      else
	{
	  /*
	   * send the event to the X server
	   */
	  
	  build_event( c );
	  
	  send_event( &event, pPriv, pKbd );
	}

      /* continue */
#else
      /*
       * If the last keybaord event we received was a key down event
       * and there are no events currently waiting on the keyboard
       * device then we may need to auto-repeat ...
       */
      
      if (last_event_down && GetReady( pKbd->replyport ))
	{
	  int	column;
	  int	key;
          int	i;
	  

	  /*
	   * find the ASCII code of the key that is pressed down
	   */
	  
	  if (pkey->state == 0)
	    {
	      column = 0;
	    }
	  else if (pkey->state & ControlMask)
	    {
	      column = 3;
	    }
	  else
	    {
	      for (i = 1, column = 1; !(i & pkey->state); i <<= 1, column++ )
	        ;
	    }
	     
          key = pinfo->pKeySyms->map[ column + pinfo->pKeySyms->mapWidth * (current_event->Device.Keyboard.Key - 1) ] & 0xff;

	  /*
	   * check to see if this ASCII code has auto-repeat suppressed
	   */
	  
	  for (i = 0; i < pinfo->autorepeat_filter_count; i++)
	    {
	      if (key == pinfo->autorepeat_filter_values[ i ])
	        {
	          /*
	           * auto-repeat is prevented on this key - so just do a normal GetMsg()
	           */
	          
	          mcb.Timeout     = MaxInt;
	          mcb.MsgHdr.Dest = pKbd->replyport;

	          while ((fnrc = GetMsg( &mcb )) < 0)
		    ;
		    
		  break;
	        }
	    }

	  if (i >= pinfo->autorepeat_filter_count)
	    {
	      /*
	       * we are now ready to start auto-repeating
	       */
	  
	      *((int *)(&mcb)) = 0;		/* ensure data & control size fields are zero */
	  
	      mcb.Timeout     = -1;
              mcb.MsgHdr.Dest = pKbd->replyport;

	      /*
	       * initialise the countdown timer process
	       */
	      
	      ticks = pinfo->kbd_autorepeat_start * TIMER_PERIOD / 1000;

	      /*
	       * start the countdown timer
	       */
	      
	      Signal( &lock );

	      /*
	       * wait for either the countdown timer to reach zero or
	       * the keyboard device to send an event
	       */
	       
	      fnrc = GetMsg( &mcb );

	      /*
	       * switch off the countdown timer
	       */
	      
	      ticks = -1;

	      /*
	       * check the return code of the GetMsg.  If it is EE_Abort
	       * then the countdown timer reached zero and we should auto-repeat
	       */
	      
	      while ((fnrc & EE_Mask) == EE_Abort && pKbd->ctrl->autoRepeat == AutoRepeatModeOn )
	        {
	          /*
		   * XXX
		   *
	           * potential bug here - what happens if autorepeat is switched off
	           * during this while loop - the loop will exit but there will be
	           * nothing in the data field of the message
	           */

		  /*
		   * generate another key down event
		   * NB/ we do not generate key up events - this will
		   * allow clever software to detect auto-repeats
		   */

	          current_event->Device.Keyboard.What = Keys_KeyDown;
		 
		  send_event( current_event, pPriv, pKbd );
		  
		  /*
		   * get ready to continue the auto-repeat
		   */
		  
                  *((int *)(&mcb)) = 0;

	          mcb.Timeout     = -1;
                  mcb.MsgHdr.Dest = pKbd->replyport;

		  /*
		   * initialise the countdown timer
		   */
		  
	          ticks = pinfo->kbd_autorepeat_interval * TIMER_PERIOD / 1000;

		  /*
		   * start countdown timer
		   */
		  
	          Signal( &lock );

		  /*
		   * wait for timeout or keyboard event
		   */
		  
	          fnrc = GetMsg( &mcb );

		  /*
		   * terminate countdown timer
		   */
		  
	          ticks = -1;
	        }
	    }
        }
      else
        {
          mcb.Timeout     = MaxInt;
          mcb.MsgHdr.Dest = pKbd->replyport;

	  /*
	   * no auto-repeating just wait for an event from the keyboard device
	   */

          while ((fnrc = GetMsg( &mcb )) < 0)
	    ;

	  /*
	   * Note/ the only error we should receive from the GetMsg is timeout
	   * so we are just going to ignore the value returned in fnrc
	   */
	}

      /*
       * we have received a message from the keyboard device
       * this message will contain one or more keyboard events
       * extract them, (in order) and send them off to the X
       * server
       */
	
     for (i = 0, current_event = (Firm_Event *)data;
     	  i < (mcb.MsgHdr.DataSize / sizeof( Firm_Event ));
     	  i++, current_event++)
       {
	 /*
	  * check the counter field to see if this is the keyboard event
	  * that we expected.  If it is a repeat of an earlier event
	  * then just ignore it
	  */
	 
         if (current_event->hdr.Counter < expected_counter)
      	   continue;
      	
         if (current_event->hdr.Counter > expected_counter)
           {
	     if (expected_counter == -1)
	       {
		 /*
		  * expected counter has not yet been initialised, so do so
		  */
		 
	         expected_counter = current_event->hdr.Counter;
	       }
	     else	
               {
                 if (fnrc == EventRc_IgnoreLost)
                   {
                     expected_counter = current_event->hdr.Counter;
                   }
                 else
                   {
                     NegAcknowledge( pstream, expected_counter );
                     break;
                   }
               }
           }
	   
         /* XXX - hack alert - translate 'bart' raw to X raw */

	 current_event->Device.Keyboard.Key += 7;

	 /* XXX - end of hack alert */
	 	 	
	 memcpy( (byte *)&(pPriv->buffer[ pPriv->Kbd_Head ]),
	 	 (byte *)current_event,
	 	 sizeof( Firm_Event ) );

	 pPriv->Kbd_Head = (pPriv->Kbd_Head + 1) % KBD_BUF_SIZE;

         if (fnrc == EventRc_Acknowledge)
           {
             Acknowledge( pstream, current_event->hdr.Counter );
          
             /* only one reply per message packet */
          
             fnrc = 0;
           }
        
         expected_counter++;
       }

     if (pKbd->enabled)
       {
         char 	data_here = 'K';
         
        
	 /*
	  * Signal( &input_event );
	  *
	  * signal waitfor that data has arrived
	  */
       	 
       	 /* NB/ write will block */
       	 
       	 write( pPriv->pipe, &data_here, 1 );
       } 

     --current_event;
     
     last_event_down = (current_event->Device.Keyboard.What == Keys_KeyDown );
#endif /* FAKE */
   }

fail:
  i = 'F';

  write( pPriv->pipe, &i, 1 );
  
  return;
  
} /* get_kbd */


DCB *
DevOpen(
  Device *	pdev,
  KbdInfoPtr	pinfo )
/*
 * create keyboard process and return initialised DCB
 */
{
  table_type	type = PC_UK;
  DCB *		pdcb = Malloc( sizeof( DCB ) );
    
   
#ifdef DEBUG
  IOdebug( "%s: Starting", PROGNAME );
#endif
  
  /*
   * see if we were able to allocate space for the structure
   */

  if (pdcb == NULL)
    {
      IOdebug( "%s: Unable to allocate space for DCB - aborting", PROGNAME );
      
      return NULL;
    }

  /*
   * initialise the DCB - DO NOT EDIT
   */
   
  pdcb->Device  = pdev;
  pdcb->Operate = DevOperate;
  pdcb->Close   = DevClose;

#ifndef streq
#define streq( s1, s2 )	(strcmp( s1, s2 ) == 0)  
#endif

#ifndef FAKE
  /*
   * find out what kind of keyboard we are supposed to be supporting
   */
  
  if (pinfo->ptype == NULL || streq( pinfo->ptype, "PC_UK" ))
    {
      type = PC_UK;
    }
  else if (streq( pinfo->ptype, "PC" ) || streq( pinfo->ptype, "PC_USA" ))
    {
      type = PC;
    }
  else if (streq( pinfo->ptype, "ST" ))
    {
      type = ST; 
    }
  else if (streq( pinfo->ptype, "PC_GERMAN" ))
    {
      type = PC_GERMAN;
    }
#endif /* !FAKE */
  
  /*
   * fill in missing info fields
   */
   
  build_translation_tables( type, &(pinfo->pKeySyms), &(pinfo->pModMap) );

  /*
   * start keyboard driver
   */
   
  Fork( KBD_STACK_SIZE, get_kbd, sizeof( pinfo ), pinfo );

  /*
   * return filled in DCB
   */

#ifdef DEBUG
  IOdebug( "%s: processes forked", PROGNAME );
#endif
  
  return pdcb;

} /* DevOpen */

