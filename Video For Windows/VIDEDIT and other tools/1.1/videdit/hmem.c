/*
 * hmem.c - code segment 1 of VIDEDIT.EXE, s01:1790-195D: hmemmove,
 * hand-written assembly in the original, rendered here as C.
 *
 * The original tests __WINFLAGS: on a 286 (WF_CPU286) it jumps to a
 * 16-bit version (s01:1832) that walks the huge blocks segment by
 * segment with __AHINCR; otherwise it copies with REP MOVSD using
 * 32-bit offsets from the first selector, which relies on that
 * selector's limit covering the whole block (true in enhanced mode).
 * Both copy backwards when the blocks overlap with the source below the
 * destination, comparing the two pointers as huge addresses.
 */

#include "videdit.h"
#include "vemain.h"

/*
 * s01:1790-195D  hmemmove  (FAR PASCAL)
 *
 * Copies cb bytes between huge blocks, overlaps allowed.  Returns lpDst.
 */
LPVOID FAR PASCAL hmemmove(LPVOID lpDst, LPVOID lpSrc, DWORD cb)
{
    BYTE _huge *hpDst = (BYTE _huge *)lpDst;
    BYTE _huge *hpSrc = (BYTE _huge *)lpSrc;

    if (cb == 0)
        return lpDst;

    if (hpSrc < hpDst && hpSrc + cb > hpDst) {
        hpDst += cb;
        hpSrc += cb;
        while (cb--)
            *--hpDst = *--hpSrc;
    } else {
        while (cb--)
            *hpDst++ = *hpSrc++;
    }
    return lpDst;
}
