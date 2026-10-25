/* Incoming SSH transport. Helios owns authentication and login sessions. */
#ifndef IOS_SSH_SERVER_H
#define IOS_SSH_SERVER_H
int ios_sshd_start(const char *, int, const char *);
void ios_sshd_stop(void);
/* Open the notification stream (sshd) or claim ssh.XXXXXXXX once. */
int ios_sshd_open(const char *);
#endif
