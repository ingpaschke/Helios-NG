/* TLS lives entirely in Linux workers. The coroutine thread never calls
 * OpenSSL, waits for a handshake, or accesses a worker's SSL object.
 * A bounded nonblocking socketpair carries plaintext; the original socket
 * retains its identity, addresses and options in the Helios protocol.
 * The registry is coroutine-thread-only. A worker owns one reference and
 * duplicate network fd; close/reboot removes the registry reference and
 * wakes the worker without joining it or risking descriptor reuse. */
#include "net_tls.h"
#include "net_wire.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>

#ifdef IOS_TLS
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/* Native records; HTL1 layout is confined to these codecs. */
typedef struct {
    uint32_t timeout_ms;
    char host[256];
} TlsClientRequest;

typedef struct {
    uint32_t state, error;
    char protocol[16], cipher[64], message[128];
} TlsStatus;

static int decode_client_request(TlsClientRequest *request, const void *data,
                                 unsigned size)
{
    WireReader reader = {data};
    uint32_t version;

    if (size != IOS_TLS_REQUEST_SIZE) {
        return EINVAL;
    }
    version = wire_read_word(&reader);
    request->timeout_ms = wire_read_word(&reader);
    wire_read_bytes(&reader, request->host, sizeof(request->host));
    if (version != 1 || request->timeout_ms < 100 || request->timeout_ms > 120000 ||
        !request->host[0] || !memchr(request->host, 0, sizeof(request->host))) {
        return EINVAL;
    }
    return 0;
}

static void encode_tls_status(void *data, const TlsStatus *status)
{
    WireWriter writer = {data};

    wire_write_bytes(&writer, "HTL1", 4);
    wire_write_word(&writer, status->state);
    wire_write_word(&writer, status->error);
    wire_write_word(&writer, 0); /* reserved */
    wire_write_bytes(&writer, status->protocol, sizeof(status->protocol));
    wire_write_bytes(&writer, status->cipher, sizeof(status->cipher));
    wire_write_bytes(&writer, status->message, sizeof(status->message));
}

typedef struct TlsConnection TlsConnection;

struct TlsConnection {
    TlsConnection *next; /* Registry: coroutine thread only. */
    int original_fd;     /* Stable Helios socket identity. */
    int application_fd;  /* Coroutine end of the plaintext socketpair. */
    int worker_fd;       /* Worker end of the plaintext socketpair. */
    int network_fd;      /* Worker-owned duplicate of original_fd. */
    unsigned timeout_ms;
    char host[256];

    /* The worker and registry each own a reference. The mutex protects all
     * fields below: the coroutine thread reads status and requests closure. */
    pthread_mutex_t lock;
    int references, cancel_requested;
    long long drain_deadline;
    unsigned state, error;
    char protocol[16], cipher[64], message[128];
};

static TlsConnection *connections;
static SSL_CTX *context;
static pthread_once_t context_once = PTHREAD_ONCE_INIT;

static long long monotonic_milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static TlsConnection *find_connection(int fd)
{
    TlsConnection *connection;
    for (connection = connections; connection; connection = connection->next) {
        if (connection->original_fd == fd) {
            return connection;
        }
    }
    return NULL;
}

static unsigned connection_state(TlsConnection *connection)
{
    unsigned s;
    pthread_mutex_lock(&connection->lock);
    s = connection->state;
    pthread_mutex_unlock(&connection->lock);
    return s;
}

static int connection_cancelled(TlsConnection *connection)
{
    int c;
    pthread_mutex_lock(&connection->lock);
    c = connection->cancel_requested;
    pthread_mutex_unlock(&connection->lock);
    return c;
}

static long long drain_deadline(TlsConnection *connection)
{
    long long d;
    pthread_mutex_lock(&connection->lock);
    d = connection->drain_deadline;
    pthread_mutex_unlock(&connection->lock);
    return d;
}

static void release_connection(TlsConnection *connection)
{
    int last;
    pthread_mutex_lock(&connection->lock);
    last = !--connection->references;
    pthread_mutex_unlock(&connection->lock);
    if (last) {
        pthread_mutex_destroy(&connection->lock);
        free(connection);
    }
}

