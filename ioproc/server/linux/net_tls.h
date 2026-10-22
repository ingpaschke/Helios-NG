#ifndef IOS_NET_TLS_H
#define IOS_NET_TLS_H
#include <poll.h>
#include <stdint.h>
#define IOS_SOL_TLS 0xfff2
#define IOS_TLS_CAPS 0
#define IOS_TLS_CLIENT 1
#define IOS_TLS_STATUS 2
#define IOS_TLS_REQUEST_SIZE 264
#define IOS_TLS_STATUS_SIZE 224
#define IOS_TLS_NEGOTIATING 1
#define IOS_TLS_READY 2
#define IOS_TLS_FAILED 3
#define IOS_TLS_CLOSED 4
int ios_tls_start(int,const void *,unsigned);
int ios_tls_status(int,void *,unsigned *);
int ios_tls_fd(int,int);
int ios_tls_eof(int);
int ios_tls_poll(struct pollfd *,nfds_t,int);
int ios_tls_shutdown(int,int);
void ios_tls_forget(int);
#endif
