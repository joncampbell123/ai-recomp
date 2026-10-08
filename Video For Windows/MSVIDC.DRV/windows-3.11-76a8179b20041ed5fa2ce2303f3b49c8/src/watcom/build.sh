#!/bin/sh
#
# build.sh - build MSVIDC.DRV from the reconstructed source with Open Watcom
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
# original.  The only floating point (QualityToThreshold in invcmap.c) is
# compiled with -fpi, inline 8087 code with emulation, so it runs in 80-bit
# precision like the original; see compat.c.
CFLAGS="-q -fr -bt=windows -mm -zu -s -3 -ox -w3 $INC"

mkdir -p out

cc() {  # cc <source> <code segment> [extra options]
    obj="out/$(basename "$1" .c).obj"
    src="$1"; seg="$2"; shift 2
    wcc $CFLAGS -nt="$seg" "$@" -fo="$obj" "$src"
}

cc ../drvproc.c   SEG1_TEXT -dLibMain=MsvidcLibMain
cc libmain.c      SEG1_TEXT
cc ../dec386.c    SEG2_TEXT
cc ../compress.c  SEG3_TEXT
cc ../decomp.c    SEG3_TEXT
cc ../invcmap.c   SEG3_TEXT -fpi
cc ../encode.c    SEG3_TEXT
cc compat.c       SEG3_TEXT
cc ../dec286.c    SEG4_TEXT
cc ../data.c      SEG3_TEXT

wrc -q -r -bt=windows $INC -fo=out/msvidc.res ../msvidc.rc

# wrc stores the build time in VS_FIXEDFILEINFO.dwFileDateLS; the original
# has 0 there, and zeroing it keeps the build reproducible.  The field is
# the last DWORD of the fixed info, 64 bytes after the "VS_VERSION_INFO" key.
key=$(grep -obUa "VS_VERSION_INFO" out/msvidc.res | head -n 1 | cut -d: -f1)
printf '\000\000\000\000' | dd of=out/msvidc.res bs=1 seek=$((key + 64)) conv=notrunc status=none

wlink @msvidc.lnk

echo "built out/msvidc.drv"

# A/B test program (see icmtest/icmtest.c)
mkdir -p icmtest/out
wcc -q -fr -bt=windows -ml -zW -s -3 -ox -w3 $INC -fo=icmtest/out/icmtest.obj icmtest/icmtest.c
wlink option quiet system windows option stack=16k option heapsize=4k \
    name icmtest/out/icmtest.exe file icmtest/out/icmtest.obj

echo "built icmtest/out/icmtest.exe"