static void fail_connection(TlsConnection *connection, unsigned error,
                            const char *message)
{
    pthread_mutex_lock(&connection->lock);
    connection->state = IOS_TLS_FAILED;
    connection->error = error;
    snprintf(connection->message, sizeof(connection->message), "%s", message);
    pthread_mutex_unlock(&connection->lock);
}

static void initialize_context(void)
{
    context = SSL_CTX_new(TLS_client_method());
    if (!context) {
        return;
    }
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, NULL);
    if (!SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) ||
        !SSL_CTX_set_default_verify_paths(context)) {
        SSL_CTX_free(context);
        context = NULL;
    }
}

static void fail_tls_operation(TlsConnection *connection, SSL *ssl, int error)
{
    int saved_errno = errno;
    unsigned long code = ERR_peek_last_error();
    char text[128];
    long verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK) {
        fail_connection(connection, IOS_TLS_ERROR_CERTIFICATE,
                        X509_verify_cert_error_string(verify));
        return;
    }
    if (code) {
        ERR_error_string_n(code, text, sizeof(text));
    } else if (error == SSL_ERROR_SYSCALL && saved_errno) {
        snprintf(text, sizeof(text), "%s", strerror(saved_errno));
    } else {
        snprintf(text, sizeof(text), "TLS I/O failure (%d)", error);
    }
    fail_connection(connection,
                    error == SSL_ERROR_SYSCALL ? IOS_TLS_ERROR_SOCKET
                                               : IOS_TLS_ERROR_PROTOCOL,
                    text);
}

/* Handshake poll also observes cancellation on the application endpoint. */
static int wait_for_handshake(TlsConnection *connection, short events,
                              long long deadline)
{
    struct pollfd f[2] = {{connection->network_fd, events, 0},
                          {connection->worker_fd, POLLIN, 0}};
    int left = (int)(deadline - monotonic_milliseconds()), n;
    if (connection_cancelled(connection)) {
        fail_connection(connection, IOS_TLS_ERROR_CANCELLED, "TLS connection closed");
        return -1;
    }
    if (left <= 0) {
        fail_connection(connection, IOS_TLS_ERROR_TIMEOUT, "TLS handshake timed out");
        return -1;
    }
    n = poll(f, 2, left > 100 ? 100 : left);
    if (n < 0 && errno != EINTR) {
        fail_connection(connection, IOS_TLS_ERROR_SOCKET, strerror(errno));
        return -1;
    }
    return 0;
}

/* Configure identity verification before any handshake bytes are exchanged. */
static int establish_tls(TlsConnection *connection, SSL *ssl, long long deadline)
{
    unsigned char ip[16];
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
    if (!SSL_set_fd(ssl, connection->network_fd)) {
        fail_connection(connection, IOS_TLS_ERROR_CONFIGURATION,
                        "Cannot attach TLS socket");
        goto done;
    }
    if (inet_pton(AF_INET, connection->host, ip) == 1 ||
        inet_pton(AF_INET6, connection->host, ip) == 1) {
        if (!X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), connection->host)) {
            goto config_error;
        }
    } else {
        if (!SSL_set1_host(ssl, connection->host) ||
            !SSL_set_tlsext_host_name(ssl, connection->host)) {
            goto config_error;
        }
    }
    for (;;) {
        int r, e;
        if (connection_cancelled(connection)) {
            fail_connection(connection, IOS_TLS_ERROR_CANCELLED,
                            "TLS connection closed");
            goto done;
        }
        if (monotonic_milliseconds() >= deadline) {
            fail_connection(connection, IOS_TLS_ERROR_TIMEOUT,
                            "TLS handshake timed out");
            goto done;
        }
        ERR_clear_error();
        r = SSL_connect(ssl);
        if (r == 1) {
            break;
        }
        e = SSL_get_error(ssl, r);
        if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
            fail_tls_operation(connection, ssl, e);
            goto done;
        }
        if (wait_for_handshake(connection, e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT,
                               deadline) < 0) {
            goto done;
        }
    }
    pthread_mutex_lock(&connection->lock);
    snprintf(connection->protocol, sizeof(connection->protocol), "%s",
             SSL_get_version(ssl));
    snprintf(connection->cipher, sizeof(connection->cipher), "%s",
             SSL_get_cipher_name(ssl));
    connection->state = IOS_TLS_READY;
    pthread_mutex_unlock(&connection->lock);
    return 0;
