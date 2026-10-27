/* Keep Linux constants separate from the historical protocol.h definitions. */
#include "net_compat.h"
#include "net_wire.h"
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

/* These are Helios/BSD wire values. They must not be replaced by Linux
 * constants: for example, Helios SOL_SOCKET is 0xffff, not Linux's value. */
#define HELIOS_SOL_SOCKET 0xffff
#define HELIOS_SOL_IOCTL 0xfff1
#define HELIOS_FIONBIO 0x8004667eU
#define HELIOS_FIONREAD 0x4004667fU
#define HELIOS_SIOCSPGRP 0x80047308U
#define HELIOS_SIOCATMARK 0x40047307U
#define HELIOS_SIOCGPGRP 0x40047309U
#define HELIOS_SOCKET_READINESS 0x4004687fU

enum HeliosSocketOption {
    HELIOS_SO_DEBUG = 0x0001,
    HELIOS_SO_ACCEPTCONN = 0x0002,
    HELIOS_SO_REUSEADDR = 0x0004,
    HELIOS_SO_KEEPALIVE = 0x0008,
    HELIOS_SO_DONTROUTE = 0x0010,
    HELIOS_SO_BROADCAST = 0x0020,
    HELIOS_SO_LINGER = 0x0080,
    HELIOS_SO_OOBINLINE = 0x0100,
    HELIOS_SO_SNDBUF = 0x1001,
    HELIOS_SO_RCVBUF = 0x1002,
    HELIOS_SO_SNDLOWAT = 0x1003,
    HELIOS_SO_RCVLOWAT = 0x1004,
    HELIOS_SO_SNDTIMEO = 0x1005,
    HELIOS_SO_RCVTIMEO = 0x1006,
    HELIOS_SO_ERROR = 0x1007,
    HELIOS_SO_TYPE = 0x1008,
    HELIOS_SO_PEERNAME = 0x8003,
    HELIOS_SO_SOCKNAME = 0x8004
};

enum HeliosReadiness {
    HELIOS_READY_READ = 1,
    HELIOS_READY_WRITE = 2,
    HELIOS_READY_URGENT = 4,
    HELIOS_READY_ERROR = 8,
    HELIOS_READY_HANGUP = 16,
    HELIOS_READY_INVALID = 32
};

/* The public descriptor keeps addresses and socket options. A provider may
 * substitute a local socketpair endpoint for plaintext data and readiness. */
int ios_net_fd(int fd, int writing)
{
    return ios_ssh_active(fd) ? ios_ssh_fd(fd, writing) : ios_tls_fd(fd, writing);
}

int ios_net_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    return ios_ssh_poll(fds, count, timeout);
}

int ios_net_shutdown(int fd, int how)
{
    return ios_ssh_shutdown(fd, how);
}

static int stream_eof(int fd)
{
    return ios_ssh_active(fd) ? ios_ssh_eof(fd) : ios_tls_eof(fd);
}

int ios_net_close(int fd)
{
    ios_ssh_forget(fd);
    ios_tls_forget(fd);
    return close(fd);
}

ssize_t ios_net_read(int fd, void *data, size_t size)
{
    int data_fd = ios_net_fd(fd, 0);
    ssize_t n;
    if (data_fd < 0) {
        return -1;
    }
    n = read(data_fd, data, size);
    return n == 0 && size ? stream_eof(fd) : n;
}

ssize_t ios_net_write(int fd, const void *data, size_t size)
{
    int data_fd = ios_net_fd(fd, 1);
    return data_fd < 0 ? -1 : write(data_fd, data, size);
}

ssize_t ios_net_sendmsg(int fd, const struct msghdr *msg, int flags)
{
    int data_fd = ios_net_fd(fd, 1);
    struct msghdr plain;
    if (data_fd < 0) {
        return -1;
    }
    if (data_fd == fd) {
        return sendmsg(data_fd, msg, flags);
    }
    if ((flags & MSG_OOB) || msg->msg_controllen) {
        errno = EOPNOTSUPP;
        return -1;
    }
    plain = *msg;
    plain.msg_name = NULL;
    plain.msg_namelen = 0;
    return sendmsg(data_fd, &plain, flags);
}

