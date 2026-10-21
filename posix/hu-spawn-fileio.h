#include "hu-spawn.h"
/* Included after fileio.c: all descriptor-table access stays in Posix. */
void hu_snapshot_free(struct hu_fd_snapshot *v)
{
    int i;
    for(i=0;i<v->used;i++)Close(v->owned[i]);
    if(v->streams)Free(v->streams);if(v->owned)Free(v->owned);if(v->flags)Free(v->flags);
    memset(v,0,sizeof(*v));
}
int hu_snapshot(struct hu_fd_snapshot *v,int wanted,const struct hu_spawn_actions *actions,const struct hu_spawn_attr *attr)
{
    int i,j,k,keep,needed,e=0;
    memset(v,0,sizeof(*v));
    Wait(&fdlock);
    v->count=fdvecsize>wanted?fdvecsize:wanted;
    v->capacity=v->count+32;
    v->streams=Malloc((v->count+1)*sizeof(Stream *));
    v->owned=Malloc(v->capacity*sizeof(Stream *));
    v->flags=Malloc(v->count*sizeof(word));
    if(!v->streams||!v->owned||!v->flags){e=ENOMEM;goto done;}
    for(i=0;i<v->count;i++){v->streams[i]=(Stream *)MinInt;v->flags[i]=0;}
    v->streams[v->count]=NULL;
    for(i=0;i<fdvecsize;i++)if(fdvec[i].flags & FD_ALLOC) {
        v->flags[i]=fdvec[i].flags;
        keep=!(fdvec[i].flags&FD_CLOEXEC)&&!(attr&&attr->closefrom&&i>=attr->closefrom);
        needed=0;
        if(actions)for(k=0;k<actions->count;k++) {
            const struct hu_spawn_action *a=&actions->items[k];
            if(a->kind==1&&a->source==i)needed=1;
            if(a->fd==i)keep=0;
        }
        if(!keep&&!needed)continue;
        for(j=0;j<i;j++)if((fdvec[j].flags & FD_ALLOC)&&v->streams[j]!=(Stream *)MinInt&&fdvec[j].pstream==fdvec[i].pstream)break;
        if(j<i)v->streams[i]=v->streams[j];
        else {
            Stream *s=CopyStream(fdvec[i].pstream->stream);
            if(!s){e=posix_error(Result2(fdvec[i].pstream->stream));goto done;}
            v->streams[i]=s;v->owned[v->used++]=s;
        }
    }
done:
    Signal(&fdlock);if(e)hu_snapshot_free(v);return e;
}
