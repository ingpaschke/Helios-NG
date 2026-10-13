# The port of the build-host tools to 64-bit hosts

What was changed in the sources of the tools in README.md so that they
build and run on 32- and 64-bit little-endian hosts, and why. Every
change is marked in the sources with a "Host port:" comment (a
behaviour fix of asm with a "Fix:" comment) and keeps the output of the
i386 build of the original sources for every input that build handles
correctly, except where a fix below says otherwise.

## Common

- cmds/support/ttypes.h: `WORD`/`UWORD` (and so `INT`, `word`, `RPTR`)
  are `int32_t`/`uint32_t`, were `long`/`unsigned long` (64 bits on
  LP64).
- cmds/support/ttypes.h refuses a big-endian host: `#error` where the
  compiler defines `__BYTE_ORDER__` as big-endian, and a check at the
  start of sysbuild and objed (exit status 20 and 1). Their big-endian
  code paths (`BYTE_SEX_ODD`, `HOSTISBIGENDIAN`) are not set by the
  Makefile and untested.
- cmds/support/queue.h: prototypes of the queue.c functions (c.c called
  them undeclared, so `Remove()`'s pointer result was an `int`);
  queue.c: PreInsert/PostInsert return `void`.
- tools/hosttools/include/module.h: the Module/Program/ImageHdr
  structures and magic numbers of Helios's include/module.h (RCS 1.14)
  with 32-bit fields from ttypes.h, sizes checked at compile time
  (56/68/12 bytes), and the three `<helios.h>` macros objed uses; it
  replaces the Helios include directory, where a `word` is a `long`.

## asm and objdump

`asm` ("Transputer Assembler V1.9") is the assembler and linker of the
Helios 1.3 operating system for the INMOS transputer, by Perihelion
Software. It assembles Helios assembler text (`.s`) to its preassembled
form (`.p`, option `-p`) and links `.p`/`.o` files and `.def`/`.lib`
libraries into a Helios image. clang's Helios mode
(`--target=t800-helios`) runs it for `-c` and for the link, and finds it
as `asm` beside clang or on PATH. `objdump` dumps a `.p` file in a
readable form.

The port produces the same bytes as the i386 build of the original
sources for every input that build handles correctly. On top of the
port it has the robustness fixes listed below: no path length limit, no
swap file, a non-zero exit status and no output after any error,
undefined symbols and duplicate definitions as errors, no endless
loops, `@file` arguments.

### Sources

cmds/asm holds every file of the RCS history at the tag `Helios1_3`
(the 1.3 release, 1992-1993), except `hash.c`, which has no RCS file
and is taken from the working tree. `module.h` is an excerpt of the 1.3
`include/module.h` (RCS 1.8). `getargs.c` and `getargs.h` are those of
cmds/support (1994), for the `@file` arguments of the later asm (fix
8); the 1994 DOS386 port of the later sources is not included.
`gencsc.c`, `str.c` (not part of the program) and `queue.c` (linked by
the original makefile, never called) are not built.

### Changes from the 1.3 sources

Word size
- `ttypes.h`: `WORD`/`UWORD` are `int32_t`/`uint32_t` (were `long`,
  which is 64 bits on LP64). `asm.h` takes its types from `ttypes.h` on
  every host instead of Helios's `<helios.h>`, whose `word` is a `long`.
- `module.h` (for `objed.c`): the image and program headers that objed
  reads and writes back, all 32-bit fields; compile-time checks that
  they are 12 and 68 bytes.
- `keyform.c`: emits `UWORD keytab[]` / `WORD dicttab[]` with `%x`/`%d`
  (were `long` with `%lx`/`%ld`); the SUN4 big-endian name swapping
  only with `HOSTISBIGENDIAN`.
- printf formats: `%ld`/`%lx` for 32-bit values are `%d`/`%x`, which
  matters for the `.p` text that preasm writes with `fprintf`.

Pointers in 32-bit places
- The file name of an `s_newfile` code entry is stored in `Value.c` and
  was read back through `Value.w`, which truncated the pointer
  (`genimage.c`, `growdata.c`, `growcode.c`).
- The `Value` union is 8 bytes on LP64 (it holds a `char *`). VM
  allocations are rounded to the pointer size (`VMwordlen` in `vm.h`)
  so that structures in VM pages stay aligned; `Code` entries stay
  contiguous within a code segment.
- The `Value`s stored in code entries start with every byte zero
  (`valzero()` in `asm.h`); setting only the 4-byte `.w` or `.v` left
  the rest undefined (never read).

Types and the host
- `keytab[]` is declared `UWORD` in `asm.h`, as `keytab.c` defines it
  (it was declared `UBYTE`); `lex.c` addresses it in bytes.
- A `VMRef` (16:16 bit fields) is tested through `VMWord()` and set
  with `MakeVMRef`, not read as `*(int *)&`.
- The run times are `time_t`; objed's `swap()` is `WORD swap(WORD)`
  (was `long`).
- `ttypes.h` stops a build on a big-endian host with an `#error`.

Prototypes and varargs
- `error`, `warn`, `warn_error`, `report`, `_trace`, `recover` (asm.c),
  `VMerror` (vm.c) and objdump's `error` took six or eleven `INT`
  arguments and passed them to `fprintf`; they are `<stdarg.h>`
  functions now, declared with the printf format attribute.
- `asm.h` declares every exported function with a full prototype, so
  each call is checked (the file-local functions keep their old-style
  declarations). A `VMRef` (a 16:16 bit-field struct) or a pointer
  passed to a `%x` is printed through `VMWord()` or with `%p`.
- The C library headers replace the implicit declarations of `malloc`,
  `free`, `exit`, `unlink`, `read`, `write`, `lseek`, `close`,
  `strncpy`, `time` and the `bcopy`/`bzero` macros.

Undefined behaviour the 32-bit build got away with
- `eval`, `dodyadic`, `domonadic`, `readnumber` and objdump's `main`
  fell off their ends; they return 0 (or the value read).
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

### Fixes

None changes the output of a run that the i386 build finishes without
an error or warning, apart from the `.p` text of the expressions that
fix 7 corrects. `make check` runs the regression tests in tests/asm.

| # | What changed | Output impact |
|---|---|---|
| 1 | `infile`/`outfile` were 128-byte arrays filled by `strcpy` (input paths of 137, output paths of 128 characters crashed asm); they are pointers now. objdump's `ebuf[300]` grows. An option missing its argument (`asm x.s -o`) is a fatal error, not a NULL `strcpy`. | none |
| 2 | Every VM page stays resident: no 256-page (2 MB) cap, no swap file `/helios/tmp/vmfile` (missing on Linux, shared by parallel runs, could evict the new page). Out of memory and more than 65535 pages (512 MB, the 16-bit page field) are fatal. `-m` and `-z` are accepted and have no effect. | none; large links that failed now work |
| 3 | `recover()` counts an error (exit 10; it only warned, exit 0). `-o` and `-d` output goes to a temporary file (`mkstemp`, the mode `fopen` would give) that is renamed to the target only when the run ends without an error, and is removed after an error, a fatal error or SIGINT/SIGTERM/SIGHUP (and the signals of fix 12); write errors are fatal. As in the original, `-o` removes the old target first; a target that is not a regular file (`/dev/null`) is written directly. | no output file after an error (was 0-byte or partial) |
| 4 | Bytes >= 0x80 outside strings and comments: only the codes that `.p` files contain are accepted, any other byte is an error (directive codes 0xe0, 0xed-0xff hung asm). The recovery loop stops at EOF (an error in the last statement hung asm). A `--` comment ends at EOF (hung asm without a final newline). After 100 errors asm stops. | none; former hangs end with exit 10/20 |
| 5 | Undefined symbols (were "set to zero", e.g. a call to address 0) and duplicate code-label or data definitions across modules (the second was ignored) are errors: every one is reported, no output, exit 10. `-wu` makes undefined symbols warnings again (the switch existed but was unused), the new `-wd` duplicate definitions; inside `-l` libraries duplicates are silently overridden as before. `@sym` of an undefined symbol inside an expression (was "VM Error: invalid VMRef 0") is handled like at the top level. | links that relied on these warnings fail; with `-wu -wd` byte-identical |
| 6 | Numbers: the digits are bounded ("Token too large"; 1040+ digits crashed asm), a value that does not fit in 32 bits is an error (wrapped silently). "Code size error" names the instruction, value, sizes, the label in the operand, the module and the offset. | none |
| 7 | `.p` output prints a negative number inside an expression as `(0-N)`: `x - -4` came out as `(x--4)` (a `--` comment) and `(-4+x)` read back as `-(4+x)`. | `.p` bytes only for such expressions |
| 8 | `@file` arguments of the later asm (`getargs.c`, fixed: read in a loop, empty file allowed, empty quotes, nesting limit 32). | none |
| 9 | A token that does not start a statement ("Unexpected symbol") is an error (recover); it was a warning and the token was dropped: `ldc 0x7fffffff` assembled as `ldc 0`, `ldc 1.5` as `ldc 1`, exit 0. The other warnings that drop or alter input are errors too: a BYTE item that is not a number or string, or out of -128..255; the growcode iteration limit (code sizes not converged). "Colon missing - assumed", "WORD not on word boundary" and the symbol table checks stay warnings (the input is used as written). | text that is not assembler, which the i386 build turned into a 1-byte `.p` with warnings, now fails |
| 10 | The output target is tested with `lstat`: a symbolic link (`/dev/stdout`), pipe or device is opened and written directly and never removed; only a regular file goes through the temporary file. With stdout redirected to a file, `-o /dev/stdout` failed ("Cannot open", exit 20; as root it removed the `/dev/stdout` link). A symbolic link to a regular file is written through (was replaced by a file). | none |
| 11 | The temporary-file signal handlers are not installed for a signal that is ignored when asm starts (`nohup`, background jobs of a shell without job control): asm died on SIGINT/SIGHUP there, the i386 build kept running. | none |
| 12 | SIGQUIT, SIGPIPE (`asm -v ... \| head`) and SIGXFSZ (`ulimit -f`) remove the temporary file too (unless ignored, fix 11). | none |
| 13 | The temporary file names that the signal handler unlinks are set and cleared with the caught signals blocked, and cleared before they are freed: a signal between `free` and `= NULL` made the handler unlink a garbage path, a signal right after `mkstemp` left the new file. | none |
| 14 | The temporary file name (target + `.XXXXXX`) is shortened to the file system's NAME_MAX: an output file name of 249-255 characters failed ("Cannot open"). | none |
| 15 | A DATA or COMMON directive for a name that is already a code label, data or common is a duplicate definition like the others: a warning with `-wd` or inside a library (as in 1.3), else an error. `-wd` did not cover a code label followed by data of the same name in a later module (exit 10). | none |
| 16 | A negated number `-N` (decimal or `#` hex) with N > 0x80000000 is an error: `-2147483649` became 2147483647 and `-4294967295` 1. N up to 0xffffffff stays valid without the minus (the 32-bit spellings), and a minus applied to a longer expression, `-(#ffffffff+2)`, is 32-bit arithmetic as before. | none |
| 17 | Operands that were cut or ignored silently are errors: `size` outside 1..255 (stored in a byte: `size 300` became 44, `size -1` was ignored), a negative BLKB/BLKW size (an empty block), a string longer than the rest of its BLKB block (cut short; excess number items are errors since fix 9). | none |
| 18 | In text input no byte >= 0x80 outside strings and comments is a `.p` token: an input is text if its name ends in `.s`, or from its first keyword (an instruction or directive spelt out, which `.p` files never contain). A stray UTF-8 character whose lead byte is a valid `.p` code (most 3-byte characters, e.g. the euro sign) was read as a directive with binary operands. `.p` files of any other name, `-l` libraries and stdin are read as before. | none |
| 19 | An error in growcode is counted once (recover counts it; growcode counted it again, so "Too many errors" came after 50). | none |
| 20 | Argument handling: an option that is the last word of an `@file` takes its argument from the outer list (was "needs an argument"); `pushargs` keeps the outer list's count and running out of memory there is fatal (the rest of the arguments was dropped), a failed `realloc` in `getargs` no longer leaks; `-w` letters set their flag (`-wu -wu` toggled it off); messages about options print `asm` as the file name (was empty); an undefined `@sym` is reported once also when used at the top level of an instruction (was once per use). | none |
| 21 | objdump: a CODE record cut off after its tag is an error (its size was EOF, -1), and a record of size 0 or -1 no longer reads `ebuf[-1]`. | none |

Not changed, as they would change the output: prefix sizing and
relaxation (`-f` default, a least-fixpoint option), the 64 KB reach of
`size 4` jump-table entries, the 31-character program name.

### Known limits

- The VM page number is 16 bits: a run needs at most 65535 pages of
  8 KB.
- A token (symbol name, string, number) is at most 1023 characters.
- A `size n` fixed chain reaches what n bytes reach (`size 4`:
  +-64 KB).
- Relaxation is that of 1.3 (by design: Helios-tree images stay test
  vectors).

### Comparison with the i386 build

The port was compared with the i386 build of the same 1.3 sources (gcc
-m32 -O0): `asm -p` on assembler sources, every asm command of Helios
tree builds re-run, a complete rebuild of the Helios 1.3 tree, large
links, objdump on `.p`, `.def` and `.lib` files, on x86-64 and on
AArch64, and on x86-64 also with AddressSanitizer and UBSan. Outputs
(`-o`, `-d`) and exit status are byte-identical, apart from the
effects of the fixes above; the `-v` statistics differ (VM pages and
memory sizes: the structures are larger on LP64, and every VM page
stays resident).

## The other tools

### Sources

"RCS" is the revision of the file's RCS history that the working file
equals; "RCS+" means the working file has later, uncommitted edits on
top of that revision.

| files | RCS |
|---|---|
| cmds/ampp/ampp.c macro.c lex.c sym.c builtin.c defs.c putback.c charstack.c output.c ampp.h | 1.11 1.4 1.5 1.3 1.10 1.2 1.6 1.2 1.5 1.6 |
| cmds/support/queue.c queue.h ttypes.h getargs.c getargs.h | 1.1 1.2 1.1 1.1 1.1 |
| cmds/com/sysbuild.c | 1.13+ (an "AM" comment) |
| cmds/com/objed.c | 1.15 |
| cmds/com/c.c | 1.137+ (Perihelion 1994 working copy) |
| cmds/hostutil/gencodes.c | 1.10 |
| cmds/hostutil/squash.c | 1.2 |
| cmds/hostutil/newmotd.c | 1.11+ (AxM banner, y2k) |
| filesys/devs/b422/gensquirt.c | 1.2+ |
| tools/hosttools/include/module.h | excerpt of include/module.h 1.14 |

### ampp

- `adddef()` takes its value as an `intptr_t` and stores a macro or a
  builtin function in the pointer member of the `Value` union (the
  callers cast the pointers to `INT`, which no longer holds one).
- `struct Symbol` `name[33]`: `cps()` stores 32 characters and a NUL.
  The 1990 overflow by one byte corrupts the i386 heap on some inputs.
- symbol hash on 32 bits (`UWORD`, was `unsigned long`; a negative
  `char` made the 64-bit sum differ).
- printf format attributes on error/warn/report/_trace/recover; `%d`
  for `INT` line numbers, `%p` for the pointers in `-t` trace output.
- division by zero and MinInt / -1 in `_div`/`_mod` raise SIGFPE on
  every host, as the x86 division instruction did (AArch64 does not
  trap).
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
  longer than 256 bytes gave the size of its last piece) and gives 0
  for an empty text (it read the list header as a piece).
