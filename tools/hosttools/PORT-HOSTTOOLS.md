<!-- Copied from the hosttools64 repository, commit 8f5e539 (README.md).
     Paths in this file refer to that repository: see README.md here
     for where each source is in this tree. verify/edge and verify/c-stub
     are tools/hosttools/tests/edge and tests/c-stub; the replay scripts
     (verify/run.sh, mkref.sh, cmp.py, treecmp.py, *-cmds.txt) compare
     with the i386 builds of the research trees and stay in hosttools64. -->

# hosttools64: the Helios-NG build-host tools for 64-bit hosts

The programs that run on the build machine during a Helios-NG (HEAD)
transputer build, ported so that they build and run natively on current
64-bit little-endian Unix hosts (x86-64 and AArch64 Linux, tested) and
produce the same bytes as the i386 builds of the original sources. With
these and the 64-bit port of the assembler (`helios-asm`, kept
separately), a clang build of the whole HEAD tree needs no 32-bit host
program.

| tool | what it does in the build | origin in Helios-NG |
|---|---|---|
| ampp | macro preprocessor: every `.a` to `.s` | cmds/ampp |
| sysbuild | concatenates the nucleus (nucleus/TRAN) | cmds/com |
| objed | prints/edits stack, heap, name of an image (host tool, not run by the TRAN build) | cmds/com (== cmds/hostutil) |
| gencodes | fault/master.fdb to include/codes.h, errno.h, signal.h | cmds/hostutil |
| squash | fault/master.fdb to etc/faults | cmds/hostutil |
| newmotd | etc/motd (top-level makefile) | cmds/hostutil |
| c | the Unix-hosted `cc`-style driver (runs the compiler, asm, ampp) | cmds/com (== cmds/hostutil) |
| keyform | generator: keytab.c of the TRAN `asm` (cmds/asm builds and runs it) | cmds/asm |
| gensquirt | generator: squirtdata.c of the B422 driver | filesys/devs/b422 |

Not here: `nc` (Helios C; clang replaces it, and its Codemist sources
keep pointers in ints throughout), `asm`/`objdump` (separate port),
buildtime/buildkeymap/testenc (ARM only or unused), mkrom (rom.i is
disabled), the help database tools, rmgen, esysbuild, the SunOS I/O
server.

## Origin and licence

Sources: the working tree of Helios-NG (github.com/axelmuhr/Helios-NG,
commit 1fe8f65e), imported unchanged in the first commit of this
repository. "RCS" is the revision of the file's RCS history the working
file equals; "RCS+" means the working file has later, uncommitted edits
on top of that revision.

| here | Helios-NG path | RCS |
|---|---|---|
| ampp/ampp.c macro.c lex.c sym.c builtin.c defs.c putback.c charstack.c output.c ampp.h | cmds/ampp/ | 1.11 1.4 1.5 1.3 1.10 1.2 1.6 1.2 1.5 1.6 |
| support/queue.c queue.h ttypes.h getargs.c getargs.h | cmds/support/ | 1.1 1.2 1.1 1.1 1.1 |
| sysbuild/sysbuild.c | cmds/com/sysbuild.c | 1.13+ (an "AM" comment) |
| objed/objed.c | cmds/com/objed.c | 1.15 |
| c/c.c | cmds/com/c.c | 1.137+ (Perihelion 1994 working copy) |
| gencodes/gencodes.c | cmds/hostutil/gencodes.c | 1.10 |
| squash/squash.c | cmds/hostutil/squash.c | 1.2 |
| newmotd/newmotd.c | cmds/hostutil/newmotd.c | 1.11+ (AxM banner, y2k) |
| keyform/keyform.c hash.c asm.h | cmds/asm/ | 1.11, 1.3, 1.10+ (AxM `ellipsis`) |
| gensquirt/gensquirt.c | filesys/devs/b422/gensquirt.c | 1.2+ |
| include/module.h | excerpt of include/module.h | 1.14 |
| LICENSE | LICENSE | |

