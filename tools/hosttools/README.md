# Build-host tools

These programs build Helios on current 32-bit and 64-bit little-endian Unix
hosts. They are compiled from the sources in this repository. Transputer builds
require the llvm-t800 toolchain.

From tools/hosttools, run:

    make
    make install PREFIX=/opt/helios-hosttools

The default output directory is build. Set BUILDDIR to choose another directory,
CC to select the host compiler, or CFLAGS and LDFLAGS to change compiler and
linker options. For example:

    make BUILDDIR=/tmp/helios-hosttools CC=aarch64-linux-gnu-gcc LDFLAGS=-static

BUILD_CC builds the generators that execute on the build machine. A cross
build installs the target tools but does not install these build-machine
programs. DESTDIR supports staged installation; OBJDUMP_NAME can rename objdump
when installing it alongside another toolchain.

The tools are asm, objdump, ampp, sysbuild, objed, c, gencodes, squash and
newmotd. The generators are keyform and gensquirt. Their sources are under
cmds/asm, cmds/ampp, cmds/com, cmds/support, cmds/hostutil and filesys/devs/b422.
The private include/module.h supplies fixed-width image headers for host
builds; native Helios builds continue using the repository's include/module.h.

The Makefile preserves the original signed-char and 32-bit arithmetic
assumptions and selects GNU C99 for the K&R definitions. CPPFLAGS,
LDLIBS and build-machine compiler/linker options are supported as well.

For a complete Transputer build, use tools/build-t800/build.sh. It builds
these host tools first; see tools/build-t800/README.md for prerequisites and
output layout.

The tools are distributed under the repository's GNU GPL version 3 licence.
The original Perihelion copyright notices are retained in the sources.
