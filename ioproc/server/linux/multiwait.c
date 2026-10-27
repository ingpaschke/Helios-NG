/* Linux uses one poll slot per descriptor, with independent coroutine
 * and select-result pointers for each direction. */
#include <poll.h>
#include "../helios.h"
#include "hostio.h"
#include "net_compat.h"

typedef struct {
    WORD *ready[4];
    WORD *result[4];
} LinuxWaiter;
static struct pollfd *wait_fds;
static LinuxWaiter *waiters;
static size_t wait_count, wait_capacity;
/* Linux's peer write-half close need not include POLLHUP. Let an exception
 * subscription wake for it too, so hu_poll can observe EOF without polling. */
#ifndef POLLRDHUP
#define POLLRDHUP 0x2000
#endif
static const short wait_events[4] = {POLLIN, POLLOUT, POLLPRI | POLLRDHUP, POLLRDHUP};
static const WORD wait_results[4] = {O_ReadOnly, O_WriteOnly, O_Exception, HU_O_HANGUP};

void InitMultiwait(void)
{
    wait_fds = NULL;
    waiters = NULL;
    wait_count = wait_capacity = 0;
}

void TidyMultiwait(void)
{
    free(wait_fds);
    free(waiters);
    InitMultiwait();
}

void RestartMultiwait(void)
{
    int i;
    wait_count = 0;
    for (i = 0; i < number_of_links; ++i) {
        link_table[i].flags &= ~Link_flags_waiting;
    }
}

static void linux_wait_set(int fd, int direction, WORD *ready, WORD *result)
{
    size_t i;
    int d;
    if (fd < 0) {
        return;
    }
    for (i = 0; i < wait_count; ++i) {
        if (wait_fds[i].fd == fd) {
            break;
        }
    }
    if (i == wait_count) {
        if (!ready) {
            return;
        }
        if (wait_count == wait_capacity) {
            size_t capacity = wait_capacity ? wait_capacity * 2 : 32;
            struct pollfd *fds = malloc(capacity * sizeof(*fds));
            LinuxWaiter *new_waiters = malloc(capacity * sizeof(*new_waiters));
            if (!fds || !new_waiters) {
                free(fds);
                free(new_waiters);
                ServerDebug("Failed to grow multiwait table");
                longjmp_exit;
            }
            if (wait_count) {
                memcpy(fds, wait_fds, wait_count * sizeof(*fds));
                memcpy(new_waiters, waiters, wait_count * sizeof(*new_waiters));
            }
            free(wait_fds);
            free(waiters);
            wait_fds = fds;
            waiters = new_waiters;
            wait_capacity = capacity;
        }
        wait_fds[i].fd = fd;
        wait_fds[i].events = wait_fds[i].revents = 0;
        memset(&waiters[i], 0, sizeof(waiters[i]));
        ++wait_count;
    }
    waiters[i].ready[direction] = ready;
    waiters[i].result[direction] = ready ? result : NULL;
    wait_fds[i].events = 0;
    for (d = 0; d < 4; ++d) {
        if (waiters[i].ready[d]) {
            wait_fds[i].events |= wait_events[d];
        }
    }
    if (!wait_fds[i].events) {
        --wait_count;
        wait_fds[i] = wait_fds[wait_count];
        waiters[i] = waiters[wait_count];
    }
}

static void linux_wait_change(WORD code, WORD *addr, intptr_t extra, WORD *result)
{
    int fd, direction = 0;
    switch (code) {
    case Multi_LinkMessage:
        if (link_table[extra].flags & Link_flags_not_selectable) {
            if (addr) {
                link_table[extra].flags |= Link_flags_waiting;
            } else {
                link_table[extra].flags &= ~Link_flags_waiting;
            }
            return;
        }
        fd = link_table[extra].fildes;
        break;
    case Multi_WindowInput:
        fd = ios_linux_window_fd((void *)extra);
        break;
    case Multi_MouseInput:
    case Multi_KeyboardInput:
    case Multi_SocketInput:
        fd = extra;
        break;
    case Multi_SocketOutput:
        fd = extra;
        direction = 1;
        break;
    case Multi_SocketExcp:
        fd = extra;
        direction = 2;
        break;
    case Multi_SocketHup:
        fd = extra;
        direction = 3;
        break;
    default:
        ServerDebug("Unknown multiwait %d", code);
        return;
    }
    linux_wait_set(fd, direction, addr, result);
}

void AddMultiwait(WORD code, WORD *addr, ...)
{
    va_list args;
    intptr_t extra;
    WORD *result = NULL;
    va_start(args, addr);
    extra = va_arg(args, intptr_t);
    if (code == Multi_SocketInput || code == Multi_SocketOutput ||
        code == Multi_SocketExcp || code == Multi_SocketHup) {
        result = (WORD *)va_arg(args, intptr_t);
    }
    va_end(args);
    linux_wait_change(code, addr, extra, result);
}

void ClearMultiwait(WORD code, ...)
{
    va_list args;
    intptr_t extra;
    va_start(args, code);
    extra = va_arg(args, intptr_t);
    va_end(args);
    linux_wait_change(code, NULL, extra, NULL);
}

WORD Multiwait(void)
{
    int i, direction, selected, timeout = Multi_nowait > 0 ? 1 : 50;
    size_t slot;
    /* MMIO links cannot be polled as descriptors. Keep their short polling
     * interval and priority over host descriptors. */
    for (i = 0; i < number_of_links; ++i) {
        if (link_table[i].flags & Link_flags_waiting) {
            int old = current_link, ready;
            current_link = i;
            ready = xprdrdy();
            current_link = old;
            if (ready) {
                link_table[i].ready = CoReady;
                return TRUE;
            }
            timeout = 1;
        }
    }
    selected = ios_net_poll(wait_fds, wait_count, timeout);
    if (selected <= 0) {
        if (selected < 0 && errno != EINTR) {
            ServerDebug("Warning: poll failed: errno %d", errno);
        }
        return FALSE;
    }
    for (slot = 0; slot < wait_count; ++slot) {
        short events = wait_fds[slot].revents;
        for (direction = 0; direction < 4; ++direction) {
            if (waiters[slot].ready[direction] &&
                (events & (wait_events[direction] | POLLHUP | POLLERR | POLLNVAL))) {
                *waiters[slot].ready[direction] = CoReady;
                if (waiters[slot].result[direction]) {
                    *waiters[slot].result[direction] |= wait_results[direction];
                }
            }
        }
        /* Invalid descriptors cannot become ready again. Remove their
         * subscription after the final wakeup instead of polling it forever. */
        if (events & POLLNVAL) {
            --wait_count;
            if (slot < wait_count) {
                wait_fds[slot] = wait_fds[wait_count];
                waiters[slot] = waiters[wait_count];
            }
            --slot;
        }
    }
    return TRUE;
}
