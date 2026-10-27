/* SSH sessions run in Linux workers. Registry and front descriptors belong
 * to the coroutine thread; only a worker may call its session's libssh2 API.
 * Bounded nonblocking socketpairs carry terminal bytes. Control requests
 * copy state under a mutex and never wait for network/authentication I/O. */
#include "net_ssh.h"
#include "net_tls.h"
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <time.h>
#ifdef IOS_SSH
#include <libssh2.h>
#include <pthread.h>
#include <signal.h>

enum StartOffset {
    START_VERSION = 0,
    START_TIMEOUT = 4,
    START_PORT = 8,
    START_COLUMNS = 12,
    START_ROWS = 16,
    START_PTY = 20,
    START_RESERVED_1 = 24,
    START_RESERVED_2 = 28,
    START_HOST = 32,
    START_USER = 288,
    START_TERMINAL = 352,
    START_COMMAND = 416
};

enum AuthOffset {
    AUTH_METHOD = 0,
    AUTH_KEY_SIZE = 4,
    AUTH_RESERVED_1 = 8,
    AUTH_RESERVED_2 = 12,
    AUTH_SECRET = 16,
    AUTH_PRIVATE_KEY = 528
};

enum StatusOffset {
    STATUS_STATE = 4,
    STATUS_ERROR = 8,
    STATUS_EXIT = 12,
    STATUS_EXIT_VALID = 16,
    STATUS_METHODS = 20,
    STATUS_ATTEMPT = 24,
    STATUS_KEY_TYPE = 32,
    STATUS_FINGERPRINT = 96,
    STATUS_MESSAGE = 192
};

enum AuthMethod { AUTH_PASSWORD = 1, AUTH_PUBLIC_KEY = 2 };

typedef struct SshConnection SshConnection;

struct SshConnection {
    SshConnection *next; /* Registry: coroutine thread only. */
    int original_fd;     /* Stable Helios socket identity. */
    int application_fd;  /* Coroutine end of the plaintext socketpair. */
    int worker_fd;       /* Worker end of the plaintext socketpair. */
    int network_fd;      /* Worker-owned duplicate of original_fd. */
    unsigned timeout_ms, remote_port;
    int pty;
    char host[256], user[64], terminal[64], command[768];

    /* The mutex protects state shared by the worker and coroutine thread.
     * Each owns a reference; closing the Helios stream never joins a worker. */
    pthread_mutex_t lock;
    int references, cancel_requested, state, error, trust, auth_ready;
    int exit_status, exit_valid;
    unsigned columns, rows, resize_serial, methods, attempt;
    char key_type[64], fingerprint[96], message[316];
    unsigned char auth[IOS_SSH_AUTH_SIZE];
};

static SshConnection *connections;
typedef struct SshUpload SshUpload;

struct SshUpload {
    SshUpload *next;
    int original_fd;
    unsigned op, size, used;
    unsigned char *data;
};

static SshUpload *uploads;

static void forget_upload(int fd)
{
    SshUpload **slot = &uploads, *upload;
    while (*slot && (*slot)->original_fd != fd) {
        slot = &(*slot)->next;
    }
    if ((upload = *slot) != NULL) {
        *slot = upload->next;
        memset(upload->data, 0, upload->size);
        free(upload->data);
        free(upload);
    }
}

static pthread_once_t library_once = PTHREAD_ONCE_INIT;
static int library_error;

static void initialize_libssh2(void)
{
    library_error = libssh2_init(0);
}

static unsigned read_le32(const unsigned char *bytes)
{
    return (unsigned)bytes[0] | (unsigned)bytes[1] << 8 | (unsigned)bytes[2] << 16 |
           (unsigned)bytes[3] << 24;
}

static void write_le32(unsigned char *bytes, unsigned n)
{
    bytes[0] = n;
    bytes[1] = n >> 8;
    bytes[2] = n >> 16;
    bytes[3] = n >> 24;
}

static long long monotonic_milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static SshConnection *find_connection(int fd)
{
    SshConnection *connection;
    for (connection = connections; connection; connection = connection->next) {
        if (connection->original_fd == fd) {
            return connection;
        }
    }
    return NULL;
}