config_error:
    fail_connection(connection, IOS_TLS_ERROR_CONFIGURATION,
                    "Cannot configure TLS peer identity");
done:
    return -1;
}

/* SSL_write retries must retain the same buffer until OpenSSL accepts it. */
static void relay_plaintext(TlsConnection *connection, SSL *ssl)
{
    unsigned char outgoing[16384], incoming[16384];
    int out_size = 0, in_size = 0, in_pos = 0;
    int app_eof = 0, tls_eof = 0, notified = 0;
    short write_want = 0, read_want = 0;
    while (!connection_cancelled(connection)) {
        struct pollfd f[2];
        int r, e, progress = 0;
        long long drain = drain_deadline(connection);
        if (drain && monotonic_milliseconds() >= drain) {
            break;
        }
        /* close() relinquishes incoming data, but bytes already accepted
         * for writing still belong to the connection until sent. */
        if (drain) {
            in_size = in_pos = 0;
        }
        if (in_size) {
            r = send(connection->worker_fd, incoming + in_pos, in_size - in_pos,
                     MSG_NOSIGNAL);
            if (r > 0) {
                in_pos += r;
                progress = 1;
                if (in_pos == in_size) {
                    in_pos = in_size = 0;
                }
            } else if (r < 0 && errno != EAGAIN && errno != EINTR) {
                if (drain_deadline(connection)) {
                    in_size = in_pos = 0;
                } else {
                    goto done;
                }
            }
        }
        if (!out_size && !app_eof) {
            r = recv(connection->worker_fd, outgoing, sizeof(outgoing), 0);
            if (r > 0) {
                out_size = r;
                progress = 1;
            } else if (!r) {
                app_eof = 1;
                progress = 1;
            } else if (errno != EAGAIN && errno != EINTR) {
                goto done;
            }
        }
        if (out_size) {
            ERR_clear_error();
            r = SSL_write(ssl, outgoing, out_size);
            if (r > 0) {
                out_size -= r;
                memmove(outgoing, outgoing + r, out_size);
                write_want = 0;
                progress = 1;
            } else {
                e = SSL_get_error(ssl, r);
                if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
                    fail_tls_operation(connection, ssl, e);
                    goto done;
                }
                write_want = e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
            }
        }
        /* Preserve SSL_write's retry buffer and operation until it succeeds. */
        if (!write_want && !in_size && !tls_eof) {
            ERR_clear_error();
            r = SSL_read(ssl, incoming, sizeof(incoming));
            if (r > 0) {
                in_size = r;
                read_want = 0;
                progress = 1;
            } else {
                e = SSL_get_error(ssl, r);
                if (e == SSL_ERROR_ZERO_RETURN) {
                    tls_eof = 1;
                    progress = 1;
                    /* Reject new application writes, but recv() still drains
                     * bytes already accepted by the Linux socketpair. */
                    shutdown(connection->worker_fd, SHUT_RD);
                } else if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                    read_want = e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
                } else {
                    fail_tls_operation(connection, ssl, e);
                    goto done;
                }
            }
        }
        if (app_eof && !out_size && !notified && !write_want) {
            ERR_clear_error();
            r = SSL_shutdown(ssl);
            if (r >= 0 || (SSL_get_shutdown(ssl) & SSL_SENT_SHUTDOWN)) {
                notified = 1;
                progress = 1;
            } else {
                e = SSL_get_error(ssl, r);
                if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
                    fail_tls_operation(connection, ssl, e);
                    goto done;
                }
                read_want = e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
            }
        }
        /* After close_notify, consume the peer's shutdown (discarding
         * application input when closing). Closing TCP with unread TLS
         * records can send RST and destroy the final outgoing records. */
        if (tls_eof && !in_size && app_eof && !out_size && notified) {
            break;
        }
        if (progress) {
            continue;
        }
        f[0].fd = connection->worker_fd;
        f[0].events = (!app_eof && !out_size ? POLLIN : 0) | (in_size ? POLLOUT : 0);
        f[1].fd = connection->network_fd;
        f[1].events =
            write_want ? write_want : (!in_size ? (read_want ? read_want : POLLIN) : 0);
        /* A hung-up fd reports HUP even with events=0. While draining a
         * full plaintext buffer it must not turn the worker into a spin loop. */
        if (!f[0].events) {
            f[0].fd = -1;
        }
        if (!f[1].events) {
            f[1].fd = -1;
        }
        f[0].revents = f[1].revents = 0;
        r = poll(f, 2, 100);
        if (r < 0 && errno != EINTR) {
            fail_connection(connection, IOS_TLS_ERROR_SOCKET, strerror(errno));
            goto done;
        }
    }
