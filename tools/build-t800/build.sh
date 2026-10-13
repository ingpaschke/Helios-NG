#!/bin/bash
# build.sh -- build Helios-NG for the transputer on Linux with the llvm-t800
# toolchain (clang through hcc, the Helios C command line).
#
# Usage: tools/build-t800/build.sh [OUT]
#
#   OUT     build directory (default: build-t800 beside this checkout's
#           top directory); OUT/prod/TRAN is the resulting Helios tree
#           (bin, lib, include, etc.), OUT/logs one log per component
#
# Environment:
#   T800    the llvm-t800 installation (default /opt/llvm-t800): its
#           bin/hcc and bin/clang compile, and the compiler-rt library
#           sysroots/helios/lib/llvmrt-generic.lib is linked into programs
#   JOBS    make -j inside each component (default 1)
#
# Needs: gcc and GNU make for the host tools, python3, and llvm-t800 with
# its Helios add-on (the llvm-t800-helios package, for llvmrt-generic.lib).
#
# The steps: copy the tree to OUT/src (without tools/ and Perihelion's 1994
# build products), add LINUXHOST.mak to makeinc/SUN4.mak there, build the
# 64-bit host tools (tools/hosttools) into OUT/host, then build the
# components in the order of the top-level makefile.  The checkout itself is
# not modified.
set -eu

BUILDT800=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$BUILDT800/../.." && pwd)
OUT=${1:-$REPO/build-t800}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
T800=${T800:-/opt/llvm-t800}
JOBS=${JOBS:-1}

[ -x "$T800/bin/hcc" ] && [ -x "$T800/bin/clang" ] ||
	{ echo "build.sh: no llvm-t800 in $T800 (set T800)" >&2; exit 1; }
RTLIB=$T800/sysroots/helios/lib/llvmrt-generic.lib
[ -f "$RTLIB" ] ||
	{ echo "build.sh: no $RTLIB (install llvm-t800-helios)" >&2; exit 1; }

export BUILDT800 BUILDOUT=$OUT
export HPROC=TRAN HHOST=SUN4 HLICENSEE=PERIHELION
export HSRC=$OUT/src HPROD=$OUT/prod/TRAN HHOSTBIN=$OUT/host/bin
export HCC=$T800/bin/hcc HCC_CLANG=$T800/bin/clang HCC_RTLIB=$RTLIB
export HCC_OPT=${HCC_OPT:-s}
export PATH=$HHOSTBIN:$PATH
# the Helios `c` driver (cmds/com/c.c), used by some components
export C_COMPILER=$HCC ASSEMBLER=$HHOSTBIN/asm LINKER=$HHOSTBIN/asm
export C_INCDIR=$HSRC/include/ C_LIBDIR=$HPROD/lib/ MACDIR=$HSRC/include/ampp/
LOGS=$OUT/logs

echo "build.sh: Helios-NG $REPO -> $OUT"
rm -rf "$HSRC" "$OUT/prod" "$OUT/tmp" "$LOGS"
mkdir -p "$LOGS" "$HHOSTBIN"

# the source tree
python3 "$BUILDT800/headco.py" "$REPO" "$HSRC" > "$LOGS/checkout.txt"
cp "$BUILDT800/LINUXHOST.mak" "$HSRC/makeinc/"
printf '\n# tools/build-t800: Linux host overrides\ninclude $(HSRC)/makeinc/LINUXHOST.mak\n' \
	>> "$HSRC/makeinc/SUN4.mak"

# the host tools
make -C "$REPO/tools/hosttools" BUILDDIR="$OUT/host/build" > "$LOGS/hosttools.txt" 2>&1 ||
	{ echo "build.sh: host tools failed, see $LOGS/hosttools.txt" >&2; exit 1; }
for t in asm objdump ampp sysbuild objed gencodes squash newmotd c; do
	cp "$OUT/host/build/$t" "$HHOSTBIN/"
