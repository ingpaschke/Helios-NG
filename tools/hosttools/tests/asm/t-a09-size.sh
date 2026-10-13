# Fix 17: operands that were truncated or ignored silently
for v in 300 256 0 -1 -2; do
	printf '\tmodule -1\n\tsize %s\n\tj .l\n.l:\n' "$v" > a9.s
	expect "a9-size$v" 10 "$ASM" -p -o a9.out a9.s
	nofile "a9-size$v-out" a9.out
done
printf '\tmodule -1\n\tblkb 3,"hello"\n' > a9s.s
expect a9-blkb-str 10 "$ASM" -p -o a9.out a9s.s
printf '\tmodule -1\n\tblkb 2,1,2,3\n' > a9n.s
expect a9-blkb-items 10 "$ASM" -p -o a9.out a9n.s
printf '\tmodule -1\n\tblkb -4\n' > a9b.s
expect a9-blkb-neg 10 "$ASM" -p -o a9.out a9b.s
printf '\tmodule -1\n\tblkw -1\n' > a9w.s
expect a9-blkw-neg 10 "$ASM" -p -o a9.out a9w.s
nofile a9-blk-out a9.out
# valid
printf '\tmodule -1\n\tsize 4\n\tj .l\n\tsize 255\n\tj .l\n\tsize 1\n\tj .l\n.l:\n\tblkb 5,"hello"\n\tblkb 31,"x" byte 0\n\tblkb 0\n' > a9ok.s
expect a9-ok 0 "$ASM" -p -o a9.out a9ok.s