ssize_t ios_net_recvmsg(int fd, struct msghdr *msg, int flags)
{
    int data_fd = ios_net_fd(fd, 0);
    ssize_t n;
    struct msghdr plain;
    if (data_fd < 0) {
        return -1;
    }
    if (data_fd == fd) {
        return recvmsg(data_fd, msg, flags);
    }
    if (flags & MSG_OOB) {
        errno = EOPNOTSUPP;
        return -1;
    }
    plain = *msg;
    plain.msg_name = NULL;
    plain.msg_namelen = 0;
    n = recvmsg(data_fd, &plain, flags);
    if (n >= 0) {
        msg->msg_flags = plain.msg_flags;
        msg->msg_controllen = plain.msg_controllen;
        if (msg->msg_name && getpeername(fd, msg->msg_name, &msg->msg_namelen) < 0) {
            msg->msg_namelen = 0;
        }
    }
    return n == 0 ? stream_eof(fd) : n;
}

ssize_t ios_net_recvfrom(int fd, void *data, size_t size, int flags,
                         struct sockaddr *addr, socklen_t *len)
{
    int data_fd = ios_net_fd(fd, 0);
    ssize_t n;
    if (data_fd < 0) {
        return -1;
    }
    if (data_fd == fd) {
        return recvfrom(data_fd, data, size, flags, addr, len);
    }
    if (flags & MSG_OOB) {
        errno = EOPNOTSUPP;
        return -1;
    }
    n = recvfrom(data_fd, data, size, flags, NULL, NULL);
    if (n >= 0 && addr && len && getpeername(fd, addr, len) < 0) {
        *len = 0;
    }
    return n == 0 && size ? stream_eof(fd) : n;
}

ssize_t ios_net_sendto(int fd, const void *data, size_t size, int flags,
                       const struct sockaddr *addr, socklen_t len)
{
    int data_fd = ios_net_fd(fd, 1);
    if (data_fd < 0) {
        return -1;
    }
    if (data_fd != fd) {
        if (flags & MSG_OOB) {
            errno = EOPNOTSUPP;
            return -1;
        }
        addr = NULL;
        len = 0;
    }
    return sendto(data_fd, data, size, flags, addr, len);
}

int32_t ios_net_deadline(int fd, int writing, int32_t now, int32_t requested,
                         unsigned tick_us)
{
    struct timeval tv;
    socklen_t len = sizeof(tv);
    int64_t ticks = requested < 0 ? INT32_MAX : requested;
    if (getsockopt(fd, SOL_SOCKET, writing ? SO_SNDTIMEO : SO_RCVTIMEO, &tv, &len) ==
            0 &&
        (tv.tv_sec || tv.tv_usec)) {
        int64_t limit =
            ((int64_t)tv.tv_sec * 1000000 + tv.tv_usec + tick_us - 1) / tick_us;
        if (limit < ticks) {
            ticks = limit;
        }
    }
    return ticks + now > INT32_MAX ? INT32_MAX : (int32_t)(ticks + now);
}

static int linux_socket_option(int level, uint32_t name)
{
    if (level == HELIOS_SOL_SOCKET) {
        switch (name) {
        case HELIOS_SO_DEBUG:
            return SO_DEBUG;
        case HELIOS_SO_ACCEPTCONN:
            return SO_ACCEPTCONN;
        case HELIOS_SO_REUSEADDR:
            return SO_REUSEADDR;
        case HELIOS_SO_KEEPALIVE:
            return SO_KEEPALIVE;
        case HELIOS_SO_DONTROUTE:
            return SO_DONTROUTE;
        case HELIOS_SO_BROADCAST:
            return SO_BROADCAST;
        case HELIOS_SO_LINGER:
            return SO_LINGER;
        case HELIOS_SO_OOBINLINE:
            return SO_OOBINLINE;
        case HELIOS_SO_SNDBUF:
            return SO_SNDBUF;
        case HELIOS_SO_RCVBUF:
            return SO_RCVBUF;
        case HELIOS_SO_SNDLOWAT:
            return SO_SNDLOWAT;
        case HELIOS_SO_RCVLOWAT:
            return SO_RCVLOWAT;
        case HELIOS_SO_SNDTIMEO:
            return SO_SNDTIMEO;
        case HELIOS_SO_RCVTIMEO:
            return SO_RCVTIMEO;
        case HELIOS_SO_ERROR:
            return SO_ERROR;
        case HELIOS_SO_TYPE:
            return SO_TYPE;
        }
    } else if (level == IPPROTO_TCP) {
        if (name == 1) {
            return TCP_NODELAY;
        }
        if (name == 2) {
            return TCP_MAXSEG;
        }
    }
    errno = ENOPROTOOPT;
    return -1;
}

