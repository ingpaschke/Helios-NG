/* Keep Linux constants separate from the historical protocol.h definitions. */
#include "net_compat.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#ifndef POLLRDHUP
#define POLLRDHUP 0x2000
#endif

int ios_net_close(int fd){ios_tls_forget(fd);return close(fd);}
ssize_t ios_net_read(int fd,void *data,size_t size)
{int io=ios_tls_fd(fd,0);ssize_t n;if(io<0)return -1;n=read(io,data,size);return n==0 && size?ios_tls_eof(fd):n;}
ssize_t ios_net_write(int fd,const void *data,size_t size)
{int io=ios_tls_fd(fd,1);return io<0?-1:write(io,data,size);}
ssize_t ios_net_sendmsg(int fd,const struct msghdr *msg,int flags)
{
    int io=ios_tls_fd(fd,1);struct msghdr plain;
    if(io<0)return -1;
    if(io==fd)return sendmsg(io,msg,flags);
    if((flags&MSG_OOB) || msg->msg_controllen){errno=EOPNOTSUPP;return -1;}
    plain=*msg;plain.msg_name=NULL;plain.msg_namelen=0;
    return sendmsg(io,&plain,flags);
}
ssize_t ios_net_recvmsg(int fd,struct msghdr *msg,int flags)
{
    int io=ios_tls_fd(fd,0);ssize_t n;struct msghdr plain;
    if(io<0)return -1;
    if(io==fd)return recvmsg(io,msg,flags);
    if(flags&MSG_OOB){errno=EOPNOTSUPP;return -1;}
    plain=*msg;plain.msg_name=NULL;plain.msg_namelen=0;
    n=recvmsg(io,&plain,flags);
    if(n>=0){
        msg->msg_flags=plain.msg_flags;msg->msg_controllen=plain.msg_controllen;
        if(msg->msg_name && getpeername(fd,msg->msg_name,&msg->msg_namelen)<0)
            msg->msg_namelen=0;
    }
    return n==0?ios_tls_eof(fd):n;
}
ssize_t ios_net_recvfrom(int fd,void *data,size_t size,int flags,struct sockaddr *addr,socklen_t *len)
{
    int io=ios_tls_fd(fd,0);ssize_t n;
    if(io<0)return -1;
    if(io==fd)return recvfrom(io,data,size,flags,addr,len);
    if(flags&MSG_OOB){errno=EOPNOTSUPP;return -1;}
    n=recvfrom(io,data,size,flags,NULL,NULL);
    if(n>=0 && addr && len && getpeername(fd,addr,len)<0)*len=0;
    return n==0 && size?ios_tls_eof(fd):n;
}
ssize_t ios_net_sendto(int fd,const void *data,size_t size,int flags,const struct sockaddr *addr,socklen_t len)
{
    int io=ios_tls_fd(fd,1);if(io<0)return -1;
    if(io!=fd){if(flags&MSG_OOB){errno=EOPNOTSUPP;return -1;}addr=NULL;len=0;}
    return sendto(io,data,size,flags,addr,len);
}

int32_t ios_net_deadline(int fd, int writing, int32_t now,
                         int32_t requested, unsigned tick_us)
{
    struct timeval tv;
    socklen_t len = sizeof(tv);
    int64_t ticks = requested < 0 ? INT32_MAX : requested;
    if (getsockopt(fd, SOL_SOCKET, writing ? SO_SNDTIMEO : SO_RCVTIMEO,
                   &tv, &len) == 0 && (tv.tv_sec || tv.tv_usec)) {
        int64_t limit = ((int64_t)tv.tv_sec * 1000000 + tv.tv_usec + tick_us - 1) / tick_us;
        if (limit < ticks) ticks = limit;
    }
    return ticks + now > INT32_MAX ? INT32_MAX : (int32_t)(ticks + now);
}

static int option(int level, uint32_t name)
{
    if (level == 0xffff) {
        switch (name) {
        case 0x0001: return SO_DEBUG;
        case 0x0002: return SO_ACCEPTCONN;
        case 0x0004: return SO_REUSEADDR;
        case 0x0008: return SO_KEEPALIVE;
        case 0x0010: return SO_DONTROUTE;
        case 0x0020: return SO_BROADCAST;
        case 0x0080: return SO_LINGER;
        case 0x0100: return SO_OOBINLINE;
        case 0x1001: return SO_SNDBUF;
        case 0x1002: return SO_RCVBUF;
        case 0x1003: return SO_SNDLOWAT;
        case 0x1004: return SO_RCVLOWAT;
        case 0x1005: return SO_SNDTIMEO;
        case 0x1006: return SO_RCVTIMEO;
        case 0x1007: return SO_ERROR;
        case 0x1008: return SO_TYPE;
        }
    } else if (level == IPPROTO_TCP) {
        if (name == 1) return TCP_NODELAY;
        if (name == 2) return TCP_MAXSEG;
    }
    errno = ENOPROTOOPT;
    return -1;
}

