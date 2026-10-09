#!/bin/sh
#
# build.sh - build VIDCAP.EXE from the reconstructed source with Open Watcom
#
# usage: WATCOM=/path/to/open-watcom/rel ./build.sh
#
# Watcom's tools can't take paths with spaces, so everything is relative
# to this directory.  Output goes to ./out.  sources.txt lists each source
# file with the code segment it goes into, in the original's order.
#
set -e

cd "$(dirname "$0")"

: "${WATCOM:=$HOME/src/open-watcom-v2/rel}"
export WATCOM
PATH="$WATCOM/binl64:$PATH"
export PATH

INC="-i=. -i=.. -i=../../common/watcom -i=$WATCOM/h -i=$WATCOM/h/win"

# medium model Win16 application, 386 code like the original.  -zWs would
# give __export functions "smart callback" entry code; there are none, and
# window and dialog procedures are entered with DS = DGROUP anyway.  The
# callbacks entered with another DS are declared _loadds (see vidcap.h).
CFLAGS="-q -fr -bt=windows -mm -zWs -s -3 -ox -w3 -wcd=303 -wcd=200 $INC ${EXTRA_CFLAGS}"

mkdir -p out
: > out/files.lnk

grep -v '^#' sources.txt | while read -r src seg opts; do
    [ -n "$src" ] || continue
    obj="out/$(basename "$src" .c).obj"
    wcc $CFLAGS $opts -nt="$seg" -fo="$obj" "../$src"
    echo "file $obj" >> out/files.lnk
done

(cd .. && wrc -q -r -bt=windows -i=. -i=../common/watcom -i=$WATCOM/h -i=$WATCOM/h/win \
    -fo=watcom/out/vidcap.res vidcap.rc)

# wrc stores the build time in VS_FIXEDFILEINFO.dwFileDateLS; the original
# has 0 there.  The field is 64 bytes after the "VS_VERSION_INFO" key.
key=$(grep -obUa "VS_VERSION_INFO" out/vidcap.res | head -n 1 | cut -d: -f1)
printf '\000\000\000\000' | dd of=out/vidcap.res bs=1 seek=$((key + 64)) conv=notrunc status=none

wlink @vidcap.lnk @out/files.lnk

echo "built out/vidcap.exe"
