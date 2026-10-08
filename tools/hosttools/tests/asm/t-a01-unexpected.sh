# A1: a token the parser does not expect is an error, not dropped
printf '\tmodule -1\n\tldc 0x7fffffff\n' > a1x.s
expect a1-hex 10 "$ASM" -N -o a1x.out a1x.s
nofile a1-hex-out a1x.out
grepq a1-hex-msg "Error: .*Unexpected symbol: 'x7fffffff'" a1-hex.log
printf '\tmodule -1\n\tldc 1.5\n' > a1f.s
expect a1-frac 10 "$ASM" -p -o a1f.out a1f.s
nofile a1-frac-out a1f.out
printf '\tmodule -1\nfoo bar\n\tldc 1\n' > a1w.s
expect a1-words 10 "$ASM" -p -o a1w.out a1w.s
# BYTE items that were dropped or truncated with a warning
printf '\tmodule -1\n\tbyte 300\n' > a1b1.s
expect a1-byte-range 10 "$ASM" -p -o a1b.out a1b1.s
printf '\tmodule -1\n\tbyte 1, foo, 2\n' > a1b2.s
expect a1-byte-sym 10 "$ASM" -p -o a1b.out a1b2.s
printf '\tmodule -1\n\tbyte 1, ), 2\n' > a1b3.s
expect a1-byte-item 10 "$ASM" -p -o a1b.out a1b3.s
nofile a1-byte-out a1b.out
# what stays valid
printf '\tmodule -1\n\tbyte 255, 0, "ab", #ff\n' > a1ok.s
expect a1-byte-ok 0 "$ASM" -p -o a1ok.out a1ok.s
# a negative item was never possible: "byte -1" dropped the item, exit 0
printf '\tmodule -1\n\tbyte 1, -1\n' > a1b4.s
expect a1-byte-neg 10 "$ASM" -p -o a1b.out a1b4.s
