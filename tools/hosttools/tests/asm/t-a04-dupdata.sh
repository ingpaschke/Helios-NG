# A4: -wd covers every duplicate definition across modules, in both orders
printf '\tmodule -1\n\tglobal _f\n_f:\n\tldc 1\n' > a4code.s
printf '\tmodule -1\n\tglobal _f\n\tdata _f 4\n' > a4data.s
printf '\tmodule -1\n\tglobal _f\n\tcommon _f 4\n' > a4comm.s
for p in "a4code a4data" "a4data a4code" "a4code a4comm" "a4comm a4code" "a4data a4comm" "a4comm a4data" "a4data a4data" "a4code a4code"; do
	set -- $p
	expect a4-$1-$2 10 "$ASM" -N -o a4.out $1.s $2.s
	nofile a4-$1-$2-out a4.out
	expect a4-$1-$2-wd 0 "$ASM" -wd -N -o a4.out $1.s $2.s
	grepq a4-$1-$2-wd-msg Warning a4-$1-$2-wd.log
	rm -f a4.out
done
# common after common is no duplicate
expect a4-comm-comm 0 "$ASM" -N -o a4.out a4comm.s a4comm.s