static int connection_cancelled(SshConnection *connection)
{
    int r;
    pthread_mutex_lock(&connection->lock);
    r = connection->cancel_requested;
    pthread_mutex_unlock(&connection->lock);
    return r;
}

static int connection_state(SshConnection *connection)
{
    int r;
    pthread_mutex_lock(&connection->lock);
    r = connection->state;
    pthread_mutex_unlock(&connection->lock);
    return r;
}

static void release_connection(SshConnection *connection)
{
    int last;
    pthread_mutex_lock(&connection->lock);
    last = !--connection->references;
    pthread_mutex_unlock(&connection->lock);
    if (last) {
        pthread_mutex_destroy(&connection->lock);
        memset(connection, 0, sizeof(*connection));
        free(connection);
    }
}

static void fail_connection(SshConnection *connection, int error, const char *message)
{
    pthread_mutex_lock(&connection->lock);
    connection->state = IOS_SSH_FAILED;
    connection->error = error;
    snprintf(connection->message, sizeof(connection->message), "%s", message);
    pthread_mutex_unlock(&connection->lock);
}

static void fail_ssh_operation(SshConnection *connection, LIBSSH2_SESSION *session)
{
    char *message = NULL;
    libssh2_session_last_error(session, &message, NULL, 0);
    fail_connection(connection, EIO, message ? message : "SSH protocol failure");
}

static int wait_for_network(SshConnection *connection, LIBSSH2_SESSION *session,
                            long long deadline)
{
    struct pollfd f;
    int directions, left, r;
    if (connection_cancelled(connection)) {
        return -1;
    }
    left = (int)(deadline - monotonic_milliseconds());
    if (left <= 0) {
        fail_connection(connection, ETIMEDOUT, "SSH operation timed out");
        return -1;
    }
    directions = libssh2_session_block_directions(session);
    f.fd = connection->network_fd;
    f.events = (directions & LIBSSH2_SESSION_BLOCK_INBOUND ? POLLIN : 0) |
               (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND ? POLLOUT : 0);
    if (!f.events) {
        f.events = POLLIN;
    }
    r = poll(&f, 1, left > 100 ? 100 : left);
    if (r < 0 && errno != EINTR) {
        fail_connection(connection, errno, strerror(errno));
        return -1;
    }
    return 0;
}

static short ssh_poll_events(LIBSSH2_SESSION *session)
{
    int d = libssh2_session_block_directions(session);
    short events = (d & LIBSSH2_SESSION_BLOCK_INBOUND ? POLLIN : 0) |
                   (d & LIBSSH2_SESSION_BLOCK_OUTBOUND ? POLLOUT : 0);
    return events ? events : POLLIN;
}

static int wait_for_control(SshConnection *connection, int auth)
{
    long long deadline = monotonic_milliseconds() + 300000;
    while (!connection_cancelled(connection)) {
        int ready;
        pthread_mutex_lock(&connection->lock);
        ready = auth ? connection->auth_ready : connection->trust;
        pthread_mutex_unlock(&connection->lock);
        if (ready) {
            return ready;
        }
        if (monotonic_milliseconds() >= deadline) {
            fail_connection(connection, ETIMEDOUT, "SSH prompt timed out");
            return 0;
        }
        poll(NULL, 0, 20);
    }
    return 0;
}

static int perform_handshake(SshConnection *connection, LIBSSH2_SESSION *session)
{
    int rc;
    long long deadline;
    deadline = monotonic_milliseconds() + connection->timeout_ms;
    for (;;) {
        if (connection_cancelled(connection)) {
            return -1;
        }
        rc = libssh2_session_handshake(session, connection->network_fd);
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        if (wait_for_network(connection, session, deadline) < 0) {
            return -1;
        }
    }
    if (rc) {
        fail_ssh_operation(connection, session);
        return -1;
    }
    return 0;
}

