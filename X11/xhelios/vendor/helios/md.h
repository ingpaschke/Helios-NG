/*-
 * md.h --
 *	Internal declarations for the md ddx interface
 *
 * Copyright (c) 1990, 1991 by Perihelion Software Ltd.
 *
 * All rights reserved
 * 
 */

#ifndef _MD_H_
#define _MD_H_

#include <helios.h>
#include <message.h>
#include <ioevents.h>

#undef BYTE

#include <signal.h>
#include <time.h>

#include    <X11/X.h>

/*
 * MAXEVENTS is the maximum number of events the mouse and keyboard functions
 * will read on a given call to their GetEvents vectors.
 */

#define MAXEVENTS 	32

typedef struct
{
	IOEventHdr	hdr;

	union
	{
		Mouse_Event	 Mouse;
		Keyboard_Event	 Keyboard;
		
	} Device;
	
} Firm_Event;

#define	MIN_KEYCODE	8	/* necessary to avoid the mouse buttons */

#define AUTOREPEAT_INITIATE	(200)		/* milliseconds */
#define AUTOREPEAT_DELAY	(50)		/* milliseconds */

typedef struct ptrPrivate 
{
	Port		replyport;
	bool		enabled;
	int		pipe;
	Firm_Event *(*	GetEvents)();
	void (*		ProcessEvent)();
	void (* 	DoneEvents)();
	short		x;
	short		y;
	void *		devPrivate;

} PtrPrivRec, * PtrPrivPtr;

typedef struct kbdPrivate
{
	Port		replyport;
	bool		enabled;
	int		pipe;
	Firm_Event *(*	GetEvents)();
	void (*		ProcessEvent)();
	void (*		DoneEvents)();
	void *		devPrivate;
	bool		map_q;
	int		offset;
	KeybdCtrl      *ctrl;

} KbPrivRec, *KbPrivPtr;

#endif /* _MD_H_ */

