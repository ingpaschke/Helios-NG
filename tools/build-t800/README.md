# Building Helios-NG with llvm-t800

`build.sh` builds this tree for the transputer on a Linux machine
(x86-64 or AArch64) with the llvm-t800 toolchain: clang compiles the C
sources through `hcc`, which takes the Helios C 2.08 command line, and the
Helios assembler, linker and the other host tools are built from
`tools/hosttools`.

## Requirements

- llvm-t800 0.4.0 or later with its Helios add-on: the packages
  `llvm-t800` and `llvm-t800-helios`, installed in `/opt/llvm-t800`
- gcc and GNU make (for the host tools), python3, `cpp`

## Build

```
tools/build-t800/build.sh            # into build-t800/ in this checkout
tools/build-t800/build.sh /tmp/hb    # or into another directory
```

The result is `build-t800/prod/TRAN`, a complete Helios tree (`bin`,
`lib`, `include`, `etc`, ...), with the X server `bin/xhelios` and its
drivers (`X11/xhelios`). `build-t800/logs` has one log per
component. The build uses a working copy in `build-t800/src`.

Environment:

| Variable | Default | Meaning |
| --- | --- | --- |
| `T800` | `/opt/llvm-t800` | the llvm-t800 installation |
| `HCC_OPT` | `s` | clang optimisation level (`-Os`) |
| `JOBS` | `1` | `make -j` inside a component |

Keep `JOBS` at 1: some component makefiles do not declare every
dependency inside a component, so a parallel make can link before a
library of the same directory exists.

## Errors

`build.sh` uses `make -k`, so a component builds as much as it can, and
counts the components whose make failed; their logs say why. A complete
build reports none.

Two passes are expected and not counted: the nucleus is built once before
`servers/servtask` (log `nucleus-pass1.txt`, which shows errors for the
parts that need servtask) and once after it. `servers/common` is not
built: it holds `modend.o` and `trace.o` for the ARM servers.

## How it works

1. `headco.py` copies the tree to `OUT/src`, excluding `tools/` and
   prebuilt artifacts in the processor directories.
2. `LINUXHOST.mak` is added at the end of `makeinc/SUN4.mak` in the copy:
   the SUN4 host make logic is used, with gcc as the host compiler, `hcc`
   as the C compiler, and the host tools built here.
3. `tools/hosttools` is built into `OUT/host`.
4. The components are built in the order of the top-level makefile,
   then `X11/xhelios`.
   Programs are linked with the compiler-rt library of llvm-t800
   (`llvmrt-generic.lib`, soft floating point) through `asm-rt`.