- Inputs within the old limits give the same output as before; longer
  ones now work.
- `_report` prints its text with `"%s"` (the text was the printf
  format: a `%` in it printed garbage or crashed, also on i386).

On kernel/ARM/hw_glapa.a (an ARM file, not part of the TRAN build) the
i386 build aborts in malloc (exit 134, empty output), its heap
corrupted by the `name[32]` overflow; the port completes with the same
exit status 10 as the other ARM files.

### sysbuild

- `WORD` from ttypes.h: the image header is read as three 32-bit words
  and the offset table written as 32-bit words (an unported LP64 build
  writes a 40-byte nucleus and exit status 0); `%d`/`%x` formats;
  format attribute on `error`.
- the image size from a part's header is checked against the bytes the
  file has after its header and against the space left in the 1 MB
  buffer: a negative size or one near 2^31 (where `isize + fsize + 3`
  wraps past the size test) made fread() write past the buffer. Where
  the read used to come up short the message is the same ("Bad header
  in file ...").
- the image buffer comes from calloc(): the alignment padding of a part
  whose size is not a multiple of 4 was never written (zero only
  because glibc serves 1 MB with a fresh mapping). The nucleus parts of
  this tree are all multiples of 4; output unchanged.

### objed

- the host module.h; magic numbers compared as `UWORD` (were
  `unsigned long`, which sign extension broke), `progtype` a `WORD`;
  `%d`/`%x` formats; the image type string printed with `"%s"`;
  `-s`/`-h` values saturate at the 32-bit limits as `atol()` did on
  i386.
- `-m` walks the module chain with bounds: a module size of 0 or less,
  or one past the image, a chain without its terminating 0 word and a
  negative image size are errors (exit 1); they looped for ever, read
  past the buffer, or printed nothing. Module names are printed with
  `%.32s` (a name need not end in a NUL). swapword() copies with
  memcpy() (big-endian hosts only).

### gencodes

- `<stdarg.h>` (`<varargs.h>` is gone), the C library headers instead
  of `extern char *malloc()`, prototypes on every host.
- `tfprintf()` fetches a `%s` argument as `char *` (it fetched every
  argument as `int`, which truncates a pointer on LP64).
- `strtonum()`: `digit` declared outside the loop and initialised (a
  non-digit character used an uninitialised value).
- No fixed-size buffers: tfprintf's `fbuf[100]`/`pbuf[100]` (a message
  of about 100 characters overflowed `pbuf`) and the `line[1000]`/
  `classline[1000]` of fdbscan grow or are measured; lines of any
  length are read whole.
