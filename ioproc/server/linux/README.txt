Linux host port of the Helios I/O server
======================================

Building
--------

From this directory, using GNU make and the Linux development headers:

    make

The executable is build/ioserver; objects and header dependencies are under
build/obj. Sources are compiled directly from this tree. Incremental builds
track header changes and changes to the compiler, flags or enabled providers.
Libraries named directly as .a or .so files are also tracked. Use LIB_DEPS to
list additional library files selected through -l or other linker options.

The compiler selects the target ABI and supplies the normal Linux startup
and linker support. Standard CC, CPPFLAGS, CFLAGS, LDFLAGS and LDLIBS variables
are supported. BUILD_DIR and OUT select the build directory and executable.
For example, a cross-build without optional network libraries is:

    make CC=aarch64-linux-gnu-gcc BUILD_DIR=build-arm64

Optional network providers are enabled independently:

    make TLS=1 SSH=1 SSHD=1

    Option       Library       Header/link overrides
    TLS          OpenSSL       TLS_CFLAGS, TLS_LIBS
    SSH          libssh2       SSH_CFLAGS, SSH_LIBS
    SSHD         libssh        SSHD_CFLAGS, SSHD_LIBS

The default linker options are -lssl -lcrypto, -lssh2 and -lssh respectively.
An omitted provider's private controls return an unsupported-option error.
An incoming SSH listener is also disabled unless ssh_port is configured.

LINK_SOURCES selects external link-driver sources instead of linkext.c.
Each source must have a distinct basename. Remove the current build's
objects and executable with make clean.

build.sh forwards existing IOS_BUILD_DIR, IOS_OUT, IOS_TLS, IOS_SSH, IOS_SSHD,
IOS_LINK_SOURCES, IOS_OPT and CFLAGS_EXTRA settings to make.

Source layout
-------------

The parent directory contains the shared I/O server and its Helios protocol
handlers. Linux-specific files supply host facilities:

    ../tcp.c         Helios /internet directory, streams and request handlers
    net_compat.c     Translate Helios socket options; route data and readiness
    net_wire.h       Byte-order helpers for SSH, TLS and socket control codecs
    net_tls.c        TLS client negotiation and plaintext relay, using OpenSSL
    net_ssh.c        Outgoing SSH negotiation and terminal relay, using libssh2
    ssh_server.c     Incoming SSH listener and native-login relay, using libssh
    multiwait.c      Coroutine readiness and socket selection, using poll
    hostio.c         Host descriptor limits, terminal waits and complete writes
    colib_linux.c    Coroutine contexts and stacks, using glibc ucontext
    t4link.c         LinkIO transport to the t4 Transputer emulator
    termcap_lite.c   ANSI terminal capabilities for the host console
    compat.c         Small libc compatibility functions
    helios.h         Host includes and compile-time Helios wire-layout checks
    linkdrv.h        Interface for additional Linux link drivers

The network headers document their control records and ownership rules.
The .clang-format file defines the style for Linux C sources and headers.

Execution and descriptor ownership
----------------------------------

The main thread runs the legacy coroutine scheduler. It alone handles Helios
requests and owns the socket registry. Host sockets remain nonblocking even
when a Helios client asks for blocking I/O: a suspended coroutine must not
stop the other clients.

TLS and SSH protocol operations run in workers. A worker owns its library
session and a duplicate of the connected network descriptor. A nonblocking
Unix socketpair carries plaintext between it and the coroutine thread:

    Helios request -> coroutine -> socketpair -> worker -> remote connection

The original socket descriptor remains the Helios socket's identity. Address
and option queries use that descriptor; reads, writes and readiness use the
provider's plaintext endpoint. The numeric Helios socket constants and record
layouts are translated explicitly rather than treated as Linux constants.

For outgoing TLS/SSH, the registry and worker each hold one reference to the
connection. Closing a Helios stream removes the registry entry and closes
its socketpair endpoint. The worker releases its own descriptors and final
reference when it finishes. A mutex protects status and control fields;
OpenSSL/libssh2 calls never run under that mutex or on the coroutine thread.
Established TLS streams may drain accepted outgoing bytes before shutdown.
Buffers are bounded so backpressure reaches the Helios writer.

Incoming SSH uses a listener thread and a bounded set of workers. The registry
mutex protects pending login tokens and descriptor handoff. The native Helios
login service owns account authentication, session creation, terminal handling
and cleanup on disconnect. The host does not log into a Linux account.

Incoming SSH configuration and protocol
--------------------------------------

The host configuration accepts:

    ssh_port       TCP port; absent or zero disables the listener
    ssh_bind       Numeric IPv4 address; default 0.0.0.0
    ssh_host_key   Private host-key path

The key directory must exist and be writable to create the first key.
Existing keys are preserved. The service supports shell and command sessions;
it does not provide SFTP or port forwarding. Account/password policy belongs
to the native Helios login service.

The native service reads four-byte little-endian tokens from /internet/sshd.
Opening /internet/ssh.XXXXXXXX claims that token's socketpair endpoint once.
Each frame has a one-byte kind, a two-byte big-endian payload length and up
to 4096 payload bytes. Protocol records are:

    A   Authenticate: username-NUL/password-NUL; empty A acknowledges success
    F   Authentication failure: reason 1 allows retry, reason 2 closes backend
    P   Terminal name-NUL, followed by big-endian 16-bit columns and rows
    S   Start shell; an empty S reply acknowledges shell or command startup
    C   Start command: payload is the command text
    D   Terminal data, in either direction; native stdout/stderr are merged
    W   Resize: big-endian 16-bit columns and rows
    E   Terminal input EOF
    Q   Close the native session
    X   Native exit status: one big-endian 32-bit word, after output

An empty P terminal name means no SSH PTY was requested. One native backend
is reused for up to three credential attempts. Failed attempts are delayed;
backend creation is paced across connections. These private streams assume
a trusted native login service.

Link drivers and wire compatibility
-----------------------------------

The built-in t4 backend connects to an emulator over a Unix or TCP socket.
Its configuration keys and reset semantics are documented in t4link.c.
IOS_LINK_SOURCES accepts a space-separated list of C files for another driver.
The driver implements ios_linux_extra_link(name), initializes its state and
calls ios_linux_register_link() with the callbacks declared in linkdrv.h.
It returns 1 for a recognized box name, or 0 otherwise.

The Helios protocol uses 32-bit words and fixed record layouts on both 32-bit
and 64-bit Linux hosts. Host pointers and library structures are kept outside
those records. This Linux configuration requires a little-endian host.
