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

The built-in t4 LinkIO backend connects to the emulator. IOS_LINK_SOURCES
accepts a space-separated list of C sources for an additional link driver.
The driver implements ios_linux_extra_link(name), initializes its link
state, calls ios_linux_register_link with the callbacks defined in
linkdrv.h, and returns 1 when it recognizes the configured box name.
It returns 0 for an unrecognized name.

The Helios wire protocol uses 32-bit words on both 32-bit and 64-bit hosts.