done
# Helios C itself is not used: a guard, so that nothing finds netcat
printf '#!/bin/sh\necho "nc: Helios C is not part of this build (hcc is)" >&2\nexit 1\n' \
	> "$HHOSTBIN/nc"
chmod +x "$HHOSTBIN/nc"

failed=0
mk() {	# mk DIR [target]
	local d=$1 t=${2:-install}
	local log=$LOGS/$(echo "$d" | tr / _)${2:+-$2}.txt
	[ -f "$HSRC/$d/TRAN/makefile" ] || return 0
	if make -k -j"$JOBS" -C "$HSRC/$d/TRAN" $t > "$log" 2>&1; then
		echo "  $d $t"
	else
		echo "  $d $t FAILED (see $log)"
		failed=$((failed + 1))
	fi
}

# the order of the top-level makefile (LibHdrs Nucleus Libraries Network
# Commands ...), expanded to directories
DEFDIRS="kernel util nucleus fault posix fplib cmds/cc/clib"
CORE="fault kernel util posix cmds/cc/clib fplib nucleus servers/servtask nucleus"
LIBS="scanlibs scanlibs/bsd scanlibs/termcap scanlibs/curses scanlibs/vectlib"
NET="network network/packages/farmlib network/packages/tftests network/packages/hwtests1"
SERVERS="servers servers/common servers/ttyserv servers/ttyserv/devs
 servers/ttyserv/ttyserv servers/lock servers/include servers/logger servers/familiar"
CMDS="cmds/support cmds/shell cmds/com cmds/textutil cmds/private cmds/ampp cmds/asm
 cmds/cc/compiler sa cmds/cdl cmds/make cmds/help cmds/emacs cmds/emacs.311
 cmds/gnu/gmake cmds/gnu/tar cmds/gnu/gtar cmds/debugger cmds/debugger/lib"
TCPIP="tcpip tcpip/helios tcpip/net tcpip/netinet tcpip/sys tcpip/rpc/rpc tcpip/nfs
 tcpip/cmds/ftp tcpip/cmds/ftpd tcpip/cmds/inetd tcpip/cmds/ping tcpip/cmds/rexecd
 tcpip/cmds/rlogin tcpip/cmds/rsh tcpip/cmds/rshd tcpip/cmds/telnet tcpip/cmds/telnetd
 tcpip/cmds/rcp tcpip/cmds/rlogind tcpip/cmds/binmail tcpip/cmds/sendmail tcpip/cmds/lpr
 tcpip/devs/pc-ether tcpip/devs/in-ether tcpip/devs/sq-ether tcpip/devs/ims-b431"
HFS="filesys filesys/fs filesys/cmds filesys/devs/raw filesys/devs/m212
 filesys/devs/he1000 filesys/devs/msc filesys/devs/b422"
EXTRA="servers/msdosfs demos cmds/public cmds/gnu
 filesys/ufs filesys/ufs/cmds filesys/ufs/fsck filesys/ufs/devs/raw filesys/ufs/devs/he1000"

mkdir -p "$HPROD"
(cd "$HSRC" && make MakeProdDir) > "$LOGS/mkprod.txt" 2>&1
# include/{codes,errno,signal}.h from fault/master.fdb with the host
# gencodes, as fault's install would
mkdir -p "$OUT/tmp/gencodes"
(cd "$OUT/tmp/gencodes" && gencodes "$HSRC/fault/master.fdb" > /dev/null &&
 cp codes.h errno.h signal.h "$HSRC/include/")
echo "build.sh: library definitions"
for d in $DEFDIRS; do mk "$d" installdef; done
(cd "$HSRC" && make Linklibs) > "$LOGS/linklibs.txt" 2>&1
echo "build.sh: nucleus and libraries"
for d in $CORE; do mk "$d"; done
(cd "$HSRC" && make Linklibs) >> "$LOGS/linklibs.txt" 2>&1
echo "build.sh: the rest"
for d in $LIBS $NET $SERVERS $CMDS $TCPIP $HFS $EXTRA; do mk "$d"; done

echo "build.sh: $HPROD ($failed components with errors)"
