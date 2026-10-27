/*
 * t4link.c -- link "box" t4 for the Linux port of the Helios I/O Server.
 *
 * The root transputer is the t4 emulator, reached over a stream socket.
 * Protocol (fixed, implemented by t4):
 *   - t4 listens; the I/O Server connects.
 *   - A new connection IS the hardware reset: t4 resets the processor and
 *     waits for a bootstrap on link 0 (INMOS boot protocol: first byte
 *     0 = poke addr,value; 1 = peek addr -> 4 reply bytes; n > 1 = n bytes
 *     of boot code).
 *   - reset_transputer() and analyse_transputer() both close any open
 *     connection and connect anew (retrying for about 5 s).
 *   - Afterwards the connection is a transparent byte stream both ways.
 *
 * host.con keys:
 *   box             = t4
 *   t4_link         = unix:/path/to/socket  |  tcp:host:port
 *                     (default unix:/tmp/t4link.sock)
 *   t4_timeout      = minimum wait in ms for a link read/write before it
 *                     counts as a timeout (default 2000)
 *   t4_retries      = connect attempts, 100 ms apart (default 50)
 *   t4_trace        = file name: hex trace of every link byte (debugging)
 *   t4_delay        = ms slept before every link read and write call
 *                     (models a slow host such as the Atari ST server;
 *                     default 0 = off; env IOS_T4_DELAY_MS overrides)
 *   t4_byte_us      = us per byte: reads and writes go one byte at a time
 *                     with this pause (models a byte-acknowledged link;
 *                     default 0 = off; env IOS_T4_BYTE_US overrides)
 *   env IOS_T4_HOLD_MS, IOS_T4_HOLD_COUNT (default 3): sleep HOLD_MS
 *                     before each of the first HOLD_COUNT message headers
 *                     (16-byte reads) after a reset (a host that is busy
 *                     setting up after the boot; default off)
 *
 * The link file descriptor number is kept stable across reconnects (the
 * new socket is dup2()ed onto the old number) because the server's
 * Multiwait() registered that number in its select() masks. After EOF or
 * a write error the descriptor is parked on the read end of a pipe that
 * is never written, so select() stops reporting it, and the server is told
 * to exit, the same way it treats a broken pipe to the Hydra link daemon.
 */

#include "helios.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>

#define link_fd (link_table[current_link].fildes)

PRIVATE char t4_target[256] = "unix:/tmp/t4link.sock";
PRIVATE int t4_min_wait = 2000; /* ms */
PRIVATE int t4_retries = 50;
PRIVATE FILE *t4_trace = NULL;
PRIVATE int t4_dead_fd = -1; /* read end of a silent pipe */
PRIVATE int t4_dead = 0;     /* connection lost since last reset */
PRIVATE long t4_bytes_in = 0, t4_bytes_out = 0;
PRIVATE int t4_delay_ms = 0; /* slow-host emulation, see above */
PRIVATE int t4_byte_us = 0;
PRIVATE int t4_hold_ms = 0, t4_hold_count = 3, t4_headers = 0;

/* Sleep that survives the server's signals (usleep returns early). */
PRIVATE void t4_sleep_us(long us)
{
    struct timespec t;
    t.tv_sec = us / 1000000L;
    t.tv_nsec = (us % 1000000L) * 1000L;
    while (nanosleep(&t, &t) < 0 && errno == EINTR)
        ;
}

PRIVATE void t4_slow(void)
{
    if (t4_delay_ms > 0) {
        t4_sleep_us((long)t4_delay_ms * 1000L);
    }
}

/*---------------------------------------------------------------- tracing */

PRIVATE long t4_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec % 100000) * 1000L + tv.tv_usec / 1000;
}

PRIVATE void t4_tracef(const char *fmt, ...)
{
    va_list ap;
    if (t4_trace == NULL) {
        return;
    }
    fprintf(t4_trace, "%9ld ", t4_ms());
    va_start(ap, fmt);
    vfprintf(t4_trace, fmt, ap);
    va_end(ap);
    fputc('\n', t4_trace);
    fflush(t4_trace);
}

