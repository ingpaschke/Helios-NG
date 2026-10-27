/* Incoming SSH runs in host workers, never in the coroutine scheduler.
 * socketpairs connect workers to ordinary Helios /internet streams: no
 * loopback TCP, host user accounts or separate daemon are involved. */
#include "ssh_server.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#ifdef IOS_SSHD
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

#define FRAME_MAX 4096
#define QUEUE_MAX 65536
#define CONNECTION_MAX 16
#define AUTH_MAX 3

/* Private native-login protocol. The header is kind + big-endian length.
 * Authentication/startup are request/reply exchanges; data is bidirectional.
 * Exit status follows all output. The backend owns native session cleanup. */
enum LoginFrame {
    LOGIN_AUTHENTICATE = 'A',
    LOGIN_FAILURE = 'F',
    LOGIN_TERMINAL = 'P',
    LOGIN_SHELL = 'S',
    LOGIN_COMMAND = 'C',
    LOGIN_DATA = 'D',
    LOGIN_RESIZE = 'W',
    LOGIN_EOF = 'E',
    LOGIN_CLOSE = 'Q',
    LOGIN_EXIT = 'X'
};
typedef struct LoginConnection LoginConnection;

struct LoginConnection {
    LoginConnection *next; /* Protected by registry_lock. */
    int network_fd;        /* Owned by libssh after accept_fd succeeds. */
    int backend_fd;        /* Worker end of the native-login socketpair. */
    int unclaimed_fd;      /* Transferred once by ios_sshd_open(). */
    uint32_t token;        /* Identifies the pending native endpoint. */
};

static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workers_finished = PTHREAD_COND_INITIALIZER;
static pthread_t listener_thread;
static LoginConnection *connections;
static int server_running, listen_fd = -1, notice_fds[2] = {-1, -1}, worker_count;
static uint32_t next_connection_id;
static char *host_key_path;
static pthread_once_t library_once = PTHREAD_ONCE_INIT;
static int library_error;
static long long next_backend_ms;

static void initialize_libssh(void)
{
    library_error = ssh_init();
}

static long long monotonic_milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Pace native task creation across connections, and failed credentials on
 * each connection, without holding the registry lock or coroutine thread. */
static int wait_until(LoginConnection *connection, long long end)
{
    for (;;) {
        struct pollfd f;
        int running, left;
        pthread_mutex_lock(&registry_lock);
        running = server_running;
        pthread_mutex_unlock(&registry_lock);
        if (!running) {
            return -1;
        }
        left = (int)(end - monotonic_milliseconds());
        if (left <= 0) {
            return 0;
        }
        if (left > 100) {
            left = 100;
        }
        f.fd = connection->network_fd;
        f.events = 0;
        f.revents = 0;
        if (poll(&f, 1, left) < 0 && errno != EINTR) {
            return -1;
        }
        if (f.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            return -1;
        }
    }
}

