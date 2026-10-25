/* Linux sockets behind the little-endian, 32-bit Helios socket ABI. */
#ifndef IOS_NET_COMPAT_H
#define IOS_NET_COMPAT_H
#define HU_O_HANGUP 0x08 /* private FG_Select close/error, without POLLPRI */
#define Multi_SocketHup 13L
#include <stdint.h>
#include <sys/socket.h>
#include <poll.h>
#include "net_tls.h"
#include "net_ssh.h"
int ios_net_fd(int,int);
int ios_net_poll(struct pollfd *,nfds_t,int);
int ios_net_shutdown(int,int);
int ios_net_close(int);
ssize_t ios_net_read(int,void *,size_t);
ssize_t ios_net_write(int,const void *,size_t);
ssize_t ios_net_sendmsg(int,const struct msghdr *,int);
ssize_t ios_net_recvmsg(int,struct msghdr *,int);
ssize_t ios_net_recvfrom(int,void *,size_t,int,struct sockaddr *,socklen_t *);
ssize_t ios_net_sendto(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
int ios_net_set(int, int, uint32_t, const void *, unsigned, int *);
int ios_net_get(int, int, uint32_t, void *, unsigned *);
void ios_net_identity(char *, unsigned, void *);
int32_t ios_net_deadline(int, int, int32_t, int32_t, unsigned);
#endif