- `classpfix` starts as `"(null)"`: a code line before the first class
  line (or the rest of a line after a 0xFF byte, which rdch() returns
  as EOF) used it uninitialised; the i386 build printed its NULL as
  `(null)`, an optimised 64-bit build printed stack garbage.
- A class mask of 0 is an error (exit 1); it looped forever.

### squash

- `getline()`, with the newline removed, instead of `gets()` (removed
  in C11): a line of any length is read whole. (`gets()` into
  `char line[100]` overflowed the stack beyond 99 characters; a
  100-byte `fgets()` would split such a line, and a piece starting
  with `#{`/`#}` would toggle copying.)

### newmotd

- `fputs(lbuff, stdout)`, not the line as a printf format.
- the centring computes `80 - strlen()` as an `int` (as `size_t`, a
  line over 80 characters gave a huge count; none is that long).

### c

- printf format attributes on error/warning/fail; the `execlp()`
  terminator is `(char *)0` (an `int` 0 is 4 bytes in a variadic call
  on LP64); the queue prototypes from queue.h.
- No fixed-size buffers for paths and arguments: AddLibrary's
  `buf[100]` (every run overflowed it once C_LIBDIR plus
  "helioslib.def" reached 100 characters, i.e. a lib dir of more than
  about 86: the i386 build wrote into the next static and carried on, a
  64-bit FORTIFY build aborts), the `[ARGBUFSIZE]` (1024) buffers of
  AddNamef, AddLibraryName, AddInclude, AddSymbol, AddLibraryPath,
  RemoveLibraryPath, AddIncludeOpts (all `-I` directories in one
  argument), AddSymbolOpts and the `cat` of `-j` sources, the 1024-byte
  `-p` command lines, the 200-entry argv of a `-p` command and the
  256-byte FORTRAN pass options are allocated to size (StrFormat,
  StrBuf). The strings passed on are unchanged.
