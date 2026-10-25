#ifndef IOS_NET_SSH_H
#define IOS_NET_SSH_H
#include <poll.h>
/* Private little-endian HSH1 socket controls; strings include their NUL.
 * START: version, timeout_ms, port, columns, rows, pty, reserved[2], then
 * host[256], user[64], terminal[64], command[768].
 * STATUS: magic, state, error, exit_status, exit_valid, auth_methods,
 * auth_attempt, reserved; key_type[64], fingerprint[96], message[316].
 * AUTH: method, key_size, reserved[2], secret[512], private_key[8192].
 * START/AUTH are sent using control 7: operation, total_size, offset,
 * followed by up to 480 bytes. Every IOC request stays below 512 bytes. */
#define IOS_SOL_SSH 0xfff3
#define IOS_SSH_START_SIZE 1184
#define IOS_SSH_STATUS_SIZE 508
#define IOS_SSH_AUTH_SIZE 8720
int ios_ssh_set(int,unsigned,const void *,unsigned);
int ios_ssh_get(int,unsigned,void *,unsigned *);
int ios_ssh_active(int);
int ios_ssh_fd(int,int);
int ios_ssh_eof(int);
int ios_ssh_poll(struct pollfd *,nfds_t,int);
int ios_ssh_shutdown(int,int);
void ios_ssh_forget(int);
#endif
