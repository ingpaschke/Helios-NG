/* Incoming SSH runs in host workers, never in the coroutine scheduler.
 * socketpairs connect workers to ordinary Helios /internet streams: no
 * loopback TCP, host user accounts or separate daemon are involved. */
#include "ssh_server.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#ifdef IOS_SSHD
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

#define FRAME_MAX 4096
#define QUEUE_MAX 65536
#define CONNECTION_MAX 16
typedef struct Login Login;
struct Login { Login *next; int network,back,front; uint32_t id; };
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t finished=PTHREAD_COND_INITIALIZER;
static pthread_t accept_thread;
static Login *logins;
static int active,listener=-1,notice[2]={-1,-1},workers;
static uint32_t serial;
static char *key_path;
static pthread_once_t library_once=PTHREAD_ONCE_INIT;
static int library_error;
static void initialize(void) { library_error=ssh_init(); }
static long long milliseconds(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
static void nonblocking(int fd)
{ fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC); }
static int exchange(int fd,void *data,int size,int writing)
{
    char *p=data;int n;long long end=milliseconds()+30000;
    while(size){
        struct pollfd f;int left=(int)(end-milliseconds());
        if(left<=0)return -1;
        n=writing?send(fd,p,size,MSG_NOSIGNAL|MSG_DONTWAIT):recv(fd,p,size,MSG_DONTWAIT);
        if(n>0){p+=n;size-=n;continue;}
        if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR))return -1;
        f.fd=fd;f.events=writing?POLLOUT:POLLIN;f.revents=0;
        if(poll(&f,1,left)<0 && errno!=EINTR)return -1;
    }
    return 0;
}
static int frame(int fd,int kind,const void *data,int size)
{
    unsigned char buf[FRAME_MAX+3];
    if(size<0 || size>FRAME_MAX)return -1;
    buf[0]=kind;buf[1]=size>>8;buf[2]=size;
    if(size)memcpy(buf+3,data,size);
    return exchange(fd,buf,size+3,1);
}
static int reply(int fd,int kind)
{ unsigned char buf[3];return exchange(fd,buf,3,0)==0 && buf[0]==kind && !buf[1] && !buf[2]?0:-1; }
static void drop_backend(Login *p)
{
    pthread_mutex_lock(&lock);
    if(p->front>=0){close(p->front);p->front=-1;}
    if(p->back>=0){shutdown(p->back,SHUT_RDWR);close(p->back);p->back=-1;}
    p->id=0;
    pthread_mutex_unlock(&lock);
}
static int backend(Login *p)
{
    int pair[2],rc;unsigned char token[4];
    drop_backend(p);
    if(socketpair(AF_UNIX,SOCK_STREAM,0,pair)<0)return -1;
    nonblocking(pair[0]);nonblocking(pair[1]);
    pthread_mutex_lock(&lock);
    if(!active){pthread_mutex_unlock(&lock);close(pair[0]);close(pair[1]);return -1;}
    p->front=pair[0];p->back=pair[1];if(!++serial)++serial;p->id=serial;
    token[0]=serial;token[1]=serial>>8;token[2]=serial>>16;token[3]=serial>>24;
    rc=send(notice[1],token,4,MSG_NOSIGNAL|MSG_DONTWAIT);
    pthread_mutex_unlock(&lock);
    if(rc!=4){drop_backend(p);return -1;}
    return 0;
}
/* SSH account strings are UTF-8; native Helios accounts use Latin-1. */
static int account(char *out,const char *in)
{
    unsigned n=0,c;
    if(!in)return -1;
    while((c=(unsigned char)*in++)!=0){
        if(c>=128){
            unsigned d=(unsigned char)*in++;
            if((c!=0xc2 && c!=0xc3) || d<0x80 || d>0xbf)return -1;
            c=((c&3)<<6)|(d&63);
        }
        if(n==31)return -1;out[n++]=c;
    }
    out[n]=0;return n;
}
static int authenticate(Login *p,const char *user,const char *password)
{
    char data[64];int n=account(data,user),m;
    if(n<=0)return -1;
    m=account(data+n+1,password);if(m<0)return -1;
    if(backend(p)<0)return -1;
    n=frame(p->back,'A',data,n+m+2);memset(data,0,sizeof(data));
    if(n<0 || reply(p->back,'A')<0){drop_backend(p);return -1;}
    return 0;
}
static int dimension(int n,int fallback,int limit)
{ return n<=0?fallback:(n>limit?limit:n); }
static int start(Login *p,const char *term,int columns,int rows,const char *command)
{
    unsigned char data[69];int n=strlen(term)+1;
    memcpy(data,term,n);data[n]=columns>>8;data[n+1]=columns;
    data[n+2]=rows>>8;data[n+3]=rows;
    return frame(p->back,'P',data,n+4)<0 ||
        frame(p->back,command?'C':'S',command,command?strlen(command):0)<0 ||
        reply(p->back,'S')<0?-1:0;
}
static void consume(unsigned char *buf,int *used,int n)
{ *used-=n;memmove(buf,buf+n,*used); }
static int append(unsigned char *buf,int *used,int kind,const void *data,int n)
{
    if(n>FRAME_MAX || *used+n+3>QUEUE_MAX)return -1;
    buf[*used]=kind;buf[*used+1]=n>>8;buf[*used+2]=n;
    if(n)memcpy(buf+*used+3,data,n);*used+=n+3;return 0;
}
static void pump(Login *p,ssh_session session,ssh_channel channel)
{
    unsigned char *input=malloc(QUEUE_MAX),*output=malloc(QUEUE_MAX),*pending=malloc(QUEUE_MAX);
    int in=0,out=0,used=0,eof=0,status=-1,exit_sent=0,end_sent=0;
    int resized=0,columns=80,rows=24,n,len,rc,progress;
    ssh_message msg;
    if(!input || !output || !pending)goto done;
    ssh_set_blocking(session,0);
    while(ssh_is_connected(session) && !ssh_channel_is_closed(channel)){
        struct pollfd f[2];
        progress=0;
        while((msg=ssh_message_get(session))!=NULL){
            if(ssh_message_type(msg)==SSH_REQUEST_CHANNEL &&
               ssh_message_channel_request_channel(msg)==channel &&
               ssh_message_subtype(msg)==SSH_CHANNEL_REQUEST_WINDOW_CHANGE){
                columns=dimension(ssh_message_channel_request_pty_width(msg),80,512);
                rows=dimension(ssh_message_channel_request_pty_height(msg),24,256);
                resized=1;ssh_message_channel_request_reply_success(msg);
            }else ssh_message_reply_default(msg);
            ssh_message_free(msg);
        }
        if(resized && in+7<=QUEUE_MAX){
            unsigned char size[4];size[0]=columns>>8;size[1]=columns;size[2]=rows>>8;size[3]=rows;
            append(input,&in,'W',size,4);resized=0;progress=1;
        }
        if(out){
            n=ssh_channel_write(channel,output,out);
            if(n>0){consume(output,&out,n);progress=1;}
            else if(n<0 && n!=SSH_AGAIN)break;
        }
        if(status>=0 && !out){
            if(!exit_sent){rc=ssh_channel_request_send_exit_status(channel,status);if(rc==SSH_OK)exit_sent=1;else if(rc!=SSH_AGAIN)break;}
            if(exit_sent && !end_sent){rc=ssh_channel_send_eof(channel);if(rc==SSH_OK)end_sent=1;else if(rc!=SSH_AGAIN)break;}
            if(end_sent){rc=ssh_channel_close(channel);if(rc!=SSH_AGAIN)break;}
        }
        if(status<0 && !eof && in+FRAME_MAX+3<=QUEUE_MAX){
            unsigned char data[FRAME_MAX];
            n=ssh_channel_read_nonblocking(channel,data,sizeof(data),0);
            if(n>0){append(input,&in,'D',data,n);progress=1;}
            else if(n<0 && n!=SSH_AGAIN && n!=SSH_EOF)break;
            if(ssh_channel_is_eof(channel) && n<=0){append(input,&in,'E',NULL,0);eof=1;}
        }
        if(in && status<0){
            n=send(p->back,input,in,MSG_NOSIGNAL|MSG_DONTWAIT);
            if(n>0){consume(input,&in,n);progress=1;}
            else if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR))break;
        }
        if(status<0 && used+FRAME_MAX<=QUEUE_MAX && out+FRAME_MAX<=QUEUE_MAX){
            n=recv(p->back,pending+used,FRAME_MAX,MSG_DONTWAIT);
            if(n>0){used+=n;progress=1;}
            else if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR))break;
        }
        while(used>=3){
            len=(pending[1]<<8)|pending[2];
            if(len>FRAME_MAX)goto done;if(used<len+3)break;
            if(pending[0]=='D'){
                if(out+len>QUEUE_MAX)break;
                memcpy(output+out,pending+3,len);out+=len;
            }else if(pending[0]=='X' && len==4){
                status=((unsigned)pending[3]<<24)|((unsigned)pending[4]<<16)|(pending[5]<<8)|pending[6];
                status&=255;
            }else goto done;
            consume(pending,&used,len+3);progress=1;
        }
        /* Drain work already buffered in libssh or our queues before
         * waiting for another descriptor event. In particular, a newly
         * received terminal echo must not sit behind a polling timeout. */
        if(progress)continue;
        f[0].fd=p->network;f[0].events=POLLIN;
        if(ssh_get_poll_flags(session)&SSH_WRITE_PENDING)f[0].events|=POLLOUT;
        f[1].fd=p->back;f[1].events=0;
        if(status<0 && used+FRAME_MAX<=QUEUE_MAX && out+FRAME_MAX<=QUEUE_MAX)f[1].events|=POLLIN;
        if(in && status<0)f[1].events|=POLLOUT;
        if(!f[1].events)f[1].fd=-1;
        if(poll(f,2,(out || used || status>=0)?20:100)<0 && errno!=EINTR)break;
    }
    if(status>=0)ssh_blocking_flush(session,2000);