done:
    return;
}

static void *tls_worker(void *arg)
{
    TlsConnection *connection = arg;
    SSL *ssl = NULL;
    long long deadline = monotonic_milliseconds() + connection->timeout_ms;
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, NULL);
    pthread_once(&context_once, initialize_context);
    if (!context) {
        fail_connection(connection, IOS_TLS_ERROR_CONFIGURATION,
                        "Cannot initialize TLS CA store");
        goto done;
    }
    ssl = SSL_new(context);
    if (!ssl) {
        fail_connection(connection, IOS_TLS_ERROR_CONFIGURATION,
                        "Cannot allocate TLS session");
        goto done;
    }
    if (establish_tls(connection, ssl, deadline) == 0) {
        relay_plaintext(connection, ssl);
    }
done:
    if (ssl) {
        SSL_free(ssl);
    }
    close(connection->network_fd);
    close(connection->worker_fd);
    pthread_mutex_lock(&connection->lock);
    if (connection->state != IOS_TLS_FAILED) {
        connection->state = IOS_TLS_CLOSED;
    }
    pthread_mutex_unlock(&connection->lock);
    release_connection(connection);
    return NULL;
}

int ios_tls_start(int fd, const void *data, unsigned size)
{
    TlsClientRequest request;
    TlsConnection *connection;
    pthread_t thread;
    int pair[2], type, error, flags;
    struct sockaddr_storage peer;
    socklen_t n = sizeof(type);
    if (decode_client_request(&request, data, size)) {
        errno = EINVAL;
        return -1;
    }
    if (find_connection(fd)) {
        errno = EALREADY;
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &n) < 0) {
        return -1;
    }
    if (type != SOCK_STREAM) {
        errno = EOPNOTSUPP;
        return -1;
    }
    n = sizeof(peer);
    if (getpeername(fd, (struct sockaddr *)&peer, &n) < 0) {
        return -1;
    }
    if (peer.ss_family != AF_INET && peer.ss_family != AF_INET6) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }
    if (!(flags & O_NONBLOCK)) {
        errno = EINVAL;
        return -1;
    }
    connection = calloc(1, sizeof(*connection));
    if (!connection) {
        return -1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair) < 0) {
        free(connection);
        return -1;
    }
    connection->network_fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (connection->network_fd < 0) {
        close(pair[0]);
        close(pair[1]);
        free(connection);
        return -1;
    }
    connection->original_fd = fd;
    connection->application_fd = pair[0];
    connection->worker_fd = pair[1];
    connection->references = 2;
    connection->timeout_ms = request.timeout_ms;
    connection->state = IOS_TLS_NEGOTIATING;
    memcpy(connection->host, request.host, sizeof(connection->host));
    pthread_mutex_init(&connection->lock, NULL);
    /* Workers start with every signal blocked: the server's handlers (SIGURG's
     * oobdata walks the socket list) must only run on the coroutine thread. */
    {
        sigset_t all, old;
        sigfillset(&all);
        pthread_sigmask(SIG_SETMASK, &all, &old);
        error = pthread_create(&thread, NULL, tls_worker, connection);
        pthread_sigmask(SIG_SETMASK, &old, NULL);
    }
    if (error) {
        close(connection->network_fd);
        close(pair[0]);
        close(pair[1]);
        pthread_mutex_destroy(&connection->lock);
        free(connection);
        errno = error;
        return -1;
    }
    pthread_detach(thread);
    connection->next = connections;
    connections = connection;
    return 0;
}

int ios_tls_status(int fd, void *data, unsigned *size)
{
    TlsConnection *connection = find_connection(fd);
    TlsStatus status;
    if (!connection) {
        errno = ENOTCONN;
        return -1;
    }
    if (*size < IOS_TLS_STATUS_SIZE) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&connection->lock);
    status.state = connection->state;
    status.error = connection->error;
    memcpy(status.protocol, connection->protocol, sizeof(status.protocol));
    memcpy(status.cipher, connection->cipher, sizeof(status.cipher));
    memcpy(status.message, connection->message, sizeof(status.message));
    pthread_mutex_unlock(&connection->lock);
    encode_tls_status(data, &status);
    *size = IOS_TLS_STATUS_SIZE;
    return 0;
}

