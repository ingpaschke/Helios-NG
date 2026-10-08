# A5: SIGQUIT, SIGPIPE and SIGXFSZ remove the temporary file too
# A background job of sh starts with SIGINT and SIGQUIT ignored (and
# asm keeps them so, A3): env --default-signal resets them, if it can.
slowin() { { cat base.s; sleep 2; } }
dfl=
env --default-signal=QUIT true 2>/dev/null && dfl="env --default-signal=INT,QUIT"
sigs="PIPE XFSZ"; [ -n "$dfl" ] && sigs="QUIT $sigs"
ulimit -c 0 2>/dev/null
for s in $sigs; do
	slowin | $dfl "$ASM" -N -o a5$s.out - > a5$s.log 2>&1 &
	p=$!
	sleep 1
	kill -$s $p 2>/dev/null
	wait $p 2>/dev/null
	rc=$?
	if [ $rc -gt 128 ]; then ok; else bad "a5-$s: exit $rc"; fi
	if ls a5$s.out* >/dev/null 2>&1; then bad "a5-$s: output or temporary file left"; else ok; fi
done
# ignored, they stay ignored (A3): with SIGPIPE ignored a closed pipe
# is a write error instead
(
	trap '' PIPE
	slowin | "$ASM" -N -o a5i.out - > a5i.log 2>&1 &
	p=$!
	sleep 1
	kill -PIPE $p 2>/dev/null
	wait $p
	echo $? > a5i.rc
)
if [ "$(cat a5i.rc)" = 0 ] && [ -s a5i.out ]; then ok; else bad "a5-ignored: exit $(cat a5i.rc)"; fi
