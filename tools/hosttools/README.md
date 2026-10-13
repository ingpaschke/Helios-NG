# tools/hosttools: the build-host tools

The programs that run on the build machine during a transputer build of
this tree, built from their sources in the tree for current 32- and
64-bit little-endian Unix hosts (x86-64 and AArch64 Linux tested). They
produce the same bytes as the i386 builds of the original sources. With
these and the llvm-t800 toolchain (clang through `hcc`), a build of the
whole tree needs no 32-bit host program.

tools/build-t800/build.sh builds these tools first, into `OUT/host`,
and uses them for the tree build (see tools/build-t800/README.md).

## Tools

| tool | what it does | sources in this tree | built for TRAN too |
|---|---|---|---|
| asm, objdump | assembler and linker; dumps `.p` and `.o` files | cmds/asm/*.c, asm.h vm.h ttypes.h module.h getargs.[ch] | asm |
| ampp | macro preprocessor: every `.a` to `.s` | cmds/ampp/*, cmds/support/queue.c | yes |
| sysbuild | concatenates the nucleus (nucleus/TRAN) | cmds/com/sysbuild.c, cmds/support/getargs.c | yes |
| objed | prints and edits stack, heap and name of an image | cmds/com/objed.c (cmds/hostutil/objed.c is a link) | yes |
| c | the Unix-hosted `cc`-style driver (runs the compiler, asm, ampp) | cmds/com/c.c (cmds/hostutil/c.c is a link), cmds/support/queue.c | yes |
| gencodes | fault/master.fdb to include/codes.h, errno.h, signal.h | cmds/hostutil/gencodes.c | no |
| squash | fault/master.fdb to etc/faults | cmds/hostutil/squash.c | no |
| newmotd | etc/motd (top-level makefile) | cmds/hostutil/newmotd.c | no |
| keyform | generator: keytab.c, the keyword table of asm | cmds/asm/keyform.c, hash.c | (host program of cmds/asm) |
| gensquirt | generator: squirtdata.c of the B422 driver | filesys/devs/b422/gensquirt.c | no |

A transputer build runs asm (also through asm-rt for links), ampp, c,
sysbuild, squash and gencodes; the top-level makefile adds newmotd.
objed is installed but not run. cmds/asm builds and runs keyform;
gensquirt runs only if b422dvr.b2u looks newer than the checked-in
squirtdata.c.

Not here: `nc` (Helios C; clang replaces it, and its Codemist sources
keep pointers in ints throughout), buildtime, buildkeymap and testenc
(ARM only or unused), mkrom (rom.i is disabled), the help database
tools, rmgen, esysbuild, the SunOS I/O server.

## Building

    make                          # all tools into ./build
    make check                    # tests/asm, tests/edge
    make install PREFIX=/opt/x    # objdump as $(OBJDUMP_NAME)
    make BUILDDIR=/tmp/asan CFLAGS='-O1 -g -fsanitize=address,undefined'

The Makefile always adds `-std=gnu99 -fsigned-char -fwrapv
-fno-strict-aliasing`, after `CFLAGS`, so that a `-std=` or
`-funsigned-char` there cannot override them: the code is K&R C,
expects a signed `char` (AArch64's is unsigned), relies on 32-bit
wrap-around arithmetic and reads words through cast pointers.
`CPPFLAGS`, `LDFLAGS` and `LDLIBS` are honoured. The host defines are
those of the original SUN4 host build (`-DSUN4`, `-D__TRAN
-D__HELIOSTRAN` where the SUN4 makefiles had them, `-DUNIX` for c,
`-DVM` for asm), without the big-endian `-DHOSTISBIGENDIAN` and
`-DBYTE_SEX_ODD`. A big-endian host is refused.

The tools are built with `CC`. The generators keyform and gensquirt
are built with `BUILD_CC` (default `cc`, the compiler of the machine
that runs make, whatever `CC` is): they only write C sources
(keytab.c, squirtdata.c), so they belong to the machine that generates
those sources, not to the one the tools are built for. keyform writes
the keyword table as 32-bit words in the byte order of the machine it
runs on, so build and target machine must have the same byte order
(both little-endian for x86-64 and AArch64).

Cross build, e.g. static AArch64 tools on an x86-64 machine:

    make CC=aarch64-linux-gnu-gcc LDFLAGS=-static BUILDDIR=build-a64
    make CC=aarch64-linux-gnu-gcc LDFLAGS=-static BUILDDIR=build-a64 \
         install DESTDIR=/tmp/stage PREFIX=/usr/local

This builds the tools for AArch64 and keyform and gensquirt for the
x86-64 build machine. `make install` installs the generators only when
`$(CC) -dumpmachine` and `$(BUILD_CC) -dumpmachine` agree, so a cross
install never mixes architectures (it says which programs it left
out). For AArch64 generators too, set `BUILD_CC=aarch64-linux-gnu-gcc
BUILD_LDFLAGS=-static`; then everything is AArch64 and all programs
are installed.

## Shared support files

- cmds/support/ttypes.h: `WORD`/`UWORD` are `int32_t`/`uint32_t` on the
  host (`long` in the original sources); refuses a big-endian host. On
  Helios it includes `<helios.h>` instead.
- cmds/support/queue.h, queue.c: prototypes, `void` PreInsert and
  PostInsert.
- cmds/asm/ttypes.h, module.h, getargs.[ch]: the assembler's own copies
  (as in the 1.3 sources; getargs from cmds/support with asm fixes 8
  and 20 of PORT.md).
- tools/hosttools/include/module.h: Helios's module and image headers
  with 32-bit fields for objed and sysbuild on the host (the tree's
  include/module.h has a `long` word, 64 bits on LP64). The TRAN builds
  use include/module.h.

The sources also compile for the transputer, with clang (hcc) and
Helios C: the host-only parts are under `#ifndef __HELIOS` (asm's
temporary output file and signal handling: on Helios the target is
written directly and removed after an error; the big-endian check; c's
vsnprintf formatting), `<helios.h>` supplies the types there, and
`__attribute__`/`inline` are defined away for compilers other than gcc
and clang.

PORT.md describes the changes of the port and the behaviour fixes, and
what the tests check.

## Licence

GNU General Public License version 3, as Helios-NG (LICENSE at the top
of the tree). The original Perihelion copyright notices are kept in the
files.