void ios_net_identity(char *name, unsigned size, void *address)
{
    struct ifaddrs *all, *p;
    uint32_t addr = htonl(INADDR_LOOPBACK);
    if (gethostname(name, size) < 0) strncpy(name, "localhost", size);
    name[size - 1] = 0;
    /* No assumption that the Linux hostname resolves in DNS or /etc/hosts. */
    if (getifaddrs(&all) == 0) {
        for (p = all; p; p = p->ifa_next)
            if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET &&
                (p->ifa_flags & IFF_UP) && !(p->ifa_flags & IFF_LOOPBACK)) {
                addr = ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr;
                break;
            }
        freeifaddrs(all);
    }
    memcpy(address, &addr, 4);
}

int ios_net_set(int fd, int level, uint32_t name, const void *data,
                unsigned size, int *nonblocking)
{
    int opt;
    if(level==IOS_SOL_TLS) {
        if(name==IOS_TLS_CLIENT)return ios_tls_start(fd,data,size);
        errno=ENOPROTOOPT;return -1;
    }
    if (level == 0xfff1) {
        int32_t value;
        if (size != 4) { errno = EINVAL; return -1; }
        memcpy(&value, data, 4);
        if (name == 0x8004667eU) { /* Helios FIONBIO */
            /* Host descriptors MUST stay nonblocking: one event loop. */
            *nonblocking = value != 0;
            return 0;
        }
        if (name == 0x80047308U) { /* SIOCSPGRP: host owns SIGURG */
            value = getpid();
            return ioctl(fd, SIOCSPGRP, &value);
        }
        errno = EOPNOTSUPP; return -1;
    }
    opt = option(level, name);
    if (opt < 0) return -1;
    if (level == 0xffff && (name == 0x1005 || name == 0x1006)) {
        int32_t wire[2];
        struct timeval tv;
        if (size != 8) { errno = EINVAL; return -1; }
        memcpy(wire, data, 8);
        tv.tv_sec = wire[0]; tv.tv_usec = wire[1];
        return setsockopt(fd, SOL_SOCKET, opt, &tv, sizeof(tv));
    }
    if (size != (name == 0x80 && level == 0xffff ? 8U : 4U)) {
        errno = EINVAL; return -1;
    }
    return setsockopt(fd, level == 0xffff ? SOL_SOCKET : level, opt, data, size);
}

int ios_net_get(int fd, int level, uint32_t name, void *data, unsigned *size)
{
    int opt, rc;
    socklen_t len = *size;
    if(level==IOS_SOL_TLS) {
#ifdef IOS_TLS
        if(name==IOS_TLS_CAPS && *size>=4){memcpy(data,"HTL1",4);*size=4;return 0;}
#endif
        if(name==IOS_TLS_STATUS)return ios_tls_status(fd,data,size);
        errno=ENOPROTOOPT;return -1;
    }
    if (level == 0xfff1) {
        unsigned long cmd;
        if (*size < 4) { errno = EINVAL; return -1; }
        /* Helios extension: non-consuming socket readiness. Wire bits are
         * explicit and independent of the Linux poll constant values. */
        if (name == 0x4004687fU) {
            struct pollfd p;
            uint32_t bits = 0;
            p.fd = fd; p.events = POLLIN|POLLOUT|POLLPRI|POLLRDHUP; p.revents = 0;
            if (ios_tls_poll(&p,1,0) < 0) return -1;
            if (p.revents & POLLIN) bits |= 1;
            if (p.revents & POLLOUT) bits |= 2;
            if (p.revents & POLLPRI) bits |= 4;
            if (p.revents & POLLERR) bits |= 8;
            if (p.revents & (POLLHUP|POLLRDHUP)) bits |= 16;
            if (p.revents & POLLNVAL) bits |= 32;
            memcpy(data,&bits,4); *size = 4; return 0;
        }
        switch (name) {
        case 0x4004667fU: cmd = FIONREAD; break;
        case 0x40047307U: cmd = SIOCATMARK; break;
        case 0x40047309U: cmd = SIOCGPGRP; break;
        default: errno = EOPNOTSUPP; return -1;
        }
        *size = 4;
        if(cmd==FIONREAD) {
            int io=ios_tls_fd(fd,0);
            if(io<0 && errno==EAGAIN){memset(data,0,4);return 0;}
            if(io<0)return -1;
            return ioctl(io,cmd,data);
        }
        return ioctl(fd, cmd, data);
    }
    if (level == 0xffff && (name == 0x8003 || name == 0x8004)) {
        rc = name == 0x8003 ? getpeername(fd, data, &len) : getsockname(fd, data, &len);
        *size = len;
        return rc;
    }
    opt = option(level, name);
    if (opt < 0) return -1;
    if (level == 0xffff && (name == 0x1005 || name == 0x1006)) {
        struct timeval tv;
        int32_t wire[2];
        len = sizeof(tv);
        if (*size < 8) { errno = EINVAL; return -1; }
        rc = getsockopt(fd, SOL_SOCKET, opt, &tv, &len);
        if (rc == 0) {
            wire[0] = tv.tv_sec; wire[1] = tv.tv_usec;
            memcpy(data, wire, 8); *size = 8;
        }
        return rc;
    }
    rc = getsockopt(fd, level == 0xffff ? SOL_SOCKET : level, opt, data, &len);
    *size = len;
    return rc;
}
