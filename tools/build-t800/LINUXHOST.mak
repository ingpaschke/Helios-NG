#-----------------------------------------------------------------------------
# Linux (x86-64 or AArch64) overrides for the Helios-NG transputer build with
# the llvm-t800 toolchain.  tools/build-t800/build.sh includes this at the end
# of makeinc/SUN4.mak in its copy of the tree, so the SUN4 host make logic
# (HHOST=SUN4) is used unchanged except for what is set here.
# Not part of the original Helios sources.
#-----------------------------------------------------------------------------

# host C compiler: native gcc, with the flags the 32-bit-clean host tool
# sources need (tools/hosttools/Makefile).  No -DHOSTISBIGENDIAN (SUN4.mak's,
# for the big-endian SPARC host): replaced here.
HOSTCC		= gcc
HOSTCFLAGS	:= -std=gnu99 -fsigned-char -fwrapv -fno-strict-aliasing \
		   -D$(HHOST) -D__$(HPROC) -D__HELIOS$(HPROC)

# the C compiler is clang through hcc, which takes Helios C's command line;
# the transputer tools are the 64-bit host tools built from tools/hosttools.
# Programs link with the scanned compiler-rt library (soft float) after
# their objects (asm-rt).
NC		= $(HCC)
ASM		= $(HHOSTBIN)/asm
LINK		= $(BUILDT800)/asm-rt
AMPP		= $(HHOSTBIN)/ampp
OBJED		= $(HHOSTBIN)/objed
SYSBUILD	= $(HHOSTBIN)/sysbuild
CPP		= cpp -traditional-cpp -P

# one private temp dir per component directory, so make -j across
# components cannot collide on $(TEMP)/foo.s
TEMP		:= $(BUILDOUT)/tmp/$(subst /,_,$(COMPONENT))_$(notdir $(CURDIR))
$(shell mkdir -p $(TEMP))

# Modern GNU make discards the vpath directory of a prerequisite that has to
# be "remade" by a recipe-less rule (fault.c: faults) and then looks for
# ./fault.c.  The 1992 gmake kept the vpath name; GPATH restores that.
GPATH		= $(HVPATH)
