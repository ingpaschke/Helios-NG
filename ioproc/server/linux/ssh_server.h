#ifndef IOS_SSH_SERVER_H
#define IOS_SSH_SERVER_H

/* Start/stop the Linux SSH listener from the coroutine thread. address is a
 * numeric IPv4 address (NULL means all interfaces); path is the host-key file.
 * Stop shuts down connections, joins the listener and waits for its workers. */
int ios_sshd_start(const char *address, int port, const char *path);
void ios_sshd_stop(void);

/* "sshd" duplicates the nonblocking notification fd. It delivers four-byte
 * little-endian tokens. "ssh.XXXXXXXX" claims a token's socketpair endpoint
 * exactly once; the caller then owns and closes that fd. The native login
 * service exchanges framed authentication, PTY, data and exit-status records.
 * Helios owns accounts and sessions; the host never creates a Linux login. */
int ios_sshd_open(const char *name);
#endif