int ios_tls_fd(int fd, int writing)
{
    TlsConnection *connection = find_connection(fd);
    unsigned s;
    if (!connection) {
        return fd;
    }
    s = connection_state(connection);
    if (s == IOS_TLS_NEGOTIATING) {
        errno = EAGAIN;
        return -1;
    }
    if (writing && (s == IOS_TLS_FAILED || s == IOS_TLS_CLOSED)) {
        errno = EPIPE;
        return -1;
    }
    return connection->application_fd;
}

int ios_tls_eof(int fd)
{
    TlsConnection *connection = find_connection(fd);
    if (connection && connection_state(connection) == IOS_TLS_FAILED) {
        errno = ECONNRESET;
        return -1;
    }
    return 0;
}

int ios_tls_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    struct pollfd *copy;
    nfds_t i;
    int n, left = timeout, pending;
    long long deadline = monotonic_milliseconds() + timeout;
    copy = calloc(count ? count : 1, sizeof(*copy));
    if (!copy) {
        return -1;
    }
    do {
        pending = 0;
        for (i = 0; i < count; i++) {
            TlsConnection *connection = find_connection(fds[i].fd);
            copy[i] = fds[i];
            copy[i].revents = 0;
            if (connection) {
                copy[i].fd = connection->application_fd;
                if (connection_state(connection) == IOS_TLS_NEGOTIATING) {
                    copy[i].events &= ~POLLOUT;
                    pending = 1;
                }
            }
        }
        n = poll(copy, count, pending && (left < 0 || left > 20) ? 20 : left);
        if (n || !pending || !timeout) {
            break;
        }
        if (timeout > 0) {
            left = (int)(deadline - monotonic_milliseconds());
        }
    } while (timeout < 0 || left > 0);
    if (n >= 0) {
        for (i = 0; i < count; i++) {
            fds[i].revents = copy[i].revents;
        }
    }
    free(copy);
    return n;
}

int ios_tls_shutdown(int fd, int how)
{
    TlsConnection *connection = find_connection(fd);
    /* TLS has no read-side half close. SHUT_RD on front would make the
     * worker's next send(back) fail with EPIPE and tear the whole
     * connection down, so treat it as a no-op on a TLS stream. */
    if (connection && how == SHUT_RD) {
        return 0;
    }
    return shutdown(connection ? connection->application_fd : fd, how);
}

void ios_tls_forget(int fd)
{
    TlsConnection **slot = &connections, *connection;
    while (*slot && (*slot)->original_fd != fd) {
        slot = &(*slot)->next;
    }
    if (!(connection = *slot)) {
        return;
    }
    *slot = connection->next;
    pthread_mutex_lock(&connection->lock);
    /* Handshake cancellation remains immediate. Established streams drain
     * in their worker, never blocking the io-server coroutine thread. */
    if (connection->state == IOS_TLS_READY) {
        connection->drain_deadline = monotonic_milliseconds() + 5000;
    } else {
        connection->cancel_requested = 1;
    }
    pthread_mutex_unlock(&connection->lock);
    close(connection->application_fd);
    release_connection(connection);
}
#else
int ios_tls_start(int fd, const void *data, unsigned size)
{
    (void)fd;
    (void)data;
    (void)size;
    errno = ENOPROTOOPT;
    return -1;
}

int ios_tls_status(int fd, void *data, unsigned *size)
{
    (void)fd;
    (void)data;
    (void)size;
    errno = ENOPROTOOPT;
    return -1;
}

int ios_tls_fd(int fd, int writing)
{
    (void)writing;
    return fd;
}

int ios_tls_eof(int fd)
{
    (void)fd;
    return 0;
}

int ios_tls_poll(struct pollfd *fds, nfds_t n, int timeout)
{
    return poll(fds, n, timeout);
}

int ios_tls_shutdown(int fd, int how)
{
    return shutdown(fd, how);
}

void ios_tls_forget(int fd)
{
    (void)fd;
}
#endif