- An empty directory in `-I`/`-L` gets no `/` (as before), without
  reading the byte before the buffer; the usage text is printed with
  `"%s"` and a failing `pipe()` leaves it unpaged.

Lengths: c and ampp have no limit on path, token or argument lengths,
gencodes and squash none on line lengths (c had 100 characters for a
library path, ampp 128 for an include path and a token).

### gensquirt

- `unsigned char c`, so a byte >= 0x80 becomes one `\xNN` escape on
  every host. This is the output of the RS/6000 build that made the
  shipped squirtdata.c; with a signed `char` (i386, x86-64) the bytes
  became out-of-range escapes such as `\x0ffffffb1`. `main` returns
  int. The output is byte-identical to the shipped
  filesys/devs/b422/TRAN/squirtdata.c.

### Comparison with the i386 builds

The tools were compared with the i386 builds of the same sources (gcc
-m32 -O0) on every input of logged tree builds, with Helios C and with
clang, plus extra cases: output files, stdout/stderr and exit status
byte for byte, on x86-64 with -O2, with AddressSanitizer and UBSan and
with `-funsigned-char` in CFLAGS, and with static AArch64 tools. All
are identical, except ampp on kernel/ARM/hw_glapa.a (see ampp). The
two nuclei that sysbuild builds are identical to the builds'
nucleus.srv.

