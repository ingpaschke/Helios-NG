/* Included after exec.c; uses its real child table, lock and waiter. */
#include "hu-spawn.h"

int hu_spawn_native(pid_t *result, const char *name,
	const struct hu_spawn_actions *actions, const struct hu_spawn_attr *attr,
	char *const argv[], char *const envv[])
{
	struct hu_fd_snapshot snapshot;
	Environ env;
	Object *objects[OV_End + 1];
	Object *directory = NULL, *source = NULL, *loader = NULL, *manager = NULL,
	       *code = NULL, *program = NULL;
	Stream *stream = NULL;
	char **variables = NULL;
	char identity[4][24], target[1024];
	const char *identity_keys[4] = {"_UID=", "_GID=", "_SIGMASK=", "_SIGIGNORE="};
	word identity_values[4];
	word result_code;
	int error = 0, env_count = 0, i, env_index, slot = 0, fd_count = 3,
	    placement = 0, fork_locked = 0;

	memset(&snapshot, 0, sizeof(snapshot));
	if( !result || !name || !argv || !argv[0] ) return EINVAL;
	if( attr )
	{
		placement = attr->placement;
		if( placement < 0 || placement > 2 || attr->closefrom < 0 ||
			attr->closefrom > 4096 )
			return EINVAL;
	}
	if( actions )
	{
		if( actions->count > 32 ) return EINVAL;
		for( i = 0; i < actions->count; i++ )
		{
			const struct hu_spawn_action *action = &actions->items[i];
			if( action->kind < 0 || action->kind > 2 || action->fd < 0 ||
				action->fd >= 4096 || action->source < 0 ||
				action->source >= 4096 )
				return EINVAL;
			if( action->kind == 2 && !action->path ) return EINVAL;
			if( fd_count <= action->fd ) fd_count = action->fd + 1;
			if( fd_count <= action->source ) fd_count = action->source + 1;
		}
	}
	if( placement == 2 && (!attr->processor || strlen(attr->processor) > 1000) )
		return EINVAL;

	/* Resolve paths relative to the child's working directory. */
	directory = Locate(
		cdobj(), attr && attr->directory ? (char *)attr->directory : ".");
	if( !directory )
	{
		error = posix_error(Result2(cdobj()));
		goto done;
	}
	source = Locate(directory, (char *)name);
	if( !source )
	{
		error = posix_error(Result2(directory));
		goto done;
	}

	/* Apply actions to a private descriptor snapshot. */
	error = hu_snapshot(&snapshot, fd_count, actions, attr);
	if( error ) goto done;
	if( actions )
		for( i = 0; i < actions->count; i++ )
		{
			const struct hu_spawn_action *action = &actions->items[i];
			if( action->kind == 0 )
				snapshot.streams[action->fd] = (Stream *)MinInt;
			else if( action->kind == 1 )
			{
				if( snapshot.streams[action->source] ==
					(Stream *)MinInt )
				{
					error = EBADF;
					goto done;
				}
				snapshot.streams[action->fd] =
					snapshot.streams[action->source];
				snapshot.flags[action->fd] &= ~FD_CLOEXEC;
			}
			else
			{
				Stream *opened =
					Open(directory, action->path, action->flags);
				if( !opened )
				{
					error = posix_error(Result2(directory));
					goto done;
				}
				snapshot.owned[snapshot.used++] = opened;
				snapshot.streams[action->fd] = opened;
				snapshot.flags[action->fd] = 0;
			}
		}
	for( i = 0; i < snapshot.count; i++ )
		if( (snapshot.flags[i] & FD_CLOEXEC) ||
			(attr && attr->closefrom && i >= attr->closefrom) )
			snapshot.streams[i] = (Stream *)MinInt;

	/* Inherit user variables; supply the current identity and signal masks. */
	if( envv )
		while( envv[env_count] )
		{
			if( ++env_count > 4096 )
			{
				error = E2BIG;
				goto done;
			}
		}
	variables = Malloc((env_count + 5) * sizeof(char *));
	if( !variables )
	{
		error = ENOMEM;
		goto done;
	}
	for( i = 0, env_index = 0; i < env_count; i++ )
	{
		if( !strncmp(envv[i], "_UID=", 5) || !strncmp(envv[i], "_GID=", 5) ||
			!strncmp(envv[i], "_SIGMASK=", 9) ||
			!strncmp(envv[i], "_SIGIGNORE=", 11) )
			continue;
		variables[env_index++] = envv[i];
	}
	identity_values[0] = UID;
	identity_values[1] = GID;
	identity_values[2] = mask.mask;
	identity_values[3] = mask.ignore;
	for( i = 0; i < 4; i++ )
	{
		int len = strlen(identity_keys[i]);
		strcpy(identity[i], identity_keys[i]);
		int2hex(identity_values[i], identity[i] + len);
		identity[i][len + 8] = 0;
		variables[env_index++] = identity[i];
	}
	variables[env_index] = NULL;

	/* The TFM chooses placement unless a local or named processor is requested. */
	if( placement == 0 && UserTFM != (Object *)MinInt && UserTFM != NULL )
		code = source;
	else
	{
		if( placement == 2 )
		{
			strcpy(target, attr->processor);
			strcat(target, "/loader");
			loader = Locate(NULL, target);
			if( !loader )
			{
				error = ENOENT;
				goto done;
			}
			strcpy(target, attr->processor);
			strcat(target, "/tasks");
		}
		else
			strcpy(target, "/tasks");
		manager = Locate(NULL, target);
		if( !manager )
		{
			error = ENOENT;
			goto done;
		}
		code = Load(loader, source);
		if( !code )
		{
			error = posix_error(Result2(source));
			goto done;
		}
	}

	/* Reserve a child slot, then talk to the process manager without
	 * forklock: it may be remote and slow, and waiter() and waitpid()
	 * need the lock meanwhile. childvec may move; keep only the index. */
	Wait(&forklock);
	fork_locked = 1;
	for( i = 1; i < childvecsize; i++ )
		if( !childvec[i].pid && childvec[i].status != PCB_Reserved )
		{
			slot = i;
			break;
		}
	if( !slot )
	{
		PCB *children = Malloc((childvecsize + childvecinc) * sizeof(PCB));
		if( !children )
		{
			error = ENOMEM;
			goto done;
		}
		memcpy(children, childvec, childvecsize * sizeof(PCB));
		memset(children + childvecsize, 0, childvecinc * sizeof(PCB));
		Free(childvec);
		childvec = children;
		slot = childvecsize;
		childvecsize += childvecinc;
	}
	memset(childvec + slot, 0, sizeof(PCB));
	childvec[slot].status = PCB_Reserved;
	Signal(&forklock);
	fork_locked = 0;

	program = Execute(manager ? manager : UserTFM, code);
	if( !program )
	{
		error = posix_error(Result2(code));
		goto done;
	}
	stream = Open(program, NULL, O_ReadWrite);
	if( !stream )
	{
		error = posix_error(Result2(program));
		goto done;
	}
	memset(objects, 0, sizeof(objects));
	objects[OV_Cdir] = directory;
	objects[OV_Task] = program;
	objects[OV_Code] = code;
	objects[OV_Source] = source;
	objects[OV_Parent] = ThisTask;
	objects[OV_Home] = Home;
	objects[OV_Console] = Console;
	objects[OV_CServer] = CServer;
	objects[OV_Session] = Session;
	objects[OV_TFM] = UserTFM;
	objects[OV_TForce] = program;
	env.Argv = (char **)argv;
	env.Envv = variables;
	env.Objv = objects;
	env.Strv = snapshot.streams;
	result_code = SendEnv(stream->Server, &env);
	if( result_code < 0 )
	{
		error = posix_error(result_code);
		goto done;
	}

	/* Publish the child only after SendEnv succeeds. */
	Wait(&forklock);
	fork_locked = 1;
	childvec[slot].pid = nextpid++;
	childvec[slot].pgrp = childvec[0].pgrp;
	childvec[slot].status = MinInt;
	childvec[slot].prog = program;
	childvec[slot].stream = stream;
	childvec[slot].starttime = (int)_cputime();
	if( !Fork(WaiterStack, waiter, 3 * sizeof(word), slot, childvec[slot].pid,
		    stream) )
	{
		error = ENOMEM;
		goto done;
	}
	*result = childvec[slot].pid;
	program = NULL;
	stream = NULL;

done:
	if( error && program )
	{
		if( stream )
			SendSignal(stream, SIGKILL);
		else
			Delete(program, NULL);
	}
	if( error && slot )
	{
		if( !fork_locked )
		{
			Wait(&forklock);
			fork_locked = 1;
		}
		memset(childvec + slot, 0, sizeof(PCB));
	}
	if( fork_locked ) Signal(&forklock);
	if( stream ) Close(stream);
	if( program ) Close(program);
	if( code && code != source ) Close(code);
	if( source ) Close(source);
	if( manager ) Close(manager);
	if( loader ) Close(loader);
	if( directory ) Close(directory);
	if( variables ) Free(variables);
	hu_snapshot_free(&snapshot);
	return error;
}
