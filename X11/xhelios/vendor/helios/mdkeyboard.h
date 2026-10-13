/*-
 * mdkeyboard.h --
 *	Internal declarations for the md keyboard interface
 *
 * Copyright (c) 1990, 1991 by Perihelion Software Ltd.
 *
 * All rights reserved.
 * 
 */

#ifndef _MDKEYBOARD_H_
#define _MDKEYBOARD_H_

#include <helios.h>
#include <codes.h> 
#include <ioevents.h>
#include <message.h>
#include <gsp.h>
#include <syslib.h>
#include <sem.h>
#include <X11/keysym.h>

typedef struct
{
    int			click, bell, bell_pitch, bell_duration;
    bool		autoRepeat;
    unsigned char	autoRepeats[ 32 ];
    unsigned long	leds;
    
} KeybdCtrl;

#ifndef _MD_H_
#include "md.h"
#endif

typedef struct 
{
    KeySym  *		map;
    KeyCode 		minKeyCode;
    KeyCode		maxKeyCode;
    int     		mapWidth;
    
} KeySymsRec, * KeySymsPtr;

typedef struct _KeyClassRec
{
    char		down[ 32 ];
    KeyCode *		modifierKeyMap;
    KeySymsRec		curKeySyms;
    int			modifierKeyCount[ 8 ];
    char		modifierMap[ 256 ];
    char		maxKeysPerModifier;
    unsigned int	state;

} KeyClassRec, * KeyClassPtr;

#define KBD_BUF_SIZE	256

#define KBD_STACK_SIZE	(20000 + IOCDataMax)

typedef struct
{
  int 			Kbd_Head;
  int			Kbd_Tail;
  int	    		trans;          	/* Original translation form */
  Firm_Event 		buffer[ KBD_BUF_SIZE ];
  int			pipe;			/* used to tell WaitFor() that data has arrived */

} mdKbPrivRec, * 	mdKbPrivPtr;

typedef struct
{
	KbPrivPtr	pKbPriv;
	KeyClassPtr	pkey;
	int		kbd_autorepeat_start;
	int		kbd_autorepeat_interval;
	int		autorepeat_filter_count;
	int *		autorepeat_filter_values;
	KeySymsRec *	pKeySyms;		/* keyboard translation table - return */
	char *		pModMap;		/* keyboard modifier map      - return */
	char *		ptype;			/* string indicating desired keyboard type */
					
} KbdInfo, * KbdInfoPtr;

extern int		kbd_autorepeat_start;
extern int		kbd_autorepeat_interval;
extern int		autorepeat_filter_count;
extern int		autorepeat_filter_values[ 15 ];

#endif /* _MDKEYBOARD_H_ */
