#ifndef HELIOS_UNIX_SPAWN_H
#define HELIOS_UNIX_SPAWN_H

#include <sys/types.h>

#define HU_SPAWN_ACTIONS_MAX 32
#define HU_SPAWN_DEFAULT 0	/* let the session's TFM choose */
#define HU_SPAWN_LOCAL 1		/* use this processor */
#define HU_SPAWN_PROCESSOR 2	/* use attr.processor */

struct hu_spawn_action
{
	int kind;		/* 0: close, 1: duplicate, 2: open */
	int fd;
	int source;
	int flags;
	char *path;
};

struct hu_spawn_actions
{
	unsigned count;
	struct hu_spawn_action items[HU_SPAWN_ACTIONS_MAX];
};

struct hu_spawn_attr
{
	int placement;
	const char *processor;
	const char *directory;
	int closefrom;		/* close descriptors from here; zero disables */
};

void hu_spawn_actions_init(struct hu_spawn_actions *actions);
void hu_spawn_actions_destroy(struct hu_spawn_actions *actions);
int hu_spawn_addclose(struct hu_spawn_actions *actions, int fd);
int hu_spawn_adddup2(struct hu_spawn_actions *actions, int source, int fd);
int hu_spawn_addopen(struct hu_spawn_actions *actions, int fd,
	const char *path, int flags);

/* Return zero or an error number. Use native waitpid/kill on the returned PID.
 * This profile is for the application thread, not concurrent spawn/close. */
int hu_spawn(pid_t *pid, const char *path, const struct hu_spawn_actions *actions,
	const struct hu_spawn_attr *attr, char *const argv[], char *const envv[]);
int hu_spawnp(pid_t *pid, const char *file, const struct hu_spawn_actions *actions,
	const struct hu_spawn_attr *attr, char *const argv[], char *const envv[]);

#endif
