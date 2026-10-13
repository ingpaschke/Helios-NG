# Fix 10: -o and -d to a symbolic link, a pipe or a device are written directly
"$ASM" -N -o ref.img base.s
expect a2-stdout-file 0 sh -c "\"$ASM\" -N -o /dev/stdout base.s > a2.out"
same a2-stdout-file-img ref.img a2.out
expect a2-stdout-pipe 0 sh -c "\"$ASM\" -N -o /dev/stdout base.s | cat > a2p.out"
same a2-stdout-pipe-img ref.img a2p.out
expect a2-devnull 0 "$ASM" -N -o /dev/null base.s
expect a2-def-stdout 0 sh -c "\"$ASM\" -N -o a2i.out -d /dev/stdout base.s > a2d.out"
ln -s a2t.out a2link
expect a2-symlink 0 "$ASM" -N -o a2link base.s
if [ -L a2link ]; then ok; else bad "a2-symlink: link replaced"; fi
same a2-symlink-img ref.img a2t.out
# a regular target still goes through a temporary file: none is left
expect a2-regular 0 "$ASM" -N -o a2r.out base.s
same a2-regular-img ref.img a2r.out
if ls a2r.out.* >/dev/null 2>&1; then bad "a2-regular: temporary file left"; else ok; fi
# a write error on a device is still an error
if [ -w /dev/full ]; then expect a2-full 20 "$ASM" -N -o /dev/full base.s; fi
