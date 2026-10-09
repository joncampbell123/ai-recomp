#!/bin/sh
#
# build.sh - build UITEST.EXE, the Win16 UI test driver, with Open Watcom
#
# usage: WATCOM=/path/to/open-watcom/rel ./build.sh
#
# Output goes to ./out.
#
set -e

cd "$(dirname "$0")"

: "${WATCOM:=$HOME/src/open-watcom-v2/rel}"
export WATCOM
PATH="$WATCOM/binl64:$PATH"
export PATH

mkdir -p out
wcc -q -fr -bt=windows -ml -zW -s -3 -ox -w3 -wcd=303 \
    -i=$WATCOM/h -i=$WATCOM/h/win -fo=out/uitest.obj uitest.c
wlink option quiet system windows name out/uitest.exe file out/uitest.obj \
    option stack=16k option heapsize=4k library mmsystem,commdlg

echo "built out/uitest.exe"
