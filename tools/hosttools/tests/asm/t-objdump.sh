# objdump: a CODE record cut off at the end of the file is an error
OBJDUMP=${ASM%/*}/objdump
if [ -x "$OBJDUMP" ]; then
	printf '\325' > od1.p
	expect od-eof 1 "$OBJDUMP" -a od1.p
	expect od-base 0 "$OBJDUMP" -a base.p
	grepq od-base-out "ldc" od-base.log
fi
