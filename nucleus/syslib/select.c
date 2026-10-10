/*------------------------------------------------------------------------
--                                                                      --
--                     H E L I O S   N U C L E U S                      --
--                     ---------------------------                      --
--                                                                      --
--            Copyright (C) 1987,1990, Perihelion Software Ltd.         --
--                        All Rights Reserved.                          --
--                                                                      --
-- syslib/select.c							--
--                                                                      --
--	System Library, the common program interface to the operating   --
--	system.								--
--                                                                      --
--	Author:  NHG 16/8/87						--
--		 NHG 03/8/90						--
--                                                                      --
------------------------------------------------------------------------*/
/* SccsId:	 %W%	%G%	Copyright (C) 1987, Perihelion Software Ltd.*/
/* $Id: select.c,v 1.8 1993/07/09 13:36:01 nickc Exp $ */

#include "sys.h"

#define RetryTimeout	(20*OneSec)

word SelectStream(word nstreams, Stream **streams, word *flags, word timeout)
{
	MCB mcb;
	Port *ports = (Port *)Malloc(nstreams * (sizeof(Port)+2*sizeof(word)));
	word *modes;		/* the mode each entry selects		*/
	word *first;		/* entry that asks for a repeated stream */
	int i, j;
	word selected;
	word e;	
	word nfound = 0;
	word delayed = 0;
	
#ifdef SYSDEB
	SysDebug(stream)("Select(%d,%x,%d)",nstreams,streams,timeout);
#endif
	if( ports == NULL ) return EC_Error|SS_SysLib|EG_NoMemory;
	modes = (word *)(ports + nstreams);
	first = modes + nstreams;
	for( i = 0; i < nstreams; i++ ) modes[i] = flags[i] & Flags_Mode;

again:	
	for( i = 0; i < nstreams; i++ )
	{
		Stream *stream = streams[i];
		word mode = modes[i];

		ports[i] = NullPort;
		first[i] = -1;
		
		if( 	mode == 0 					||
			CheckStream(stream,C_ReOpen) != Err_Null 	|| 
			(stream->Flags&Flags_Selectable) == 0 
		   ) continue;

		flags[i] &= ~O_Selected;

		/* A stream given more than once (dup'ed descriptors) is	*/
		/* locked and asked only once, by its first entry, for all	*/
		/* the modes; the reply is shared out below.			*/
		for( j = 0; j < i; j++ )
			if( streams[j] == stream && modes[j] != 0 ) break;
		if( j < i ) { first[i] = j; continue; }
		for( j = i + 1; j < nstreams; j++ )
			if( streams[j] == stream ) mode |= modes[j];
	
#ifdef SYSDEB
	SysDebug(stream)("Select %x %S",flags[i],stream);
#endif
		/* lock the stream for the duration of the select */
		Wait(&stream->Mutex);
		
		if( stream->Type == Type_Pipe )	ports[i] = PipeSelect(stream,mode);
		else
		{
		retry:
			ports[i] = stream->Reply;
			
			InitMCB(&mcb,MsgHdr_Flags_preserve,stream->Server,ports[i],
				FC_GSP|FG_Select|stream->FnMod|mode);
			
			while( (e = PutMsg( &mcb ) ) != Err_Null ) 
			{
#ifdef SYSDEB
				SysDebug(error)("Select Request Error %E",e);
#endif
				if( (e & EC_Mask) >= EC_Error )
				{ 
					Signal(&stream->Mutex); 
					ports[i] = NullPort; 
					break; 
				}
				if( (e & EC_Mask) == EC_Warn ) 
				{ 
					if( (e = ReOpen(stream)) < Err_Null )
					goto retry; 
				}
			}
		}
	}
	
	/* With no port to wait on (no streams, or none selectable)	*/
	/* MultiWait returns at once: just wait for the timeout.	*/
	for( i = 0; i < nstreams && ports[i] == NullPort; i++ );
	if( i == nstreams )
	{
		if( timeout == -1 ) forever Delay(RetryTimeout);
		if( timeout > 0 ) Delay(timeout);
		Free(ports);
		return 0;
	}

	/* all the requests are out, get the replies 			*/
	/* first we get all those which have replied immediately 	*/

	e = 0;
	mcb.Timeout = 0;	/* 0 timeout == poll */
	for(;;)
	{
		selected = MultiWait(&mcb,nstreams,ports);
		if( selected == -1 )		/* no selection		*/
		{
			/* When the timeout is zero, we must wait here	*/
			/* a while to avoid a race condition. If we give*/
			/* up too soon, the server has no time to reply.*/
			
			if( timeout != 0 || delayed || nfound > 0 ) break;
			Delay(OneSec/100); /* == 10ms */
			delayed = 1;
			continue;
		}
		if( selected == EK_Timeout) continue;
		if( selected < 0 )		/* error - fail		*/
		{ e = mcb.MsgHdr.FnRc; break; }
		/* set the flags to the bits which were selected	*/
		flags[selected] &= ~Flags_Mode;
		flags[selected] |= O_Selected|(mcb.MsgHdr.FnRc&Flags_Mode);
		nfound++;
	}

	/* now wait for the timeout period or until a reply arrives	*/
	/* but only if no selects have succeded yet.			*/
	if( e == 0 && nfound == 0 && timeout != 0 )
	{
		mcb.Timeout = timeout;
		if( timeout == -1 || mcb.Timeout > RetryTimeout ) mcb.Timeout = RetryTimeout;
		mcb.MsgHdr.FnRc = 0;
		selected = MultiWait(&mcb,nstreams,ports);
		e = mcb.MsgHdr.FnRc;
		if( selected >= 0 && e > 0 ) 
		{
			flags[selected] &= ~Flags_Mode;
			flags[selected] |= O_Selected|(e&Flags_Mode);
			nfound++;
		}
		elif( e == EK_Timeout )
		{
			if( timeout != -1 ) timeout -= mcb.Timeout;
			else e = 0; 
		}
	}

	/* share the reply for a repeated stream among its entries,	*/
	/* each one getting only the modes it selects			*/
	for( i = 0; i < nstreams; i++ )
	{
		if( (j = (int)first[i]) < 0 ) continue;
		first[j] = j;
		if( (flags[j] & O_Selected) && (flags[j] & modes[i]) != 0 )
		{
			flags[i] &= ~Flags_Mode;
			flags[i] |= O_Selected|(flags[j] & modes[i]);
			nfound++;
		}
	}
	for( i = 0; i < nstreams; i++ )
	{
		if( first[i] != i || (flags[i] & O_Selected) == 0 ) continue;
		if( (flags[i] & modes[i]) == 0 )
		{
			flags[i] &= ~(O_Selected|Flags_Mode);
			flags[i] |= modes[i];
			nfound--;
		}
		else flags[i] &= ~Flags_Mode|modes[i];
	}
	
	/* free all the ports and unlock the streams */
	for( i = 0; i < nstreams; i++ )
	{
		if( ports[i] != NullPort ) 
		{
			FreePort(ports[i]);
			streams[i]->Reply = NewPort();
			Signal(&(streams[i]->Mutex));
		}
	}

	/* If nothing has been selected, and we still have some	*/
	/* of the timeout to go, go back and do it again.	*/
	
	if( e >= 0 && nfound == 0 && ( timeout > 0 || timeout == -1) ) goto again;
	
	Free(ports);
#ifdef SYSDEB
	SysDebug(stream)("Select: error %x nfound %d",e,nfound);
#endif		
	return e<0?e:nfound;
}
