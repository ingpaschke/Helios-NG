<!-- Copied from the helios-asm repository, commit 5b88a9f (README.md).
     Paths in this file refer to that repository: its sources are now
     cmds/asm/*, its tests tools/hosttools/tests/asm, its Makefile is
     replaced by tools/hosttools/Makefile. See README.md here. -->

# helios-asm: the Helios transputer assembler/linker for 64-bit hosts

`asm` ("Transputer Assembler V1.9") is the assembler and linker of the
Helios 1.3 operating system for the INMOS transputer, by Perihelion
Software. It assembles Helios assembler text (`.s`) to its preassembled
form (`.p`, option `-p`) and links `.p`/`.o` files and `.def`/`.lib`
libraries into a Helios image. clang's Helios mode (`--target=t800-helios`)
runs it for `-c` and for the link, and finds it as `asm` beside clang or on
PATH. `objdump` dumps a `.p` file in a readable form.

This directory is a port of that program to current 32- and 64-bit
little-endian Unix hosts (x86-64 and AArch64 Linux, tested). It produces
the same bytes as the i386 build of the original sources for every input
that build handles correctly. On top of the port it has the robustness
fixes of the 2026 review (see Fixes below): no path length limit, no swap
file, a non-zero exit status and no output after any error, undefined
symbols and duplicate definitions as errors, no endless loops, @file
arguments.

## Origin and licence

- Source: Helios-NG, `cmds/asm`,
  every file checked out of its RCS history at the tag `Helios1_3` (the
  1.3 release, 1992-1993), except `hash.c`, which has no RCS file and is
  taken from the working tree. `module.h` is an excerpt of the 1.3
  `include/module.h` (RCS 1.8).
- Not taken: `gencsc.c` and `str.c` (not part of the program), `queue.c`
  (linked by the original makefile, never called), the Helios makefiles.
- `getargs.c`, `getargs.h`: from Helios-NG HEAD `cmds/support` (1994),
  for the @file arguments of HEAD's asm (fix 8).
- Licence: GNU General Public License version 3, as Helios-NG; see
  LICENSE (copied from Helios-NG). The original copyright notices of
  Perihelion Software Ltd. are kept in the files.

## Building

    make                                   # asm, objdump
    make CC=aarch64-linux-gnu-gcc          # cross: keyform runs on the
                                           # build host (BUILD_CC, default cc)
    make install PREFIX=/opt/x ASM_NAME=asm OBJDUMP_NAME=helios-objdump

`keyform` generates `keytab.c`, the keyword table, as a list of 32-bit
words in the byte order of the host it runs on: build and target host must
have the same byte order (both little-endian for x86-64 and AArch64).
The port as a whole assumes a little-endian host (`ttypes.h` stops a
big-endian build with an `#error`).

The Makefile always adds `-std=gnu99 -fsigned-char -fwrapv
-fno-strict-aliasing` (see the comments there): the code is K&R C, expects
a signed `char` (AArch64's is unsigned), and relies on 32-bit wrap-around
arithmetic.

## Changes from the 1.3 sources

Every change is marked with a "Host port:" comment. No change alters the
output for an input the i386 build handles.

Word size
- `ttypes.h`: `WORD`/`UWORD` are `int32_t`/`uint32_t` (were `long`, which
  is 64 bits on LP64). `asm.h` takes its types from `ttypes.h` on every
  host instead of Helios's `<helios.h>`, whose `word` is a `long`.
- `module.h` (new, for `objed.c`): the image and program headers that
  objed reads and writes back, all 32-bit fields; compile-time checks
  that they are 12 and 68 bytes.
- `keyform.c`: emits `UWORD keytab[]` / `WORD dicttab[]` with `%x`/`%d`
  (were `long` with `%lx`/`%ld`); the SUN4 big-endian name swapping only
  with `HOSTISBIGENDIAN`.
- printf formats: `%ld`/`%lx` for 32-bit values are `%d`/`%x`, which
  matters for the `.p` text that preasm writes with `fprintf`.

Pointers in 32-bit places
- The file name of an `s_newfile` code entry is stored in `Value.c` and
  was read back through `Value.w`, which truncated the pointer
  (`genimage.c`, `growdata.c`, `growcode.c`).
- The `Value` union is 8 bytes on LP64 (it holds a `char *`). VM
  allocations are rounded to the pointer size (`VMwordlen` in `vm.h`) so
  that structures in VM pages stay aligned; `Code` entries stay
  contiguous within a code segment.
- The resident VM page limit was doubled on LP64 (512 pages of 8 KB), as
  the structures in VM grow by at most a factor of two. Fix 2 below
  removed the limit.
- The `Value`s stored in code entries start with every byte zero
  (`valzero()` in `asm.h`); setting only the 4-byte `.w` or `.v` left
  the rest undefined (never read).

Types and the host
- `keytab[]` is declared `UWORD` in `asm.h`, as `keytab.c` defines it
  (it was declared `UBYTE`); `lex.c` addresses it in bytes.
- A `VMRef` (16:16 bit fields) is tested through `VMWord()` and set with
  `MakeVMRef`, not read as `*(int *)&`.
- The run times are `time_t`; objed's `swap()` is `WORD swap(WORD)`
  (was `long`).
- `ttypes.h` stops a build on a big-endian host with an `#error`.

Prototypes and varargs
- `error`, `warn`, `warn_error`, `report`, `_trace`, `recover` (asm.c),
  `VMerror` (vm.c) and objdump's `error` took six or eleven `INT`
  arguments and passed them to `fprintf`; they are `<stdarg.h>` functions
  now, declared with the printf format attribute.
- `asm.h` declares every exported function with a full prototype, so each
  call is checked (the file-local functions keep their old-style
  declarations). A `VMRef` (a 16:16 bit-field struct) or a pointer passed
  to a `%x` is printed through `VMWord()` or with `%p`.
- The C library headers replace the implicit declarations of `malloc`,
  `free`, `exit`, `unlink`, `read`, `write`, `lseek`, `close`, `strncpy`,
  `time` and the `bcopy`/`bzero` macros.

Undefined behaviour the 32-bit build got away with
- `eval`, `dodyadic`, `domonadic`, `readnumber` and objdump's `main` fell
  off their ends; they return 0 (or the value read).
- `dodyadic`: shift counts are taken modulo 32 (what the i386 shift
  instructions did). A division by zero is a fatal error on every host
  (x86 trapped with SIGFPE, AArch64 would return 0); `MinInt / -1`
  wraps.
- Locals that are live across a `longjmp` back into `growcode` and
  `_asm` are `volatile`.
- The automatic program name took `outfile[-1]` before checking for the
  start of the string; the test order is swapped.
- Implicit `int` declarations, a `NULL` assigned to a `WORD`, `char *`
  passed as `UBYTE *`: typed or cast.

## Fixes

Every fix is marked with a "Fix:" comment and has its own commit. The
finding ids refer to Helios-NG `tools/llvm-t800/research/ASM-REVIEW.md`
(H, M, L) and `REVIEW-ASM-FIXES.md` section A (A1-A12). `make check`
runs the regression tests in `tests/`.
None changes the output of a run that the i386 build finishes without an
error or warning (corpus and link replay below), apart from the `.p` text
of the expressions that fix 7 corrects.

| # | Findings | What changed | Output impact |
|---|---|---|---|
| 1 | H1, H2, L1 | `infile`/`outfile` were 128-byte arrays filled by `strcpy` (input paths of 137, output paths of 128 characters crashed asm); they are pointers now. objdump's `ebuf[300]` grows. An option missing its argument (`asm x.s -o`) is a fatal error, not a NULL `strcpy`. | none |
| 2 | H5, H6, H7, M10 | Every VM page stays resident: no 256-page (2 MB) cap, no swap file `/helios/tmp/vmfile` (missing on Linux, shared by parallel runs, could evict the new page). Out of memory and more than 65535 pages (512 MB, the 16-bit page field) are fatal. `-m` and `-z` are accepted and have no effect. | none; large links that failed now work |
| 3 | H4, M8 | `recover()` counts an error (exit 10; it only warned, exit 0). `-o` and `-d` output goes to a temporary file (`mkstemp`, the mode `fopen` would give) that is renamed to the target only when the run ends without an error, and is removed after an error, a fatal error or SIGINT/SIGTERM/SIGHUP (and the signals of fix 12); write errors are fatal. As before, `-o` removes the old target first; a target that is not a regular file (`/dev/null`) is written directly. | no output file after an error (was 0-byte or partial) |
| 4 | M1, M2, M3 | Bytes >= 0x80 outside strings and comments: only the codes that `.p` files contain are accepted, any other byte is an error (directive codes 0xe0, 0xed-0xff hung asm). The recovery loop stops at EOF (an error in the last statement hung asm). A `--` comment ends at EOF (hung asm without a final newline). After 100 errors asm stops. | none; former hangs end with exit 10/20 |
| 5 | H3, H8, M7 | Undefined symbols (were "set to zero", e.g. a call to address 0) and duplicate code-label or data definitions across modules (the second was ignored) are errors: every one is reported, no output, exit 10. `-wu` makes undefined symbols warnings again (the switch existed but was unused), the new `-wd` duplicate definitions; inside `-l` libraries duplicates are silently overridden as before. `@sym` of an undefined symbol inside an expression (was "VM Error: invalid VMRef 0") is handled like at the top level. | links that relied on these warnings fail (list below); with `-wu -wd` byte-identical |
| 6 | M5, M9 (M6) | Numbers: the digits are bounded ("Token too large"; 1040+ digits crashed asm), a value that does not fit in 32 bits is an error (wrapped silently). "Code size error" names the instruction, value, sizes, the label in the operand, the module and the offset. Division by zero was already an error in the port. | none |
| 7 | M4 | `.p` output prints a negative number inside an expression as `(0-N)`: `x - -4` came out as `(x--4)` (a `--` comment) and `(-4+x)` read back as `-(4+x)`. | `.p` bytes only for such expressions; none of the 3685 corpus files has one |
| 8 | (HEAD) | `@file` arguments from HEAD's asm (`getargs.c`, fixed: read in a loop, empty file allowed, empty quotes, nesting limit 32). | none |
| 9 | A1 | A token that does not start a statement ("Unexpected symbol") is an error (recover); it was a warning and the token was dropped: `ldc 0x7fffffff` assembled as `ldc 0`, `ldc 1.5` as `ldc 1`, exit 0. The other warnings that drop or alter input are errors too: a BYTE item that is not a number or string, or out of -128..255; the growcode iteration limit (code sizes not converged). "Colon missing - assumed", "WORD not on word boundary" and the symbol table checks stay warnings (the input is used as written). | 4 corpus files (`speed/w/big.s`, a text file of "camhekacamli" lines, 1-byte `.p`) now fail |
| 10 | A2 | The output target is tested with `lstat`: a symbolic link (`/dev/stdout`), pipe or device is opened and written directly and never removed; only a regular file goes through the temporary file. With stdout redirected to a file, `-o /dev/stdout` failed ("Cannot open", exit 20; as root it removed the `/dev/stdout` link). A symbolic link to a regular file is written through (was replaced by a file). | none |
| 11 | A3 | The temporary-file signal handlers are not installed for a signal that is ignored when asm starts (`nohup`, background jobs of a shell without job control): asm died on SIGINT/SIGHUP there, the i386 build kept running. | none |
| 12 | A5 | SIGQUIT, SIGPIPE (`asm -v ... \| head`) and SIGXFSZ (`ulimit -f`) remove the temporary file too (unless ignored, fix 11). | none |
| 13 | A6 | The temporary file names that the signal handler unlinks are set and cleared with the caught signals blocked, and cleared before they are freed: a signal between `free` and `= NULL` made the handler unlink a garbage path, a signal right after `mkstemp` left the new file. | none |
| 14 | A7 | The temporary file name (target + `.XXXXXX`) is shortened to the file system's NAME_MAX: an output file name of 249-255 characters failed ("Cannot open"). | none |
| 15 | A4 | A DATA or COMMON directive for a name that is already a code label, data or common is a duplicate definition like the others: a warning with `-wd` or inside a library (as in 1.3), else an error. `-wd` did not cover a code label followed by data of the same name in a later module (exit 10). | none |
| 16 | A8 | A negated number `-N` (decimal or `#` hex) with N > 0x80000000 is an error: `-2147483649` became 2147483647 and `-4294967295` 1. N up to 0xffffffff stays valid without the minus (the 32-bit spellings), and a minus applied to a longer expression, `-(#ffffffff+2)`, is 32-bit arithmetic as before. | none |
| 17 | A9 | Operands that were cut or ignored silently are errors: `size` outside 1..255 (stored in a byte: `size 300` became 44, `size -1` was ignored), a negative BLKB/BLKW size (an empty block), a string longer than the rest of its BLKB block (cut short; excess number items are errors since fix 9). | none |
| 18 | A10 | In text input no byte >= 0x80 outside strings and comments is a `.p` token: an input is text if its name ends in `.s`, or from its first keyword (an instruction or directive spelt out, which `.p` files never contain). A stray UTF-8 character whose lead byte is a valid `.p` code (most 3-byte characters, e.g. the euro sign) was read as a directive with binary operands. `.p` files of any other name, `-l` libraries and stdin are read as before. | none |
| 19 | A11 | An error in growcode is counted once (recover counts it; growcode counted it again, so "Too many errors" came after 50). | none |
| 20 | A12 | Argument nits: an option that is the last word of an `@file` takes its argument from the outer list (was "needs an argument"); `pushargs` keeps the outer list's count and running out of memory there is fatal (the rest of the arguments was dropped), a failed `realloc` in `getargs` no longer leaks; `-w` letters set their flag (`-wu -wu` toggled it off); messages about options print `asm` as the file name (was empty); an undefined `@sym` is reported once also when used at the top level of an instruction (was once per use). | none |
| 21 | (port review) | objdump: a CODE record cut off after its tag is an error (its size was EOF, -1), and a record of size 0 or -1 no longer reads `ebuf[-1]`. | none |

Not changed (they change output or need a decision): prefix sizing and
relaxation (`-f` default, a least-fixpoint option), the 64 KB reach of
`size 4` jump-table entries, `printtab` (L5), `-i` (L2), the 31-character
program name (L4), `\x` escapes (L6), `module -2` (L8), objed on `-x`
images (L11).

### Links that now fail

Replaying the Helios build logs (below), 58 links that the i386 asm
finished with "undefined - set to zero" warnings now fail; no duplicate
definitions were found. Causes:

- clang (fixed in llvm-project c3d0a50b652e, "[T800] Helios: no library
  calls the link cannot resolve"): `he1000.d` `.memset` (hcc `-r`: a run
  of zero stores merged into memset, no stub in library code), `fs`
  `_wcslen` (a loop turned into wcslen, which Helios lacks), `servtask.i`
  `_memmove` (its own copy loop turned into memmove, Clib only), `ufs`
  (HEAD) and `tcpip` `_puts`/`_putchar` (printf calls rewritten; both
  servers define their own printf and link no Clib); `loader.i` and
  `procman.i` `__stack_error` in d19930902-kdef-hcc-Os are a stale build
  from before e5822a210a12.
- Helios sources and makefiles: `dlib` `__rd*chk`/`__wr*chk` (1.3 tag
  `dlibstar.a` line 63 has `_if 'defp helios.C40`, always true, so the
  C40-only stubs are taken; d19930902 has `_if _defp`), `hl`
  `_NULL`/`_progname`/`_stderr` (an RCS `$Log` comment swallows the
  includes; Norcroft reported errors and the build went on), `mount`,
  `ufsfmt`, `umount` `_EFTYPE` (not in the 1.3 `errno.h`; Norcroft
  "inventing extern int EFTYPE"), HEAD `fault.i` `___rt_sdiv10`
  (`fstart.a` stubs an ARM helper), HEAD `asm` `_getargs`/`_pushargs`/
  `_popargs` (its makefile does not link `cmds/support/getargs`).

## Known limits

- The VM page number is 16 bits: a run needs at most 65535 pages of 8 KB.
- A token (symbol name, string, number) is at most 1023 characters.
- A `size n` fixed chain reaches what n bytes reach (`size 4`: +-64 KB).
- Relaxation is that of 1.3 (by design: Helios-tree images stay test
  vectors).

## Verification

The port, against the i386 build of the same 1.3 sources
(`h13-rebuild/host/bin/asm`, gcc -m32 -O0): `asm -p` on 3696 `.s` files,
every asm command of the Helios 1.3 tree build logs re-run, a complete
rebuild of the Helios 1.3 tree with this asm, large links (a 2.5 MB NetHack
image with `-m`, synthetic single modules up to 500 VM pages), objdump on
`.p`, `.def` and `.lib` files: byte-identical outputs and identical exit
status, on x86-64 and on AArch64 (under qemu-user); on x86-64 also with
AddressSanitizer and UBSan. Byte-identical means the outputs (`-o`, `-d`)
and the exit status; the `-v` statistics differ (VM pages and memory
sizes: the structures are larger on LP64, and the resident page limit
was doubled, then removed by fix 2).

The fixes, after each commit and at the end, against the same i386 build:
- `asm -p` on the 3696 corpus `.s` files (Helios-NG fixtures, m3, m4, m5,
  newatw-test; 20 s timeout): 3685 byte-identical with exit 0; the 11 that
  hang the i386 build (9 unexpanded ampp sources, 2 binary files) end with
  exit 10 or 20 and a message.
- 16351 asm commands replayed from the logs of the 17 h13-rebuild and
  hhead-rebuild variants (inputs that still exist; the build directories
  are read, outputs go elsewhere): all 3975 that the i386 build finishes
  give byte-identical images, libraries and .def files, except the 58
  links above (exit 10, byte-identical with `-wu -wd`); the others fail
  with the same exit status.
- The review's reproducers: crashes, hangs and limits end with a message
  and a non-zero exit, or with correct output (a comment at EOF).
- Large inputs without `-m`: the 127-object NetHack link (2,564,124
  bytes; with `-wu`, as it has no libraries) is byte-identical to the
  i386 build with `-m`; the synthetic g2700 and g4000 and the 80k-, 160k-
  and 320k-label single modules (gen.py 8000/16000/32000) are identical
  to the i386 build without the page cap.
- AddressSanitizer + UBSan (x86-64) over corpus, link replay, reproducers
  and the large links: no reports.
- AArch64 (static, gcc 14 cross, qemu-user): the corpus as on x86-64, and
  2256 replayed commands of two variants with the same exit status and
  outputs as on x86-64; the NetHack link identical.
- llvm-project `llvm/test/CodeGen/T800/helios` with T800_HELIOS_ASM set to
  this asm: all pass (fixes 1-8).

Fixes 9-20 (REVIEW-ASM-FIXES A1-A12) and the later "Host port:" changes,
against the build before fix 9 and the i386 build, after each commit and
at the end:
- Corpus: 3681 files byte-identical with exit 0 in all three; the 4
  `speed/w/big.s` files (text that is no assembler, which the i386 build
  turned into a 1-byte `.p` with warnings) now fail with exit 10 (fix 9);
  the 11 above unchanged.
- Link replay: all 3917 commands that both earlier builds finish give
  byte-identical outputs; the 58 links above fail as before and are
  byte-identical to the i386 build with `-wu -wd`; the 12375 others fail
  with the same exit status.
- objdump (no option, -a, -an, -as, -ans) on 524 corpus `.p` files and
  100 `.def`/`.lib` files: identical to the build before.
- `make check` (tests/, one or more tests per fix): all pass on x86-64,
  with AddressSanitizer + UBSan, and on AArch64 under qemu-user.
- AddressSanitizer + UBSan over corpus, link replay, review reproducers
  and tests: no reports. AArch64 (static, gcc 14 cross, qemu-user):
  corpus and the 16351 replayed commands identical to x86-64.
