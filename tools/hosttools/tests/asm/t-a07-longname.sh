# Fix 14: an output file name of 249-255 characters (temporary name length)
for len in 248 249 250 255; do
	f=$(printf "%${len}s" "" | tr ' ' o)
	expect a7-$len 0 "$ASM" -N -o "$f" base.s
	same a7-$len-img base.img "$f"
	if ls ooooooo*.* > /dev/null 2>&1; then bad "a7-$len: temporary file left"; else ok; fi
	rm -f "$f"
done
mkdir a7d
f=a7d/$(printf "%255s" "" | tr ' ' p)
expect a7-def 0 "$ASM" -N -o a7d/img -d "$f" base.s
if [ -s "$f" ]; then ok; else bad "a7-def: no .def file"; fi
