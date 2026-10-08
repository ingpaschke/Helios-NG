#!/bin/bash
# edge.sh BINDIR OUTDIR [--update] -- regression tests on the edge cases of
# the 2026-10-08 review (REVIEW-HOSTTOOLS64.md): over-long tokens, paths,
# lines and messages, bad image sizes, broken module chains, a '%' in a
# _report, classless code lines, a zero class mask.
#
# Each case records its exit status, stdout+stderr and output files under
# OUTDIR/CASE/, with OUTDIR replaced by OUT; these are compared with
# expected/CASE/. --update writes expected/ instead (check the diff!).
# Exits 1 on a difference. The i386 reference tools fail or differ on
# most of these inputs by design: run them only to see what changed.
set -u
B=$(cd "$1" && pwd) || exit 2; O=$2; UPDATE=${3:-}
rm -rf "$O"; mkdir -p "$O"; O=$(cd "$O" && pwd)
E=$(cd "$(dirname "$0")" && pwd); IN=$E/in; X=$E/expected
D=$(cd "$E/../c-stub" && pwd)
# SIGFPE of _div/_mod must reach the default action under ASan too
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0:handle_sigfpe=0
export UBSAN_OPTIONS=print_stacktrace=1
TO="timeout 10"
ulimit -c 0
ulimit -f 4096	# 4 MB per file: the old objed printed a size-0 module for ever
G=$O/gen; mkdir -p $G; python3 $E/gen.py $G
LONG=$O/$(printf 'd%.0s' $(seq 1 120))/$(printf 'e%.0s' $(seq 1 60))	# > 128 characters

# run NAME CMD...: in a fresh OUTDIR/NAME, record rc and output
run() {
  local n=$1; shift
  mkdir -p $O/$n
  ( cd $O/$n && $TO "$@" > out 2>&1; echo $? > rc ) 2>/dev/null
  # (qemu-user reports a fatal signal of the program itself)
  VERIFY_OUT=$O perl -ni -e 's/\Q$ENV{VERIFY_OUT}\E/OUT/g; print unless /^qemu: uncaught target signal/' $O/$n/out
}

# ---- ampp
for f in $IN/ampp-*.a; do
  n=$(basename $f .a)
  case $n in ampp-ti) continue;; esac
  mkdir -p $O/$n; cp $f $O/$n/in.a
  run $n $B/ampp -o out.s in.a
done
mkdir -p $LONG; cp $IN/ampp-inc.m $LONG/inc.m
mkdir -p $O/ampp-ti; cp $IN/ampp-ti.a $O/ampp-ti/in.a
run ampp-ti $B/ampp -i$LONG -o out.s in.a		# _include through a long -i dir
run ampp-longinput $B/ampp -i$LONG -o out.s inc.m	# input file found through it

# ---- gencodes
for f in $IN/gencodes-*.fdb; do
  n=$(basename $f .fdb); run $n $B/gencodes $f
done

# ---- squash
mkdir -p $O/squash-long
( cd $O/squash-long && $TO $B/squash < $IN/squash-long.in > out 2>&1; echo $? > rc )

# ---- sysbuild
for p in ok short neg big ovf; do
  run sysbuild-$p $B/sysbuild out.nuc $G/sb-$p.i
done
run sysbuild-pad $B/sysbuild out.nuc $G/sb-ok.i $G/sb-ok8.i $G/sb-ok.i	# zero padding

# ---- objed
for i in good zerosize noterm pastend negsize longname short; do
  run objed-$i $B/objed -i -m $G/ob-$i
done

# ---- c, with stub compiler/assembler (see ../run.sh)
export C_COMPILER=$D/nc ASSEMBLER=$D/asm LINKER=$D/asm AMPP=$D/ampp MACRO=$D/ampp
export C_INCDIR=$LONG/include/ MACDIR=$LONG/include/ampp/ VERIFY_OUT=$O VERIFY_CTREE=$LONG
export PATH=$D:$PATH
crun() {	# crun NAME LIBDIR ARGS...
  local n=$1; export C_LIBDIR=$2; shift 2
  export CLOG=$O/$n/log; mkdir -p $O/$n; touch $O/$n/foo.c $O/$n/bar.o
  run $n $B/c "$@"
}
mkdir -p $LONG/lib $LONG/lib2
touch $LONG/lib/helioslib.def $LONG/lib/helios.lib $LONG/lib/c.lib $LONG/lib/cstart.o $LONG/lib2/foo.lib
crun c-longlibdir $LONG/lib/ -c foo.c			# H1: every run did AddLibrary
crun c-longL $LONG/lib/ -o prog bar.o -L$LONG/lib2 -lfoo	# -L<long> -lfoo
I10=""; for k in 0 1 2 3 4 5 6 7 8 9; do I10="$I10 -I$LONG/inc$k"; done
crun c-manyI $LONG/lib/ -c $I10 foo.c			# -i list over 1024 characters
crun c-pipe $LONG/lib/ -p -c $I10 foo.c			# -p command line over 1024
crun c-longD $LONG/lib/ -c -DX$(printf 'x%.0s' $(seq 1 1500)) foo.c
crun c-manyargs $LONG/lib/ -p -c $(for k in $(seq 1 250); do printf -- '-DD%d ' $k; done) foo.c
for n in c-longlibdir c-longL c-manyI c-pipe c-longD c-manyargs; do
  [ -s $O/$n/log ] || echo "no stub log" >> $O/$n/out
done

rm -rf $O/gen $O/d*
# ---- compare (or update)
if [ "$UPDATE" = --update ]; then
  rm -rf $X; mkdir -p $X
  (cd $O && tar cf - --exclude='in.a' --exclude='*.c' --exclude='*.o' .) | (cd $X && tar xf -)
  echo "expected/ updated from $O"; exit 0
fi
fails=0; n=0
for c in $(cd $X && ls); do n=$((n+1))
  if diff -r -x in.a -x '*.c' -x '*.o' $X/$c $O/$c > /dev/null 2>&1; then :
  else echo "FAIL $c"; fails=$((fails+1)); fi
done
for c in $(cd $O && ls); do [ -e $X/$c ] || { echo "FAIL $c (no expected result)"; fails=$((fails+1)); }; done
echo "edge: $n cases, $fails failing"
[ $fails = 0 ]
