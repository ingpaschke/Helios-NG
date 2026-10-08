# tools/hosttools: the build-host tools for 64-bit hosts

The programs that run on the build machine during a transputer build of
this tree, built from their sources in the tree for current 32- and
64-bit little-endian Unix hosts (x86-64 and AArch64 Linux tested). They
produce the same bytes as the i386 builds of the original sources. With
these and clang's Helios mode (llvm-project, `hcc`), a build of the whole
tree needs no 32-bit host program.

    make                          # all tools into ./build
    make check                    # tests/asm, tests/edge
    make install PREFIX=/opt/x    # objdump as $(OBJDUMP_NAME)

See the Makefile for cross builds (CC, BUILD_CC) and the flags the code
needs. tools/llvm-t800/build/rebuild.sh builds them into its work
directory and uses them for the tree build.

| tool | sources in this tree | built for TRAN too | port history |
|---|---|---|---|
| asm, objdump | cmds/asm/*.c, asm.h vm.h ttypes.h module.h getargs.[ch] | asm | helios-asm 5b88a9f |
| keyform (generator) | cmds/asm/keyform.c, hash.c | (host program of cmds/asm) | helios-asm 5b88a9f |
| ampp | cmds/ampp/*, cmds/support/queue.c | yes | hosttools64 8f5e539 |
| sysbuild | cmds/com/sysbuild.c, cmds/support/getargs.c | yes | hosttools64 8f5e539 |
| objed | cmds/com/objed.c (cmds/hostutil/objed.c is a link) | yes | hosttools64 8f5e539 |
| c | cmds/com/c.c (cmds/hostutil/c.c is a link), cmds/support/queue.c | yes | hosttools64 8f5e539 |
| gencodes, squash, newmotd | cmds/hostutil/ | no | hosttools64 8f5e539 |
| gensquirt (generator) | filesys/devs/b422/gensquirt.c | no | hosttools64 8f5e539 |

Shared support files:
- cmds/support/ttypes.h: `WORD`/`UWORD` are `int32_t`/`uint32_t` on the
  host (were `long`); refuses a big-endian host. On Helios it includes
  `<helios.h>` instead.
- cmds/support/queue.h, queue.c: prototypes, `void` PreInsert/PostInsert.
- cmds/asm/ttypes.h, module.h, getargs.[ch]: the assembler's own copies
  (as in the 1.3 sources; getargs from cmds/support with the fixes of
  PORT-ASM.md fix 8 and 20).
- tools/hosttools/include/module.h: Helios's module/image headers with
  32-bit fields for objed and sysbuild on the host (the tree's
  include/module.h has a `long` word, 64 bits on LP64). The TRAN builds
  use include/module.h.

Origin. The ports were made in two separate repositories first and are
merged here; their histories are in the commit messages of this tree:
- helios-asm (28 commits, last 5b88a9f): cmds/asm at the RCS tag
  Helios1_3 (asm 1.3, "V1.9") ported to 64 bits plus the fixes of the
  2026 review. It replaces the HEAD working files of cmds/asm, which
  were the 1.3 sources plus the 1994 DOS386 port and `@file` arguments;
  `@file` arguments are kept (fix 8), the DOS386 parts are not. Details:
  PORT-ASM.md, tools/llvm-t800/research/HOST-TOOLS-64.md section 3.1,
  ASM-REVIEW.md, REVIEW-ASM-FIXES.md.
- hosttools64 (23 commits, last 8f5e539): the HEAD working files of the
  other tools, ported. Details: PORT-HOSTTOOLS.md,
  tools/llvm-t800/research/HOST-TOOLS-64.md, REVIEW-HOSTTOOLS64.md.

Changes made when merging (not in those repositories): the sources also
compile for the transputer again, with clang (hcc) and Helios C: the
host-only parts are under `#ifndef __HELIOS` (asm's temporary output
file and signal handling: on Helios the target is written directly and
removed after an error; the big-endian check; c's vsnprintf formatting),
`<helios.h>` supplies the types there, and `__attribute__`/`inline` are
defined away for compilers other than gcc and clang.

Licence: GNU General Public License version 3, as Helios-NG (LICENSE at
the top of the tree). The original Perihelion copyright notices are kept
in the files.
