# Fix 20: @file and option edge cases
# an option as the last word of an @file takes the next outer word
printf -- '-N\n-o\n' > a12.args
expect a12-optarg 0 "$ASM" @a12.args a12.out base.s
same a12-optarg-img base.img a12.out
# nested: the option ends the inner file, its argument ends the outer one
printf -- '-N @a12.args\n' > a12o.args
printf -- '@a12o.args a12n.out\n' > a12n.args
expect a12-optarg-nested 0 "$ASM" @a12n.args base.s
same a12-optarg-nested-img base.img a12n.out
# still an error at the very end
expect a12-optarg-end 20 "$ASM" base.s @a12.args
grepq a12-optarg-end-msg "Option -o needs an argument" a12-optarg-end.log
# -w flags set, they do not toggle
printf '\tmodule -1\n\tldc _undef\n' > a12u.s
expect a12-wu 0 "$ASM" -N -wu -o a12u.out a12u.s
expect a12-wuwu 0 "$ASM" -N -wu -wu -o a12u.out a12u.s
expect a12-wuu 0 "$ASM" -N -wuu -o a12u.out a12u.s
# no empty file name in messages about options
expect a12-msg 20 "$ASM" -N -o nodir/x base.s
if grep -q 'Error:  :' a12-msg.log; then bad "a12-msg: empty file name"; else ok; fi
# an undefined @symbol is reported once, wherever it is used
printf '\tmodule -1\n\tldc @_d\n\tldc @_d\n\tword @_d\n\tword @_d\n' > a12at.s
expect a12-at 10 "$ASM" -N -o a12at.out a12at.s
if [ "$(grep -c 'DATA label _d undefined' a12-at.log)" = 1 ]; then ok; else bad "a12-at: $(grep -c 'DATA label _d undefined' a12-at.log) messages, expected 1"; fi
