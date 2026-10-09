#!/bin/sh
#
# build.sh - build VIDEDIT.EXE from the reconstructed source with Open Watcom
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

# medium model Win16 application, 386 code like the original.  -zWs gives
# callbacks the "smart callback" entry code (DS loaded from SS); the
# original's window procedures load nothing and rely on DS being DGROUP.
CFLAGS="-q -fr -bt=windows -mm -zWs -s -3 -ox -w3 -wcd=303 $INC"

mkdir -p out
: > out/files.lnk

grep -v '^#' sources.txt | while read -r src seg; do
    [ -n "$src" ] || continue
    obj="out/$(basename "$src" .c).obj"
    wcc $CFLAGS -nt="$seg" -fo="$obj" "../$src"
    echo "file $obj" >> out/files.lnk
done

(cd .. && wrc -q -r -bt=windows -i=. -i=../common/watcom -i=$WATCOM/h -i=$WATCOM/h/win \
    -fo=watcom/out/videdit.res videdit.rc)

# wrc stores the build time in VS_FIXEDFILEINFO.dwFileDateLS; the original
# has 0 there.  The field is 64 bytes after the "VS_VERSION_INFO" key.
key=$(grep -obUa "VS_VERSION_INFO" out/videdit.res | head -n 1 | cut -d: -f1)
printf '\000\000\000\000' | dd of=out/videdit.res bs=1 seek=$((key + 64)) conv=notrunc status=none

wlink @videdit.lnk @imports.lnk @out/files.lnk

echo "built out/videdit.exe"
