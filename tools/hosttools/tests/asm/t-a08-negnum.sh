# Fix 16: -N with N > 0x80000000 is an error (was clamped or wrapped)
for v in -2147483649 -4294967295 -#80000001 -#ffffffff; do
	printf '\tmodule -1\n\tldc %s\n' "$v" > a8.s
	expect "a8$v" 10 "$ASM" -p -o a8.out a8.s
	nofile "a8$v-out" a8.out
	grepq "a8$v-msg" "does not fit in 32 bits" "a8$v.log"
done
# the limits and the 32-bit spellings stay valid
for v in -2147483648 -#80000000 4294967295 '#ffffffff' '-(#ffffffff+2)' '-1' '0-2147483648'; do
	printf '\tmodule -1\n\tldc %s\n' "$v" > a8.s
	expect "a8-ok$v" 0 "$ASM" -p -o a8.out a8.s
done
printf '\tmodule -1\n\tword -2147483648, -2147483647\n' > a8w.s
expect a8-word 0 "$ASM" -N -o a8w.out a8w.s
