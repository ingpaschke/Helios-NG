# A6: a signal at any point, also while the output is renamed or the
# temporary file removed, leaves either the complete output or nothing.
# (The race of A6 itself cannot be hit on purpose; this is a smoke test.)
awk 'BEGIN { print "\tmodule -1"; for (i = 0; i < 8000; i++) printf "l%d: ldc l%d - l0\n", i, i }' > a6.s
"$ASM" -N -o a6ref.out a6.s
mkdir a6d
i=0
while [ $i -lt 40 ]; do
	"$ASM" -N -o a6d/o a6.s 2>/dev/null &
	p=$!
	sleep 0.0$((i % 10))$((i / 10)) 2>/dev/null || sleep 1
	kill -TERM $p 2>/dev/null
	wait $p 2>/dev/null
	for f in a6d/*; do
		[ -e "$f" ] || continue
		if [ "$f" = a6d/o ] && cmp -s a6d/o a6ref.out; then rm -f a6d/o; continue; fi
		bad "a6-race: $f left (run $i)"; rm -f "$f"
	done
	i=$((i+1))
done
ok
