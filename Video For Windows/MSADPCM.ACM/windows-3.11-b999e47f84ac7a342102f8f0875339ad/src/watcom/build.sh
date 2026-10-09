#!/bin/sh
#
# build.sh - build MSADPCM.ACM from the reconstructed source with Open Watcom
#
# usage: WATCOM=/path/to/open-watcom/rel ./build.sh
#
# Watcom's tools can't take paths with spaces, so everything is relative
# to this directory.  Output goes to ./out.
#
set -e

cd "$(dirname "$0")"

: "${WATCOM:=$HOME/src/open-watcom-v2/rel}"
export WATCOM
PATH="$WATCOM/binl64:$PATH"
export PATH

INC="-i=. -i=.. -i=$WATCOM/h -i=$WATCOM/h/win"

# medium model Win16 DLL: SS != DS, no stack checks, 386 code like the
# original
CFLAGS="-q -fr -bt=windows -mm -zu -s -3 -ox -w3 -wcd=303 $INC"

mkdir -p out

cc() {  # cc <source> <code segment> [extra options]
    obj="out/$(basename "$1" .c).obj"
    src="$1"; seg="$2"; shift 2
    wcc $CFLAGS -nt="$seg" "$@" -fo="$obj" "$src"
}

cc ../init.c      SEG1_TEXT
cc ../codec.c     SEG2_TEXT
cc ../msadpcm.c   SEG3_TEXT
cc ../adpcm386.c  SEG4_TEXT

wrc -q -r -bt=windows $INC -fo=out/msadpcm.res ../msadpcm.rc

# wrc stores the build time in VS_FIXEDFILEINFO.dwFileDateLS; the original
# has 0 there, and zeroing it keeps the build reproducible.  The field is
# the last DWORD of the fixed info, 64 bytes after the "VS_VERSION_INFO" key.
key=$(grep -obUa "VS_VERSION_INFO" out/msadpcm.res | head -n 1 | cut -d: -f1)
printf '\000\000\000\000' | dd of=out/msadpcm.res bs=1 seek=$((key + 64)) conv=notrunc status=none

wlink @msadpcm.lnk

echo "built out/msadpcm.acm"

# A/B test program (see acmtest/acmtest.c)
mkdir -p acmtest/out
wcc -q -fr -bt=windows -ml -zW -s -3 -ox -w3 -wcd=303 $INC -fo=acmtest/out/acmtest.obj acmtest/acmtest.c
wlink option quiet system windows option stack=16k option heapsize=4k \
    name acmtest/out/acmtest.exe file acmtest/out/acmtest.obj library toolhelp

echo "built acmtest/out/acmtest.exe"

# MSACM-level test program (see msacmtest/msacmtst.c)
mkdir -p msacmtest/out
wcc -q -fr -bt=windows -ml -zW -s -3 -ox -w3 -wcd=303 $INC -fo=msacmtest/out/msacmtst.obj msacmtest/msacmtst.c
wlink option quiet system windows option stack=16k option heapsize=4k \
    name msacmtest/out/msacmtst.exe file msacmtest/out/msacmtst.obj

echo "built msacmtest/out/msacmtst.exe"
