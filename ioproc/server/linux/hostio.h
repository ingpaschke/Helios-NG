/* Host operations shared by the Unix adapter and the Linux scheduler. */
#ifndef IOS_LINUX_HOSTIO_H
#define IOS_LINUX_HOSTIO_H

void ios_linux_raise_fd_limit(void);
int ios_linux_wait_input(int fd, int timeout_ms);
int ios_linux_write_all(int fd, WORD amount, const BYTE *buffer);

/* Implemented by sunlocal.c, which owns the private Unix window table.
 * Returns a borrowed descriptor, or -1 for an inactive console window.
 * The caller must not close the descriptor. */
int ios_linux_window_fd(void *window);

#endif