done:
    free(input);free(output);free(pending);
}
static void *worker(void *arg)
{
    Login *p=arg,**slot;ssh_bind bind=NULL;ssh_session session=NULL;ssh_channel channel=NULL;
    ssh_message msg;ssh_key key=NULL;char term[65]="xterm",interactive_user[64]="";
    int authenticated=0,columns=80,rows=24,started=0,attempts=0,rc,process_config=0,owned=0;
    long timeout=30;
    bind=ssh_bind_new();session=ssh_new();if(!bind || !session)goto done;
    ssh_options_set(session,SSH_OPTIONS_TIMEOUT,&timeout);
    ssh_bind_options_set(bind,SSH_BIND_OPTIONS_PROCESS_CONFIG,&process_config);
    if(ssh_pki_import_privkey_file(key_path,NULL,NULL,NULL,&key)!=SSH_OK)goto done;
    if(ssh_bind_options_set(bind,SSH_BIND_OPTIONS_IMPORT_KEY,key)!=SSH_OK){ssh_key_free(key);goto done;}
    if(ssh_bind_accept_fd(bind,session,p->network)!=SSH_OK){
        /* accept_fd can take ownership before a later allocation fails. */
        owned=ssh_get_fd(session)==p->network;goto done;
    }
    owned=1;
    if(ssh_handle_key_exchange(session)!=SSH_OK)goto done;
    while(!started && (msg=ssh_message_get(session))!=NULL){
        rc=-1;
        if(ssh_message_type(msg)==SSH_REQUEST_AUTH && !authenticated && ++attempts<=10){
            int method=ssh_message_subtype(msg);const char *password=NULL,*user=ssh_message_auth_user(msg);
            if(method==SSH_AUTH_METHOD_NONE)password="";
            else if(method==SSH_AUTH_METHOD_PASSWORD)password=ssh_message_auth_password(msg);
            else if(method==SSH_AUTH_METHOD_INTERACTIVE){
                if(ssh_message_auth_kbdint_is_response(msg)){
                    /* INFO_RESPONSE carries answers, not a username. */
                    user=interactive_user;password=ssh_userauth_kbdint_getanswer(session,0);
                }else if(user && strlen(user)<sizeof(interactive_user)){
                    const char *prompt="Password: ";char echo=0;
                    strcpy(interactive_user,user);
                    ssh_message_auth_interactive_request(msg,"Helios login","",1,&prompt,&echo);
                    ssh_message_free(msg);continue;
                }
            }
            if(password && authenticate(p,user,password)==0){
                authenticated=1;rc=ssh_message_auth_reply_success(msg,0);
            }else ssh_message_auth_set_methods(msg,SSH_AUTH_METHOD_PASSWORD|SSH_AUTH_METHOD_INTERACTIVE);
        }else if(ssh_message_type(msg)==SSH_REQUEST_SERVICE){
            rc=ssh_message_service_reply_success(msg);
        }else if(ssh_message_type(msg)==SSH_REQUEST_CHANNEL_OPEN && authenticated && !channel &&
                 ssh_message_subtype(msg)==SSH_CHANNEL_SESSION){
            channel=ssh_message_channel_request_open_reply_accept(msg);if(channel)rc=0;
        }else if(ssh_message_type(msg)==SSH_REQUEST_CHANNEL && channel &&
                 ssh_message_channel_request_channel(msg)==channel){
            int request=ssh_message_subtype(msg);
            if(request==SSH_CHANNEL_REQUEST_PTY){
                const char *value=ssh_message_channel_request_pty_term(msg);size_t i,n=value?strlen(value):0;
                if(n && n<=64){
                    for(i=0;i<n && (unsigned char)value[i]>=33 && (unsigned char)value[i]<=126;i++);
                    if(i==n){
                        strcpy(term,value);
                        columns=dimension(ssh_message_channel_request_pty_width(msg),80,512);
                        rows=dimension(ssh_message_channel_request_pty_height(msg),24,256);
                        rc=ssh_message_channel_request_reply_success(msg);
                    }
                }
            }else if(request==SSH_CHANNEL_REQUEST_SHELL || request==SSH_CHANNEL_REQUEST_EXEC){
                const char *command=request==SSH_CHANNEL_REQUEST_EXEC?ssh_message_channel_request_command(msg):NULL;
                if((request==SSH_CHANNEL_REQUEST_SHELL || (command && *command && strlen(command)<=FRAME_MAX)) &&
                   start(p,term,columns,rows,command)==0){
                    rc=ssh_message_channel_request_reply_success(msg);started=1;
                }
            }
        }
        if(rc<0)ssh_message_reply_default(msg);
        ssh_message_free(msg);
        if(attempts>10)break;
    }
    if(started)pump(p,session,channel);
done:
    drop_backend(p);
    pthread_mutex_lock(&lock);rc=p->network;p->network=-1;pthread_mutex_unlock(&lock);
    if(channel)ssh_channel_free(channel);
    if(session){ssh_set_blocking(session,0);ssh_disconnect(session);ssh_free(session);}
    if(!owned)close(rc);
    if(bind)ssh_bind_free(bind);
    pthread_mutex_lock(&lock);
    for(slot=&logins;*slot && *slot!=p;slot=&(*slot)->next);
    if(*slot)*slot=p->next;
    free(p);--workers;pthread_cond_broadcast(&finished);pthread_mutex_unlock(&lock);
    return NULL;
}
static void *accept_connections(void *unused)
{
    for(;;){
        struct pollfd f;int fd,rc,on=1;Login *p;pthread_t thread;
        f.fd=listener;f.events=POLLIN;f.revents=0;
        pthread_mutex_lock(&lock);rc=active;pthread_mutex_unlock(&lock);if(!rc)break;
        if(poll(&f,1,100)<=0)continue;
        fd=accept(listener,NULL,NULL);if(fd<0)continue;fcntl(fd,F_SETFD,FD_CLOEXEC);
        setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&on,sizeof(on));
        p=calloc(1,sizeof(*p));if(!p){close(fd);continue;}
        p->network=fd;p->front=p->back=-1;
        pthread_mutex_lock(&lock);
        if(!active || workers==CONNECTION_MAX){pthread_mutex_unlock(&lock);close(fd);free(p);continue;}
        p->next=logins;logins=p;++workers;
        rc=pthread_create(&thread,NULL,worker,p);
        if(rc){logins=p->next;--workers;close(fd);free(p);}else pthread_detach(thread);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}
