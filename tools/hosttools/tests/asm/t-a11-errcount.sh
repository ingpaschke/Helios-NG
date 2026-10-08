# A11: growcode errors are counted once ("Too many errors" at 100, not 50)
awk 'BEGIN { print "\tmodule -1\n\tdata _d 4\nl:"; for (i = 0; i < 60; i++) print "\tldc l - _d" }' > a11.s
expect a11-60 10 "$ASM" -N -o a11.out a11.s
if [ "$(grep -c 'Cannot mix' a11-60.log)" = 60 ]; then ok; else bad "a11-60: $(grep -c 'Cannot mix' a11-60.log) errors reported, expected 60"; fi
awk 'BEGIN { print "\tmodule -1\n\tdata _d 4\nl:"; for (i = 0; i < 120; i++) print "\tldc l - _d" }' > a11b.s
expect a11-120 20 "$ASM" -N -o a11.out a11b.s
grepq a11-120-msg "Too many errors" a11-120.log
