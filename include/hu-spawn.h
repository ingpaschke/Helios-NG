#ifndef HELIOS_UNIX_SPAWN_H
#define HELIOS_UNIX_SPAWN_H
#include <sys/types.h>
#define HU_SPAWN_ACTIONS_MAX 32
#define HU_SPAWN_DEFAULT 0
#define HU_SPAWN_LOCAL 1
#define HU_SPAWN_PROCESSOR 2
struct hu_spawn_action {int kind,fd,source,flags;char *path;};
struct hu_spawn_actions {unsigned count;struct hu_spawn_action items[HU_SPAWN_ACTIONS_MAX];};
struct hu_spawn_attr {int placement;const char *processor;const char *directory;int closefrom;};
void hu_spawn_actions_init(struct hu_spawn_actions *);
void hu_spawn_actions_destroy(struct hu_spawn_actions *);
int hu_spawn_addclose(struct hu_spawn_actions *,int);
int hu_spawn_adddup2(struct hu_spawn_actions *,int,int);
int hu_spawn_addopen(struct hu_spawn_actions *,int,const char *,int);
/* Return zero or an error number. Use native waitpid/kill on the returned PID.
 * This profile is for the application thread, not concurrent spawn/close. */
int hu_spawn(pid_t *,const char *,const struct hu_spawn_actions *,
             const struct hu_spawn_attr *,char *const[],char *const[]);
int hu_spawnp(pid_t *,const char *,const struct hu_spawn_actions *,
              const struct hu_spawn_attr *,char *const[],char *const[]);
#endif