int ios_sshd_open(const char *name)
{
    Login *p;unsigned id;int fd=-1;char tail;
    pthread_mutex_lock(&lock);
    if(active && !strcmp(name,"sshd"))fd=dup(notice[0]);
    else if(active && strlen(name)==12 && sscanf(name,"ssh.%8x%c",&id,&tail)==1){
        for(p=logins;p;p=p->next)if(p->id==id && p->front>=0){fd=p->front;p->front=-1;break;}
    }
    pthread_mutex_unlock(&lock);
    if(fd>=0)nonblocking(fd);else errno=ENOENT;
    return fd;
}
void ios_sshd_stop(void)
{
    Login *p;
    pthread_mutex_lock(&lock);if(!active){pthread_mutex_unlock(&lock);return;}active=0;
    for(p=logins;p;p=p->next){if(p->network>=0)shutdown(p->network,SHUT_RDWR);if(p->back>=0)shutdown(p->back,SHUT_RDWR);}
    pthread_mutex_unlock(&lock);pthread_join(accept_thread,NULL);
    pthread_mutex_lock(&lock);while(workers)pthread_cond_wait(&finished,&lock);
    close(listener);close(notice[0]);close(notice[1]);listener=notice[0]=notice[1]=-1;
    free(key_path);key_path=NULL;pthread_mutex_unlock(&lock);
}
int ios_sshd_start(const char *address,int port,const char *path)
{
    struct sockaddr_in a;ssh_key key=NULL;int on=1,rc;
    if(port<=0 || port>65535 || !path || !*path){errno=EINVAL;return -1;}
    pthread_once(&library_once,initialize);if(library_error){errno=EIO;return -1;}
    if(active){errno=EALREADY;return -1;}
    if(access(path,F_OK)<0){
        char *temp=malloc(strlen(path)+32);
        if(!temp)return -1;sprintf(temp,"%s.%ld",path,(long)getpid());
        rc=ssh_pki_generate(SSH_KEYTYPE_ED25519,0,&key);
        if(rc==SSH_OK)rc=ssh_pki_export_privkey_file(key,NULL,NULL,NULL,temp);
        if(rc==SSH_OK){chmod(temp,0640);rc=link(temp,path);if(rc<0 && errno==EEXIST)rc=0;}
        unlink(temp);free(temp);ssh_key_free(key);key=NULL;if(rc!=SSH_OK)return -1;
    }
    if(ssh_pki_import_privkey_file(path,NULL,NULL,NULL,&key)!=SSH_OK){errno=EIO;return -1;}ssh_key_free(key);
    key_path=strdup(path);if(!key_path)return -1;
    memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(port);
    if(inet_pton(AF_INET,address?address:"0.0.0.0",&a.sin_addr)!=1){errno=EINVAL;goto fail;}
    listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0)goto fail;
    nonblocking(listener);setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
    if(bind(listener,(struct sockaddr *)&a,sizeof(a))<0 || listen(listener,16)<0 || socketpair(AF_UNIX,SOCK_STREAM,0,notice)<0)goto fail;
    nonblocking(notice[0]);nonblocking(notice[1]);active=1;
    rc=pthread_create(&accept_thread,NULL,accept_connections,NULL);
    if(rc){active=0;errno=rc;goto fail;}
    return 0;
fail:
    if(listener>=0)close(listener);if(notice[0]>=0)close(notice[0]);if(notice[1]>=0)close(notice[1]);
    listener=notice[0]=notice[1]=-1;free(key_path);key_path=NULL;return -1;
}
#else
int ios_sshd_start(const char *address,int port,const char *path){errno=EOPNOTSUPP;return -1;}
void ios_sshd_stop(void){}
int ios_sshd_open(const char *name){errno=ENOENT;return -1;}
#endif
