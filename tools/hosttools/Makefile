# Makefile -- the Helios-NG build-host tools for any 32- or 64-bit,
# little-endian Unix host (x86-64 and AArch64 Linux tested), built from
# their sources in this tree.  See README.md.
#
#   make                          native build into ./build
#   make BUILDDIR=/tmp/b CFLAGS='-O1 -g -fsanitize=address,undefined'
#   make CC=aarch64-linux-gnu-gcc LDFLAGS=-static
#                                 cross build: the tools for AArch64, the
#                                 generators for this (the build) machine
#   make install DESTDIR=... PREFIX=...
#                                 the tools; the generators only when they
#                                 are built for the same machine as the tools
#   make check                    regression tests (tests/asm, tests/edge)
#
# Tools:
#   asm       assembler/linker                         (cmds/asm)
#   objdump   .p/.o dumper                             (cmds/asm)
#   ampp      macro preprocessor for .a files          (cmds/ampp)
#   sysbuild  nucleus builder                          (cmds/com)
#   objed     image header editor                      (cmds/com)
#   gencodes  fault database -> codes.h errno.h signal.h (cmds/hostutil)
#   squash    fault database -> etc/faults             (cmds/hostutil)
#   newmotd   etc/motd                                 (cmds/hostutil)
#   c         Unix-hosted cc driver                    (cmds/com)
# Generators, built with BUILD_CC for the machine that runs make (the build
# machine): they write C sources (keytab.c, squirtdata.c), which is all
# they are for; nothing runs them on the machine the tools are built for.
#   keyform   keyword table keytab.c of asm            (cmds/asm)
#   gensquirt squirtdata.c of the B422 driver          (filesys/devs/b422)
#
# The same sources are also compiled for the transputer by the Helios
# make system (cmds/asm, cmds/ampp, cmds/com TRAN): the host-only parts
# are under #ifndef __HELIOS.

CC		?= cc
# the build machine's compiler, whatever CC is (not $(CC): a cross CC
# would make generators that cannot run here)
BUILD_CC	?= cc
CFLAGS		?= -O2 -g
BUILD_CFLAGS	?= -O2
CPPFLAGS	?=
LDFLAGS		?=
LDLIBS		?=
BUILD_LDFLAGS	?=
BUILDDIR	?= build
PREFIX		?= /usr/local
BINDIR		?= $(PREFIX)/bin
INSTALL		?= install
OBJDUMP_NAME	?= objdump

# What the code needs, whatever CFLAGS says (so it comes after CFLAGS, where
# a later -std= or -funsigned-char cannot override it):
#  -std=gnu99            old-style (K&R) function definitions; C23 dropped them
#  -fsigned-char         char is signed on the original hosts and on i386,
#                        the reference build; AArch64 defaults to unsigned
#  -fwrapv               32-bit arithmetic wraps, as on the original hosts
#  -fno-strict-aliasing  words are read through cast pointers
REQ_CFLAGS	= -std=gnu99 -fsigned-char -fwrapv -fno-strict-aliasing
# -Wall -Wextra, less the warnings about the style of the 1990 code
WARN_CFLAGS	= -Wall -Wextra -Wno-sign-compare -Wno-implicit-fallthrough \
		  -Wno-parentheses -Wno-unused-but-set-variable \
		  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function

ALL_CFLAGS	= $(CPPFLAGS) $(WARN_CFLAGS) $(CFLAGS) $(REQ_CFLAGS)
GEN_CFLAGS	= $(WARN_CFLAGS) $(BUILD_CFLAGS) $(REQ_CFLAGS)

# Install the generators only when they run where the tools run: a cross
# build would otherwise put build-machine programs next to the tools.
TARGET_MACHINE	:= $(shell $(CC) -dumpmachine 2>/dev/null)
BUILD_MACHINE	:= $(shell $(BUILD_CC) -dumpmachine 2>/dev/null)
ifeq ($(TARGET_MACHINE),$(BUILD_MACHINE))
INSTALL_PROGRAMS = $(PROGRAMS)
else
INSTALL_PROGRAMS = $(TOOLS)
endif

# The host defines of the original SUN4 host build, less -DHOSTISBIGENDIAN
# and -DBYTE_SEX_ODD (big-endian host only)
HOSTDEFS	= -DSUN4
TRANDEFS	= -DSUN4 -D__TRAN -D__HELIOSTRAN

# the tree
TOP		:= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
S		= $(TOP)/cmds/support
A		= $(TOP)/cmds/asm
P		= $(TOP)/cmds/ampp
COM		= $(TOP)/cmds/com
HU		= $(TOP)/cmds/hostutil
B422		= $(TOP)/filesys/devs/b422
# Helios's include/module.h with 32-bit fields, for objed and sysbuild
# (the tree's include/module.h has a long word, 64 bits on LP64)
HINC		= $(TOP)/tools/hosttools/include

TOOLS		= asm objdump ampp sysbuild objed gencodes squash newmotd c
GENERATORS	= keyform gensquirt
PROGRAMS	= $(TOOLS) $(GENERATORS)

ASM_OBJS	= asm gencode genimage growdata growcode keytab getargs objed \
		  lex encode mem module preasm sym syn vm
