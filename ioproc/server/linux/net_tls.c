/* TLS lives entirely in Linux workers. The coroutine thread never calls
 * OpenSSL, waits for a handshake, or accesses a worker's SSL object.
 * A bounded nonblocking socketpair carries plaintext; the original socket
 * retains its identity, addresses and options in the Helios protocol.
 * The registry is coroutine-thread-only. A worker owns one reference and
 * duplicate network fd; close/reboot removes the registry reference and
 * wakes the worker without joining it or risking descriptor reuse. */
#include "net_tls.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>

#ifdef IOS_TLS
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

typedef struct TLS TLS;
struct TLS {
    TLS *next;
    int fd,front,back,network,refs,cancel;
    long long drain_deadline;
    unsigned timeout,state,error;
    char host[256],protocol[16],cipher[64],message[128];
    pthread_mutex_t lock;
};
static TLS *connections;
static SSL_CTX *context;
static pthread_once_t context_once=PTHREAD_ONCE_INIT;

static uint32_t get32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t x)
{p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static long long milliseconds(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static TLS *lookup(int fd)
{TLS *p;for(p=connections;p;p=p->next)if(p->fd==fd)return p;return NULL;}
static unsigned state(TLS *p)
{unsigned s;pthread_mutex_lock(&p->lock);s=p->state;pthread_mutex_unlock(&p->lock);return s;}
static int cancelled(TLS *p)
{int c;pthread_mutex_lock(&p->lock);c=p->cancel;pthread_mutex_unlock(&p->lock);return c;}
static long long closing(TLS *p)
{long long d;pthread_mutex_lock(&p->lock);d=p->drain_deadline;pthread_mutex_unlock(&p->lock);return d;}
static void release(TLS *p)
{int last;pthread_mutex_lock(&p->lock);last=!--p->refs;pthread_mutex_unlock(&p->lock);
 if(last){pthread_mutex_destroy(&p->lock);free(p);}}
static void failure(TLS *p,unsigned error,const char *message)
{
    pthread_mutex_lock(&p->lock);
    p->state=IOS_TLS_FAILED;p->error=error;
    snprintf(p->message,sizeof(p->message),"%s",message);
    pthread_mutex_unlock(&p->lock);
}
static void initialize_context(void)
{
    context=SSL_CTX_new(TLS_client_method());
    if(!context)return;
    SSL_CTX_set_verify(context,SSL_VERIFY_PEER,NULL);
    if(!SSL_CTX_set_min_proto_version(context,TLS1_2_VERSION) ||
       !SSL_CTX_set_default_verify_paths(context)) {
        SSL_CTX_free(context);context=NULL;
    }
}
static void ssl_failure(TLS *p,SSL *ssl,int error)
{
    int saved_errno=errno;
    unsigned long code=ERR_peek_last_error();char text[128];
    long verify=SSL_get_verify_result(ssl);
    if(verify!=X509_V_OK){failure(p,2,X509_verify_cert_error_string(verify));return;}
    if(code)ERR_error_string_n(code,text,sizeof(text));
    else if(error==SSL_ERROR_SYSCALL && saved_errno)
        snprintf(text,sizeof(text),"%s",strerror(saved_errno));
    else snprintf(text,sizeof(text),"TLS I/O failure (%d)",error);
    failure(p,error==SSL_ERROR_SYSCALL?1:3,text);
}
/* Handshake poll also observes cancellation on the application endpoint. */
static int wait_handshake(TLS *p,short events,long long deadline)
{
    struct pollfd f[2]={{p->network,events,0},{p->back,POLLIN,0}};
    int left=(int)(deadline-milliseconds()),n;
    if(cancelled(p)){failure(p,5,"TLS connection closed");return -1;}
    if(left<=0){failure(p,4,"TLS handshake timed out");return -1;}
    n=poll(f,2,left>100?100:left);
    if(n<0 && errno!=EINTR){failure(p,1,strerror(errno));return -1;}
    return 0;
}
static void *worker(void *arg)
{
    TLS *p=arg;SSL *ssl=NULL;
    unsigned char outgoing[16384],incoming[16384],ip[16];
    int out_size=0,in_size=0,in_pos=0,app_eof=0,tls_eof=0,notified=0;
    short write_want=0,read_want=0;
    long long deadline=milliseconds()+p->timeout;
    sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGPIPE);
    pthread_sigmask(SIG_BLOCK,&signals,NULL);
    pthread_once(&context_once,initialize_context);
    if(!context){failure(p,6,"Cannot initialize TLS CA store");goto done;}
    ssl=SSL_new(context);
    if(!ssl){failure(p,6,"Cannot allocate TLS session");goto done;}
    SSL_set_mode(ssl,SSL_MODE_ENABLE_PARTIAL_WRITE);
    if(!SSL_set_fd(ssl,p->network)){failure(p,6,"Cannot attach TLS socket");goto done;}
    if(inet_pton(AF_INET,p->host,ip)==1 || inet_pton(AF_INET6,p->host,ip)==1) {
        if(!X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),p->host))goto config_error;
    }else {
        if(!SSL_set1_host(ssl,p->host) || !SSL_set_tlsext_host_name(ssl,p->host))goto config_error;
    }
    for(;;) {
        int r,e;
        if(cancelled(p)){failure(p,5,"TLS connection closed");goto done;}
        if(milliseconds()>=deadline){failure(p,4,"TLS handshake timed out");goto done;}
        ERR_clear_error();r=SSL_connect(ssl);
        if(r==1)break;
        e=SSL_get_error(ssl,r);
        if(e!=SSL_ERROR_WANT_READ && e!=SSL_ERROR_WANT_WRITE){ssl_failure(p,ssl,e);goto done;}
        if(wait_handshake(p,e==SSL_ERROR_WANT_READ?POLLIN:POLLOUT,deadline)<0)goto done;
    }
    pthread_mutex_lock(&p->lock);
    snprintf(p->protocol,sizeof(p->protocol),"%s",SSL_get_version(ssl));
    snprintf(p->cipher,sizeof(p->cipher),"%s",SSL_get_cipher_name(ssl));
    p->state=IOS_TLS_READY;
    pthread_mutex_unlock(&p->lock);
    while(!cancelled(p)) {
        struct pollfd f[2];int r,e,progress=0;
        long long drain=closing(p);
        if(drain && milliseconds()>=drain)break;
        /* close() relinquishes incoming data, but bytes already accepted
         * for writing still belong to the connection until sent. */
        if(drain)in_size=in_pos=0;
        if(in_size) {
            r=send(p->back,incoming+in_pos,in_size-in_pos,MSG_NOSIGNAL);
            if(r>0){in_pos+=r;progress=1;if(in_pos==in_size)in_pos=in_size=0;}
            else if(r<0 && errno!=EAGAIN && errno!=EINTR) {
                if(closing(p))in_size=in_pos=0;
                else goto done;
            }
        }
        if(!out_size && !app_eof) {
            r=recv(p->back,outgoing,sizeof(outgoing),0);
            if(r>0){out_size=r;progress=1;}
            else if(!r){app_eof=1;progress=1;}
            else if(errno!=EAGAIN && errno!=EINTR)goto done;
        }
        if(out_size) {
            ERR_clear_error();r=SSL_write(ssl,outgoing,out_size);
            if(r>0){out_size-=r;memmove(outgoing,outgoing+r,out_size);write_want=0;progress=1;}
            else {
                e=SSL_get_error(ssl,r);
                if(e!=SSL_ERROR_WANT_READ && e!=SSL_ERROR_WANT_WRITE){ssl_failure(p,ssl,e);goto done;}
                write_want=e==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;
            }
        }
        /* Preserve SSL_write's retry buffer and operation until it succeeds. */
        if(!write_want && !in_size && !tls_eof) {
            ERR_clear_error();r=SSL_read(ssl,incoming,sizeof(incoming));
            if(r>0){in_size=r;read_want=0;progress=1;}
            else {
                e=SSL_get_error(ssl,r);
                if(e==SSL_ERROR_ZERO_RETURN){
                    tls_eof=1;progress=1;
                    /* Reject new application writes, but recv() still drains
                     * bytes already accepted by the Linux socketpair. */
                    shutdown(p->back,SHUT_RD);
                }
                else if(e==SSL_ERROR_WANT_READ || e==SSL_ERROR_WANT_WRITE)
                    read_want=e==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;
                else {ssl_failure(p,ssl,e);goto done;}
            }
        }
        if(app_eof && !out_size && !notified && !write_want) {
            ERR_clear_error();r=SSL_shutdown(ssl);
            if(r>=0 || (SSL_get_shutdown(ssl)&SSL_SENT_SHUTDOWN)){notified=1;progress=1;}
            else {
                e=SSL_get_error(ssl,r);
                if(e!=SSL_ERROR_WANT_READ && e!=SSL_ERROR_WANT_WRITE){ssl_failure(p,ssl,e);goto done;}
                read_want=e==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;
            }
        }
        /* After close_notify, consume the peer's shutdown (discarding
         * application input when closing). Closing TCP with unread TLS
         * records can send RST and destroy the final outgoing records. */
        if(tls_eof && !in_size && app_eof && !out_size && notified)break;
        if(progress)continue;
        f[0].fd=p->back;f[0].events=(!app_eof && !out_size?POLLIN:0)|(in_size?POLLOUT:0);
        f[1].fd=p->network;f[1].events=write_want?write_want:(!in_size?(read_want?read_want:POLLIN):0);
        /* A hung-up fd reports HUP even with events=0. While draining a
         * full plaintext buffer it must not turn the worker into a spin loop. */
        if(!f[0].events)f[0].fd=-1;
        if(!f[1].events)f[1].fd=-1;
        f[0].revents=f[1].revents=0;
        r=poll(f,2,100);
        if(r<0 && errno!=EINTR){failure(p,1,strerror(errno));goto done;}
    }
    goto done;