/* Publish the host key before waiting for the native client's trust decision. */
static int approve_host_key(SshConnection *connection, LIBSSH2_SESSION *session)
{
    const char *hash, *key;
    size_t key_size;
    int type, rc;
    unsigned i;
    hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    key = libssh2_session_hostkey(session, &key_size, &type);
    if (!hash || !key || key_size < 4) {
        fail_connection(connection, EIO, "SSH server supplied no host key");
        return -1;
    }
    pthread_mutex_lock(&connection->lock);
    strcpy(connection->fingerprint, "SHA256-hex:");
    for (i = 0; i < 32; i++) {
        sprintf(connection->fingerprint + 11 + 2 * i, "%02x", (unsigned char)hash[i]);
    }
    i = ((unsigned char)key[0] << 24) | ((unsigned char)key[1] << 16) |
        ((unsigned char)key[2] << 8) | (unsigned char)key[3];
    if (i && i < sizeof(connection->key_type) && i <= key_size - 4) {
        memcpy(connection->key_type, key + 4, i);
    }
    connection->state = IOS_SSH_HOST_KEY;
    pthread_mutex_unlock(&connection->lock);
    rc = wait_for_control(connection, 0);
    if (rc != 1) {
        if (rc < 0) {
            fail_connection(connection, EACCES, "SSH host key declined");
        }
        return -1;
    }
    return 0;
}

/* Credentials are copied once: libssh2 requires identical arguments on retry. */
static int authenticate_session(SshConnection *connection, LIBSSH2_SESSION *session)
{
    unsigned char auth[IOS_SSH_AUTH_SIZE];
    const char *methods;
    int rc, result = -1;
    long long deadline;
    deadline = monotonic_milliseconds() + connection->timeout_ms;
    do {
        methods =
            libssh2_userauth_list(session, connection->user, strlen(connection->user));
        if (methods || libssh2_userauth_authenticated(session)) {
            break;
        }
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            fail_ssh_operation(connection, session);
            goto done;
        }
        if (wait_for_network(connection, session, deadline) < 0) {
            goto done;
        }
    } while (1);
    pthread_mutex_lock(&connection->lock);
    connection->methods = methods
                              ? ((strstr(methods, "password") ? AUTH_PASSWORD : 0) |
                                 (strstr(methods, "publickey") ? AUTH_PUBLIC_KEY : 0))
                              : 0;
    pthread_mutex_unlock(&connection->lock);
    while (!libssh2_userauth_authenticated(session)) {
        if (!connection->methods) {
            fail_connection(connection, EOPNOTSUPP,
                            "Server offers no password or public-key authentication");
            goto done;
        }
        pthread_mutex_lock(&connection->lock);
        connection->state = IOS_SSH_AUTHENTICATING;
        pthread_mutex_unlock(&connection->lock);
        if (!wait_for_control(connection, 1)) {
            goto done;
        }
        pthread_mutex_lock(&connection->lock);
        memcpy(auth, connection->auth, sizeof(auth));
        memset(connection->auth, 0, sizeof(connection->auth));
        connection->auth_ready = 0;
        pthread_mutex_unlock(&connection->lock);
        deadline = monotonic_milliseconds() + connection->timeout_ms;
        for (;;) {
            if (connection_cancelled(connection)) {
                goto done;
            }
            if (read_le32(auth + AUTH_METHOD) == AUTH_PASSWORD) {
                rc = libssh2_userauth_password(session, connection->user,
                                               (char *)auth + AUTH_SECRET);
            } else {
                rc = libssh2_userauth_publickey_frommemory(
                    session, connection->user, strlen(connection->user), NULL, 0,
                    (char *)auth + AUTH_PRIVATE_KEY, read_le32(auth + AUTH_KEY_SIZE),
                    (char *)auth + AUTH_SECRET);
            }
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                break;
            }
            if (wait_for_network(connection, session, deadline) < 0) {
                goto done;
            }
        }
        memset(auth, 0, sizeof(auth));
        if (rc) {
            pthread_mutex_lock(&connection->lock);
            connection->attempt++;
            snprintf(connection->message, sizeof(connection->message),
                     "Authentication failed");
            pthread_mutex_unlock(&connection->lock);
        }
    }
    result = 0;
done:
    memset(auth, 0, sizeof(auth));
    return result;
}