static void make_nonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static int transfer_bytes(int fd, void *data, int size, int writing)
{
    char *connection = data;
    int n;
    long long end = monotonic_milliseconds() + 30000;
    while (size) {
        struct pollfd f;
        int left = (int)(end - monotonic_milliseconds());
        if (left <= 0) {
            return -1;
        }
        n = writing ? send(fd, connection, size, MSG_NOSIGNAL | MSG_DONTWAIT)
                    : recv(fd, connection, size, MSG_DONTWAIT);
        if (n > 0) {
            connection += n;
            size -= n;
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            return -1;
        }
        f.fd = fd;
        f.events = writing ? POLLOUT : POLLIN;
        f.revents = 0;
        if (poll(&f, 1, left) < 0 && errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

static int send_native_frame(int fd, int kind, const void *data, int size)
{
    unsigned char buf[FRAME_MAX + 3];
    if (size < 0 || size > FRAME_MAX) {
        return -1;
    }
    buf[0] = kind;
    buf[1] = size >> 8;
    buf[2] = size;
    if (size) {
        memcpy(buf + 3, data, size);
    }
    return transfer_bytes(fd, buf, size + 3, 1);
}

static int expect_native_reply(int fd, int kind)
{
    unsigned char buf[3];
    return transfer_bytes(fd, buf, 3, 0) == 0 && buf[0] == kind && !buf[1] && !buf[2]
               ? 0
               : -1;
}

static void close_native_backend(LoginConnection *connection)
{
    pthread_mutex_lock(&registry_lock);
    if (connection->unclaimed_fd >= 0) {
        close(connection->unclaimed_fd);
        connection->unclaimed_fd = -1;
    }
    if (connection->backend_fd >= 0) {
        shutdown(connection->backend_fd, SHUT_RDWR);
        close(connection->backend_fd);
        connection->backend_fd = -1;
    }
    connection->token = 0;
    pthread_mutex_unlock(&registry_lock);
}

static int open_native_backend(LoginConnection *connection)
{
    int pair[2], rc;
    unsigned char token[4];
    long long when;
    if (connection->backend_fd >= 0) {
        return 0;
    }
    pthread_mutex_lock(&registry_lock);
    when = monotonic_milliseconds();
    if (next_backend_ms > when) {
        when = next_backend_ms;
    }
    next_backend_ms = when + 250;
    pthread_mutex_unlock(&registry_lock);
    if (wait_until(connection, when) < 0) {
        return -1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) {
        return -1;
    }
    make_nonblocking(pair[0]);
    make_nonblocking(pair[1]);
    pthread_mutex_lock(&registry_lock);
    if (!server_running) {
        pthread_mutex_unlock(&registry_lock);
        close(pair[0]);
        close(pair[1]);
        return -1;
    }
    connection->unclaimed_fd = pair[0];
    connection->backend_fd = pair[1];
    if (!++next_connection_id) {
        ++next_connection_id;
    }
    connection->token = next_connection_id;
    token[0] = next_connection_id;
    token[1] = next_connection_id >> 8;
    token[2] = next_connection_id >> 16;
    token[3] = next_connection_id >> 24;
    rc = send(notice_fds[1], token, 4, MSG_NOSIGNAL | MSG_DONTWAIT);
    pthread_mutex_unlock(&registry_lock);
    if (rc != 4) {
        close_native_backend(connection);
        return -1;
    }
    return 0;
}

/* SSH account strings are UTF-8; native Helios accounts use Latin-1. */
static int account_to_latin1(char *out, const char *in)
{
    unsigned n = 0, c;
    if (!in) {
        return -1;
    }
    while ((c = (unsigned char)*in++) != 0) {
        if (c >= 128) {
            unsigned d = (unsigned char)*in++;
            if ((c != 0xc2 && c != 0xc3) || d < 0x80 || d > 0xbf) {
                return -1;
            }
            c = ((c & 3) << 6) | (d & 63);
        }
        if (n == 31) {
            return -1;
        }
        out[n++] = c;
    }
    out[n] = 0;
    return n;
}

static int authenticate_native_user(LoginConnection *connection, const char *user,
                                    const char *password)
{
    char data[64];
    unsigned char head[3], reason;
    int n = account_to_latin1(data, user), m, rc = 1;
    if (n <= 0) {
        goto done;
    }
    m = account_to_latin1(data + n + 1, password);
    if (m < 0) {
        goto done;
    }
    rc = -1;
    if (open_native_backend(connection) < 0) {
        goto done;
    }
    if (send_native_frame(connection->backend_fd, LOGIN_AUTHENTICATE, data, n + m + 2) <
            0 ||
        transfer_bytes(connection->backend_fd, head, 3, 0) < 0) {
        goto done;
    }
    if (head[0] == LOGIN_AUTHENTICATE && !head[1] && !head[2]) {
        rc = 0;
    } else if (head[0] == LOGIN_FAILURE && !head[1] && head[2] == 1 &&
               transfer_bytes(connection->backend_fd, &reason, 1, 0) == 0 &&
               reason == 1) {
        rc = 1;
    }
done:
    memset(data, 0, sizeof(data));
    return rc;
}

static int clamp_dimension(int n, int fallback, int limit)
{
    return n <= 0 ? fallback : (n > limit ? limit : n);
}

static int start_native_session(LoginConnection *connection, const char *term,
                                int columns, int rows, const char *command)
{
    unsigned char data[69];
    int n = strlen(term) + 1;
    memcpy(data, term, n);
    data[n] = columns >> 8;
    data[n + 1] = columns;
    data[n + 2] = rows >> 8;
    data[n + 3] = rows;
    return send_native_frame(connection->backend_fd, LOGIN_TERMINAL, data, n + 4) < 0 ||
                   send_native_frame(connection->backend_fd,
                                     command ? LOGIN_COMMAND : LOGIN_SHELL, command,
                                     command ? strlen(command) : 0) < 0 ||
                   expect_native_reply(connection->backend_fd, LOGIN_SHELL) < 0
               ? -1
               : 0;
}

static void consume_bytes(unsigned char *buf, int *used, int n)
{
    *used -= n;
    memmove(buf, buf + n, *used);
}

static int queue_native_frame(unsigned char *buf, int *used, int kind, const void *data,
                              int n)
{
    if (n > FRAME_MAX || *used + n + 3 > QUEUE_MAX) {
        return -1;
    }
    buf[*used] = kind;
    buf[*used + 1] = n >> 8;
    buf[*used + 2] = n;
    if (n) {
        memcpy(buf + *used + 3, data, n);
    }
    *used += n + 3;
    return 0;
}

static void finish_ssh_disconnect(LoginConnection *connection, ssh_session session)
{
    long long end = monotonic_milliseconds() + 5000;
    /* Sending CHANNEL_CLOSE only closes our half. Keep processing the
     * peer's window adjustments, close acknowledgement and disconnect.
     * Closing TCP with those packets unread can reset the connection while
     * the client is still consuming output or receiving the exit status. */
    while (ssh_is_connected(session) && monotonic_milliseconds() < end) {
        struct pollfd f;
        ssh_message msg = ssh_message_get(session);
        if (msg) {
            ssh_message_reply_default(msg);
            ssh_message_free(msg);
            continue;
        }
        if (!ssh_is_connected(session)) {
            break;
        }
        f.fd = connection->network_fd;
        f.events = POLLIN;
        f.revents = 0;
        if (ssh_get_poll_flags(session) & SSH_WRITE_PENDING) {
            f.events |= POLLOUT;
        }
        if (poll(&f, 1, 50) < 0 && errno != EINTR) {
            break;
        }
    }
}

/* Consume complete native D/X frames. A partial frame or a full SSH output
 * queue remains buffered for the next iteration. Return -1 on protocol error. */
static int decode_native_output(unsigned char *from_helios, int *pending_size,
                                unsigned char *to_client, int *output_size, int *status)
{
    int from_helios_size = *pending_size;
    int to_client_size = *output_size;
    int exit_status = *status;
    int payload_size, progress = 0;
    while (from_helios_size >= 3) {
        payload_size = (from_helios[1] << 8) | from_helios[2];
        if (payload_size > FRAME_MAX) {
            return -1;
        }
        if (from_helios_size < payload_size + 3) {
            break;
        }
        if (from_helios[0] == LOGIN_DATA) {
            if (to_client_size + payload_size > QUEUE_MAX) {
                break;
            }
            memcpy(to_client + to_client_size, from_helios + 3, payload_size);
            to_client_size += payload_size;
        } else if (from_helios[0] == LOGIN_EXIT && payload_size == 4) {
            exit_status = ((unsigned)from_helios[3] << 24) |
                          ((unsigned)from_helios[4] << 16) | (from_helios[5] << 8) |
                          from_helios[6];
            exit_status &= 255;
        } else {
            return -1;
        }
        consume_bytes(from_helios, &from_helios_size, payload_size + 3);
        progress = 1;
    }
    *pending_size = from_helios_size;
    *output_size = to_client_size;
    *status = exit_status;
    return progress;
}

static void relay_terminal(LoginConnection *connection, ssh_session session,
                           ssh_channel channel)
{
    unsigned char *to_helios = malloc(QUEUE_MAX), *to_client = malloc(QUEUE_MAX),
                  *from_helios = malloc(QUEUE_MAX);
    int to_helios_size = 0, to_client_size = 0, from_helios_size = 0, client_eof = 0,
        exit_status = -1, exit_sent = 0, eof_sent = 0;
    int resized = 0, columns = 80, rows = 24, n, rc, progress;
    ssh_message message;
    if (!to_helios || !to_client || !from_helios) {
        goto done;
    }
    ssh_set_blocking(session, 0);
    while (ssh_is_connected(session) && !ssh_channel_is_closed(channel)) {
        struct pollfd descriptors[2];
        progress = 0;
        while ((message = ssh_message_get(session)) != NULL) {
            if (ssh_message_type(message) == SSH_REQUEST_CHANNEL &&
                ssh_message_channel_request_channel(message) == channel &&
                ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_WINDOW_CHANGE) {
                columns = clamp_dimension(
                    ssh_message_channel_request_pty_width(message), 80, 512);
                rows = clamp_dimension(ssh_message_channel_request_pty_height(message),
                                       24, 256);
                resized = 1;
                ssh_message_channel_request_reply_success(message);
            } else {
                ssh_message_reply_default(message);
            }
            ssh_message_free(message);
        }
        if (resized && to_helios_size + 7 <= QUEUE_MAX) {
            unsigned char size[4];
            size[0] = columns >> 8;
            size[1] = columns;
            size[2] = rows >> 8;
            size[3] = rows;
            queue_native_frame(to_helios, &to_helios_size, LOGIN_RESIZE, size, 4);
            resized = 0;
            progress = 1;
        }
        /* Deliver native output before announcing its exit status. */
        if (to_client_size) {
            n = ssh_channel_write(channel, to_client, to_client_size);
            if (n > 0) {
                consume_bytes(to_client, &to_client_size, n);
                progress = 1;
            } else if (n < 0 && n != SSH_AGAIN) {
                break;
            }
        }
        if (exit_status >= 0 && !to_client_size) {
            if (!exit_sent) {
                rc = ssh_channel_request_send_exit_status(channel, exit_status);
                if (rc == SSH_OK) {
                    exit_sent = 1;
                } else if (rc != SSH_AGAIN) {
                    break;
                }
            }
            if (exit_sent && !eof_sent) {
                rc = ssh_channel_send_eof(channel);
                if (rc == SSH_OK) {
                    eof_sent = 1;
                } else if (rc != SSH_AGAIN) {
                    break;
                }
            }
            if (eof_sent) {
                rc = ssh_channel_close(channel);
                if (rc == SSH_OK) {
                    finish_ssh_disconnect(connection, session);
                }
                if (rc != SSH_AGAIN) {
                    break;
                }
            }
        }
        /* Frame client stdin and EOF for the native terminal service. */
        if (exit_status < 0 && !client_eof &&
            to_helios_size + FRAME_MAX + 3 <= QUEUE_MAX) {
            unsigned char data[FRAME_MAX];
            /* On the SSH server this is client stdin. Both native stdout
             * and stderr arrive below as backend D frames. */
            n = ssh_channel_read_nonblocking(channel, data, sizeof(data), 0);
            if (n > 0) {
                queue_native_frame(to_helios, &to_helios_size, LOGIN_DATA, data, n);
                progress = 1;
            } else if (n < 0 && n != SSH_AGAIN && n != SSH_EOF) {
                break;
            }
            if (ssh_channel_is_eof(channel) && n <= 0) {
                queue_native_frame(to_helios, &to_helios_size, LOGIN_EOF, NULL, 0);
                client_eof = 1;
            }
        }
        if (to_helios_size && exit_status < 0) {
            n = send(connection->backend_fd, to_helios, to_helios_size,
                     MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n > 0) {
                consume_bytes(to_helios, &to_helios_size, n);
                progress = 1;
            } else if (n == 0 ||
                       (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                break;
            }
        }
        /* Keep partial native frames until their payload is complete. */
        if (exit_status < 0 && from_helios_size + FRAME_MAX <= QUEUE_MAX &&
            to_client_size + FRAME_MAX <= QUEUE_MAX) {
            n = recv(connection->backend_fd, from_helios + from_helios_size, FRAME_MAX,
                     MSG_DONTWAIT);
            if (n > 0) {
                from_helios_size += n;
                progress = 1;
            } else if (n == 0 ||
                       (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                break;
            }
        }
        rc = decode_native_output(from_helios, &from_helios_size, to_client,
                                  &to_client_size, &exit_status);
        if (rc < 0) {
            goto done;
        }
        if (rc > 0) {
            progress = 1;
        }
        /* Drain work already buffered in libssh or our queues before
         * waiting for another descriptor event. In particular, a newly
         * received terminal echo must not sit behind a polling timeout. */
        if (progress) {
            continue;
        }
        descriptors[0].fd = connection->network_fd;
        descriptors[0].events = POLLIN;
        if (ssh_get_poll_flags(session) & SSH_WRITE_PENDING) {
            descriptors[0].events |= POLLOUT;
        }
        descriptors[1].fd = connection->backend_fd;
        descriptors[1].events = 0;
        if (exit_status < 0 && from_helios_size + FRAME_MAX <= QUEUE_MAX &&
            to_client_size + FRAME_MAX <= QUEUE_MAX) {
            descriptors[1].events |= POLLIN;
        }
        if (to_helios_size && exit_status < 0) {
            descriptors[1].events |= POLLOUT;
        }
        if (!descriptors[1].events) {
            descriptors[1].fd = -1;
        }
        if (poll(descriptors, 2,
                 (to_client_size || from_helios_size || exit_status >= 0) ? 20 : 100) <
                0 &&
            errno != EINTR) {
            break;
        }
    }
    if (exit_status >= 0) {
        ssh_blocking_flush(session, 2000);
    }
done:
    free(to_helios);
    free(to_client);
    free(from_helios);
}

/* SSH terminal names are bounded printable ASCII strings. */
static int read_terminal_request(ssh_message message, char *terminal, int *columns,
                                 int *rows)
{
    const char *value = ssh_message_channel_request_pty_term(message);
    size_t i, size = value ? strlen(value) : 0;

    if (!size || size > 64) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        if ((unsigned char)value[i] < 33 || (unsigned char)value[i] > 126) {
            return 0;
        }
    }
    strcpy(terminal, value);
    *columns = clamp_dimension(ssh_message_channel_request_pty_width(message), 80, 512);
    *rows = clamp_dimension(ssh_message_channel_request_pty_height(message), 24, 256);
    return 1;
}

/* Authentication and startup are synchronous only within this worker.
 * The native Session Manager remains the authority for account decisions. */
static int negotiate_session(LoginConnection *connection, ssh_session session,
                             ssh_channel *result)
{
    ssh_channel channel = NULL;
    ssh_message msg;
    char term[65] = "xterm", interactive_user[64] = "";
    int authenticated = 0, columns = 80, rows = 24;
    int started = 0, attempts = 0, rc, pty = 0;
    while (!started && (msg = ssh_message_get(session)) != NULL) {
        rc = -1;
        if (ssh_message_type(msg) == SSH_REQUEST_AUTH && !authenticated &&
            attempts < AUTH_MAX) {
            int method = ssh_message_subtype(msg);
            const char *password = NULL, *user = ssh_message_auth_user(msg);
            if (method == SSH_AUTH_METHOD_NONE) {
                password = "";
            } else if (method == SSH_AUTH_METHOD_PASSWORD) {
                password = ssh_message_auth_password(msg);
            } else if (method == SSH_AUTH_METHOD_INTERACTIVE) {
                if (ssh_message_auth_kbdint_is_response(msg)) {
                    /* INFO_RESPONSE carries answers, not a username. */
                    user = interactive_user;
                    password = ssh_userauth_kbdint_getanswer(session, 0);
                } else if (user && strlen(user) < sizeof(interactive_user)) {
                    const char *prompt = "Password: ";
                    char echo = 0;
                    strcpy(interactive_user, user);
                    ssh_message_auth_interactive_request(msg, "Helios login", "", 1,
                                                         &prompt, &echo);
                    ssh_message_free(msg);
                    continue;
                }
            }
            if (password) {
                int result;
                attempts++;
                result = authenticate_native_user(connection, user, password);
                if (result == 0) {
                    authenticated = 1;
                    rc = ssh_message_auth_reply_success(msg, 0);
                } else if (result < 0) {
                    ssh_message_free(msg);
                    goto done;
                } else if (wait_until(connection, monotonic_milliseconds() + 500) < 0) {
                    ssh_message_free(msg);
                    goto done;
                }
            }
            if (!authenticated) {
                ssh_message_auth_set_methods(msg, SSH_AUTH_METHOD_PASSWORD |
                                                      SSH_AUTH_METHOD_INTERACTIVE);
            }
        } else if (ssh_message_type(msg) == SSH_REQUEST_SERVICE) {
            rc = ssh_message_service_reply_success(msg);
        } else if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL_OPEN && authenticated &&
                   !channel && ssh_message_subtype(msg) == SSH_CHANNEL_SESSION) {
            channel = ssh_message_channel_request_open_reply_accept(msg);
            if (channel) {
                rc = 0;
            }
        } else if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL && channel &&
                   ssh_message_channel_request_channel(msg) == channel) {
            int request = ssh_message_subtype(msg);
            if (request == SSH_CHANNEL_REQUEST_PTY) {
                if (read_terminal_request(msg, term, &columns, &rows)) {
                    pty = 1;
                    rc = ssh_message_channel_request_reply_success(msg);
                }
            } else if (request == SSH_CHANNEL_REQUEST_SHELL ||
                       request == SSH_CHANNEL_REQUEST_EXEC) {
                const char *command = request == SSH_CHANNEL_REQUEST_EXEC
                                          ? ssh_message_channel_request_command(msg)
                                          : NULL;
                if ((request == SSH_CHANNEL_REQUEST_SHELL ||
                     (command && *command && strlen(command) <= FRAME_MAX)) &&
                    start_native_session(connection, pty ? term : "", columns, rows,
                                         command) == 0) {
                    rc = ssh_message_channel_request_reply_success(msg);
                    started = 1;
                }
            }
        }
        if (rc < 0) {
            ssh_message_reply_default(msg);
        }
        ssh_message_free(msg);
        if (!authenticated && attempts >= AUTH_MAX) {
            break;
        }
    }
done:
    *result = channel;
    return started;
}

static void *login_worker(void *arg)
{
    LoginConnection *connection = arg, **slot;
    ssh_bind settings = NULL;
    ssh_session session = NULL;
    ssh_channel channel = NULL;
    ssh_key key = NULL;
    int rc, process_config = 0, owned = 0;
    long timeout = 30;

    settings = ssh_bind_new();
    session = ssh_new();
    if (!settings || !session) {
        goto done;
    }
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
    ssh_bind_options_set(settings, SSH_BIND_OPTIONS_PROCESS_CONFIG, &process_config);
    if (ssh_pki_import_privkey_file(host_key_path, NULL, NULL, NULL, &key) != SSH_OK) {
        goto done;
    }
    if (ssh_bind_options_set(settings, SSH_BIND_OPTIONS_IMPORT_KEY, key) != SSH_OK) {
        ssh_key_free(key);
        goto done;
    }
    if (ssh_bind_accept_fd(settings, session, connection->network_fd) != SSH_OK) {
        /* accept_fd can take ownership before a later allocation fails. */
        owned = ssh_get_fd(session) == connection->network_fd;
        goto done;
    }
    owned = 1;
    if (ssh_handle_key_exchange(session) != SSH_OK) {
        goto done;
    }
    if (negotiate_session(connection, session, &channel)) {
        relay_terminal(connection, session, channel);
    }
done:
    close_native_backend(connection);
    pthread_mutex_lock(&registry_lock);
    rc = connection->network_fd;
    connection->network_fd = -1;
    pthread_mutex_unlock(&registry_lock);
    if (channel) {
        ssh_channel_free(channel);
    }
    if (session) {
        ssh_set_blocking(session, 0);
        ssh_disconnect(session);
        ssh_free(session);
    }
    if (!owned) {
        close(rc);
    }
    if (settings) {
        ssh_bind_free(settings);
    }
    pthread_mutex_lock(&registry_lock);
    for (slot = &connections; *slot && *slot != connection; slot = &(*slot)->next)
        ;
    if (*slot) {
        *slot = connection->next;
    }
    free(connection);
    --worker_count;
    pthread_cond_broadcast(&workers_finished);
    pthread_mutex_unlock(&registry_lock);
    return NULL;
}

static void *accept_connections(void *unused)
{
    (void)unused;
    for (;;) {
        struct pollfd f;
        int fd, rc, on = 1;
        LoginConnection *connection;
        pthread_t thread;
        f.fd = listen_fd;
        f.events = POLLIN;
        f.revents = 0;
        pthread_mutex_lock(&registry_lock);
        rc = server_running;
        pthread_mutex_unlock(&registry_lock);
        if (!rc) {
            break;
        }
        if (poll(&f, 1, 100) <= 0) {
            continue;
        }
        fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            continue;
        }
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        connection = calloc(1, sizeof(*connection));
        if (!connection) {
            close(fd);
            continue;
        }
        connection->network_fd = fd;
        connection->unclaimed_fd = connection->backend_fd = -1;
        pthread_mutex_lock(&registry_lock);
        if (!server_running || worker_count == CONNECTION_MAX) {
            pthread_mutex_unlock(&registry_lock);
            close(fd);
            free(connection);
            continue;
        }
        connection->next = connections;
        connections = connection;
        ++worker_count;
        rc = pthread_create(&thread, NULL, login_worker, connection);
        if (rc) {
            connections = connection->next;
            --worker_count;
            close(fd);
            free(connection);
        } else {
            pthread_detach(thread);
        }
        pthread_mutex_unlock(&registry_lock);
    }
    return NULL;
}

int ios_sshd_open(const char *name)
{
    LoginConnection *connection;
    unsigned id;
    int fd = -1;
    char tail;
    pthread_mutex_lock(&registry_lock);
    if (server_running && !strcmp(name, "sshd")) {
        fd = dup(notice_fds[0]);
    } else if (server_running && strlen(name) == 12 &&
               sscanf(name, "ssh.%8x%c", &id, &tail) == 1) {
        for (connection = connections; connection; connection = connection->next) {
            if (connection->token == id && connection->unclaimed_fd >= 0) {
                fd = connection->unclaimed_fd;
                connection->unclaimed_fd = -1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&registry_lock);
    if (fd >= 0) {
        make_nonblocking(fd);
    } else {
        errno = ENOENT;
    }
    return fd;
}

void ios_sshd_stop(void)
{
    LoginConnection *connection;
    pthread_mutex_lock(&registry_lock);
    if (!server_running) {
        pthread_mutex_unlock(&registry_lock);
        return;
    }
    server_running = 0;
    for (connection = connections; connection; connection = connection->next) {
        if (connection->network_fd >= 0) {
            shutdown(connection->network_fd, SHUT_RDWR);
        }
        if (connection->backend_fd >= 0) {
            shutdown(connection->backend_fd, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&registry_lock);
    pthread_join(listener_thread, NULL);
    pthread_mutex_lock(&registry_lock);
    while (worker_count) {
        pthread_cond_wait(&workers_finished, &registry_lock);
    }
    close(listen_fd);
    close(notice_fds[0]);
    close(notice_fds[1]);
    listen_fd = notice_fds[0] = notice_fds[1] = -1;
    free(host_key_path);
    host_key_path = NULL;
    pthread_mutex_unlock(&registry_lock);
}

/* Publish a complete key atomically. A concurrent creator may win link(). */
static int prepare_host_key(const char *path)
{
    ssh_key key = NULL;
    int rc;
    if (access(path, F_OK) < 0) {
        char *directory = malloc(strlen(path) + 16), *temp = malloc(strlen(path) + 32);
        if (!directory || !temp) {
            free(directory);
            free(temp);
            return -1;
        }
        sprintf(directory, "%s.XXXXXX", path);
        if (!mkdtemp(directory)) {
            free(directory);
            free(temp);
            return -1;
        }
        sprintf(temp, "%s/key", directory);
        rc = ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key);
        if (rc == SSH_OK) {
            rc = ssh_pki_export_privkey_file(key, NULL, NULL, NULL, temp);
        }
        if (rc == SSH_OK && chmod(temp, 0640) < 0) {
            rc = SSH_ERROR;
        }
        if (rc == SSH_OK) {
            rc = link(temp, path);
            if (rc < 0 && errno == EEXIST) {
                rc = 0;
            }
        }
        unlink(temp);
        rmdir(directory);
        free(directory);
        free(temp);
        ssh_key_free(key);
        key = NULL;
        if (rc != SSH_OK) {
            return -1;
        }
    }
    if (ssh_pki_import_privkey_file(path, NULL, NULL, NULL, &key) != SSH_OK) {
        errno = EIO;
        return -1;
    }
    ssh_key_free(key);
    return 0;
}

int ios_sshd_start(const char *address, int port, const char *path)
{
    struct sockaddr_in a;
    int on = 1, rc;
    if (port <= 0 || port > 65535 || !path || !*path) {
        errno = EINVAL;
        return -1;
    }
    pthread_once(&library_once, initialize_libssh);
    if (library_error) {
        errno = EIO;
        return -1;
    }
    if (server_running) {
        errno = EALREADY;
        return -1;
    }
    if (prepare_host_key(path) < 0) {
        return -1;
    }
    host_key_path = strdup(path);
    if (!host_key_path) {
        return -1;
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, address ? address : "0.0.0.0", &a.sin_addr) != 1) {
        errno = EINVAL;
        goto fail;
    }
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        goto fail;
    }
    make_nonblocking(listen_fd);
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind(listen_fd, (struct sockaddr *)&a, sizeof(a)) < 0 ||
        listen(listen_fd, 16) < 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, notice_fds) < 0) {
        goto fail;
    }
    make_nonblocking(notice_fds[0]);
    make_nonblocking(notice_fds[1]);
    next_backend_ms = 0;
    server_running = 1;
    rc = pthread_create(&listener_thread, NULL, accept_connections, NULL);
    if (rc) {
        server_running = 0;
        errno = rc;
        goto fail;
    }
    return 0;
fail:
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    if (notice_fds[0] >= 0) {
        close(notice_fds[0]);
    }
    if (notice_fds[1] >= 0) {
        close(notice_fds[1]);
    }
    listen_fd = notice_fds[0] = notice_fds[1] = -1;
    free(host_key_path);
    host_key_path = NULL;
    return -1;
}
#else
int ios_sshd_start(const char *address, int port, const char *path)
{
    (void)address;
    (void)port;
    (void)path;
    errno = EOPNOTSUPP;
    return -1;
}

void ios_sshd_stop(void)
{
}

int ios_sshd_open(const char *name)
{
    (void)name;
    errno = ENOENT;
    return -1;
}
#endif