config_error:
    failure(p,6,"Cannot configure TLS peer identity");
done:
    if(ssl)SSL_free(ssl);
    close(p->network);close(p->back);
    pthread_mutex_lock(&p->lock);
    if(p->state!=IOS_TLS_FAILED)p->state=IOS_TLS_CLOSED;
    pthread_mutex_unlock(&p->lock);
    release(p);return NULL;
}

int ios_tls_start(int fd,const void *data,unsigned size)
{
    const unsigned char *wire=data;TLS *p;pthread_t thread;
    int pair[2],type,error,flags;struct sockaddr_storage peer;socklen_t n=sizeof(type);
    if(size!=IOS_TLS_REQUEST_SIZE || get32(wire)!=1 ||
       get32(wire+4)<100 || get32(wire+4)>120000 || !wire[8] ||
       !memchr(wire+8,0,256)){errno=EINVAL;return -1;}
    if(lookup(fd)){errno=EALREADY;return -1;}
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
    p->fd=fd;p->front=pair[0];p->back=pair[1];p->refs=2;
    p->timeout=get32(wire+4);p->state=IOS_TLS_NEGOTIATING;
    memcpy(p->host,wire+8,256);pthread_mutex_init(&p->lock,NULL);
    error=pthread_create(&thread,NULL,worker,p);
    if(error){close(p->network);close(pair[0]);close(pair[1]);pthread_mutex_destroy(&p->lock);free(p);errno=error;return -1;}
    pthread_detach(thread);p->next=connections;connections=p;
    return 0;
}
int ios_tls_status(int fd,void *data,unsigned *size)
{
    TLS *p=lookup(fd);unsigned char *w=data;
    if(!p){errno=ENOTCONN;return -1;}
    if(*size<IOS_TLS_STATUS_SIZE){errno=EINVAL;return -1;}
    memset(w,0,IOS_TLS_STATUS_SIZE);memcpy(w,"HTL1",4);
    pthread_mutex_lock(&p->lock);
    put32(w+4,p->state);put32(w+8,p->error);
    memcpy(w+16,p->protocol,16);memcpy(w+32,p->cipher,64);memcpy(w+96,p->message,128);
    pthread_mutex_unlock(&p->lock);*size=IOS_TLS_STATUS_SIZE;return 0;
}
int ios_tls_fd(int fd,int writing)
{
    TLS *p=lookup(fd);unsigned s;if(!p)return fd;s=state(p);
    if(s==IOS_TLS_NEGOTIATING){errno=EAGAIN;return -1;}
    if(writing && (s==IOS_TLS_FAILED || s==IOS_TLS_CLOSED)){errno=EPIPE;return -1;}
    return p->front;
}
int ios_tls_eof(int fd)
{TLS *p=lookup(fd);if(p && state(p)==IOS_TLS_FAILED){errno=ECONNRESET;return -1;}return 0;}
int ios_tls_poll(struct pollfd *fds,nfds_t count,int timeout)
{
    struct pollfd *copy;nfds_t i;int n,left=timeout,pending;long long deadline=milliseconds()+timeout;
    copy=calloc(count?count:1,sizeof(*copy));if(!copy)return -1;
    do {
        pending=0;
        for(i=0;i<count;i++) {
            TLS *p=lookup(fds[i].fd);copy[i]=fds[i];copy[i].revents=0;
            if(p){copy[i].fd=p->front;if(state(p)==IOS_TLS_NEGOTIATING){copy[i].events&=~POLLOUT;pending=1;}}
        }
        n=poll(copy,count,pending && (left<0 || left>20)?20:left);
        if(n || !pending || !timeout)break;
        if(timeout>0)left=(int)(deadline-milliseconds());
    }while(timeout<0 || left>0);
    if(n>=0)for(i=0;i<count;i++)fds[i].revents=copy[i].revents;
    free(copy);return n;
}
int ios_tls_shutdown(int fd,int how)
{TLS *p=lookup(fd);return shutdown(p?p->front:fd,how);}
void ios_tls_forget(int fd)
{
    TLS **slot=&connections,*p;
    while(*slot && (*slot)->fd!=fd)slot=&(*slot)->next;
    if(!(p=*slot))return;
    *slot=p->next;
    pthread_mutex_lock(&p->lock);
    /* Handshake cancellation remains immediate. Established streams drain
     * in their worker, never blocking the io-server coroutine thread. */
    if(p->state==IOS_TLS_READY)p->drain_deadline=milliseconds()+5000;
    else p->cancel=1;
    pthread_mutex_unlock(&p->lock);
    close(p->front);release(p);
}
#else
int ios_tls_start(int fd,const void *data,unsigned size){errno=ENOPROTOOPT;return -1;}
int ios_tls_status(int fd,void *data,unsigned *size){errno=ENOPROTOOPT;return -1;}
int ios_tls_fd(int fd,int writing){return fd;}
int ios_tls_eof(int fd){return 0;}
int ios_tls_poll(struct pollfd *fds,nfds_t n,int timeout){return poll(fds,n,timeout);}
int ios_tls_shutdown(int fd,int how){return shutdown(fd,how);}
void ios_tls_forget(int fd){}
#endif
