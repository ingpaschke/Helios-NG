# A10: bytes >= 0x80 in text input are never .p tokens
# the euro sign e2 82 ac: e2 is a valid .p directive code
printf '\tmodule -1\n\tldc 1 \342\202\254\n' > a10.s
expect a10-euro 10 "$ASM" -p -o a10.out a10.s
nofile a10-euro-out a10.out
# also when the name does not say .s: the first keyword says text
cp a10.s a10.txt
expect a10-euro-txt 10 "$ASM" -p -o a10.out a10.txt
printf '\tmodule -1\n\tldc 1\n\t\325\n' > a10d.s
expect a10-direct 10 "$ASM" -p -o a10.out a10d.s
# inside strings and comments they are data, as before
printf '\tmodule -1\n\tblkb 3,"\342\202\254" -- \342\202\254\n' > a10ok.s
expect a10-string 0 "$ASM" -p -o a10ok.out a10ok.s
# .p files (tokens >= 0x80), also read from stdin or as a library
expect a10-p 0 "$ASM" -N -o a10p.img base.p
same a10-p-img base.img a10p.img
expect a10-pp 0 "$ASM" -p -o a10pp.p base.p
expect a10-stdin 0 sh -c "\"$ASM\" -N -o a10s.img - < base.p"
same a10-stdin-img base.img a10s.img