void ios_net_identity(char *name, unsigned size, void *address)
{
    struct ifaddrs *all, *p;
    uint32_t addr = htonl(INADDR_LOOPBACK);
    if (gethostname(name, size) < 0) {
        strncpy(name, "localhost", size);
    }
    name[size - 1] = 0;
    /* No assumption that the Linux hostname resolves in DNS or /etc/hosts. */
    if (getifaddrs(&all) == 0) {
        for (p = all; p; p = p->ifa_next) {
            if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET &&
                (p->ifa_flags & IFF_UP) && !(p->ifa_flags & IFF_LOOPBACK)) {
                addr = ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr;
                break;
            }
        }
        freeifaddrs(all);
    }
    memcpy(address, &addr, 4);
}

/* Helios socket timeouts contain signed 32-bit seconds and microseconds. */
typedef struct {
    int32_t seconds, microseconds;
} HeliosSocketTimeout;

static HeliosSocketTimeout decode_socket_timeout(const void *data)
{
    WireReader reader = {data};
    HeliosSocketTimeout timeout;

    timeout.seconds = (int32_t)wire_read_word(&reader);
    timeout.microseconds = (int32_t)wire_read_word(&reader);
    return timeout;
}

static void encode_socket_timeout(void *data, const HeliosSocketTimeout *timeout)
{
    WireWriter writer = {data};

    wire_write_word(&writer, timeout->seconds);
    wire_write_word(&writer, timeout->microseconds);
}

int ios_net_set(int fd, int level, uint32_t name, const void *data, unsigned size,
                int *nonblocking)
{
    int opt;
    if (level == IOS_SOL_SSH) {
        return ios_ssh_set(fd, name, data, size);
    }
    if (level == IOS_SOL_TLS) {
        if (ios_ssh_active(fd)) {
            errno = EALREADY;
            return -1;
        }
        if (name == IOS_TLS_CLIENT) {
            return ios_tls_start(fd, data, size);
        }
        errno = ENOPROTOOPT;
        return -1;
    }
    if (level == HELIOS_SOL_IOCTL) {
        int32_t value;
        if (size != 4) {
            errno = EINVAL;
            return -1;
        }
        memcpy(&value, data, 4);
        if (name == HELIOS_FIONBIO) { /* Helios FIONBIO */
            /* Host descriptors MUST stay nonblocking: one event loop. */
            *nonblocking = value != 0;
            return 0;
        }
        if (name == HELIOS_SIOCSPGRP) { /* SIOCSPGRP: host owns SIGURG */
            value = getpid();
            return ioctl(fd, SIOCSPGRP, &value);
        }
        errno = EOPNOTSUPP;
        return -1;
    }
    opt = linux_socket_option(level, name);
    if (opt < 0) {
        return -1;
    }
    if (level == HELIOS_SOL_SOCKET &&
        (name == HELIOS_SO_SNDTIMEO || name == HELIOS_SO_RCVTIMEO)) {
        HeliosSocketTimeout timeout;
        struct timeval tv;
        if (size != 8) {
            errno = EINVAL;
            return -1;
        }
        timeout = decode_socket_timeout(data);
        tv.tv_sec = timeout.seconds;
        tv.tv_usec = timeout.microseconds;
        return setsockopt(fd, SOL_SOCKET, opt, &tv, sizeof(tv));
    }
    if (size != (name == HELIOS_SO_LINGER && level == HELIOS_SOL_SOCKET ? 8U : 4U)) {
        errno = EINVAL;
        return -1;
    }
    return setsockopt(fd, level == HELIOS_SOL_SOCKET ? SOL_SOCKET : level, opt, data,
                      size);
}

