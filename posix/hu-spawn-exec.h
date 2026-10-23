/* Included after exec.c; uses its real child table, lock and waiter. */
#include "hu-spawn.h"
int hu_spawn_native(pid_t *result,const char *name,const struct hu_spawn_actions *actions,
                    const struct hu_spawn_attr *attr,char *const argv[],char *const envv[])
{
    struct hu_fd_snapshot fdv;
    Environ env;Object *objects[OV_End+1];
    Object *directory=NULL,*source=NULL,*loader=NULL,*manager=NULL,*code=NULL,*program=NULL;
    Stream *stream=NULL;
    char **vars=NULL;char ids[4][24],target[1024];
    const char *keys[4]={"_UID=","_GID=","_SIGMASK=","_SIGIGNORE="};
    word values[4];word rc;int e=0,n=0,i,j,slot=0,wanted=3,placement=0,locked=0;
    memset(&fdv,0,sizeof(fdv));
    if(!result||!name||!argv||!argv[0])return EINVAL;
    if(attr){placement=attr->placement;if(placement<0||placement>2||attr->closefrom<0||attr->closefrom>4096)return EINVAL;}
    if(actions) {
        if(actions->count>32)return EINVAL;
        for(i=0;i<actions->count;i++) {
            const struct hu_spawn_action *a=&actions->items[i];
            if(a->kind<0||a->kind>2||a->fd<0||a->fd>=4096||a->source<0||a->source>=4096)return EINVAL;
            if(a->kind==2&&!a->path)return EINVAL;
            if(wanted<=a->fd)wanted=a->fd+1;if(wanted<=a->source)wanted=a->source+1;
        }
    }
    if(placement==2&&(!attr->processor||strlen(attr->processor)>1000))return EINVAL;
    directory=Locate(cdobj(),attr&&attr->directory?(char *)attr->directory:".");
    if(!directory){e=posix_error(Result2(cdobj()));goto done;}
    source=Locate(directory,(char *)name);
    if(!source){e=posix_error(Result2(directory));goto done;}
    e=hu_snapshot(&fdv,wanted,actions,attr);if(e)goto done;
    if(actions)for(i=0;i<actions->count;i++) {
        const struct hu_spawn_action *a=&actions->items[i];
        if(a->kind==0)fdv.streams[a->fd]=(Stream *)MinInt;
        else if(a->kind==1) {
            if(fdv.streams[a->source]==(Stream *)MinInt){e=EBADF;goto done;}
            fdv.streams[a->fd]=fdv.streams[a->source];fdv.flags[a->fd]&=~FD_CLOEXEC;
        }else {
            Stream *s=Open(directory,a->path,a->flags);
            if(!s){e=posix_error(Result2(directory));goto done;}
            fdv.owned[fdv.used++]=s;fdv.streams[a->fd]=s;fdv.flags[a->fd]=0;
        }
    }
    for(i=0;i<fdv.count;i++)if((fdv.flags[i]&FD_CLOEXEC)||(attr&&attr->closefrom&&i>=attr->closefrom))fdv.streams[i]=(Stream *)MinInt;
    if(envv)while(envv[n]){if(++n>4096){e=E2BIG;goto done;}}
    vars=Malloc((n+5)*sizeof(char *));if(!vars){e=ENOMEM;goto done;}
    for(i=0,j=0;i<n;i++) {
        if(!strncmp(envv[i],"_UID=",5)||!strncmp(envv[i],"_GID=",5)||
           !strncmp(envv[i],"_SIGMASK=",9)||!strncmp(envv[i],"_SIGIGNORE=",11))continue;
        vars[j++]=envv[i];
    }
    values[0]=UID;values[1]=GID;values[2]=mask.mask;values[3]=mask.ignore;
    for(i=0;i<4;i++){int len=strlen(keys[i]);strcpy(ids[i],keys[i]);int2hex(values[i],ids[i]+len);ids[i][len+8]=0;vars[j++]=ids[i];}
    vars[j]=NULL;
    if(placement==0&&UserTFM!=(Object *)MinInt&&UserTFM!=NULL)code=source;
    else {
        if(placement==2) {
            strcpy(target,attr->processor);strcat(target,"/loader");loader=Locate(NULL,target);
            if(!loader){e=ENOENT;goto done;}
            strcpy(target,attr->processor);strcat(target,"/tasks");
        }else strcpy(target,"/tasks");
        manager=Locate(NULL,target);if(!manager){e=ENOENT;goto done;}
        code=Load(loader,source);if(!code){e=posix_error(Result2(source));goto done;}
    }
    /* Reserve a child slot, then talk to the process manager without
     * forklock: it may be remote and slow, and waiter() and waitpid()
     * need the lock meanwhile. childvec may move; keep only the index. */
    Wait(&forklock);locked=1;
    for(i=1;i<childvecsize;i++)if(!childvec[i].pid&&childvec[i].status!=PCB_Reserved){slot=i;break;}
    if(!slot) {
        PCB *v=Malloc((childvecsize+childvecinc)*sizeof(PCB));
        if(!v){e=ENOMEM;goto done;}
        memcpy(v,childvec,childvecsize*sizeof(PCB));memset(v+childvecsize,0,childvecinc*sizeof(PCB));
        Free(childvec);childvec=v;slot=childvecsize;childvecsize+=childvecinc;
    }
    memset(childvec+slot,0,sizeof(PCB));childvec[slot].status=PCB_Reserved;
    Signal(&forklock);locked=0;
    program=Execute(manager?manager:UserTFM,code);
    if(!program){e=posix_error(Result2(code));goto done;}
    stream=Open(program,NULL,O_ReadWrite);if(!stream){e=posix_error(Result2(program));goto done;}
    memset(objects,0,sizeof(objects));
    objects[OV_Cdir]=directory;objects[OV_Task]=program;objects[OV_Code]=code;objects[OV_Source]=source;
    objects[OV_Parent]=ThisTask;objects[OV_Home]=Home;objects[OV_Console]=Console;objects[OV_CServer]=CServer;
    objects[OV_Session]=Session;objects[OV_TFM]=UserTFM;objects[OV_TForce]=program;
    env.Argv=(char **)argv;env.Envv=vars;env.Objv=objects;env.Strv=fdv.streams;
    rc=SendEnv(stream->Server,&env);if(rc<0){e=posix_error(rc);goto done;}
    Wait(&forklock);locked=1;
    childvec[slot].pid=nextpid++;childvec[slot].pgrp=childvec[0].pgrp;
    childvec[slot].status=MinInt;childvec[slot].prog=program;childvec[slot].stream=stream;
    childvec[slot].starttime=(int)_cputime();
    if(!Fork(WaiterStack,waiter,3*sizeof(word),slot,childvec[slot].pid,stream)){e=ENOMEM;goto done;}
    *result=childvec[slot].pid;program=NULL;stream=NULL;
done:
    if(e&&program){if(stream)SendSignal(stream,SIGKILL);else Delete(program,NULL);}
    if(e&&slot){if(!locked){Wait(&forklock);locked=1;}memset(childvec+slot,0,sizeof(PCB));}
    if(locked)Signal(&forklock);
    if(stream)Close(stream);if(program)Close(program);
    if(code&&code!=source)Close(code);if(source)Close(source);
    if(manager)Close(manager);if(loader)Close(loader);if(directory)Close(directory);
    if(vars)Free(vars);hu_snapshot_free(&fdv);
    return e;
}
