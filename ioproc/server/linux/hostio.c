/* Linux host operations used by the shared Unix I/O adapter. */
#include "../helios.h"
#include "hostio.h"
#include <poll.h>
#include <sys/resource.h>

#define IOS_NOFILE_TARGET 4096

/* Each open Helios stream consumes a descriptor and a coroutine stack.
 * Raise the soft limit for busy sessions, with a cap for 32-bit hosts. */
void ios_linux_raise_fd_limit(void)
{
    struct rlimit limit;
    rlim_t wanted = IOS_NOFILE_TARGET;

    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        return;
    }
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < wanted) {
        wanted = limit.rlim_max;
    }
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= wanted) {
        return;
    }
    limit.rlim_cur = wanted;
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        printf(" Helios: unable to raise the descriptor limit to %ld.\n", (long)wanted);
    }
}

/* Unlike select, poll accepts terminal descriptors beyond FD_SETSIZE. */
int ios_linux_wait_input(int fd, int timeout_ms)
{
    struct pollfd descriptor;

    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    return poll(&descriptor, 1, timeout_ms);
}

/* Leave errno for the Unix adapter to translate into a Helios error. */
int ios_linux_write_all(int fd, WORD amount, const BYTE *buffer)
{
    int written = 0;

    while (written < amount) {
        int count = write(fd, buffer + written, amount - written);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            if (count == 0) {
                errno = EIO;
            }
            return -1;
        }
        written += count;
    }
    return 0;
}

void goto_sleep(word delay)
{
    usleep(delay);
}