/* Report plaintext readiness without reading bytes or clearing socket errors. */
static int get_readiness(int fd, void *data, unsigned *size)
{
    struct pollfd p;
    uint32_t bits = 0;
    p.fd = fd;
    p.events = POLLIN | POLLOUT | POLLPRI | POLLRDHUP;
    p.revents = 0;
    if (ios_net_poll(&p, 1, 0) < 0) {
        return -1;
    }
    if (p.revents & POLLIN) {
        bits |= HELIOS_READY_READ;
    }
    if (p.revents & POLLOUT) {
        bits |= HELIOS_READY_WRITE;
    }
    if (p.revents & POLLPRI) {
        bits |= HELIOS_READY_URGENT;
    }
    if (p.revents & POLLERR) {
        bits |= HELIOS_READY_ERROR;
    }
    if (p.revents & (POLLHUP | POLLRDHUP)) {
        bits |= HELIOS_READY_HANGUP;
    }
    if (p.revents & POLLNVAL) {
        bits |= HELIOS_READY_INVALID;
    }
    memcpy(data, &bits, 4);
    *size = 4;
    return 0;
}

static int get_helios_ioctl(int fd, uint32_t name, void *data, unsigned *size)
{
    unsigned long cmd;
    if (*size < 4) {
        errno = EINVAL;
        return -1;
    }
    /* Helios extension: non-consuming socket readiness. Wire bits are
     * explicit and independent of the Linux poll constant values. */
    if (name == HELIOS_SOCKET_READINESS) {
        return get_readiness(fd, data, size);
    }
    switch (name) {
    case HELIOS_FIONREAD:
        cmd = FIONREAD;
        break;
    case HELIOS_SIOCATMARK:
        cmd = SIOCATMARK;
        break;
    case HELIOS_SIOCGPGRP:
        cmd = SIOCGPGRP;
        break;
    default:
        errno = EOPNOTSUPP;
        return -1;
    }
    *size = 4;
    if (cmd == FIONREAD) {
        int data_fd = ios_net_fd(fd, 0);
        if (data_fd < 0 && errno == EAGAIN) {
            memset(data, 0, 4);
            return 0;
        }
        if (data_fd < 0) {
            return -1;
        }
        return ioctl(data_fd, cmd, data);
    }
    return ioctl(fd, cmd, data);
}

int ios_net_get(int fd, int level, uint32_t name, void *data, unsigned *size)
{
    int opt, rc;
    socklen_t len = *size;
    if (level == IOS_SOL_SSH) {
        return ios_ssh_get(fd, name, data, size);
    }
    if (level == IOS_SOL_TLS) {
#ifdef IOS_TLS
        if (name == IOS_TLS_CAPS && *size >= 4) {
            memcpy(data, "HTL1", 4);
            *size = 4;
            return 0;
        }
#endif
        if (name == IOS_TLS_STATUS) {
            return ios_tls_status(fd, data, size);
        }
        errno = ENOPROTOOPT;
        return -1;
    }
    if (level == HELIOS_SOL_IOCTL) {
        return get_helios_ioctl(fd, name, data, size);
    }
    if (level == HELIOS_SOL_SOCKET &&
        (name == HELIOS_SO_PEERNAME || name == HELIOS_SO_SOCKNAME)) {
        rc = name == HELIOS_SO_PEERNAME ? getpeername(fd, data, &len)
                                        : getsockname(fd, data, &len);
        *size = len;
        return rc;
    }
    opt = linux_socket_option(level, name);
    if (opt < 0) {
        return -1;
    }
    if (level == HELIOS_SOL_SOCKET &&
        (name == HELIOS_SO_SNDTIMEO || name == HELIOS_SO_RCVTIMEO)) {
        struct timeval tv;
        HeliosSocketTimeout timeout;
        len = sizeof(tv);
        if (*size < 8) {
            errno = EINVAL;
            return -1;
        }
        rc = getsockopt(fd, SOL_SOCKET, opt, &tv, &len);
        if (rc == 0) {
            timeout.seconds = tv.tv_sec;
            timeout.microseconds = tv.tv_usec;
            encode_socket_timeout(data, &timeout);
            *size = 8;
        }
        return rc;
    }
    rc = getsockopt(fd, level == HELIOS_SOL_SOCKET ? SOL_SOCKET : level, opt, data,
                    &len);
    *size = len;
    return rc;
}
