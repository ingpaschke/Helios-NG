/*
 * mouse device driver code
 *
 * Copyright (c) 1990, 1991 by Perihelion Software Ltd.
 *
 * All rights reserved
 *
 * Author:	N Clifton
 * Version:	0.4
 * Date:	23/2/91
 */
 
#include <helios.h>
#include <device.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "mdmouse.h"

extern void		Fork( WORD, VoidFnPtr, WORD, ... );
extern Port		EnableEvents( Stream *, WORD );
extern void 		Acknowledge( Stream *, word );
extern void 		NegAcknowledge( Stream *, word );
extern Stream *		fdstream( int fd );


#define NUM_EVENTS	(1 << 4)		/* must be a power of 2 */

typedef struct
  {
    Port		port;			/* port to gain mouse events */
    Semaphore		mouse_event;		/* signalled when an event arrives */
    Semaphore		buffer_lock;		/* lock on access to next three fields */
    Firm_Event *	buffer;			/* buffer of mouse events, NUM_EVENTS long */
    int			buffer_head;		/* place for next insertion of an event */
    int			global_x;		/* current location of mouse */
    int			global_y;		/* ditto */
    int			global_time;		/* time of move to this position */
    int			expected;		/* used for debugging mouse event capture */
    
  } Mouse_Data;


word
DevClose( DCB * pdcb )
/*
 * close down the device driver
 *
 * returns non-zero upon success, 0 upon failure
 */
{
  if (pdcb)
    Free( pdcb );

  /* we should really disable events on the mouse port and close down the stream ... */
  
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
  
  return;

  pdcb = pdcb;
  preq = preq;

} /* DevOperate */


void
get_mouse_event( Mouse_Data * pmouse )
{
  byte		data[ IOCDataMax ];
  MCB		mcb;


  /* point to my data buffer */
    
  mcb.Data = data;

  forever
    {
      Firm_Event *	current_event;
      int		num;


      /* store the port, wait forever */
      
      mcb.MsgHdr.Dest = pmouse->port;
      mcb.Timeout     = -1;

      /* wait for packets from mouse service */
      
      while (GetMsg( &mcb ) < 0)
      	; 

      /* calculate number of packets received */
      
      num           = (mcb.MsgHdr.DataSize / sizeof( Firm_Event ));

      /* point to first packet */
      
      current_event = (Firm_Event *)data;

      /* grab the data structure */
      
      Wait( &pmouse->buffer_lock );

      /* for each packet received ... */
      
      while (num--)
	{
	  if (current_event->Device.Mouse.Buttons == Buttons_Unchanged)
	    {
	      /*
	       * if the mouse has moved, but no buttons have been pressed
	       *  then update the global mouse position, but do not store
	       *  the packet
	       */
	  
	      pmouse->global_x    = current_event->Device.Mouse.X;
	      pmouse->global_y    = current_event->Device.Mouse.Y;
	      pmouse->global_time = current_event->hdr.Stamp;
	    }
	  else
	    {
	      /*
	       * otherwise store the packet in the buffer
	       */
	      
	      memcpy( (char *)(pmouse->buffer + pmouse->buffer_head), (char *)current_event, sizeof( *current_event ) );

	      pmouse->buffer_head++;
	      
	      pmouse->buffer_head &= (NUM_EVENTS - 1);
	    }
#ifdef DEBUG
	  ++pmouse->expected;
	  
	  if (current_event->hdr.Counter != pmouse->expected)
	    {
	      IOdebug( "expected count %d, got %d", pmouse->expected, current_event->hdr.Counter );

	      pmouse->expected = current_event->hdr.Counter;
	    }
#endif
	  ++current_event;
	}

      Signal( &pmouse->mouse_event );

      Signal( &pmouse->buffer_lock );
    }

  return;
  
} /* get_mouse_event */


#define any_up( state )	  	((state) & 0x8000)
#define any_down( state )	(!(any_up( state )) && ((state) & 0x00ff))

#define NUM_PROCESSES		10
#define THIRD_DELTA		20

#define add_event( pevent )									\
  Wait( &pPriv->lock );										\
  												\
  memcpy( (byte *)&(pPriv->buffer[ pPriv->Mouse_Head ]), (pevent), sizeof( Firm_Event ) ); 	\
  												\
  pPriv->Mouse_Head = (pPriv->Mouse_Head + 1) % MS_BUF_SIZE;					\
 												\
  Signal( &pPriv->lock );									\
  												\
  ++count;


#define loose_event()				\
  Wait( &pPriv->lock );				\
                                                \
  if (pPriv->Mouse_Head != pPriv->Mouse_Tail)	\
  {						\
    if (--pPriv->Mouse_Head < 0)		\
      {						\
	pPriv->Mouse_Head = MS_BUF_SIZE - 1;	\
      }						\
  }						\
  						\
  Signal( &pPriv->lock );
  