static int open_channel(SshConnection *connection, LIBSSH2_SESSION *session,
                        LIBSSH2_CHANNEL **result)
{
    LIBSSH2_CHANNEL *channel;
    int rc;
    long long deadline;
    pthread_mutex_lock(&connection->lock);
    connection->state = IOS_SSH_OPENING;
    connection->message[0] = 0;
    pthread_mutex_unlock(&connection->lock);
    deadline = monotonic_milliseconds() + connection->timeout_ms;
    while (!(channel = libssh2_channel_open_session(session))) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            fail_ssh_operation(connection, session);
            return -1;
        }
        if (wait_for_network(connection, session, deadline) < 0) {
            return -1;
        }
    }
    *result = channel;
    if (connection->pty) {
        deadline = monotonic_milliseconds() + connection->timeout_ms;
        for (;;) {
            if (connection_cancelled(connection)) {
                return -1;
            }
            rc = libssh2_channel_request_pty_ex(
                channel, connection->terminal, strlen(connection->terminal), NULL, 0,
                connection->columns, connection->rows, 0, 0);
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                break;
            }
            if (wait_for_network(connection, session, deadline) < 0) {
                return -1;
            }
        }
        if (rc) {
            fail_ssh_operation(connection, session);
            return -1;
        }
    }
    if (connection->command[0]) {
        deadline = monotonic_milliseconds() + connection->timeout_ms;
        for (;;) {
            if (connection_cancelled(connection)) {
                return -1;
            }
            rc = libssh2_channel_exec(channel, connection->command);
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                break;
            }
            if (wait_for_network(connection, session, deadline) < 0) {
                return -1;
            }
        }
    } else {
        deadline = monotonic_milliseconds() + connection->timeout_ms;
        for (;;) {
            if (connection_cancelled(connection)) {
                return -1;
            }
            rc = libssh2_channel_shell(channel);
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                break;
            }
            if (wait_for_network(connection, session, deadline) < 0) {
                return -1;
            }
        }
    }
    if (rc) {
        fail_ssh_operation(connection, session);
        return -1;
    }
    return 0;
}

