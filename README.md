# Helios-NG 1.31 SP2

Helios is a distributed operating system for networks of INMOS Transputers.
It provides message-based services, processor and task management, a Unix-style
shell and utilities, C and POSIX libraries, networking and an X Window System
server. A host I/O server connects Helios to the host's files, terminal and
network services.

This branch provides the Helios sources, tools for building them on Linux,
and a Linux host I/O server. The Transputer build uses the llvm-t800 toolchain
and includes the X11R4 server and ATW800/2 device drivers.

## What's new compared with master

Compared with upstream `master` at `1fe8f65e`, SP2 includes:

- **Kernel and messaging fixes:** correct handling of port-table exhaustion,
  task teardown, semaphore grants and message waits; coordinated flow control
  and XON retries when memory becomes available.
- **Library and service fixes:** corrected allocation and error paths, pipe
  and FIFO buffering, socket-select cancellation and timeouts, descriptor
  handling, and numerous C library, utility and server defects.
- **Unix compatibility improvements:** native program launching with input/output
  redirection and processor selection, IPv4 DNS resolution, socket shutdown,
  shared descriptor status and terminal-resize signal delivery. The resolver configuration is
  `/helios/etc/resolv.cnf`, using an Atari-compatible filename.
- **Linux I/O server:** native x86-64 and AArch64 builds, an i386 build,
  `/internet` access through Linux sockets, optional OpenSSL TLS support and
  a link-driver registration interface.
- **Linux build tools:** host assembler, linker and utilities for current
  32-bit and 64-bit hosts, plus a build script for the Transputer system.
- **X11 support:** X11R4 server sources, the Helios device layer and ATW800/2
  display, keyboard and mouse drivers.
- **Terminal defaults:** login preserves `TERM` and includes local and X11
  commands in `PATH`; `map` clears VT-compatible terminals correctly.

## Build Helios

Requirements:

- Linux on x86-64 or AArch64.
- llvm-t800 0.4.0 or later, including its Helios add-on, installed in
  `/opt/llvm-t800` or selected with `T800`.
- GCC, GNU make, Python 3 and `cpp`.

From the repository root:

```sh
tools/build-t800/build.sh
```

The resulting Helios tree is under `build-t800/prod/TRAN`, with component
logs under `build-t800/logs`. Keep `JOBS=1` because some component makefiles
have incomplete dependencies for parallel builds.

See [the Transputer build instructions](tools/build-t800/README.md) for
options and [the host-tool instructions](tools/hosttools/README.md) for
building the assembler and utilities separately.

## Build the Linux I/O server

```sh
cd ioproc/server/linux
IOS_TARGET=native ./build.sh
```

The executable is `build/ioserver`. To enable TLS, install OpenSSL development
headers and libraries and build with:

```sh
IOS_TARGET=native IOS_TLS=1 ./build.sh
```

See [the Linux I/O server instructions](ioproc/server/linux/README.txt) for
cross-compilation, output paths and link-driver configuration.

## X Window System

The system build installs `bin/xhelios`, its device drivers and the ATW800/2
configuration. See [the X11 overview](X11/README.md) and
[the server instructions](X11/xhelios/README.md) for configuration and usage.

## Licence and attribution

Helios was developed by Perihelion Distributed Software. Helios-NG makes its
sources available under the [GNU GPL version 3](LICENSE), with permission
from Tim King and Nick Garnett. Original copyright notices remain in the
sources. The included X11R4 code carries its own
[copyright notices and licence terms](X11/X11R4/mit/COPYRIGHTS).
