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
`lib`, `include`, `etc`, ...). `build-t800/logs` has one log per
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

## Components that report errors

`build.sh` uses `make -k`, so a component builds as much as it can. These
report errors (the build prints them; their logs say why):

| Component | Why |
| --- | --- |
| `nucleus` | built twice in the order of the top-level makefile: the first pass misses parts the second one finds |
| `tcpip`, `tcpip/devs/in-ether`, `tcpip/devs/ims-b431` | the firmware of Ethernet boards of the time (`b407.b2h`, `imsb431.b2h`) is not in Helios-NG |
| `filesys`, `filesys/devs/msc`, `filesys/devs/b422` | the same for disk controllers (`msc02.gen`, `b422dvr.b2u`) |
| `servers/common` | its makefile names some sources through a variable that is empty here |
| `filesys/ufs/cmds` | includes `/hsrc/include/dirent.h`, a path of Perihelion's machines |
| `filesys/ufs/devs/raw` | its install directory is not created |
| `demos` | the keyboard demo uses a `LinkInfo` member that no longer exists |

Apart from those parts, the components build and install.

## How it works

1. `headco.py` copies the tree to `OUT/src`, excluding `tools/` and
   prebuilt artifacts in the processor directories.
2. `LINUXHOST.mak` is added at the end of `makeinc/SUN4.mak` in the copy:
   the SUN4 host make logic is used, with gcc as the host compiler, `hcc`
   as the C compiler, and the host tools built here.
3. `tools/hosttools` is built into `OUT/host`.
4. The components are built in the order of the top-level makefile.
   Programs are linked with the compiler-rt library of llvm-t800
   (`llvmrt-generic.lib`, soft floating point) through `asm-rt`.