void
get_mouse( PtrPrivRec *	pMouse )
/*
 * mouse monitoring process
 */
{
  Mouse_Data *	pmouse;
  Firm_Event *	pold_buf;
  mdMsPrivPtr	pPriv;
  int		prev_stamp = 0;
  int		prev_state = 0;
  bool		third_down = false;		/* true if the emulated third mouse button is down */
  int		current_x;
  int		current_y;
  bool		do_third;		/* true iff we are emulating a third mouse button */
  int		i;
  

  if (pMouse->replyport == NullPort)
    {
      Object *	pobj;
      Stream *	pstream;
      
      
      /* find mouse service */
  
      if ((pobj = Locate( NULL, "/mouse" )) == NULL)
        {
          goto fail;
	}
      else if ((pstream = Open( pobj, "/mouse", O_ReadOnly )) == NULL)
        {
	  Close( pobj );
	  
          goto fail;
        }
      else if ((pMouse->replyport = EnableEvents( pstream, Event_Mouse )) == NullPort)
        {
	  Close( pobj );
          Close( pstream );
	  
          goto fail;
        }
      
      Close( pobj );
    }
  
  /* initialise buffer pointers */
  
  pPriv = (mdMsPrivPtr)(pMouse->devPrivate);
  
  pPriv->Mouse_Head = pPriv->Mouse_Tail = 0;
  
  /* should we emulate a third button ? */

  do_third   = (pPriv->num_buttons == 2);

  /* allocate space for data structures */
  
  pold_buf = (Firm_Event *)Malloc( NUM_EVENTS * sizeof( Firm_Event ) );

  pmouse   = (Mouse_Data *)Malloc( 1 * sizeof( Mouse_Data ) );

  if (pold_buf == NULL || pmouse == NULL)
    {
      /* oh oh ! */

      if (pold_buf)
	Free( pold_buf );
      else if (pmouse)
	Free( pmouse );
      
      goto fail;     
    }
  
  /* initialise the data structures */
  
  pmouse->port        = pMouse->replyport;
  pmouse->buffer      = (Firm_Event *)Malloc( NUM_EVENTS * sizeof( Firm_Event ) );
  pmouse->buffer_head = 0;
  pmouse->global_x    = 0;
  pmouse->global_y    = 0;
  pmouse->global_time = 0;
  pmouse->expected    = -1;
  
  InitSemaphore( &(pmouse->mouse_event), 0 );	/* no events received */
  InitSemaphore( &(pmouse->buffer_lock), 1 );	/* anyone can access the data structure */
  
  /* write to pipe to signal readiness and to initialise the connection */
  
  i = 'I';
  
  write( pPriv->pipe, &i, 1 );
  
  /*
   * fork off listener processes
   * We do this because we have been loosing packets from the mouse
   * service when there is no-one listening on the message port.
   * What happens is that during busy periods of mouse activity the
   * finite amount of time required to process mouse packets means
   * that one process cannot be listening to the message port all
   * the time.
   */
  
  for (i = 0; i < NUM_PROCESSES; i++)
    {
      Fork( IOCDataMax + 100, get_mouse_event, sizeof( pmouse ), pmouse );
    }

  /* wait for something to happen */
  
  Wait( &pmouse->mouse_event );

  /* grab data structure */
  
  Wait( &pmouse->buffer_lock );

  /* get current position */

  if (pmouse->buffer_head)
    {
      /*
       * if the first event includes any button presses then extract
       * position of button press as the current position ...
       */
  
      current_x = pmouse->buffer->Device.Mouse.X;
      current_y = pmouse->buffer->Device.Mouse.Y;
    }
  else
    {
      /*
       * ... otherwise use the global coordinates
       */
      
      current_x = pmouse->global_x;
      current_y = pmouse->global_y;
    }
  
  /* release data structure */

  Signal( &pmouse->buffer_lock );
  
  /* start forever loop */
  
  forever
    {
      int		x;
      int		y;
      int		head;
      Firm_Event *	pbuf;
      int		time;
      int		count = 0;
      
      
      /* wait for something to happen */
  
      Wait( &pmouse->mouse_event );

      /* grab data structure */
  
      Wait( &pmouse->buffer_lock );

      /* extract fields from structure */
  
      head = pmouse->buffer_head;
      pbuf = pmouse->buffer;
      x    = pmouse->global_x;
      y    = pmouse->global_y;
      time = pmouse->global_time;
      
      /* reset fields */

      pmouse->buffer      = pold_buf;
      pmouse->buffer_head = 0;
  
      /* reset event semaphore */

      i = TestSemaphore( &pmouse->mouse_event );

      while (i--)
	{
	  Wait( &pmouse->mouse_event );
	}
      
      /* release lock */

      Signal( &pmouse->buffer_lock );

      /* restore old buffer pointer */
      
      pold_buf = pbuf;

      /* if there are button events to process ... */

      if (head > 0)
	{
	  /* scan through the button events */
	  
	  for (i = 0; i < head; i++)
	    {
	      Firm_Event *	pdata  = &(pbuf[ i ]);
	      Mouse_Event *	pevent = &(pbuf[ i ].Device.Mouse);
	      IOEventHdr *	phdr   = &(pbuf[ i ].hdr);


	      /* if this event occured elsewhere than at current mouse position ... */
	      
	      if (pevent->X != current_x ||
		  pevent->Y != current_y  )
		{
		  int	save_buttons = pevent->Buttons;
		  int	save_x       = pevent->X;
		  int	save_y       = pevent->Y;
		  int	x            = save_x - current_x;
		  

		  /* Mouse service is too enthusiastic about moving to the right ! */
		  
		  if (x >= 18)
		    x = 8;
		  
		  /* move the mouse to the event's location */
		  
		  pevent->Buttons = Buttons_Unchanged;
		  pevent->X       = x;
		  pevent->Y	 -= current_y;

		  add_event( pdata );

		  current_x       = save_x;
		  current_y       = save_y;
		  pevent->Buttons = save_buttons;
		}

	      /* if we are emulating a third mouse button ... */
	      
	      if (do_third)
		{
		  int	new_state = pevent->Buttons;
	      
	      
		  /* IOdebug( "mouse: checking buttons, prev state = %x, new state = %x", prev_state, new_state ); */

		  if (!third_down                 &&
		      any_down( prev_state )      &&
		      any_down( pevent->Buttons ) &&
		      (phdr->Stamp - prev_stamp) < THIRD_DELTA )
		    {
		      third_down = true;
		  
		      /* loose previous event */

		      loose_event();
		      
		      pevent->Buttons = Buttons_Button2_Down;

		      /* IOdebug( "mouse: third button down" ); */
		    }
		  else if (third_down)
		    {
		      if (any_up( prev_state )     &&
			  any_up( pevent->Buttons ) )
			{
			  third_down = false;
		      
			  /* loose previous event */
		      
			  loose_event();
			  
			  pevent->Buttons = Buttons_Button2_Up;

			  /* IOdebug( "mouse: third button up" ); */
			}
		      else
			{
			  /* third button still down - ignore this event */

			  /* IOdebug( "mouse: third button still down" ); */
			  
			  prev_state = new_state;
			  prev_stamp = phdr->Stamp;
			  
			  continue;
			}
		    }
	      	
		  prev_state = new_state;
		  prev_stamp = phdr->Stamp;
		}

	      /* remove mouse's position information */

	      pevent->X = pevent->Y = 0;

	      /* and add to the queue */

	      add_event( pdata );
	    }
	}
      else
	{
	  /* if the mouse has moved .... */

	  if (current_x != x ||
	      current_y != y  )
	    {
	      Firm_Event	event;
	      int		new_x = x - current_x;
	      

	      /* Mouse service is too enthusiastic about mnoving to the right ! */

	      if (new_x >= 18)
		{
		  new_x = 8;
		}
	      
	      /* create a mouse movement event to current position */
	  
	      event.hdr.Type    = Event_Mouse;
	      event.hdr.Counter = 0;
	      event.hdr.Stamp   = time;
	  
	      event.Device.Mouse.X       = new_x;
	      event.Device.Mouse.Y       = y - current_y;
	      event.Device.Mouse.Buttons = Buttons_Unchanged;
		  
	      add_event( &event );

	      current_x = x;
	      current_y = y;
	    }
	}

      /* if the mouse is enabled ... */
      
      if (pMouse->enabled && count)
	{
	  /* tell the X server that events are here */

	  /* IOdebug( "mouse: writing to X server" ); */
	  
	  (void) write( pPriv->pipe, &i, count );
	}

      /* carry on processing */
    }

 fail:
  i = 'F';

  write( pPriv->pipe, &i, 1 );
  
  return;
  
} /* get_mouse */


DCB *
DevOpen(
  Device *	pdev,
  PtrPrivPtr	pPtrPriv )
/*
 * create mouse process and return initialised DCB
 */
{
  DCB *		pdcb = Malloc( sizeof( DCB ) );
    
   
  /*
   * see if we were able to allocate space for the structure
   */

  if (pdcb == NULL)
    {
      IOdebug( "Mouse Driver: Unable to allocate space for DCB" );
      
      return NULL;
    }

  /*
   * initialise the DCB - DO NOT EDIT
   */
   
  pdcb->Device  = pdev;
  pdcb->Operate = DevOperate;
  pdcb->Close   = DevClose;

  Fork( MOUSE_STACK_SIZE, get_mouse, sizeof( pPtrPriv ), pPtrPriv );
  
  /*
   * return filled in DCB
   */
 
  return pdcb;

} /* DevOpen */

