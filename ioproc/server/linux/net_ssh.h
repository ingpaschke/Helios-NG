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

/* Values below are part of the HSH1 protocol, not libssh2 constants. */
enum ios_ssh_operation {
    IOS_SSH_CAPABILITIES = 0,
    IOS_SSH_START = 1,
    IOS_SSH_STATUS = 2,
    IOS_SSH_TRUST = 3,
    IOS_SSH_AUTHENTICATE = 4,
    IOS_SSH_RESIZE = 5,
    IOS_SSH_SEND_EOF = 6,
    IOS_SSH_UPLOAD = 7
};

enum ios_ssh_state {
    IOS_SSH_CONNECTING = 1,
    IOS_SSH_HOST_KEY = 2,
    IOS_SSH_AUTHENTICATING = 3,
    IOS_SSH_OPENING = 4,
    IOS_SSH_READY = 5,
    IOS_SSH_CLOSED = 6,
    IOS_SSH_FAILED = 7
};

/* Coroutine-thread API. START borrows a connected, nonblocking TCP fd.
 * Only its worker calls libssh2. Setters copy controls under the connection
 * mutex; none waits for a network operation or a user's response. */
int ios_ssh_set(int fd, unsigned operation, const void *data, unsigned size);
int ios_ssh_get(int fd, unsigned operation, void *data, unsigned *size);
int ios_ssh_active(int fd);

/* fd() returns the plaintext endpoint, or -1/EAGAIN until READY. Reads merge
 * remote stdout/stderr. EOF retains the separate status/error query.
 * forget() cancels the worker and drops the registry reference without joining;
 * worker-owned duplicate descriptors remain valid until its cleanup completes. */
int ios_ssh_fd(int fd, int writing);
int ios_ssh_eof(int fd);
int ios_ssh_poll(struct pollfd *fds, nfds_t count, int timeout_ms);
int ios_ssh_shutdown(int fd, int how);
void ios_ssh_forget(int fd);
#endif