PRIVATE void t4_tracedata(const char *dir, const unsigned char *p, int n, int want)
{
    int i;
    if (t4_trace == NULL) {
        return;
    }
    fprintf(t4_trace, "%9ld %s %d/%d:", t4_ms(), dir, n, want);
    for (i = 0; i < n && i < 64; i++) {
        fprintf(t4_trace, " %02x", p[i]);
    }
    if (n > 64) {
        fprintf(t4_trace, " ...");
    }
    fputc('\n', t4_trace);
    fflush(t4_trace);
}

/*------------------------------------------------------------ connection */

PRIVATE int t4_connect_once(void)
{
    int s;

    if (!strncmp(t4_target, "unix:", 5)) {
        struct sockaddr_un a;
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s < 0) {
            return -1;
        }
        memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        strncpy(a.sun_path, t4_target + 5, sizeof(a.sun_path) - 1);
        if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
            close(s);
            return -1;
        }
        return s;
    }
    if (!strncmp(t4_target, "tcp:", 4)) {
        char host[200], *colon;
        struct addrinfo hints, *res, *r;
        strncpy(host, t4_target + 4, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        colon = strrchr(host, ':');
        if (colon == NULL) {
            return -1;
        }
        *colon++ = '\0';
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host[0] ? host : "127.0.0.1", colon, &hints, &res) != 0) {
            return -1;
        }
        s = -1;
        for (r = res; r != NULL; r = r->ai_next) {
            s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
            if (s < 0) {
                continue;
            }
            if (connect(s, r->ai_addr, r->ai_addrlen) == 0) {
                int one = 1;
                setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                break;
            }
            close(s);
            s = -1;
        }
        freeaddrinfo(res);
        return s;
    }
    return -1;
}

/* Close the current connection, keeping the descriptor number reserved. */
PRIVATE void t4_disconnect(void)
{
    if (link_fd >= 0) {
        t4_tracef("disconnect (in %ld out %ld bytes)", t4_bytes_in, t4_bytes_out);
        if (dup2(t4_dead_fd, link_fd) < 0) { /* closes the socket */
            ServerDebug("t4: dup2 on disconnect failed (%s)", strerror(errno));
        }
    }
}

PRIVATE void t4_connect(void)
{
    int i, s = -1;

    t4_disconnect();
    for (i = 0; i < t4_retries; i++) {
        s = t4_connect_once();
        if (s >= 0) {
            break;
        }
        usleep(100000);
    }
    if (s < 0) {
        ServerDebug("t4: cannot connect to %s (%s)", t4_target, strerror(errno));
        t4_dead = 1;
        return;
    }
    if (link_fd < 0) {
        link_fd = s; /* first connection picks the number */
    }
    elif (s != link_fd)
    {
        if (dup2(s, (int)link_fd) < 0) {
            ServerDebug("t4: cannot move connection onto descriptor %d (%s)",
                        (int)link_fd, strerror(errno));
            close(s);
            t4_dead = 1;
            return;
        }
        close(s);
    }
    t4_dead = 0;
    t4_headers = 0;
    t4_bytes_in = t4_bytes_out = 0;
    t4_tracef("connected to %s, fd %ld (= processor reset)", t4_target, (long)link_fd);
}

PRIVATE void t4_link_broken(const char *why)
{
    if (t4_dead) {
        return;
    }
    t4_dead = 1;
    t4_tracef("link broken: %s", why);
    printf("\r\nFATAL ERROR : link to t4 (%s) has been broken: %s.\r\n", t4_target,
           why);
    printf("Attempting to exit safely.\r\n");
    t4_disconnect();
    Special_Exit = TRUE;
}

/* Wait until fd is readable (events = POLLIN) or writable; ms timeout. */
PRIVATE int64_t t4_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

