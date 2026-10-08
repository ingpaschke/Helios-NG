# A3: an ignored signal stays ignored (nohup, background jobs).
# asm reads base.s from a pipe that stays open for 2 s, so the signal
# comes while it runs with its temporary file open.
slowin() { { cat base.s; sleep 2; } }
(
	trap '' INT HUP
	slowin | "$ASM" -N -o a3.out - > a3.log 2>&1 &
	p=$!
	sleep 1
	kill -INT $p 2>/dev/null; kill -HUP $p 2>/dev/null
	wait $p
	echo $? > a3.rc
)
if [ "$(cat a3.rc)" = 0 ] && [ -s a3.out ]; then ok; else bad "a3-sigign: exit $(cat a3.rc)"; fi
# a signal that is not ignored still removes the temporary file
slowin | "$ASM" -N -o a3t.out - > a3t.log 2>&1 &
p=$!
sleep 1
kill -TERM $p 2>/dev/null
wait $p 2>/dev/null
rc=$?
if [ $rc = 143 ]; then ok; else bad "a3-term: exit $rc, expected 143"; fi
if ls a3t.out* >/dev/null 2>&1; then bad "a3-term: output or temporary file left"; else ok; fi
