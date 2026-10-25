/* SSH sessions run in Linux workers. Registry and front descriptors belong
 * to the coroutine thread; only a worker may call its session's libssh2 API.
 * Bounded nonblocking socketpairs carry terminal bytes. Control requests
 * copy state under a mutex and never wait for network/authentication I/O. */
#include "net_ssh.h"
#include "net_tls.h"
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <time.h>
#ifdef IOS_SSH
#include <libssh2.h>
#include <pthread.h>
#include <signal.h>

#define CONNECTING 1
#define HOSTKEY 2
#define AUTH 3
#define OPENING 4
#define READY 5
#define CLOSED 6
#define FAILED 7
typedef struct SSH SSH;
struct SSH {
    SSH *next;
    int fd,front,back,network,refs,cancel,state,error,trust,auth_ready;
    int exit_status,exit_valid;
    unsigned timeout,port,columns,rows,resize_serial,methods,attempt;
    int pty;
    char host[256],user[64],terminal[64],command[768];
    char key_type[64],fingerprint[96],message[316];
    unsigned char auth[IOS_SSH_AUTH_SIZE];
    pthread_mutex_t lock;
};
static SSH *connections;
typedef struct Upload Upload;
struct Upload {Upload *next;int fd;unsigned op,size,used;unsigned char *data;};
static Upload *uploads;
static void forget_upload(int fd)
{
    Upload **slot=&uploads,*p;
    while(*slot&&(*slot)->fd!=fd)slot=&(*slot)->next;
    if((p=*slot)!=NULL){*slot=p->next;memset(p->data,0,p->size);free(p->data);free(p);}
}
static pthread_once_t library_once=PTHREAD_ONCE_INIT;
static int library_error;
static void initialize(void){library_error=libssh2_init(0);}
static unsigned get32(const unsigned char *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static void put32(unsigned char *p,unsigned n)
{p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static long long milliseconds(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static SSH *lookup(int fd){SSH *p;for(p=connections;p;p=p->next)if(p->fd==fd)return p;return NULL;}
static int cancelled(SSH *p)
{int r;pthread_mutex_lock(&p->lock);r=p->cancel;pthread_mutex_unlock(&p->lock);return r;}
static int state(SSH *p)
{int r;pthread_mutex_lock(&p->lock);r=p->state;pthread_mutex_unlock(&p->lock);return r;}
static void release(SSH *p)
{int last;pthread_mutex_lock(&p->lock);last=!--p->refs;pthread_mutex_unlock(&p->lock);
 if(last){pthread_mutex_destroy(&p->lock);memset(p,0,sizeof(*p));free(p);}}
static void failure(SSH *p,int error,const char *message)
{pthread_mutex_lock(&p->lock);p->state=FAILED;p->error=error;
 snprintf(p->message,sizeof(p->message),"%s",message);pthread_mutex_unlock(&p->lock);}
static void ssh_failure(SSH *p,LIBSSH2_SESSION *s)
{char *message=NULL;libssh2_session_last_error(s,&message,NULL,0);failure(p,EIO,message?message:"SSH protocol failure");}
static int wait_network(SSH *p,LIBSSH2_SESSION *s,long long deadline)
{
    struct pollfd f;int directions,left,r;
    if(cancelled(p))return -1;
    left=(int)(deadline-milliseconds());
    if(left<=0){failure(p,ETIMEDOUT,"SSH operation timed out");return -1;}
    directions=libssh2_session_block_directions(s);
    f.fd=p->network;f.events=(directions&LIBSSH2_SESSION_BLOCK_INBOUND?POLLIN:0)|
        (directions&LIBSSH2_SESSION_BLOCK_OUTBOUND?POLLOUT:0);
    if(!f.events)f.events=POLLIN;
    r=poll(&f,1,left>100?100:left);
    if(r<0 && errno!=EINTR){failure(p,errno,strerror(errno));return -1;}
    return 0;
}
static short network_events(LIBSSH2_SESSION *s)
{
    int d=libssh2_session_block_directions(s);
    short events=(d&LIBSSH2_SESSION_BLOCK_INBOUND?POLLIN:0)|(d&LIBSSH2_SESSION_BLOCK_OUTBOUND?POLLOUT:0);
    return events?events:POLLIN;
}
/* The same operation and arguments are retained across EAGAIN retries. */
#define RETRY(operation) do { \
    deadline=milliseconds()+p->timeout; \
    do { if(cancelled(p))goto done; rc=(operation); \
         if(rc!=LIBSSH2_ERROR_EAGAIN)break; \
         if(wait_network(p,s,deadline)<0)goto done; \
    } while(1); \
} while(0)
static int await_control(SSH *p,int auth)
{
    long long deadline=milliseconds()+300000;
    while(!cancelled(p)) {
        int ready;
        pthread_mutex_lock(&p->lock);ready=auth?p->auth_ready:p->trust;pthread_mutex_unlock(&p->lock);
        if(ready)return ready;
        if(milliseconds()>=deadline){failure(p,ETIMEDOUT,"SSH prompt timed out");return 0;}
        poll(NULL,0,20);
    }
    return 0;
}
static void *worker(void *arg)
{
    SSH *p=arg;LIBSSH2_SESSION *s=NULL;LIBSSH2_CHANNEL *channel=NULL;
    unsigned char auth[IOS_SSH_AUTH_SIZE],outgoing[8192],incoming[8192];
    int rc=0,out_size=0,out_pos=0,in_size=0,in_pos=0,app_eof=0,eof_sent=0;
    int stream=0;unsigned resize_serial=0;
    short read_want=0,write_want=0;
    long long deadline;
    const char *hash,*key,*methods;size_t key_size;int type;unsigned i;
    pthread_once(&library_once,initialize);
    if(library_error){failure(p,EIO,"Cannot initialize libssh2");goto done;}
    s=libssh2_session_init();if(!s){failure(p,ENOMEM,"Cannot allocate SSH session");goto done;}
    libssh2_session_set_blocking(s,0);
    RETRY(libssh2_session_handshake(s,p->network));
    if(rc){ssh_failure(p,s);goto done;}
    hash=libssh2_hostkey_hash(s,LIBSSH2_HOSTKEY_HASH_SHA256);
    key=libssh2_session_hostkey(s,&key_size,&type);
    if(!hash||!key||key_size<4){failure(p,EIO,"SSH server supplied no host key");goto done;}
    pthread_mutex_lock(&p->lock);
    strcpy(p->fingerprint,"SHA256-hex:");
    for(i=0;i<32;i++)sprintf(p->fingerprint+11+2*i,"%02x",(unsigned char)hash[i]);
    i=((unsigned char)key[0]<<24)|((unsigned char)key[1]<<16)|((unsigned char)key[2]<<8)|(unsigned char)key[3];
    if(i && i<sizeof(p->key_type) && i<=key_size-4)memcpy(p->key_type,key+4,i);
    p->state=HOSTKEY;pthread_mutex_unlock(&p->lock);
    rc=await_control(p,0);
    if(rc!=1){if(rc<0)failure(p,EACCES,"SSH host key declined");goto done;}
    deadline=milliseconds()+p->timeout;
    do {
        methods=libssh2_userauth_list(s,p->user,strlen(p->user));
        if(methods || libssh2_userauth_authenticated(s))break;
        if(libssh2_session_last_errno(s)!=LIBSSH2_ERROR_EAGAIN){ssh_failure(p,s);goto done;}
        if(wait_network(p,s,deadline)<0)goto done;
    }while(1);
    pthread_mutex_lock(&p->lock);
    p->methods=methods?((strstr(methods,"password")?1:0)|(strstr(methods,"publickey")?2:0)):0;
    pthread_mutex_unlock(&p->lock);
    while(!libssh2_userauth_authenticated(s)) {
        if(!p->methods){failure(p,EOPNOTSUPP,"Server offers no password or public-key authentication");goto done;}
        pthread_mutex_lock(&p->lock);p->state=AUTH;pthread_mutex_unlock(&p->lock);
        if(!await_control(p,1))goto done;
        pthread_mutex_lock(&p->lock);memcpy(auth,p->auth,sizeof(auth));
        memset(p->auth,0,sizeof(p->auth));p->auth_ready=0;pthread_mutex_unlock(&p->lock);
        if(get32(auth)==1) {
            RETRY(libssh2_userauth_password(s,p->user,(char *)auth+16));
        }else {
            RETRY(libssh2_userauth_publickey_frommemory(s,p->user,strlen(p->user),NULL,0,
                (char *)auth+528,get32(auth+4),(char *)auth+16));
        }
        memset(auth,0,sizeof(auth));
        if(rc){pthread_mutex_lock(&p->lock);p->attempt++;
            snprintf(p->message,sizeof(p->message),"Authentication failed");pthread_mutex_unlock(&p->lock);}
    }
    pthread_mutex_lock(&p->lock);p->state=OPENING;p->message[0]=0;pthread_mutex_unlock(&p->lock);
    deadline=milliseconds()+p->timeout;
    while(!(channel=libssh2_channel_open_session(s))) {
        if(libssh2_session_last_errno(s)!=LIBSSH2_ERROR_EAGAIN){ssh_failure(p,s);goto done;}
        if(wait_network(p,s,deadline)<0)goto done;
    }
    if(p->pty) {
        RETRY(libssh2_channel_request_pty_ex(channel,p->terminal,strlen(p->terminal),NULL,0,p->columns,p->rows,0,0));
        if(rc){ssh_failure(p,s);goto done;}
    }
    if(p->command[0]){RETRY(libssh2_channel_exec(channel,p->command));}
    else {RETRY(libssh2_channel_shell(channel));}
    if(rc){ssh_failure(p,s);goto done;}
    pthread_mutex_lock(&p->lock);p->state=READY;pthread_mutex_unlock(&p->lock);
    while(!cancelled(p)) {
        struct pollfd f[2];int n,progress=0;
        unsigned serial,columns,rows;
        pthread_mutex_lock(&p->lock);serial=p->resize_serial;columns=p->columns;rows=p->rows;pthread_mutex_unlock(&p->lock);
        if(p->pty && resize_serial!=serial) {
            RETRY(libssh2_channel_request_pty_size(channel,columns,rows));
            if(rc){ssh_failure(p,s);goto done;}resize_serial=serial;
        }
        if(in_size) {
            n=send(p->back,incoming+in_pos,in_size-in_pos,MSG_NOSIGNAL);
            if(n>0){in_pos+=n;progress=1;if(in_pos==in_size)in_size=in_pos=0;}
            else if(n<0 && errno!=EAGAIN && errno!=EINTR)goto done;
        }
        if(!out_size && !app_eof) {
            n=recv(p->back,outgoing,sizeof(outgoing),0);
            if(n>0){out_size=n;out_pos=0;progress=1;}
            else if(n==0){app_eof=1;progress=1;}
            else if(errno!=EAGAIN && errno!=EINTR)goto done;
        }
        if(out_size) {
            n=libssh2_channel_write(channel,(char *)outgoing+out_pos,out_size-out_pos);
            if(n>0){out_pos+=n;progress=1;write_want=0;if(out_pos==out_size)out_size=out_pos=0;}
            else if(n==LIBSSH2_ERROR_EAGAIN)write_want=network_events(s);
            else if(n<0 && n!=LIBSSH2_ERROR_EAGAIN){ssh_failure(p,s);goto done;}
        }
        if(!in_size) {
            /* Merge stdout/stderr for this terminal-oriented provider. */
            for(i=0;i<2;i++) {
                n=libssh2_channel_read_ex(channel,stream,(char *)incoming,sizeof(incoming));stream^=1;
                if(n>0){in_size=n;read_want=0;progress=1;break;}
                if(n==LIBSSH2_ERROR_EAGAIN)read_want=network_events(s);
                if(n<0 && n!=LIBSSH2_ERROR_EAGAIN){ssh_failure(p,s);goto done;}
            }
        }
        if(app_eof && !out_size && !eof_sent) {
            rc=libssh2_channel_send_eof(channel);
            if(!rc){eof_sent=1;progress=1;}
            else if(rc==LIBSSH2_ERROR_EAGAIN)write_want=network_events(s);
            else if(rc!=LIBSSH2_ERROR_EAGAIN){ssh_failure(p,s);goto done;}
        }
        if(libssh2_channel_eof(channel) && !in_size)break;
        if(progress)continue;
        f[0].fd=p->back;f[0].events=(!app_eof&&!out_size?POLLIN:0)|(in_size?POLLOUT:0);
        f[1].fd=p->network;
        f[1].events=((out_size||(app_eof&&!eof_sent))?write_want:0)|(!in_size?(read_want?read_want:POLLIN):0);
        if(!f[0].events)f[0].fd=-1;if(!f[1].events)f[1].fd=-1;
        n=poll(f,2,50);
        if(n<0 && errno!=EINTR){failure(p,errno,strerror(errno));goto done;}
    }
    if(!cancelled(p)) {
        char *exit_signal=NULL;size_t exit_signal_size=0;
        RETRY(libssh2_channel_close(channel));
        if(rc){ssh_failure(p,s);goto done;}
        libssh2_channel_get_exit_signal(channel,&exit_signal,&exit_signal_size,NULL,NULL,NULL,NULL);
        pthread_mutex_lock(&p->lock);
        p->exit_status=exit_signal_size?255:libssh2_channel_get_exit_status(channel);p->exit_valid=1;
        if(exit_signal_size)snprintf(p->message,sizeof(p->message),"Remote process terminated by %s",exit_signal);
        pthread_mutex_unlock(&p->lock);
        if(exit_signal)libssh2_free(s,exit_signal);
    }
done:
    memset(auth,0,sizeof(auth));
    /* Tear down TCP before freeing a nonblocking session: channel_free can
     * otherwise return EAGAIN and leave its channel/session allocated. */
    shutdown(p->network,SHUT_RDWR);
    if(s){libssh2_session_set_blocking(s,1);libssh2_session_set_timeout(s,100);}
    if(channel)libssh2_channel_free(channel);
    if(s)libssh2_session_free(s);
    close(p->network);close(p->back);
    pthread_mutex_lock(&p->lock);if(p->state!=FAILED)p->state=CLOSED;pthread_mutex_unlock(&p->lock);
    release(p);return NULL;
}
static int start(int fd,const unsigned char *w,unsigned size)
{
    SSH *p;pthread_t thread;int pair[2],type,flags,error;
    struct sockaddr_storage peer;socklen_t n=sizeof(type);
    if(size!=IOS_SSH_START_SIZE || get32(w)!=1 || get32(w+4)<100 || get32(w+4)>120000 ||
       !get32(w+8)||get32(w+8)>65535 || get32(w+12)>65535 || get32(w+16)>65535 ||
       get32(w+20)>1 || get32(w+24)||get32(w+28) ||
       !w[32]||!memchr(w+32,0,256)||!w[288]||!memchr(w+288,0,64)||
       !memchr(w+352,0,64)||!memchr(w+416,0,768)) {errno=EINVAL;return -1;}
    if(lookup(fd)||ios_tls_fd(fd,0)!=fd){errno=EALREADY;return -1;}
    if(getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&n)<0)return -1;
    if(type!=SOCK_STREAM){errno=EOPNOTSUPP;return -1;}
    n=sizeof(peer);if(getpeername(fd,(struct sockaddr *)&peer,&n)<0)return -1;
    if(peer.ss_family!=AF_INET && peer.ss_family!=AF_INET6){errno=EAFNOSUPPORT;return -1;}
    flags=fcntl(fd,F_GETFL);if(flags<0)return -1;
    if(!(flags&O_NONBLOCK)){errno=EINVAL;return -1;}
    p=calloc(1,sizeof(*p));if(!p)return -1;
    if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair)<0){free(p);return -1;}
    p->network=fcntl(fd,F_DUPFD_CLOEXEC,0);
    if(p->network<0){close(pair[0]);close(pair[1]);free(p);return -1;}
    p->fd=fd;p->front=pair[0];p->back=pair[1];p->refs=2;p->state=CONNECTING;
    p->timeout=get32(w+4);p->port=get32(w+8);p->columns=get32(w+12);p->rows=get32(w+16);p->pty=get32(w+20);
    memcpy(p->host,w+32,256);memcpy(p->user,w+288,64);memcpy(p->terminal,w+352,64);memcpy(p->command,w+416,768);
    pthread_mutex_init(&p->lock,NULL);
    {sigset_t all,old;sigfillset(&all);pthread_sigmask(SIG_SETMASK,&all,&old);
     error=pthread_create(&thread,NULL,worker,p);pthread_sigmask(SIG_SETMASK,&old,NULL);}
    if(error){close(p->network);close(pair[0]);close(pair[1]);pthread_mutex_destroy(&p->lock);free(p);errno=error;return -1;}
    pthread_detach(thread);p->next=connections;connections=p;return 0;
}
int ios_ssh_set(int fd,unsigned op,const void *data,unsigned size)
{
    SSH *p;const unsigned char *w=data;int rc=0;
    if(op==7) {
        Upload *u;unsigned operation,total,pos,count;
        if(size<13||size>492){errno=EINVAL;return -1;}
        operation=get32(w);total=get32(w+4);pos=get32(w+8);count=size-12;
        if((operation!=1&&operation!=4)||total!=(operation==1?IOS_SSH_START_SIZE:IOS_SSH_AUTH_SIZE)||
           pos>total||count>total-pos){forget_upload(fd);errno=EINVAL;return -1;}
        if(!pos) {
            forget_upload(fd);u=calloc(1,sizeof(*u));if(!u)return -1;
            u->data=calloc(1,total);if(!u->data){free(u);return -1;}
            u->fd=fd;u->op=operation;u->size=total;u->next=uploads;uploads=u;
        }
        for(u=uploads;u&&u->fd!=fd;u=u->next){}
        if(!u||u->used!=pos||u->size!=total||u->op!=operation){forget_upload(fd);errno=EINVAL;return -1;}
        memcpy(u->data+pos,w+12,count);u->used+=count;
        if(u->used<total)return 0;
        rc=ios_ssh_set(fd,operation,u->data,total);forget_upload(fd);return rc;
    }
    if(op==1)return start(fd,w,size);
    p=lookup(fd);if(!p){errno=ENOTCONN;return -1;}
    pthread_mutex_lock(&p->lock);
    switch(op) {
    case 3:
        if(size!=4||get32(w)>1||p->state!=HOSTKEY||p->trust){rc=EINVAL;break;}
        p->trust=get32(w)?1:-1;break;
    case 4:
        if(size!=IOS_SSH_AUTH_SIZE || (get32(w)!=1&&get32(w)!=2) || get32(w+4)>8192 ||
           (get32(w)==2&&!get32(w+4)) || get32(w+8)||get32(w+12)||!memchr(w+16,0,512) ||
           p->state!=AUTH||p->auth_ready){rc=EINVAL;break;}
        memcpy(p->auth,w,size);p->auth_ready=1;p->state=OPENING;break;
    case 5:
        if(size!=8||!get32(w)||!get32(w+4)||get32(w)>65535||get32(w+4)>65535||p->state!=READY||!p->pty){rc=EINVAL;break;}
        p->columns=get32(w);p->rows=get32(w+4);p->resize_serial++;break;
    case 6:
        if(size!=4 || get32(w) || p->state!=READY){rc=EINVAL;break;}
        if(shutdown(p->front,SHUT_WR)<0)rc=errno;break;
    default:rc=ENOPROTOOPT;
    }
    pthread_mutex_unlock(&p->lock);if(rc){errno=rc;return -1;}return 0;
}
int ios_ssh_get(int fd,unsigned op,void *data,unsigned *size)
{
    SSH *p;unsigned char *w=data;
    if(op==0 && *size>=4){memcpy(w,"HSH1",4);*size=4;return 0;}
    if(op!=2){errno=ENOPROTOOPT;return -1;}
    p=lookup(fd);if(!p){errno=ENOTCONN;return -1;}
    if(*size<IOS_SSH_STATUS_SIZE){errno=EINVAL;return -1;}
    memset(w,0,IOS_SSH_STATUS_SIZE);memcpy(w,"HSH1",4);
    pthread_mutex_lock(&p->lock);
    put32(w+4,p->state);put32(w+8,p->error);put32(w+12,p->exit_status);put32(w+16,p->exit_valid);
    put32(w+20,p->methods);put32(w+24,p->attempt);
    memcpy(w+32,p->key_type,64);memcpy(w+96,p->fingerprint,96);memcpy(w+192,p->message,316);
    pthread_mutex_unlock(&p->lock);*size=IOS_SSH_STATUS_SIZE;return 0;
}
int ios_ssh_active(int fd){return lookup(fd)!=NULL;}
int ios_ssh_fd(int fd,int writing)
{
    SSH *p=lookup(fd);int s;if(!p)return fd;s=state(p);
    if(s<READY){errno=EAGAIN;return -1;}
    if(writing && s!=READY){errno=EPIPE;return -1;}return p->front;
}
int ios_ssh_eof(int fd)
{SSH *p=lookup(fd);if(p&&state(p)==FAILED){errno=ECONNRESET;return -1;}return 0;}
int ios_ssh_poll(struct pollfd *fds,nfds_t count,int timeout)
{
    struct pollfd *copy;nfds_t i;int n;
    if(!connections)return ios_tls_poll(fds,count,timeout);
    copy=calloc(count?count:1,sizeof(*copy));if(!copy)return -1;
    for(i=0;i<count;i++) {SSH *p=lookup(fds[i].fd);copy[i]=fds[i];
        if(p){copy[i].fd=p->front;if(state(p)<READY)copy[i].events&=~POLLOUT;}}
    n=ios_tls_poll(copy,count,timeout);
    if(n>=0)for(i=0;i<count;i++)fds[i].revents=copy[i].revents;
    free(copy);return n;
}
int ios_ssh_shutdown(int fd,int how)
{SSH *p=lookup(fd);return p?shutdown(p->front,how):ios_tls_shutdown(fd,how);}
void ios_ssh_forget(int fd)
{
    SSH **slot=&connections,*p;
    forget_upload(fd);
    while(*slot&&(*slot)->fd!=fd)slot=&(*slot)->next;
    if(!(p=*slot))return;*slot=p->next;
    pthread_mutex_lock(&p->lock);p->cancel=1;pthread_mutex_unlock(&p->lock);
    close(p->front);release(p);
}
#else
int ios_ssh_set(int fd,unsigned op,const void *data,unsigned size){errno=ENOPROTOOPT;return -1;}
int ios_ssh_get(int fd,unsigned op,void *data,unsigned *size){errno=ENOPROTOOPT;return -1;}
int ios_ssh_active(int fd){return 0;}
int ios_ssh_fd(int fd,int writing){return fd;}
int ios_ssh_eof(int fd){return 0;}
int ios_ssh_poll(struct pollfd *fds,nfds_t count,int timeout){return ios_tls_poll(fds,count,timeout);}
int ios_ssh_shutdown(int fd,int how){return ios_tls_shutdown(fd,how);}
void ios_ssh_forget(int fd){}
#endif