ASM_OBJECTS	= $(addprefix $(BUILDDIR)/asm.d/,$(addsuffix .o,$(ASM_OBJS)))
ASM_HDR		= $(A)/asm.h $(A)/vm.h $(A)/ttypes.h $(A)/getargs.h $(A)/module.h
# the asm sources: -DSUN4 -DVM, the Unix host code paths with the paged
# (VM) symbol and code store; -fno-builtin-exp: syn.c has its own exp()
ASM_CFLAGS	= $(HOSTDEFS) -DVM -fno-builtin-exp -I$(A) -I$(BUILDDIR)/asm.d

AMPP_SRC	= $(P)/ampp.c $(P)/macro.c $(P)/lex.c $(P)/sym.c $(P)/builtin.c \
		  $(P)/defs.c $(P)/putback.c $(P)/charstack.c $(P)/output.c \
		  $(S)/queue.c
AMPP_HDR	= $(P)/ampp.h $(S)/ttypes.h $(S)/queue.h

.PHONY: all clean install check
all: $(addprefix $(BUILDDIR)/,$(PROGRAMS))

$(BUILDDIR) $(BUILDDIR)/asm.d:
	mkdir -p $@

$(BUILDDIR)/asm: $(ASM_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(ASM_OBJECTS) $(LDLIBS)

$(BUILDDIR)/asm.d/%.o: $(A)/%.c $(ASM_HDR) | $(BUILDDIR)/asm.d
	$(CC) $(ALL_CFLAGS) $(ASM_CFLAGS) -c -o $@ $<

$(BUILDDIR)/asm.d/keytab.o: $(BUILDDIR)/asm.d/keytab.c $(ASM_HDR)
	$(CC) $(ALL_CFLAGS) $(ASM_CFLAGS) -c -o $@ $<

$(BUILDDIR)/asm.d/lex.o: $(A)/hash.c

# keyform writes keytab.c into the current directory. The table is a list
# of 32-bit words holding bytes in the build host's order: build and
# target host must have the same byte order.
$(BUILDDIR)/asm.d/keytab.c: $(BUILDDIR)/keyform | $(BUILDDIR)/asm.d
	cd $(BUILDDIR)/asm.d && $(abspath $(BUILDDIR))/keyform > keyform.log

$(BUILDDIR)/objdump: $(A)/objdump.c $(ASM_HDR) | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ASM_CFLAGS) -o $@ $(A)/objdump.c \
		$(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/keyform: $(A)/keyform.c $(A)/hash.c $(ASM_HDR) | $(BUILDDIR)
	$(BUILD_CC) $(GEN_CFLAGS) $(HOSTDEFS) -DVM -I$(A) -o $@ \
		$(A)/keyform.c $(BUILD_LDFLAGS)

$(BUILDDIR)/ampp: $(AMPP_SRC) $(AMPP_HDR) | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(HOSTDEFS) -I$(P) -I$(S) -o $@ $(AMPP_SRC) \
		$(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/sysbuild: $(COM)/sysbuild.c $(S)/getargs.c $(S)/getargs.h \
		$(HINC)/module.h $(S)/ttypes.h | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(TRANDEFS) -I$(HINC) -I$(S) -o $@ \
		$(COM)/sysbuild.c $(S)/getargs.c $(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/objed: $(COM)/objed.c $(HINC)/module.h $(S)/ttypes.h | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(TRANDEFS) -I$(HINC) -I$(S) -o $@ $(COM)/objed.c \
		$(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/gencodes: $(HU)/gencodes.c | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(HOSTDEFS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/squash: $(HU)/squash.c | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(HOSTDEFS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/newmotd: $(HU)/newmotd.c | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(HOSTDEFS) -o $@ $< $(LDFLAGS) $(LDLIBS)

# c: -DUNIX selects the Unix-hosted driver
$(BUILDDIR)/c: $(COM)/c.c $(S)/queue.c $(S)/queue.h | $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) -DUNIX $(TRANDEFS) -I$(S) -o $@ \
		$(COM)/c.c $(S)/queue.c $(LDFLAGS) $(LDLIBS)

$(BUILDDIR)/gensquirt: $(B422)/gensquirt.c | $(BUILDDIR)
	$(BUILD_CC) $(GEN_CFLAGS) $(HOSTDEFS) -o $@ $< $(BUILD_LDFLAGS)

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	for p in $(INSTALL_PROGRAMS); do \
	  n=$$p; [ $$p = objdump ] && n=$(OBJDUMP_NAME); \
	  $(INSTALL) -m 755 $(BUILDDIR)/$$p $(DESTDIR)$(BINDIR)/$$n || exit 1; \
	done
ifneq ($(TARGET_MACHINE),$(BUILD_MACHINE))
	@echo "not installed: $(GENERATORS) (built for $(BUILD_MACHINE), the tools for $(TARGET_MACHINE))"
endif

# tests/asm: the asm fixes; tests/edge: edge cases of the other tools
check: all
	sh tests/asm/run.sh $(abspath $(BUILDDIR))/asm
	bash tests/edge/edge.sh $(BUILDDIR) $(BUILDDIR)/edge.out

clean:
	rm -rf $(BUILDDIR)
