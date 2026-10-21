/* Included by link1.c under FLOWCONTROL. All callers run at executive
 * priority. Keep one sender: it can block on a link while other processes
 * update the desired state, but must finish that broadcast before sending
 * the next one.
 *
 * The desired state has hysteresis. In particular, allocating the sender's
 * own stack must not undo an XON requested at the low-water mark.
 */
static void FlowControlDesired(RootStruct *root)
{
	if( root->BufferCount > root->FreePool->Size )
		root->Flags |= Root_Flags_flowoff;
	else if( root->FreePool->Size > 0 &&
	         root->BufferCount <= (root->FreePool->Size-1)/3 )
		root->Flags &= ~Root_Flags_flowoff;
}

static void FlowControlWorker(void)
{
	RootStruct *root = GetRoot();
	forever
	{
		word wanted;
		FlowControlDesired(root);
		wanted = (root->Flags & Root_Flags_flowoff) != 0;
		if( wanted == ((root->Flags & Root_Flags_xoffed) != 0) )
			break;
		FlowControl(wanted ? Proto_Xoff : Proto_Xon);
	}
	root->Flags &= ~Root_Flags_flowworker;
}

void FlowControlCheck(void)
{
	RootStruct *root = GetRoot();
	FlowControlDesired(root);
	if( root->Flags & Root_Flags_flowworker ) return;
	if( ((root->Flags & Root_Flags_flowoff) != 0) ==
	    ((root->Flags & Root_Flags_xoffed) != 0) ) return;
	root->Flags |= Root_Flags_flowworker;
	if( !NewWorker(FlowControlWorker,0,0) )
		root->Flags &= ~Root_Flags_flowworker;
	/* Allocation failure is retried by TimeoutHandler, even when the
	 * paused links deliver no further messages. Never block that handler. */
}
