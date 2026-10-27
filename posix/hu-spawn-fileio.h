#include "hu-spawn.h"

/* Included after fileio.c: all descriptor-table access stays in Posix. */
/* A CopyStream copy shares its parent's Server port, and closing it as a
 * served stream would free that port under the parent's descriptor: drop
 * only the copy. Pipe copies are independent and SendEnv closes them.
 * Streams opened for the child are closed: the child ReOpens its own. */
void hu_snapshot_free(struct hu_fd_snapshot *snapshot)
{
	int i;
	for( i = 0; i < snapshot->used; i++ )
	{
		Stream *copy = snapshot->owned[i];
		if( i < snapshot->copies && copy->Type != Type_Pipe )
			copy->Type = Type_Pseudo;
		Close(copy);
	}
	if( snapshot->streams ) Free(snapshot->streams);
	if( snapshot->owned ) Free(snapshot->owned);
	if( snapshot->flags ) Free(snapshot->flags);
	memset(snapshot, 0, sizeof(*snapshot));
}

int hu_snapshot(struct hu_fd_snapshot *snapshot, int wanted,
	const struct hu_spawn_actions *actions, const struct hu_spawn_attr *attr)
{
	int i, j, action_index, keep, needed, error = 0;

	memset(snapshot, 0, sizeof(*snapshot));
	Wait(&fdlock);
	snapshot->count = fdvecsize > wanted ? fdvecsize : wanted;
	snapshot->capacity = snapshot->count + 32;
	snapshot->streams = Malloc((snapshot->count + 1) * sizeof(Stream *));
	snapshot->owned = Malloc(snapshot->capacity * sizeof(Stream *));
	snapshot->flags = Malloc(snapshot->count * sizeof(word));
	if( !snapshot->streams || !snapshot->owned || !snapshot->flags )
	{
		error = ENOMEM;
		goto done;
	}
	for( i = 0; i < snapshot->count; i++ )
	{
		snapshot->streams[i] = (Stream *)MinInt;
		snapshot->flags[i] = 0;
	}
	snapshot->streams[snapshot->count] = NULL;

	/* A slot with FD_ALLOC set but no pstream is an open in progress in
	 * another thread (fileio.c allocates the slot before the open
	 * completes); it has no stream to inherit yet, so skip it. */
	for( i = 0; i < fdvecsize; i++ )
		if( (fdvec[i].flags & FD_ALLOC) && fdvec[i].pstream != NULL )
		{
			snapshot->flags[i] = fdvec[i].flags;
			keep = !(fdvec[i].flags & FD_CLOEXEC) &&
			       !(attr && attr->closefrom && i >= attr->closefrom);
			needed = 0;
			if( actions )
				for( action_index = 0; action_index < actions->count;
					action_index++ )
				{
					const struct hu_spawn_action *action =
						&actions->items[action_index];
					if( action->kind == 1 && action->source == i )
						needed = 1;
					if( action->fd == i ) keep = 0;
				}
			if( !keep && !needed ) continue;
			for( j = 0; j < i; j++ )
				if( (fdvec[j].flags & FD_ALLOC) &&
					snapshot->streams[j] != (Stream *)MinInt &&
					fdvec[j].pstream == fdvec[i].pstream )
					break;
			if( j < i )
				snapshot->streams[i] = snapshot->streams[j];
			else
			{
				Stream *copy = CopyStream(fdvec[i].pstream->stream);
				if( !copy )
				{
					error = posix_error(
						Result2(fdvec[i].pstream->stream));
					goto done;
				}
				snapshot->streams[i] = copy;
				snapshot->owned[snapshot->used++] = copy;
				snapshot->copies = snapshot->used;
			}
		}

done:
	Signal(&fdlock);
	if( error ) hu_snapshot_free(snapshot);
	return error;
}