PRIVATE int t4_wait(short events, int ms)
{
    struct pollfd p;
    int r;
    int64_t deadline = t4_monotonic_ms() + ms;

    if (t4_dead || link_fd < 0) {
        return 0;
    }
    p.fd = (int)link_fd;
    p.events = events;
    p.revents = 0;
    for (;;) {
        r = poll(&p, 1, ms);
        if (r >= 0 || errno != EINTR) {
            break;
        }
        ms = (int)(deadline - t4_monotonic_ms());
        if (ms <= 0) {
            return 0;
        }
    }
    return (r > 0);
}

PRIVATE int t4_wait_ms(int timeout)
{
    int ms = timeout * 100; /* server timeouts are in 1/10 s */
    if (ms < t4_min_wait) {
        ms = t4_min_wait;
    }
    return ms;
}

/*----------------------------------------------------------- box methods */

void t4_init_link(void)
{
    char *conf;
    int p[2];
    WORD v;

    number_of_links = 1;
    link_table[0].fildes = -1;
    strcpy((char *)link_table[0].link_name, "t4");

    conf = get_config("t4_link");
    if (conf != NULL) {
        strncpy(t4_target, conf, sizeof(t4_target) - 1);
        if (strncmp(t4_target, "unix:", 5) && strncmp(t4_target, "tcp:", 4)) {
            ServerDebug("t4_link must be unix:/path or tcp:host:port, not %s", conf);
            longjmp_exit;
        }
    }
    v = get_int_config("t4_timeout");
    if (v != Invalid_config && v > 0) {
        t4_min_wait = (int)v;
    }
    v = get_int_config("t4_retries");
    if (v != Invalid_config && v > 0) {
        t4_retries = (int)v;
    }
    v = get_int_config("t4_delay");
    if (v != Invalid_config && v > 0) {
        t4_delay_ms = (int)v;
    }
    v = get_int_config("t4_byte_us");
    if (v != Invalid_config && v > 0) {
        t4_byte_us = (int)v;
    }
    if (getenv("IOS_T4_DELAY_MS") != NULL) {
        t4_delay_ms = atoi(getenv("IOS_T4_DELAY_MS"));
    }
    if (getenv("IOS_T4_HOLD_MS") != NULL) {
        t4_hold_ms = atoi(getenv("IOS_T4_HOLD_MS"));
    }
    if (getenv("IOS_T4_HOLD_COUNT") != NULL) {
        t4_hold_count = atoi(getenv("IOS_T4_HOLD_COUNT"));
    }
    if (getenv("IOS_T4_BYTE_US") != NULL) {
        t4_byte_us = atoi(getenv("IOS_T4_BYTE_US"));
    }
    conf = get_config("t4_trace");
    if (getenv("IOS_T4_TRACE") != NULL) {
        conf = getenv("IOS_T4_TRACE");
    }
    if (conf != NULL) {
        t4_trace = fopen(conf, "w");
        if (t4_trace == NULL) {
            ServerDebug("t4: cannot open trace file %s", conf);
        }
    }

    signal(SIGPIPE, SIG_IGN);
    if (pipe(p) == 0) {
        t4_dead_fd = p[0]; /* p[1] stays open and is never written */
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        fcntl(p[1], F_SETFD, FD_CLOEXEC);
    }
    t4_tracef("t4 box: target %s, min wait %d ms, %d connect retries", t4_target,
              t4_min_wait, t4_retries);
    if (t4_delay_ms > 0 || t4_byte_us > 0) {
        ServerDebug("t4: slow link emulation: %d ms per call, %d us per byte",
                    t4_delay_ms, t4_byte_us);
    }
}

int t4_open_link(int tabno)
{ /* The connection is made by the first reset, not here: connecting
     already resets the emulated processor. */
    return 1;
    use(tabno)
}

void t4_free_link(int tabno)
{
    if (link_table[tabno].fildes >= 0) {
        t4_tracef("free link");
        close((int)link_table[tabno].fildes);
        link_table[tabno].fildes = -1;
    }
}

