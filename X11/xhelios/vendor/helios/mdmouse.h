/*-
 * mdmouse.h --
 *	Internal declarations for the md mouse interface
 *
 * Copyright (c) 1990, 1991 by Perihelion Software Ltd.
 *
 * All rights reserved
 * 
 */

#ifndef _MDMOUSE_H_
#define _MDMOUSE_H_

#include <helios.h>
#include <codes.h> 
#include <syslib.h>
#include <message.h>
#include <gsp.h>
#include <ioevents.h>
#include <xioevents.h>
#include <sem.h>


#ifndef _MD_H_
#include "md.h"
#endif

/*
 * size of internal buffer of mouse events
 * should be an exact power of two
 *
 */

#define MS_BUF_SIZE	  256

#define MOUSE_STACK_SIZE  (10000 + IOCDataMax)

/*
 * private mouse structure
 */
 
typedef struct
{
  Semaphore	lock;		/* lock on this data structure */
  int 		Mouse_Head;
  int		Mouse_Tail;
  int		buttons;	/* current button status, 1 = down, 0 = up, bit 0 = button0, bit1 = button1 */
  bool		mouseMoved;	/* flag set if mouse has moved */
  Firm_Event 	buffer[ MS_BUF_SIZE ];
  int		pipe;		/* write to this pipe to signal event arrival */
  int		debug;		/* field used for debugging */
  int		num_buttons;	/* number of buttons on mouse */
    
} mdMsPrivRec, * mdMsPrivPtr;

extern Semaphore	input_event;
extern Semaphore        input_event_lock;

#endif /* _MDMOUSE_H_ */