/* Both directions have a bounded buffer. Poll only when neither can advance. */
static int relay_session(SshConnection *connection, LIBSSH2_SESSION *session,
                         LIBSSH2_CHANNEL *channel)
{
    unsigned char outgoing[8192], incoming[8192];
    int rc, out_size = 0, out_pos = 0, in_size = 0, in_pos = 0;
    int app_eof = 0, eof_sent = 0, stream = 0;
    unsigned i, resize_serial = 0;
    short read_want = 0, write_want = 0;
    long long deadline;
    while (!connection_cancelled(connection)) {
        struct pollfd f[2];
        int n, progress = 0;
        unsigned serial, columns, rows;
        pthread_mutex_lock(&connection->lock);
        serial = connection->resize_serial;
        columns = connection->columns;
        rows = connection->rows;
        pthread_mutex_unlock(&connection->lock);
        if (connection->pty && resize_serial != serial) {
            deadline = monotonic_milliseconds() + connection->timeout_ms;
            for (;;) {
                if (connection_cancelled(connection)) {
                    return -1;
                }
                rc = libssh2_channel_request_pty_size(channel, columns, rows);
                if (rc != LIBSSH2_ERROR_EAGAIN) {
                    break;
                }
                if (wait_for_network(connection, session, deadline) < 0) {
                    return -1;
                }
            }
            if (rc) {
                fail_ssh_operation(connection, session);
                return -1;
            }
            resize_serial = serial;
        }
        if (in_size) {
            n = send(connection->worker_fd, incoming + in_pos, in_size - in_pos,
                     MSG_NOSIGNAL);
            if (n > 0) {
                in_pos += n;
                progress = 1;
                if (in_pos == in_size) {
                    in_size = in_pos = 0;
                }
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                return -1;
            }
        }
        if (!out_size && !app_eof) {
            n = recv(connection->worker_fd, outgoing, sizeof(outgoing), 0);
            if (n > 0) {
                out_size = n;
                out_pos = 0;
                progress = 1;
            } else if (n == 0) {
                app_eof = 1;
                progress = 1;
            } else if (errno != EAGAIN && errno != EINTR) {
                return -1;
            }
        }
        if (out_size) {
            n = libssh2_channel_write(channel, (char *)outgoing + out_pos,
                                      out_size - out_pos);
            if (n > 0) {
                out_pos += n;
                progress = 1;
                write_want = 0;
                if (out_pos == out_size) {
                    out_size = out_pos = 0;
                }
            } else if (n == LIBSSH2_ERROR_EAGAIN) {
                write_want = ssh_poll_events(session);
            } else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
                fail_ssh_operation(connection, session);
                return -1;
            }
        }
        if (!in_size) {
            /* Merge stdout/stderr for this terminal-oriented provider. */
            for (i = 0; i < 2; i++) {
                n = libssh2_channel_read_ex(channel, stream, (char *)incoming,
                                            sizeof(incoming));
                stream ^= 1;
                if (n > 0) {
                    in_size = n;
                    read_want = 0;
                    progress = 1;
                    break;
                }
                if (n == LIBSSH2_ERROR_EAGAIN) {
                    read_want = ssh_poll_events(session);
                }
                if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
                    fail_ssh_operation(connection, session);
                    return -1;
                }
            }
        }
        if (app_eof && !out_size && !eof_sent) {
            rc = libssh2_channel_send_eof(channel);
            if (!rc) {
                eof_sent = 1;
                progress = 1;
            } else if (rc == LIBSSH2_ERROR_EAGAIN) {
                write_want = ssh_poll_events(session);
            } else if (rc != LIBSSH2_ERROR_EAGAIN) {
                fail_ssh_operation(connection, session);
                return -1;
            }
        }
        if (libssh2_channel_eof(channel) && !in_size) {
            break;
        }
        if (progress) {
            continue;
        }
        f[0].fd = connection->worker_fd;
        f[0].events = (!app_eof && !out_size ? POLLIN : 0) | (in_size ? POLLOUT : 0);
        f[1].fd = connection->network_fd;
        f[1].events = ((out_size || (app_eof && !eof_sent)) ? write_want : 0) |
                      (!in_size ? (read_want ? read_want : POLLIN) : 0);
        if (!f[0].events) {
            f[0].fd = -1;
        }
        if (!f[1].events) {
            f[1].fd = -1;
        }
        n = poll(f, 2, 50);
        if (n < 0 && errno != EINTR) {
            fail_connection(connection, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int collect_exit_status(SshConnection *connection, LIBSSH2_SESSION *session,
                               LIBSSH2_CHANNEL *channel)
{
    int rc;
    long long deadline;
    char *exit_signal = NULL;
    size_t exit_signal_size = 0;
    deadline = monotonic_milliseconds() + connection->timeout_ms;
    for (;;) {
        if (connection_cancelled(connection)) {
            return -1;
        }
        rc = libssh2_channel_close(channel);
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        if (wait_for_network(connection, session, deadline) < 0) {
            return -1;
        }
    }
    if (rc) {
        fail_ssh_operation(connection, session);
        return -1;
    }
    libssh2_channel_get_exit_signal(channel, &exit_signal, &exit_signal_size, NULL,
                                    NULL, NULL, NULL);
    pthread_mutex_lock(&connection->lock);
    connection->exit_status =
        exit_signal_size ? 255 : libssh2_channel_get_exit_status(channel);
    connection->exit_valid = 1;
    if (exit_signal_size) {
        snprintf(connection->message, sizeof(connection->message),
                 "Remote process terminated by %s", exit_signal);
    }
    pthread_mutex_unlock(&connection->lock);
    if (exit_signal) {
        libssh2_free(session, exit_signal);
    }
    return 0;
}

static void *ssh_worker(void *arg)
{
    SshConnection *connection = arg;
    LIBSSH2_SESSION *session = NULL;
    LIBSSH2_CHANNEL *channel = NULL;

    pthread_once(&library_once, initialize_libssh2);
    if (library_error) {
        fail_connection(connection, EIO, "Cannot initialize libssh2");
        goto done;
    }
    session = libssh2_session_init();
    if (!session) {
        fail_connection(connection, ENOMEM, "Cannot allocate SSH session");
        goto done;
    }
    libssh2_session_set_blocking(session, 0);

    if (perform_handshake(connection, session) < 0 ||
        approve_host_key(connection, session) < 0 ||
        authenticate_session(connection, session) < 0 ||
        open_channel(connection, session, &channel) < 0) {
        goto done;
    }
    pthread_mutex_lock(&connection->lock);
    connection->state = IOS_SSH_READY;
    pthread_mutex_unlock(&connection->lock);
    if (relay_session(connection, session, channel) == 0 &&
        !connection_cancelled(connection)) {
        collect_exit_status(connection, session, channel);
    }
done:
    /* Tear down TCP before freeing a nonblocking session: channel_free can
     * otherwise return EAGAIN and leave its channel/session allocated. */
    shutdown(connection->network_fd, SHUT_RDWR);
    if (session) {
        libssh2_session_set_blocking(session, 1);
        libssh2_session_set_timeout(session, 100);
    }
    if (channel) {
        libssh2_channel_free(channel);
    }
    if (session) {
        libssh2_session_free(session);
    }
    close(connection->network_fd);
    close(connection->worker_fd);
    pthread_mutex_lock(&connection->lock);
    if (connection->state != IOS_SSH_FAILED) {
        connection->state = IOS_SSH_CLOSED;
    }
    pthread_mutex_unlock(&connection->lock);
    release_connection(connection);
    return NULL;
}

static int start_connection(int fd, const unsigned char *wire, unsigned size)
{
    SshConnection *connection;
    pthread_t thread;
    int pair[2], type, flags, error;
    struct sockaddr_storage peer;
    socklen_t n = sizeof(type);
    if (size != IOS_SSH_START_SIZE || read_le32(wire) != 1 ||
        read_le32(wire + START_TIMEOUT) < 100 ||
        read_le32(wire + START_TIMEOUT) > 120000 || !read_le32(wire + START_PORT) ||
        read_le32(wire + START_PORT) > 65535 ||
        read_le32(wire + START_COLUMNS) > 65535 ||
        read_le32(wire + START_ROWS) > 65535 || read_le32(wire + START_PTY) > 1 ||
        read_le32(wire + START_RESERVED_1) || read_le32(wire + START_RESERVED_2) ||
        !wire[32] || !memchr(wire + START_HOST, 0, 256) || !wire[288] ||
        !memchr(wire + START_USER, 0, 64) || !memchr(wire + START_TERMINAL, 0, 64) ||
        !memchr(wire + START_COMMAND, 0, 768)) {
        errno = EINVAL;
        return -1;
    }
    if (find_connection(fd) || ios_tls_fd(fd, 0) != fd) {
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
    connection->state = IOS_SSH_CONNECTING;
    connection->timeout_ms = read_le32(wire + START_TIMEOUT);
    connection->remote_port = read_le32(wire + START_PORT);
    connection->columns = read_le32(wire + START_COLUMNS);
    connection->rows = read_le32(wire + START_ROWS);
    connection->pty = read_le32(wire + START_PTY);
    memcpy(connection->host, wire + START_HOST, 256);
    memcpy(connection->user, wire + START_USER, 64);
    memcpy(connection->terminal, wire + START_TERMINAL, 64);
    memcpy(connection->command, wire + START_COMMAND, 768);
    pthread_mutex_init(&connection->lock, NULL);
    {
        sigset_t all, old;
        sigfillset(&all);
        pthread_sigmask(SIG_SETMASK, &all, &old);
        error = pthread_create(&thread, NULL, ssh_worker, connection);
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

/* IOC control payloads are small. Assemble one ordered START/AUTH record,
 * apply it only when complete, then erase the temporary credentials. */
static int receive_control_fragment(int fd, const unsigned char *wire, unsigned size)
{
    int rc;
    SshUpload *upload;
    unsigned operation, total, pos, count;
    if (size < 13 || size > 492) {
        errno = EINVAL;
        return -1;
    }
    operation = read_le32(wire);
    total = read_le32(wire + 4);
    pos = read_le32(wire + 8);
    count = size - 12;
    if ((operation != IOS_SSH_START && operation != IOS_SSH_AUTHENTICATE) ||
        total !=
            (operation == IOS_SSH_START ? IOS_SSH_START_SIZE : IOS_SSH_AUTH_SIZE) ||
        pos > total || count > total - pos) {
        forget_upload(fd);
        errno = EINVAL;
        return -1;
    }
    if (!pos) {
        forget_upload(fd);
        upload = calloc(1, sizeof(*upload));
        if (!upload) {
            return -1;
        }
        upload->data = calloc(1, total);
        if (!upload->data) {
            free(upload);
            return -1;
        }
        upload->original_fd = fd;
        upload->op = operation;
        upload->size = total;
        upload->next = uploads;
        uploads = upload;
    }
    for (upload = uploads; upload && upload->original_fd != fd; upload = upload->next) {
    }
    if (!upload || upload->used != pos || upload->size != total ||
        upload->op != operation) {
        forget_upload(fd);
        errno = EINVAL;
        return -1;
    }
    memcpy(upload->data + pos, wire + 12, count);
    upload->used += count;
    if (upload->used < total) {
        return 0;
    }
    rc = ios_ssh_set(fd, operation, upload->data, total);
    forget_upload(fd);
    return rc;
}

int ios_ssh_set(int fd, unsigned op, const void *data, unsigned size)
{
    SshConnection *connection;
    const unsigned char *wire = data;
    int error = 0;
    if (op == IOS_SSH_UPLOAD) {
        return receive_control_fragment(fd, wire, size);
    }
    if (op == IOS_SSH_START) {
        return start_connection(fd, wire, size);
    }
    connection = find_connection(fd);
    if (!connection) {
        errno = ENOTCONN;
        return -1;
    }
    pthread_mutex_lock(&connection->lock);
    switch (op) {
    case IOS_SSH_TRUST:
        if (size != 4 || read_le32(wire) > 1 || connection->state != IOS_SSH_HOST_KEY ||
            connection->trust) {
            error = EINVAL;
            break;
        }
        connection->trust = read_le32(wire) ? 1 : -1;
        break;
    case IOS_SSH_AUTHENTICATE:
        if (size != IOS_SSH_AUTH_SIZE ||
            (read_le32(wire + AUTH_METHOD) != AUTH_PASSWORD &&
             read_le32(wire + AUTH_METHOD) != AUTH_PUBLIC_KEY) ||
            read_le32(wire + AUTH_KEY_SIZE) > 8192 ||
            (read_le32(wire + AUTH_METHOD) == AUTH_PUBLIC_KEY &&
             !read_le32(wire + AUTH_KEY_SIZE)) ||
            read_le32(wire + AUTH_RESERVED_1) || read_le32(wire + AUTH_RESERVED_2) ||
            !memchr(wire + AUTH_SECRET, 0, 512) ||
            connection->state != IOS_SSH_AUTHENTICATING || connection->auth_ready) {
            error = EINVAL;
            break;
        }
        memcpy(connection->auth, wire, size);
        connection->auth_ready = 1;
        connection->state = IOS_SSH_OPENING;
        break;
    case IOS_SSH_RESIZE:
        if (size != 8 || !read_le32(wire) || !read_le32(wire + 4) ||
            read_le32(wire) > 65535 || read_le32(wire + 4) > 65535 ||
            connection->state != IOS_SSH_READY || !connection->pty) {
            error = EINVAL;
            break;
        }
        connection->columns = read_le32(wire);
        connection->rows = read_le32(wire + 4);
        connection->resize_serial++;
        break;
    case IOS_SSH_SEND_EOF:
        if (size != 4 || read_le32(wire) || connection->state != IOS_SSH_READY) {
            error = EINVAL;
            break;
        }
        if (shutdown(connection->application_fd, SHUT_WR) < 0) {
            error = errno;
        }
        break;
    default:
        error = ENOPROTOOPT;
    }
    pthread_mutex_unlock(&connection->lock);
    if (error) {
        errno = error;
        return -1;
    }
    return 0;
}

int ios_ssh_get(int fd, unsigned op, void *data, unsigned *size)
{
    SshConnection *connection;
    unsigned char *wire = data;
    if (op == IOS_SSH_CAPABILITIES && *size >= 4) {
        memcpy(wire, "HSH1", 4);
        *size = 4;
        return 0;
    }
    if (op != IOS_SSH_STATUS) {
        errno = ENOPROTOOPT;
        return -1;
    }
    connection = find_connection(fd);
    if (!connection) {
        errno = ENOTCONN;
        return -1;
    }
    if (*size < IOS_SSH_STATUS_SIZE) {
        errno = EINVAL;
        return -1;
    }
    memset(wire, 0, IOS_SSH_STATUS_SIZE);
    memcpy(wire, "HSH1", 4);
    pthread_mutex_lock(&connection->lock);
    write_le32(wire + STATUS_STATE, connection->state);
    write_le32(wire + STATUS_ERROR, connection->error);
    write_le32(wire + STATUS_EXIT, connection->exit_status);
    write_le32(wire + STATUS_EXIT_VALID, connection->exit_valid);
    write_le32(wire + STATUS_METHODS, connection->methods);
    write_le32(wire + STATUS_ATTEMPT, connection->attempt);
    memcpy(wire + STATUS_KEY_TYPE, connection->key_type, 64);
    memcpy(wire + STATUS_FINGERPRINT, connection->fingerprint, 96);
    memcpy(wire + STATUS_MESSAGE, connection->message, 316);
    pthread_mutex_unlock(&connection->lock);
    *size = IOS_SSH_STATUS_SIZE;
    return 0;
}

int ios_ssh_active(int fd)
{
    return find_connection(fd) != NULL;
}

int ios_ssh_fd(int fd, int writing)
{
    SshConnection *connection = find_connection(fd);
    int status;
    if (!connection) {
        return fd;
    }
    status = connection_state(connection);
    if (status < IOS_SSH_READY) {
        errno = EAGAIN;
        return -1;
    }
    if (writing && status != IOS_SSH_READY) {
        errno = EPIPE;
        return -1;
    }
    return connection->application_fd;
}

int ios_ssh_eof(int fd)
{
    SshConnection *connection = find_connection(fd);
    if (connection && connection_state(connection) == IOS_SSH_FAILED) {
        errno = ECONNRESET;
        return -1;
    }
    return 0;
}

int ios_ssh_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    struct pollfd *copy;
    nfds_t i;
    int n;
    if (!connections) {
        return ios_tls_poll(fds, count, timeout);
    }
    copy = calloc(count ? count : 1, sizeof(*copy));
    if (!copy) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        SshConnection *connection = find_connection(fds[i].fd);
        copy[i] = fds[i];
        if (connection) {
            copy[i].fd = connection->application_fd;
            if (connection_state(connection) < IOS_SSH_READY) {
                copy[i].events &= ~POLLOUT;
            }
        }
    }
    n = ios_tls_poll(copy, count, timeout);
    if (n >= 0) {
        for (i = 0; i < count; i++) {
            fds[i].revents = copy[i].revents;
        }
    }
    free(copy);
    return n;
}

int ios_ssh_shutdown(int fd, int how)
{
    SshConnection *connection = find_connection(fd);
    return connection ? shutdown(connection->application_fd, how)
                      : ios_tls_shutdown(fd, how);
}

void ios_ssh_forget(int fd)
{
    SshConnection **slot = &connections, *connection;
    forget_upload(fd);
    while (*slot && (*slot)->original_fd != fd) {
        slot = &(*slot)->next;
    }
    if (!(connection = *slot)) {
        return;
    }
    *slot = connection->next;
    pthread_mutex_lock(&connection->lock);
    connection->cancel_requested = 1;
    pthread_mutex_unlock(&connection->lock);
    close(connection->application_fd);
    release_connection(connection);
}
#else
int ios_ssh_set(int fd, unsigned op, const void *data, unsigned size)
{
    (void)fd;
    (void)op;
    (void)data;
    (void)size;
    errno = ENOPROTOOPT;
    return -1;
}

int ios_ssh_get(int fd, unsigned op, void *data, unsigned *size)
{
    (void)fd;
    (void)op;
    (void)data;
    (void)size;
    errno = ENOPROTOOPT;
    return -1;
}

int ios_ssh_active(int fd)
{
    (void)fd;
    return 0;
}

int ios_ssh_fd(int fd, int writing)
{
    (void)writing;
    return fd;
}

int ios_ssh_eof(int fd)
{
    (void)fd;
    return 0;
}

int ios_ssh_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    return ios_tls_poll(fds, count, timeout);
}

int ios_ssh_shutdown(int fd, int how)
{
    return ios_tls_shutdown(fd, how);
}

void ios_ssh_forget(int fd)
{
    (void)fd;
}
#endif
