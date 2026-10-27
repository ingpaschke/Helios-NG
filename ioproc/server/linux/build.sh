#!/bin/sh
# Compatibility entry point for distribution builds using IOS_* settings.
# New builds can call make directly with CC, CFLAGS, TLS, SSH and SSHD.
set -e
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD=${IOS_BUILD_DIR:-$HERE/build}
exec make -f "$HERE/Makefile" \
    BUILD_DIR="$BUILD" OUT="${IOS_OUT:-$BUILD/ioserver}" \
    CC="${CC:-cc}" \
    CFLAGS="${CFLAGS:--g ${IOS_OPT:--O2 -D_FORTIFY_SOURCE=2}} ${CFLAGS_EXTRA:-}" \
    TLS="${IOS_TLS:-0}" SSH="${IOS_SSH:-0}" SSHD="${IOS_SSHD:-0}" \
    LINK_SOURCES="${IOS_LINK_SOURCES:-$HERE/linkext.c}" "$@"
