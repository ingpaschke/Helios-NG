Linux host port of the Helios I/O server

Build from this directory:

    IOS_TARGET=native ./build.sh

The default output is build/ioserver; objects and staged sources are under
build/. The build requires a C compiler and Linux development headers.

IOS_TARGET=native selects the host ABI (x86-64 or AArch64). IOS_TARGET=i386
selects the 32-bit x86 build, which requires the 32-bit Linux libraries.
If IOS_TARGET is omitted, the build selects native on AArch64 and i386
on other hosts. CC selects the compiler, including a cross compiler.
IOS_BUILD_DIR and IOS_OUT choose the build directory and executable path.

IOS_TLS=1 enables OpenSSL for native builds. TLS_CFLAGS and TLS_LIBS select
its headers and libraries; the defaults link against libssl and libcrypto.
IOS_SSH=1 enables the optional SSH session provider using libssh2. SSH_CFLAGS
and SSH_LIBS select that library (default -lssh2). It requires a native build.
SSH network operations run in workers; terminal data, authentication and
window-size controls use the Helios socket service without blocking coroutines.

IOS_SSHD=1 enables incoming SSH using libssh; SSHD_CFLAGS and SSHD_LIBS
select that library (default -lssh). Configure ssh_port, ssh_bind (numeric
IPv4 address, default 0.0.0.0), and ssh_host_key in the host configuration.
A zero or absent ssh_port disables the listener. The host-key directory
must exist and be writable to create a key on first use; existing keys are
preserved. SSH protocol operations run in bounded worker connections.
The native login service reads four-byte little-endian connection tokens
from /internet/sshd and opens /internet/ssh.XXXXXXXX for each token. These
are selectable Helios streams backed by local socketpairs, without TCP.
Frames contain a one-byte kind, a two-byte big-endian length (max 4096),
and payload: A sends username-NUL/password-NUL and receives empty A or F;
P sets terminal-NUL/16-bit columns/rows; S starts a shell, C a command;
empty S acknowledges startup. D carries terminal data, W sets dimensions,
E requests terminal EOF, Q closes, and X carries a 32-bit exit status.
Helios authenticates users, creates their sessions and terminates them on
disconnect. The host provides no Linux login, SFTP or port forwarding.
One native backend is reused for up to three credential attempts. Failures
are delayed; native backend creation is paced across connections. F with
reason 1 permits another A frame, while reason 2 closes the backend.
These private streams assume a trusted native login service.
An empty terminal name in P denotes a session without an SSH PTY request.

The built-in t4 LinkIO backend connects to the emulator. IOS_LINK_SOURCES
accepts a space-separated list of C sources for an additional link driver.
The driver implements ios_linux_extra_link(name), initializes its link
state, calls ios_linux_register_link with the callbacks defined in
linkdrv.h, and returns 1 when it recognizes the configured box name.
It returns 0 for an unrecognized name.

The Helios wire protocol uses 32-bit words on both 32-bit and 64-bit hosts.