A complete build of the tree with only 64-bit host programs (asm,
objdump and these tools) gives the same products as one with the i386
tools, apart from yacc (cmds/public/yacc-1.4), which embeds its build
directory in its assert strings.

## Tests

`make check` runs both test suites.

- tests/asm/run.sh ASM: one or more tests per asm fix (t-*.sh), each in
  a fresh temporary directory.
- tests/edge/edge.sh BINDIR OUTDIR [--update]: 44 cases on inputs
  beyond the old limits or malformed (in tests/edge/in; binary images
  made by gen.py): long identifiers, numbers, `_report` and `_include`
  texts, include directories of more than 128 characters, `%` in
  `_report`, `_strsize` of 600 and of 0 characters, division traps;
  gencodes with a long message, a long line, no class line, a 0xFF
  byte, a zero mask; squash with long lines; sysbuild with short,
  negative, huge and wrapping sizes and odd-sized parts (zero padding);
  objed with a size-0 module, a chain without its 0 word, a size past
  the image, a negative image size, unterminated names, a short header;
  c with a 160-character lib dir, `-L<long> -lfoo`, ten long `-I`
  directories (with and without `-p`), a 1500-character `-D`, 250
  arguments through `-p`. c runs the stub compiler, assembler and ampp
  of tests/c-stub, which log their argv. The results are compared with
  tests/edge/expected. The i386 builds of the original sources fail or
  differ on many of these inputs (crashes, hangs, garbage beyond the
  old limits).
