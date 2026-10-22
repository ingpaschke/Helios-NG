#!/bin/sh
# Build the Linux io-server directly from the maintained Helios-NG sources.
# IOS_BUILD_DIR and IOS_OUT select outputs; IOS_TARGET=native or i386.
# CC, CFLAGS_EXTRA and LDFLAGS support native/cross builds.
# IOS_TLS=1 enables OpenSSL; TLS_CFLAGS/TLS_LIBS select the dependency.
# IOS_LINK_SOURCES optionally supplies external link drivers (space-separated).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TARGET=${IOS_TARGET:-auto}
if [ "$TARGET" = auto ]; then
  case $(uname -m) in
    aarch64|arm64) TARGET=native ;;
    *) TARGET=i386 ;;
  esac
fi
case $TARGET in
  i386|native) ;;
  *) echo "build.sh: IOS_TARGET must be auto, i386 or native" >&2; exit 1 ;;
esac
CC=${CC:-gcc}
SRV=$(cd "$HERE/.." && pwd)
OPT=${IOS_OPT:--O2 -D_FORTIFY_SOURCE=2}
OUT=${IOS_OUT:-$HERE/build/ioserver}
[ -f "$SRV/server.c" ] || { echo "build.sh: no I/O Server sources in $SRV" >&2; exit 1; }
echo "building from $SRV ($OPT)"
BUILD=${IOS_BUILD_DIR:-$HERE/build}
mkdir -p "$BUILD"
BUILD=$(cd "$BUILD" && pwd)
case "$BUILD/" in
  "$SRV/"|"$SRV/linux/"|"$SRV/sun/") echo "Refusing source directory as build directory" >&2; exit 1 ;;
esac
SRC=$BUILD/src
OBJ=$BUILD/obj
INC=$OBJ/inc

rm -rf "$SRC" "$OBJ"
mkdir -p "$SRC/sun" "$SRC/linux" "$OBJ/sun" "$OBJ/linux" "$INC/gnu" "$INC/sys"

# 1. pristine copies
for f in server.c files.c devices.c tload.c cofuns.c debug.c terminal.c \
         linkio.c tcp.c defines.h barthdr protocol.h structs.h fundefs.h \
         server.h debugopt.h sccs.h; do
  cp "$SRV/$f" "$SRC/$f"
done
for f in sunlocal.c sunlocal.h linklib.c; do
  cp "$SRV/sun/$f" "$SRC/sun/$f"
done

# Linux sources are maintained in-tree; copying does not rewrite them.
cp "$HERE/"*.c "$HERE/"*.h "$SRC/linux/"
cp "$HERE/helios.h" "$SRC/helios.h"

# stub system headers that SunOS/SVR4 had and Linux does not
: > "$INC/gnu/stubs-32.h"
echo '#include <stdarg.h>'    > "$INC/varargs.h"
echo '#include <termios.h>'   > "$INC/termio.h"
echo '#include <sys/ioctl.h>' > "$INC/sys/filio.h"
echo '#include <sys/ioctl.h>' > "$INC/sys/sockio.h"

# 4. compile and link
# -Wall, without the warnings the original sources give by the dozen and
# that were checked to be harmless (NOTES.md, "Warnings"): signedness of
# char pointers, unused variables/functions/results, indentation, a
# char used as an index for a small count, precedence that is intended,
# false "may be used uninitialized" (switches over isxdigit, ARM-only
# paths) and the deliberate strncpy truncations.
WNO="-Wno-pointer-sign -Wno-unused-variable -Wno-unused-but-set-variable \
 -Wno-unused-function -Wno-unused-result -Wno-misleading-indentation \
 -Wno-char-subscripts -Wno-parentheses -Wno-maybe-uninitialized \
 -Wno-stringop-truncation"
CFLAGS="-m32 -isystem $INC -isystem /usr/include/x86_64-linux-gnu \
 -std=gnu89 -fcommon -fno-strict-aliasing -fno-stack-protector -fno-pie \
 -g $OPT -fsigned-char -fno-aggressive-loop-optimizations -fwrapv -Wall $WNO \
 -DLINUX -Di486V4 -D__GNU__=1 -DETC_DIR -I$SRC"
LIBS="/usr/lib32/libc.so.6 /usr/lib32/libm.so.6 /usr/lib32/libgcc_s.so.1"
if [ "$TARGET" = native ]; then
  CFLAGS="-isystem $INC -std=gnu89 -fcommon -fno-strict-aliasing \
 -g $OPT -fsigned-char -fno-aggressive-loop-optimizations -fwrapv -Wall $WNO \
 -DLINUX -Di486V4 -D__GNU__=1 -DETC_DIR -I$SRC \
 -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast \
 -Werror=int-conversion -Werror=incompatible-pointer-types ${CFLAGS_EXTRA:-}"
fi

if [ "$TARGET" = i386 ]; then
cp "$HERE/start.S" "$OBJ/start.S"
$CC -m32 -c "$OBJ/start.S" -o "$OBJ/start.o"
fi

TLS_LINK=""
if [ "${IOS_TLS:-0}" = 1 ]; then
  [ "$TARGET" = native ] || { echo "TLS requires IOS_TARGET=native" >&2; exit 1; }
  CFLAGS="$CFLAGS -DIOS_TLS -pthread ${TLS_CFLAGS:-}"
  TLS_LINK="${TLS_LIBS:--lssl -lcrypto} -pthread -ldl"
fi
OBJS=""
for s in server files devices tload cofuns debug terminal linkio tcp \
         sun/sunlocal sun/linklib linux/t4link linux/colib_linux linux/termcap_lite \
         linux/compat linux/net_compat linux/net_tls; do
  (cd "$SRC/$(dirname $s)" && $CC $CFLAGS -c "$SRC/$s.c" -o "$OBJ/$s.o")
  OBJS="$OBJS $OBJ/$s.o"
done

# Optional link drivers are compiled as separate translation units.
# A driver implements ios_linux_extra_link(name), registering its LinkIO
# callbacks and returning 1 when it recognizes the selected box.
LINK_SOURCES=${IOS_LINK_SOURCES:-$HERE/linkext.c}
index=0
for source in $LINK_SOURCES; do
  $CC $CFLAGS -c "$source" -o "$OBJ/linkext$index.o"
  OBJS="$OBJS $OBJ/linkext$index.o"
  index=$((index + 1))
done

if [ "$TARGET" = i386 ]; then
  ld -m elf_i386 -dynamic-linker /lib/ld-linux.so.2 -o "$OUT" \
     "$OBJ/start.o" $OBJS $LIBS
else
  $CC ${LDFLAGS:-} -o "$OUT" $OBJS -lm $TLS_LINK
fi
echo "built $OUT"