void t4_reset(void)
{
    t4_tracef("reset_transputer");
    t4_connect();
}

void t4_analyse(void)
{
    t4_tracef("analyse_transputer");
    t4_connect();
}

WORD t4_rdrdy(void)
{
    struct pollfd p;

    if (t4_dead || link_fd < 0) {
        return FALSE;
    }
    p.fd = (int)link_fd;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, 0) <= 0) {
        return FALSE;
    }
    return TRUE; /* includes EOF / error: the next read reports it */
}

WORD t4_wrrdy(void)
{
    return TRUE;
}

WORD t4_fetch_block(int count, BYTE *data, int timeout)
{
    int amount = 0, x;
    int ms = t4_wait_ms(timeout);
    int64_t deadline;

    t4_slow();
    if (count == 16 && t4_hold_ms > 0 && t4_headers++ < t4_hold_count) {
        t4_tracef("hold %d ms before header %d", t4_hold_ms, t4_headers);
        t4_sleep_us((long)t4_hold_ms * 1000L);
    }
    deadline = t4_monotonic_ms() + ms;
    while (amount < count) {
        int remaining = (int)(deadline - t4_monotonic_ms());
        if (remaining <= 0 || !t4_wait(POLLIN, remaining)) {
            if (!t4_dead) {
                t4_tracef("read timeout after %d ms, got %d of %d", ms, amount, count);
            }
            /* A timeout before the first byte is a normal idle poll. A      */
            /* timeout part way through a block leaves the rest of the       */
            /* message in the stream, and on a TCP transport there is no way */
            /* to resynchronise the framing, so treat it as a broken link.   */
            if (amount > 0 && amount < count) {
                t4_tracedata("R", (unsigned char *)data, amount, count);
                t4_link_broken("short read mid-message");
                return count - amount;
            }
            break;
        }
        if (t4_byte_us > 0) {
            t4_sleep_us((long)t4_byte_us);
        }
        x = recv((int)link_fd, data + amount, t4_byte_us > 0 ? 1 : count - amount,
                 MSG_DONTWAIT);
        if (x < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if (x <= 0) {
            t4_tracedata("R", (unsigned char *)data, amount, count);
            t4_link_broken(x == 0 ? "connection closed by t4" : strerror(errno));
            return count - amount;
        }
        amount += x;
    }
    t4_bytes_in += amount;
    t4_tracedata("R", (unsigned char *)data, amount, count);
    return count - amount;
}

WORD t4_send_block(int count, BYTE *data, int timeout)
{
    int amount = 0, x;
    int ms = t4_wait_ms(timeout);
    int64_t deadline;

    t4_slow();
    deadline = t4_monotonic_ms() + ms;
    while (amount < count) {
        int remaining = (int)(deadline - t4_monotonic_ms());
        if (remaining <= 0 || !t4_wait(POLLOUT, remaining)) {
            if (!t4_dead) {
                t4_tracef("write timeout after %d ms, sent %d of %d", ms, amount,
                          count);
            }
            break;
        }
        if (t4_byte_us > 0) {
            t4_sleep_us((long)t4_byte_us);
        }
        x = send((int)link_fd, data + amount, t4_byte_us > 0 ? 1 : count - amount,
                 MSG_DONTWAIT | MSG_NOSIGNAL);
        if (x < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if (x <= 0) {
            t4_tracedata("W", (unsigned char *)data, amount, count);
            t4_link_broken(x == 0 ? "write returned 0" : strerror(errno));
            return count - amount;
        }
        amount += x;
    }
    t4_bytes_out += amount;
    t4_tracedata("W", (unsigned char *)data, amount, count);
    return count - amount;
}

WORD t4_byte_from_link(UBYTE *where)
{
    return t4_fetch_block(1, (BYTE *)where, 1);
}

WORD t4_byte_to_link(int data)
{
    BYTE b = (BYTE)(data & 0xFF);
    return t4_send_block(1, &b, 1);
}
