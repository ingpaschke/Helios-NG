/* Linux implementation of the 32-bit Helios socket ABI. */
#ifndef IOS_NET_COMPAT_H
#define IOS_NET_COMPAT_H

#include <stdint.h>
#include <sys/socket.h>
#include <poll.h>
#include "net_tls.h"
#include "net_ssh.h"

#define HU_O_HANGUP 0x08 /* FG_Select close/error notification, not urgent data. */
#define Multi_SocketHup 13L

/* Called by the coroutine thread. fd is always the original Helios socket;
 * providers translate it to a plaintext endpoint internally. Poll results
 * describe plaintext readiness, including during TLS/SSH negotiation. */
int ios_net_fd(int fd, int writing);
int ios_net_poll(struct pollfd *fds, nfds_t count, int timeout_ms);
int ios_net_shutdown(int fd, int how);
int ios_net_close(int fd);
ssize_t ios_net_read(int fd, void *data, size_t size);
ssize_t ios_net_write(int fd, const void *data, size_t size);
ssize_t ios_net_sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t ios_net_recvmsg(int fd, struct msghdr *message, int flags);
ssize_t ios_net_recvfrom(int fd, void *data, size_t size, int flags,
                         struct sockaddr *address, socklen_t *address_size);
ssize_t ios_net_sendto(int fd, const void *data, size_t size, int flags,
                       const struct sockaddr *address, socklen_t address_size);

/* level/name are Helios constants, and data is a Helios wire record.
 * nonblocking records the client's mode; the Linux fd stays nonblocking.
 * get() receives the available buffer size and returns the bytes written. */
int ios_net_set(int fd, int level, uint32_t name, const void *data, unsigned size,
                int *nonblocking);
int ios_net_get(int fd, int level, uint32_t name, void *data, unsigned *size);

/* Supply a hostname and an IPv4 address without requiring local DNS setup.
 * address points to four bytes in network byte order; name_size must be > 0. */
void ios_net_identity(char *name, unsigned name_size, void *address);

/* Combine a Helios duration with SO_RCVTIMEO/SO_SNDTIMEO. now/requested are
 * coroutine ticks; tick_us gives their duration. Negative requested is infinite.
 * The absolute result saturates at INT32_MAX. */
int32_t ios_net_deadline(int fd, int writing, int32_t now, int32_t requested,
                         unsigned tick_us);
#endif
