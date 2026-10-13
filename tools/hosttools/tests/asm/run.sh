#!/bin/sh
# Regression tests for the asm fixes in tools/hosttools/PORT.md (Fixes).
#
#   tests/asm/run.sh [ASM]      default: the asm beside this directory
#   make check
#
# Each t-*.sh file here is sourced in a fresh temporary directory that
# holds base.s (a module that links on its own) and the helpers below.
# Prints one line per failure and a summary; exits 1 if a test failed.

D=$(cd "$(dirname "$0")" && pwd)
ASM=${1:-$D/../../build/asm}
case $ASM in /*) ;; *) ASM=$(pwd)/$ASM ;; esac
T=$(mktemp -d "${TMPDIR:-/tmp}/asmtest.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2

pass=0
fail=0
ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL: $*"; }

# expect NAME RC CMD... : run CMD (stdout and stderr to NAME.log), check rc
expect() {
	n=$1; want=$2; shift 2
	"$@" > "$n.log" 2>&1
	got=$?
	if [ "$got" = "$want" ]; then ok
	else bad "$n: exit $got, expected $want"; sed 's/^/    /' "$n.log" | head -5
	fi
}

# nofile NAME FILE : FILE must not exist
nofile() { if [ -e "$2" ]; then bad "$1: $2 exists"; else ok; fi; }

# same NAME A B : files A and B must be identical
same() { if cmp -s "$2" "$3"; then ok; else bad "$1: $2 and $3 differ"; fi; }

# grepq NAME PATTERN FILE : FILE must contain PATTERN
grepq() { if grep -q -e "$2" "$3"; then ok; else bad "$1: no '$2' in $3"; fi; }

printf '\tmodule -1\n.start:\n\tldc 1\n\tj .lab\n\tldc 2\n.lab:\n\tldc .lab - .start\n' > base.s
expect base 0 "$ASM" -N -o base.img base.s
expect base-p 0 "$ASM" -p -o base.p base.s

for t in "$D"/t-*.sh; do
	[ -f "$t" ] || continue
	rm -f ./*.out ./*.log
	. "$t"
done

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
