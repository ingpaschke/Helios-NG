#ifndef IOS_NET_TLS_H
#define IOS_NET_TLS_H

#include <poll.h>
#include <stdint.h>

/* Private HTL1 controls. All integer fields are 32-bit little-endian words.
 * CLIENT: version, timeout_ms, hostname[256] including the terminating NUL.
 * STATUS: magic, state, error, reserved, protocol[16], cipher[64], message[128].
 * Strings are bounded byte arrays; host C structures are never sent on wire. */
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

enum ios_tls_error {
    IOS_TLS_ERROR_SOCKET = 1,
    IOS_TLS_ERROR_CERTIFICATE = 2,
    IOS_TLS_ERROR_PROTOCOL = 3,
    IOS_TLS_ERROR_TIMEOUT = 4,
    IOS_TLS_ERROR_CANCELLED = 5,
    IOS_TLS_ERROR_CONFIGURATION = 6
};

/* Coroutine-thread API. start() borrows a connected, nonblocking TCP fd.
 * A worker owns its duplicate and the SSL object. Status is copied under a
 * mutex. forget() removes the registry entry immediately, while an established
 * worker may finish sending already accepted plaintext for up to five seconds. */
int ios_tls_start(int fd, const void *request, unsigned size);
int ios_tls_status(int fd, void *status, unsigned *size);

/* fd() returns the plaintext endpoint, or -1/EAGAIN during negotiation.
 * eof() distinguishes a failed TLS stream from an orderly plaintext EOF.
 * SHUT_RD is a no-op: TLS does not have a read-side half close. */
int ios_tls_fd(int fd, int writing);
int ios_tls_eof(int fd);
int ios_tls_poll(struct pollfd *fds, nfds_t count, int timeout_ms);
int ios_tls_shutdown(int fd, int how);
void ios_tls_forget(int fd);
#endif