Licence: GNU General Public License version 3, as Helios-NG; see LICENSE
(copied from Helios-NG). The original Perihelion copyright notices are
kept in the files. All of these sources are Perihelion's (section 9 of
Helios-NG tools/llvm-t800/research/HOST-TOOLS-64.md).

## Building

    make                                  # all tools into ./build
    make BUILDDIR=/tmp/asan CFLAGS='-O1 -g -fsanitize=address,undefined'
    make install PREFIX=/opt/x

The Makefile always adds `-std=gnu99 -fsigned-char -fwrapv
-fno-strict-aliasing`, after `CFLAGS`, so that a `-std=` or
`-funsigned-char` there cannot override them: the code is K&R C,
expects a signed `char` (AArch64's is unsigned), relies on 32-bit
wrap-around arithmetic and reads words through cast pointers.
`CPPFLAGS`, `LDFLAGS` and `LDLIBS` are honoured. The host defines are
those of the original SUN4 host build (`-DSUN4`, `-D__TRAN
-D__HELIOSTRAN` where the SUN4 makefiles had them, `-DUNIX` for c),
without the big-endian `-DHOSTISBIGENDIAN`/`-DBYTE_SEX_ODD`; a
big-endian host is refused (see Changes).

The tools are built with `CC`. The generators keyform and gensquirt
are built with `BUILD_CC` (default `cc`, the compiler of the machine
that runs make, whatever `CC` is): they only write C sources
(keytab.c, squirtdata.c), so they belong to the machine that generates
those sources, not to the one the tools are built for.

Cross build, e.g. static AArch64 tools on an x86-64 machine:

    make CC=aarch64-linux-gnu-gcc LDFLAGS=-static BUILDDIR=build-a64
    make CC=aarch64-linux-gnu-gcc LDFLAGS=-static BUILDDIR=build-a64 \
         install DESTDIR=/tmp/stage PREFIX=/usr/local

This builds the seven tools for AArch64 and keyform/gensquirt for the
x86-64 build machine. `make install` installs the generators only when
`$(CC) -dumpmachine` and `$(BUILD_CC) -dumpmachine` agree, so a cross
install never mixes architectures (it says which programs it left
out). For AArch64 generators too, set `BUILD_CC=aarch64-linux-gnu-gcc
BUILD_LDFLAGS=-static`; then everything is AArch64 and all nine are
installed. The cross build was tested with gcc 12 (Debian 12) and the
tools run under qemu-aarch64 (see Verification).

## Changes

Every change is marked with a "Host port:" comment and keeps the output
of the i386 build for every input that build handles.

Common
- `support/ttypes.h`: `WORD`/`UWORD` (and so `INT`, `word`, `RPTR`) are
  `int32_t`/`uint32_t`, were `long`/`unsigned long` (64 bits on LP64).
- `support/ttypes.h` refuses a big-endian host: `#error` where the
  compiler defines `__BYTE_ORDER__` as big-endian, and a check at the
  start of sysbuild, objed and keyform (exit status 20, 1, 20). Their
  big-endian code paths (`BYTE_SEX_ODD`, `HOSTISBIGENDIAN`) are not set
  by the Makefile and untested.
- `support/queue.h`: prototypes of the queue.c functions (c.c called
  them undeclared, so `Remove()`'s pointer result was an `int`);
  `queue.c`: PreInsert/PostInsert return `void`.
- `include/module.h` (new): the Module/Program/ImageHdr structures and
  magic numbers of Helios's module.h with 32-bit fields from ttypes.h,
  sizes checked at compile time (56/68/12 bytes), and the three
  `<helios.h>` macros objed uses; replaces `-idirafter` the Helios
  include directory, where a `word` is a `long`.

ampp
- `adddef()` takes its value as an `intptr_t` and stores a macro or a
  builtin function in the pointer member of the `Value` union (the
  callers cast the pointers to `INT`, which no longer holds one).
- `struct Symbol` `name[33]`: `cps()` stores 32 characters and a NUL.
  The 1990 overflow by one byte corrupts the i386 heap on some inputs
  (see Verification) and is a heap-buffer-overflow for ASan on 142 of
  the 146 logged commands.
- symbol hash on 32 bits (`UWORD`, was `unsigned long`; a negative
  `char` made the 64-bit sum differ).
- printf format attributes on error/warn/report/_trace/recover; `%d` for
  `INT` line numbers, `%p` for the pointers in `-t` trace output.
- division by zero and MinInt / -1 in `_div`/`_mod` raise SIGFPE on
  every host, as the x86 division instruction did (AArch64 does not trap).
- locals live across a `longjmp` (`pbsave` in include, `infiles` in
  main) are `volatile`; `main` declared `int`.
- `token` grows as needed (was `token[128]`, filled without a bound: an
  identifier or number of 128 characters or more overwrote the symbol
  table on x86-64 and other globals on i386).
- include and input file paths (`-i` directory + `/` + name) are
  allocated to size (were `char path[128]`, an overflow for 127
  characters or more). The `/` is always inserted, as before.
- `_include` and `_report` take their whole text (`defstring()`); they
  used only the newest 256-byte piece of it and wrote the terminating
  NUL one byte past a full piece. `_strsize` counts every piece (a text
  longer than 256 bytes gave the size of its last piece) and gives 0 for
  an empty text (it read the list header as a piece).
- Inputs within the old limits give the same output as before; longer
  ones now work.
- `_report` prints its text with `"%s"` (the text was the printf
  format: a `%` in it printed garbage or crashed, also on i386).

sysbuild
- `WORD` from ttypes.h: the image header is read as three 32-bit words
  and the offset table written as 32-bit words (the unported LP64 build
  wrote a 40-byte nucleus and exit status 0); `%d`/`%x` formats; format
  attribute on `error`.
- the image size from a part's header is checked against the bytes the
  file has after its header and against the space left in the 1 MB
  buffer: a negative size or one near 2^31 (where `isize + fsize + 3`
  wraps past the size test) made fread() write past the buffer. Where
  the read used to come up short the message is the same ("Bad
  header in file ...").
- the image buffer comes from calloc(): the alignment padding of a part
  whose size is not a multiple of 4 was never written (zero only
  because glibc serves 1 MB with a fresh mapping). The HEAD nucleus
  parts are all multiples of 4; output unchanged.

objed
- the host module.h; magic numbers compared as `UWORD` (were
  `unsigned long`, which sign extension broke), `progtype` a `WORD`;
  `%d`/`%x` formats; the image type string printed with `"%s"`;
  `-s`/`-h` values saturate at the 32-bit limits as `atol()` did on i386.
- `-m` walks the module chain with bounds: a module size of 0 or less,
  or one past the image, a chain without its terminating 0 word and a
  negative image size are errors (exit 1); they looped for ever, read
  past the buffer, or printed nothing. Module names are printed with
  `%.32s` (a name need not end in a NUL). swapword() copies with
  memcpy() (big-endian hosts only).

gencodes
- `<stdarg.h>` (`<varargs.h>` is gone), the C library headers instead
  of `extern char *malloc()`, prototypes on every host.
- `tfprintf()` fetches a `%s` argument as `char *` (it fetched every
  argument as `int`, which truncates a pointer on LP64).
- `strtonum()`: `digit` declared outside the loop and initialised (a
  non-digit character used an uninitialised value).
- No fixed-size buffers: tfprintf's `fbuf[100]`/`pbuf[100]` (a message
  of about 100 characters overflowed `pbuf`) and the `line[1000]`/
  `classline[1000]` of fdbscan grow or are measured; lines of any length
  are read whole.
- `classpfix` starts as `"(null)"`: a code line before the first class
  line (or the rest of a line after a 0xFF byte, which rdch() returns as
  EOF) used it uninitialised; the i386 reference printed its NULL as
  `(null)`, an optimised 64-bit build printed stack garbage.
- A class mask of 0 is an error (exit 1); it looped forever.

squash
- `getline()` and the newline removed, for `gets()` (removed in C11):
  a line of any length is read whole. (`gets()` into `char line[100]`
  overflowed the stack beyond 99 characters; a 100-byte `fgets()` would
  split such a line, and a piece starting with `#{`/`#}` would toggle
  copying.)

newmotd
- `fputs(lbuff, stdout)`, not the line as a printf format.
- the centring computes `80 - strlen()` as an `int` (as `size_t`, a
  line over 80 characters gave a huge count; none is that long).

c
- printf format attributes on error/warning/fail; the `execlp()`
  terminator is `(char *)0` (an `int` 0 is 4 bytes in a variadic call
  on LP64); the queue prototypes from queue.h.
- No fixed-size buffers for paths and arguments: AddLibrary's `buf[100]`
  (every run overflowed it once C_LIBDIR plus "helioslib.def" reached
  100 characters, i.e. a lib dir of more than about 86: the i386 build
  wrote into the next static and carried on, a 64-bit FORTIFY build
  aborts), the `[ARGBUFSIZE]` (1024) buffers of AddNamef,
  AddLibraryName, AddInclude, AddSymbol, AddLibraryPath,
  RemoveLibraryPath, AddIncludeOpts (all `-I` directories in one
  argument), AddSymbolOpts and the `cat` of `-j` sources, the 1024-byte
  `-p` command lines, the 200-entry argv of a `-p` command and the
  256-byte FORTRAN pass options are allocated to size (StrFormat,
  StrBuf). The strings passed on are unchanged.
- An empty directory in `-I`/`-L` gets no `/` (as before), without
  reading the byte before the buffer; the usage text is printed with
  `"%s"` and a failing `pipe()` leaves it unpaged.

keyform
- `asm.h` takes ttypes.h and queue.h on every host (was Helios's
  `<helios.h>` for ANSI compilers); prototypes, `void` functions.
- `keytab` 4-byte aligned (it is read as `struct keyentry` and `INT`).
- an entry that would not fit in the 5000-byte `keytab` is an error
  (exit 20; HEAD uses 4672 bytes), it was written past the array.
- `%d`/`%x` formats; the output keeps the HEAD spelling (`long keytab[]`,
  `0x%08xL`), which nc and clang read as 32-bit words for the transputer.
- The names are byte-reversed within each word only on a big-endian host
  (`HOSTISBIGENDIAN`, as makeinc/SUN4.mak sets it). HEAD reversed them
  unless SWAPKEYTABLE or __DOS386, i.e. also on a little-endian Unix
  host, where the table came out wrong; hhead-rebuild overlay 50 added
  `__linux__` to that list.

gensquirt
- `unsigned char c`, so a byte >= 0x80 becomes one `\xNN` escape on
  every host. This is the output of the RS/6000 build that made the
  shipped squirtdata.c; with a signed `char` (i386, x86-64) the bytes
  became out-of-range escapes such as `\x0ffffffb1`. `main` returns int.

## Verification

Reference: the i386 builds of the same HEAD sources that the hhead-rebuild
builds use (hhead-rebuild/host/bin, gcc -m32 -O0), plus keyform built from
the tree's cmds/asm as its makefile does (overlay 50) and gensquirt built
with -funsigned-char (see above); `verify/mkref.sh REFDIR` assembles that
directory. Inputs: every input of the logged HEAD builds v/head (Helios C)
and v/head-hcc-Os-full (clang), plus extras:

| tool | runs | inputs |
|---|---|---|
| ampp | 146 + 151 | the 146 distinct logged commands; every `.a` file of the tree with `-dhelios.TRAN 1 basic.m` |
| sysbuild | 6 | the 2 logged nucleus builds, the same through an `@file`, usage, missing input |
| objed | 1669 | `-i -m` on all 834 files of both builds' prod/; `-i -s -h -n` on copies of them; out-of-range `-s`/`-h` |
| gencodes | 3 | HEAD and Helios 1.3 master.fdb, usage |
| squash | 2 | HEAD and 1.3 master.fdb |
| newmotd | 1 | (same day) |
| keyform | 1 | keytab.c and stdout |
| gensquirt | 1 | b422dvr.b2u, run as `../gensquirt` |
| c | 58 + 58 | the 58 distinct logged `c` commands, stub compiler/assembler logging their argv; once in a tree at a short path (4 characters), once at a long one (155) |

The c stubs replace only the tree path (by `CTREE`) and run.sh's output
directory (by `OUT`) in the logged argv, so a wrong, truncated or
wrongly joined path is a difference; the runs with the short and the
long tree must give the same logs. run.sh records a run that cannot
start (a failing `cd`, a c run that called no stub) as `cdfail`/`nolog`
and exits 1, and cmp.py then fails, instead of comparing two failures
as identical.

Every run's output files, stdout/stderr and exit status compared byte for
byte (2096 runs per configuration), as of the review fixes (2026-10-08):

| configuration | result |
|---|---|
| x86-64, gcc 15, -O2 | identical, except ampp on kernel/ARM/hw_glapa.a (below); c: 116 of 116 |
| x86-64, -O1 -fsanitize=address,undefined | same; no sanitizer report |
| x86-64, -O2 -funsigned-char in CFLAGS | same (the required -fsigned-char comes after CFLAGS; before, ampp differed on 4 binary Unix archives named `.a`) |
| AArch64, gcc 12 cross (Debian 12 container), static, all nine programs, under qemu-aarch64 | same |
| the tools of ad1e65e (before the review fixes), -O2 | c at the long path: 58 of 58 abort ("buffer overflow detected", H1), reported as 58 `nolog` and cmp.py exit 1; the rest as above |

- ampp on kernel/ARM/hw_glapa.a (an ARM file, not part of the TRAN
  build): the i386 build aborts in malloc (exit 134, empty output), its
  heap corrupted by the `name[32]` overflow; the port completes with the
  same exit status 10 as the other ARM files.
- gensquirt: the output is byte-identical to the shipped
  filesys/devs/b422/TRAN/squirtdata.c and to the i386 -funsigned-char
  build; the signed-char i386 build differs in 1620 of 1691 lines.
- sysbuild: both nuclei are also identical to the builds' nucleus.srv
  (with the calloc'd image buffer too).

Edge cases: `verify/edge/edge.sh BINDIR OUTDIR` runs 44 cases from the
review on inputs beyond the old limits or malformed (in verify/edge/in,
binary images made by gen.py): long identifiers, numbers, `_report`
and `_include` texts, include directories of more than 128 characters,
`%` in `_report`, `_strsize` of 600 and of 0 characters, division
traps; gencodes with a long message, a long line, no class line, a
0xFF byte, a zero mask; squash with long lines; sysbuild with short,
negative, huge and wrapping sizes and odd-sized parts (zero padding);
objed with a size-0 module, a chain without its 0 word, a size past
the image, a negative image size, unterminated names, a short header;
c with a 160-character lib dir, `-L<long> -lfoo`, ten long `-I`
directories (with and without `-p`), a 1500-character `-D`, 250
arguments through `-p`. The results are compared with
verify/edge/expected (made with the -O2 build, checked by hand):

| configuration | edge.sh |
|---|---|
| x86-64 -O2, ASan+UBSan, -funsigned-char; AArch64 under qemu | 44 of 44 as expected |
| i386 reference | 18 differ (crashes, hangs, garbage beyond the old limits); the c cases pass (its overflows were silent) |
| the tools of ad1e65e | 27 differ (aborts in c, ampp, gencodes; objed loops, squash splits lines) |

Full tree: the HEAD tree built from scratch twice with clang (rebuild.sh
--cc=hcc: variant head-hcc-Os-full, hcc/clang, HCC_RTLIB as env.sh, all
overlay patches), once with the i386 tools and once with host/bin
holding only 64-bit programs (helios-asm's asm/objdump and these tools),
HOSTCC = 64-bit gcc and cmds/asm building keyform from this port:
417 of 417 prod/ files identical, except prod/TRAN/local/bin/yacc,
whose only difference is the build directory name in its assert strings
(`/tmp/claude-1000/hh32` vs `hh64`). Over the whole source tree with
all intermediate files (10516 files): 10505 identical, 8 differ only by
that directory name (yacc's objects, the overlay's absolute include in
include/sys/errno.h), 3 are the intended differences (cmds/asm/makefile
and makeinc/LINUXHOST.mak of the test overlay, the keyform binary); the
generated codes.h/errno.h/signal.h and keytab.c are identical. The build
logs are identical apart from asm's `-v` statistics (memory and VM pages,
and relaxation change counts: that is the helios-asm port, whose output
is identical). The tools run by that build: ampp 80 times, c 30, asm 938,
sysbuild, squash and gencodes once each, keyform in cmds/asm; nc never
(a guard script stood in its place). The same pair of builds with Helios
C (rebuild.sh without --cc, i386 nc in both, everything else 64-bit in
one) gives the same result: 417 prod/ files, 416 identical, yacc differs
only by the directory name; over the source tree the same 10505 / 8 / 3.

That full-tree comparison was made with the tools of ad1e65e, before
the review fixes; the fixed tools give the same bytes on every logged
input above.

The scripts are in verify/: `mkref.sh REFDIR` (the i386 reference),
`run.sh BINDIR OUTDIR` (every tool on the inputs above), `cmp.py REF
NEW` (compares two such runs, exit 1 if a run did not start),
`edge/edge.sh BINDIR OUTDIR` (the edge cases), `treecmp.py` (two
build trees). run.sh expects the hhead-rebuild trees v/head and
v/head-hcc-Os-full and, for c, a scratch copy of v/head at a short path
(`CTREE`, under 60 characters); the long-path copy defaults to a
symbolic link of more than 128 characters (`CTREE_LONG`). The numbers
above used `unshare -rm` to bind-mount one copy at /mnt (`CTREE=/mnt`)
and its real 155-character path as `CTREE_LONG`.

## Notes for the Helios-NG integration (makeinc/LINUX.mak, next step)

- A clang TRAN build runs from HHOSTBIN: asm (also through asm-rt for
  links), ampp, c, sysbuild, squash, gencodes; the top-level makefile
  adds newmotd (EndMess2); objed is installed but not run. The only
  in-tree host program built with HOSTCC is keyform (cmds/asm); gensquirt
  runs only if b422dvr.b2u looks newer than the checked-in squirtdata.c.
- HOSTCC = plain cc/gcc with `-std=gnu99 -fsigned-char -fwrapv
  -fno-strict-aliasing`; never `-DHOSTISBIGENDIAN` on a little-endian
  host (SUN4.mak sets it).
- cmds/asm: the makefile sets its own HOSTCC/HOSTCFLAGS per HHOST and
  compiles keyform.c against cmds/asm/asm.h, which takes Helios's
  `<helios.h>` (word = long) and the long ttypes.h. A 64-bit in-tree
  keyform needs this port's keyform.c with an asm.h that uses a 32-bit
  ttypes.h when built for the host (the full-tree test compiled
  hosttools64/keyform/keyform.c from the makefile rule instead).
- With HCOMPILER=clang nothing calls nc; HHOSTBIN must still not let an
  `nc` lookup fall through to netcat on PATH.
- hhead-rebuild overlay 40 writes an absolute path into
  include/sys/errno.h; a relative `#include "../errno.h"` makes the tree
  relocatable. yacc (cmds/public/yacc-1.4) embeds its source paths
  (assert), so its products depend on the build directory.
- Lengths: c and ampp have no limit on path, token or argument
  lengths, gencodes and squash none on line lengths (c had 100
  characters for a library path, ampp 128 for an include path and a
  token; see Changes). asm crashes on paths of 128 characters or more:
  keep HSRC short.
