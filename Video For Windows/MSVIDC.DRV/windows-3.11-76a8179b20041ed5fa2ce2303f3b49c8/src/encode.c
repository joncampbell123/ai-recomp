/*
 * encode.c - Microsoft Video 1 (CRAM) frame encoders
 *
 * Reconstructed from MSVIDC.DRV (Windows 3.1x, md5 76a8179b20041ed5fa2ce2303f3b49c8),
 * segment 3, offsets 0x1D2C-0x34B9.
 *
 * The compiled code is MSC 7 / VC 1.0 style with 386 code generation: ENTER/LEAVE,
 * 32-bit register arithmetic, intrinsic memcpy emitted as REP MOVSD.  No C runtime
 * helpers are called from this range.  Huge pointer steps use __AHINCR/__AHSHIFT
 * inline, which is what BYTE _huge * arithmetic compiles to.
 *
 * Both encoders work on 4x4 blocks fetched into 16-entry RGBQUAD buffers in DGROUP.
 * For each block they try, in order:
 *   1. skip      - the block is close enough to the same block of the previous frame
 *   2. solid     - one colour (the block average)
 *   3. 2-colour  - pixels split on mean luminance, each half replaced by its average
 *   4. 8-colour  - the same split done separately in each 2x2 quadrant (always accepted)
 * After step 2 or 3, if the approximation itself is close enough to the previous
 * frame, the block is skipped as well.  "Close enough" means BlockDiff() <= the
 * spatial threshold (vs. the source) or the temporal threshold (vs. the previous frame).
 */

#include <windows.h>
#include <string.h>
#include "msvidc.h"

/* 4x4 block buffers, one set per encoder */
static RGBQUAD aCur16[16];            /* DS:1278 */
static RGBQUAD aTry16[16];            /* DS:12B8 */
static RGBQUAD aPrev16[16];           /* DS:12F8 */
static RGBQUAD aCur8[16];             /* DS:1338 */
static RGBQUAD aTry8[16];             /* DS:1378 */
static RGBQUAD aPrev8[16];            /* DS:13B8 */

/* Per-quadrant average colour of one half (dark or bright) of a block.  Element [0] is used for the whole block in 2-colour mode. */
typedef struct {
    UINT b[4];
    UINT g[4];
    UINT r[4];
} QAVG;

/* DWORD-aligned scan line width; all 16-bit arithmetic */
#define DIBSTRIDE(lpbi)     (((((UINT)(lpbi)->biWidth * (lpbi)->biBitCount) + 31) & ~31) >> 3)

#define RGB555(r, g, b)     ((((WORD)(r) >> 3) << 10) | (((WORD)(g) & 0xF8) << 2) | ((WORD)(b) >> 3))
#define LUMA(r, g, b)       (((UINT)(r) * 30 + (UINT)(g) * 59 + (UINT)(b) * 11) / 100)
#define ABSDIFF(a, b)       ((a) > (b) ? (a) - (b) : (b) - (a))

/* Write out the pending run of skipped blocks as 0x8400|n codes, n <= 1023. */
#define FLUSH_SKIPS()                               \
    while (nSkip) {                                 \
        n = nSkip < 0x3FF ? nSkip : 0x3FF;          \
        nSkip -= n;                                 \
        *pOut++ = 0x8400 | n;                       \
        g_cSkipCodes++;                             \
        dwSize += 2;                                \
    }

/*
 * 1010:1D2C-1F73  GetBlock
 *
 * Copies the 4x4 block at pSrc (bottom-up DIB, rows in memory order) into pBlock as
 * 16 RGBQUADs in row-major order.  Handles 8 bpp (through g_rgbqIn, copying
 * rgbReserved too), 16 bpp RGB555 (through g_ab5to8), 24 and 32 bpp; other depths
 * leave pBlock untouched.  For 16/24/32 bpp rgbReserved keeps whatever it held.
 * Returns pSrc advanced by 4 pixels, i.e. the next block on the same row.
 *
 * static NEAR _fastcall: pBlock in BX; lpbi, pSrc on the stack (RET 8).
 */
static BYTE _huge * NEAR _fastcall GetBlock(RGBQUAD *pBlock, LPBITMAPINFOHEADER lpbi, BYTE _huge *pSrc)
{
    BYTE _huge *p = pSrc;
    int      bpp = lpbi->biBitCount;
    UINT     cbSkip = DIBSTRIDE(lpbi) - bpp * 4 / 8;    /* stride minus the 4 pixels read */
    UINT     iPixel = 0;
    int      x, y;
    RGBQUAD *pq;
    WORD     w;

    pSrc += bpp * 4 / 8;

    switch (bpp) {
    case 8:
        for (y = 4; y; y--) {
            pq = &pBlock[iPixel];
            iPixel += 4;
            for (x = 4; x; x--)
                *pq++ = g_rgbqIn[*p++];
            p += cbSkip;
        }
        break;

    case 16:
        for (y = 4; y; y--) {
            pq = &pBlock[iPixel];
            iPixel += 4;
            for (x = 4; x; x--, pq++) {
                w = *(WORD _huge *)p;
                p += 2;
                pq->rgbRed   = g_ab5to8[(w & 0x7C00) >> 10];
                pq->rgbGreen = g_ab5to8[(w & 0x03E0) >> 5];
                pq->rgbBlue  = g_ab5to8[w & 0x001F];
            }
            p += cbSkip;
        }
        break;

    case 24:
        for (y = 4; y; y--) {
            pq = &pBlock[iPixel];
            iPixel += 4;
            for (x = 4; x; x--, pq++) {
                pq->rgbBlue  = *p++;
                pq->rgbGreen = *p++;
                pq->rgbRed   = *p++;
            }
            p += cbSkip;
        }
        break;

    case 32:
        for (y = 4; y; y--) {
            pq = &pBlock[iPixel];
            iPixel += 4;
            for (x = 4; x; x--, pq++) {
                pq->rgbBlue  = *p++;
                pq->rgbGreen = *p++;
                pq->rgbRed   = *p;
                p += 2;                         /* skip the 4th byte */
            }
            p += cbSkip;
        }
        break;
    }

    return pSrc;
}

/*
 * 1010:1F74-2037  BlockDiff
 *
 * Sum over the 16 pixels of the squared blue, green and red differences
 * (rgbReserved ignored), divided by 16.
 *
 * static NEAR _fastcall: p1 in BX, p2 in AX; returns DX:AX.
 */
static DWORD NEAR _fastcall BlockDiff(RGBQUAD *p1, RGBQUAD *p2)
{
    DWORD dwErr = 0;
    UINT  d;
    int   i;

    for (i = 16; i; i--, p1++, p2++) {
        d = ABSDIFF(p1->rgbRed, p2->rgbRed);
        dwErr += (UINT)(d * d);
        d = ABSDIFF(p1->rgbGreen, p2->rgbGreen);
        dwErr += (UINT)(d * d);
        d = ABSDIFF(p1->rgbBlue, p2->rgbBlue);
        dwErr += (UINT)(d * d);
    }

    return dwErr >> 4;
}

/*
 * 1010:2038-2A8B  CompressFrame16
 *
 * Encodes one frame as 16-bit Microsoft Video 1 (CRAM-16) from an 8/16/24/32 bpp DIB.
 * Called from the ICM_COMPRESS handler (1010:0422) when lpbiOutput->biBitCount != 8.
 * Only whole 4x4 blocks are coded: (int)biWidth/4 by (int)biHeight/4.
 *
 * Output words:  0x8400|n            skip n blocks (n <= 1023)
 *                0x8000|RGB555       solid block (adjusted so it is never 0x84xx-0x87xx)
 *                mask, cA, cB        2-colour: set bits use cA; mask < 0x8000
 *                mask, cA0|0x8000, cB0, cA1, cB1, cA2, cB2, cA3, cB3
 *                                    8-colour, one colour pair per 2x2 quadrant
 *                0x0000              end of frame
 *
 * Returns the number of bytes written including the end word, or (DWORD)-1 if the
 * status callback returned non-zero (nothing further is written in that case).
 *
 * FAR _cdecl (caller pops 0x24).
 */
DWORD FAR _cdecl CompressFrame16(
    LPBITMAPINFOHEADER lpbiIn,          /* bp+06 */
    BYTE _huge *pIn,                    /* bp+0A */
    WORD _huge *pOut,                   /* bp+0E */
    DWORD       dwThreshold,            /* bp+12  max BlockDiff vs. the source block */
    DWORD       dwThresholdTemporal,    /* bp+16  max BlockDiff vs. the previous frame */
    LPBITMAPINFOHEADER lpbiPrev,        /* bp+1A  NULL: no previous frame (key frame) */
    BYTE _huge *pPrev,                  /* bp+1E */
    STATUSPROC  Status,                 /* bp+22  may be NULL */
    LPARAM      lParam)                 /* bp+26 */
{
    UINT    cbIn, cbPrev;
    UINT    xBlocks, yBlocks, x, y;
    UINT    nStatusStep;
    UINT    nSkip, n;
    DWORD   dwSize;
    BYTE _huge *pSrc;
    BYTE _huge *pSrcPrev;
    UINT    luma[16];
    UINT    lumaAvg[4];
    DWORD   dwLumaSum;
    UINT    sumR, sumG, sumB;
    UINT    cnt[2][4];                  /* [0] darker than average, [1] not darker */
    UINT    aSumR[2][4], aSumG[2][4], aSumB[2][4];
    QAVG    avg[2];
    RGBQUAD rgbAvg;                     /* rgbReserved is never set */
    WORD    mask, w;
    UINT    i, q;
    BOOL    fHi;

    cbIn = DIBSTRIDE(lpbiIn);
    if (lpbiPrev)
        cbPrev = DIBSTRIDE(lpbiPrev);   /* quirk: left uninitialised, yet used below, if lpbiPrev == NULL but pPrev != NULL */

    xBlocks = (int)lpbiIn->biWidth / 4;
    yBlocks = (int)lpbiIn->biHeight / 4;

    if (xBlocks < 100)
        nStatusStep = 4;
    else if (xBlocks < 200)
        nStatusStep = 2;
    else
        nStatusStep = 1;

    dwSize = 0;
    g_cSkipCodes = g_cSkipped = g_cSkippedApprox = g_c8Color = g_c2Color =
        g_cSolid = g_dwStat1722 = g_c8ColorQuads = g_cSolidFixups = 0;

    nSkip = 0;

    for (y = 0; y < yBlocks; y++) {
        if (Status && (y % nStatusStep) == 0) {
            if (Status(lParam, ICSTATUS_STATUS, (LONG)(y * 100 / yBlocks)))
                return (DWORD)-1;
        }

        pSrc = pIn;
        pSrcPrev = pPrev;

        for (x = 0; x < xBlocks; x++) {
            pSrc = GetBlock(aCur16, lpbiIn, pSrc);

            if (lpbiPrev) {
                pSrcPrev = GetBlock(aPrev16, lpbiPrev, pSrcPrev);
                if (BlockDiff(aCur16, aPrev16) <= dwThresholdTemporal)
                    goto Skip;
            }

            /* ---- solid: block average ---- */
            dwLumaSum = 0;
            sumR = sumG = sumB = 0;
            for (i = 0; i < 16; i++) {
                sumR += aCur16[i].rgbRed;
                sumG += aCur16[i].rgbGreen;
                sumB += aCur16[i].rgbBlue;
                luma[i] = LUMA(aCur16[i].rgbRed, aCur16[i].rgbGreen, aCur16[i].rgbBlue);
                dwLumaSum += luma[i];
            }
            sumR >>= 4;
            sumG >>= 4;
            sumB >>= 4;
            rgbAvg.rgbRed   = (BYTE)sumR;
            rgbAvg.rgbGreen = (BYTE)sumG;
            rgbAvg.rgbBlue  = (BYTE)sumB;

            aTry16[0] = rgbAvg;
            memcpy(&aTry16[1], &aTry16[0], 15 * sizeof(RGBQUAD));  /* overlapping forward copy replicates entry 0 */

            if (BlockDiff(aCur16, aTry16) <= dwThreshold) {
                if (lpbiPrev && BlockDiff(aTry16, aPrev16) <= dwThresholdTemporal)
                    goto SkipApprox;
                FLUSH_SKIPS();
                goto Solid;                 /* with sumR/sumG/sumB */
            }

            /* ---- 2-colour: split on mean luminance ---- */
            lumaAvg[0] = (UINT)(dwLumaSum >> 4);

            cnt[0][0] = cnt[1][0] = 0;
            aSumR[0][0] = aSumR[1][0] = 0;
            aSumG[0][0] = aSumG[1][0] = 0;
            aSumB[0][0] = aSumB[1][0] = 0;
            mask = 0;

            for (i = 0; i < 16; i++) {
                if (luma[i] < lumaAvg[0]) {
                    cnt[0][0]++;
                    aSumR[0][0] += aCur16[i].rgbRed;
                    aSumG[0][0] += aCur16[i].rgbGreen;
                    aSumB[0][0] += aCur16[i].rgbBlue;
                } else {
                    mask |= 1 << i;
                    cnt[1][0]++;
                    aSumR[1][0] += aCur16[i].rgbRed;
                    aSumG[1][0] += aCur16[i].rgbGreen;
                    aSumB[1][0] += aCur16[i].rgbBlue;
                }
            }

            if (cnt[1][0]) {
                avg[1].r[0] = aSumR[1][0] / cnt[1][0];
                avg[1].g[0] = aSumG[1][0] / cnt[1][0];
                avg[1].b[0] = aSumB[1][0] / cnt[1][0];
            } else
                avg[1].r[0] = avg[1].g[0] = avg[1].b[0] = 0;

            if (cnt[0][0]) {
                avg[0].r[0] = aSumR[0][0] / cnt[0][0];
                avg[0].g[0] = aSumG[0][0] / cnt[0][0];
                avg[0].b[0] = aSumB[0][0] / cnt[0][0];
            } else
                avg[0].r[0] = avg[0].g[0] = avg[0].b[0] = 0;

            /* B, G, R only; rgbReserved keeps the value from the solid attempt */
            for (i = 0; i < 16; i++) {
                fHi = luma[i] >= lumaAvg[0];
                aTry16[i].rgbRed   = (BYTE)avg[fHi].r[0];
                aTry16[i].rgbGreen = (BYTE)avg[fHi].g[0];
                aTry16[i].rgbBlue  = (BYTE)avg[fHi].b[0];
            }

            if (BlockDiff(aCur16, aTry16) <= dwThreshold) {
                if (lpbiPrev && BlockDiff(aTry16, aPrev16) <= dwThresholdTemporal)
                    goto SkipApprox;
                FLUSH_SKIPS();

                if (mask == 0) {
                    sumR = avg[0].r[0];
                    sumG = avg[0].g[0];
                    sumB = avg[0].b[0];
                    goto Solid;
                }
                if (mask == 0xFFFF) {
                    sumR = avg[1].r[0];
                    sumG = avg[1].g[0];
                    sumB = avg[1].b[0];
                    goto Solid;
                }

                /* bit 15 of the mask must be clear; invert it and swap the colours if needed */
                if (mask & 0x8000) {
                    *pOut++ = ~mask;
                    *pOut++ = RGB555(avg[0].r[0], avg[0].g[0], avg[0].b[0]);
                    *pOut++ = RGB555(avg[1].r[0], avg[1].g[0], avg[1].b[0]);
                } else {
                    *pOut++ = mask;
                    *pOut++ = RGB555(avg[1].r[0], avg[1].g[0], avg[1].b[0]);
                    *pOut++ = RGB555(avg[0].r[0], avg[0].g[0], avg[0].b[0]);
                }
                dwSize += 6;
                g_c2Color++;
                continue;
            }

            /* ---- 8-colour: split each 2x2 quadrant on its own mean luminance ---- */
            FLUSH_SKIPS();
            g_c8Color++;

            lumaAvg[0] = (luma[0]  + luma[1]  + luma[4]  + luma[5])  >> 2;
            lumaAvg[1] = (luma[2]  + luma[3]  + luma[6]  + luma[7])  >> 2;
            lumaAvg[2] = (luma[8]  + luma[9]  + luma[12] + luma[13]) >> 2;
            lumaAvg[3] = (luma[10] + luma[11] + luma[14] + luma[15]) >> 2;

            /* the binary zeroes these stack arrays with individual stores;
             * far memset because SS != DS in the DLL */
            _fmemset(cnt, 0, sizeof(cnt));
            _fmemset(aSumR, 0, sizeof(aSumR));
            _fmemset(aSumG, 0, sizeof(aSumG));
            _fmemset(aSumB, 0, sizeof(aSumB));
            mask = 0;

            for (i = 0; i < 16; i++) {
                q = g_abQuadrant[i];
                if (luma[i] < lumaAvg[q]) {
                    cnt[0][q]++;
                    aSumR[0][q] += aCur16[i].rgbRed;
                    aSumG[0][q] += aCur16[i].rgbGreen;
                    aSumB[0][q] += aCur16[i].rgbBlue;
                } else {
                    mask |= 1 << i;
                    cnt[1][q]++;
                    aSumR[1][q] += aCur16[i].rgbRed;
                    aSumG[1][q] += aCur16[i].rgbGreen;
                    aSumB[1][q] += aCur16[i].rgbBlue;
                }
            }

            *pOut++ = (mask & 0x8000) ? ~mask : mask;
            dwSize += 18;

            for (q = 0; q < 4; q++) {
                if (cnt[1][q]) {
                    avg[1].r[q] = aSumR[1][q] / cnt[1][q];
                    avg[1].g[q] = aSumG[1][q] / cnt[1][q];
                    avg[1].b[q] = aSumB[1][q] / cnt[1][q];
                } else
                    avg[1].b[q] = avg[1].g[q] = avg[1].r[q] = 0;

                if (cnt[0][q]) {
                    avg[0].r[q] = aSumR[0][q] / cnt[0][q];
                    avg[0].g[q] = aSumG[0][q] / cnt[0][q];
                    avg[0].b[q] = aSumB[0][q] / cnt[0][q];
                } else
                    avg[0].b[q] = avg[0].g[q] = avg[0].r[q] = 0;

                /* bit 15 of the first colour marks the block as 8-colour */
                if (mask & 0x8000) {
                    *pOut++ = RGB555(avg[0].r[q], avg[0].g[q], avg[0].b[q]) | (q == 0 ? 0x8000 : 0);
                    *pOut++ = RGB555(avg[1].r[q], avg[1].g[q], avg[1].b[q]);
                } else {
                    *pOut++ = RGB555(avg[1].r[q], avg[1].g[q], avg[1].b[q]) | (q == 0 ? 0x8000 : 0);
                    *pOut++ = RGB555(avg[0].r[q], avg[0].g[q], avg[0].b[q]);
                }
            }
            continue;

SkipApprox:
            g_cSkippedApprox++;
Skip:
            g_cSkipped++;
            nSkip++;
            continue;

Solid:
            w = RGB555(sumR, sumG, sumB) | 0x8000;
            if ((w & 0xFC00) == 0x8400) {       /* would read as a skip code: clear the red LSB */
                g_cSolidFixups++;
                w = (w ^ 0x8400) | 0x8000;
            }
            *pOut++ = w;
            g_cSolid++;
            dwSize += 2;
        }

        pIn += cbIn * 4;                        /* 16-bit product */
        if (pPrev)
            pPrev += cbPrev * 4;
    }

    FLUSH_SKIPS();
    *pOut = 0;
    return dwSize + 2;
}

/*
 * 1010:2A8C-34B9  CompressFrame8
 *
 * Encodes one frame as 8-bit Microsoft Video 1 (CRAM-8) from an 8/16/24/32 bpp DIB.
 * Colours are mapped to the output palette g_rgbqOut through the RGB555 inverse
 * table g_lpInvPal, and the error tests use the palette colours.
 * Called from the ICM_COMPRESS handler (1010:03EE) when lpbiOutput->biBitCount == 8.
 *
 * Output words:  0x8400|n            skip n blocks (n <= 1023)
 *                0x8000|idx          solid block
 *                mask, (iA | iB<<8)  2-colour: set bits use iA; mask < 0x8000
 *                mask', 4 x (iA | iB<<8)
 *                                    8-colour, one index pair per 2x2 quadrant; mask'
 *                                    always has bits 15 and 13 set (see below)
 *                0x0000              end of frame
 *
 * Returns the byte count including the end word, (DWORD)-1 on abort from the status
 * callback, or 0 without writing anything if g_lpInvPal has not been built.
 *
 * FAR _cdecl (caller pops 0x24).  Arguments as for CompressFrame16.
 */
DWORD FAR _cdecl CompressFrame8(
    LPBITMAPINFOHEADER lpbiIn,          /* bp+06 */
    BYTE _huge *pIn,                    /* bp+0A */
    WORD _huge *pOut,                   /* bp+0E */
    DWORD       dwThreshold,            /* bp+12 */
    DWORD       dwThresholdTemporal,    /* bp+16 */
    LPBITMAPINFOHEADER lpbiPrev,        /* bp+1A */
    BYTE _huge *pPrev,                  /* bp+1E */
    STATUSPROC  Status,                 /* bp+22 */
    LPARAM      lParam)                 /* bp+26 */
{
    UINT    cbIn, cbPrev;
    UINT    xBlocks, yBlocks, x, y;
    UINT    nStatusStep;
    UINT    nSkip, n;
    DWORD   dwSize;
    BYTE _huge *pSrc;
    BYTE _huge *pSrcPrev;
    UINT    luma[16];
    UINT    lumaAvg[4];
    DWORD   dwLumaSum;
    UINT    sumR, sumG, sumB;
    UINT    cnt[2][4];
    UINT    aSumR[2][4], aSumG[2][4], aSumB[2][4];
    QAVG    avg[2];
    RGBQUAD rgb, rgbLo, rgbHi;
    BYTE    idx, idxLo, idxHi, t;
    WORD    mask, w;
    UINT    i, q;

    cbIn = DIBSTRIDE(lpbiIn);
    if (lpbiPrev)
        cbPrev = DIBSTRIDE(lpbiPrev);

    xBlocks = (int)lpbiIn->biWidth / 4;
    yBlocks = (int)lpbiIn->biHeight / 4;

    if (xBlocks < 100)
        nStatusStep = 4;
    else if (xBlocks < 200)
        nStatusStep = 2;
    else
        nStatusStep = 1;

    dwSize = 0;
    g_cSkipCodes = g_cSkipped = g_cSkippedApprox = g_c8Color = g_c2Color =
        g_cSolid = g_dwStat1722 = g_c8ColorQuads = g_cSolidFixups = 0;

    nSkip = 0;

    if (g_lpInvPal == NULL)
        return 0;

    for (y = 0; y < yBlocks; y++) {
        pSrc = pIn;
        pSrcPrev = pPrev;

        if (Status && (y % nStatusStep) == 0) {
            if (Status(lParam, ICSTATUS_STATUS, (LONG)(y * 100 / yBlocks)))
                return (DWORD)-1;
        }

        for (x = 0; x < xBlocks; x++) {
            pSrc = GetBlock(aCur8, lpbiIn, pSrc);

            if (lpbiPrev) {
                pSrcPrev = GetBlock(aPrev8, lpbiPrev, pSrcPrev);
                if (BlockDiff(aCur8, aPrev8) <= dwThresholdTemporal)
                    goto Skip;
            }

            /* ---- solid: nearest palette colour to the block average ---- */
            dwLumaSum = 0;
            sumR = sumG = sumB = 0;
            for (i = 0; i < 16; i++) {
                sumR += aCur8[i].rgbRed;
                sumG += aCur8[i].rgbGreen;
                sumB += aCur8[i].rgbBlue;
                luma[i] = LUMA(aCur8[i].rgbRed, aCur8[i].rgbGreen, aCur8[i].rgbBlue);
                dwLumaSum += luma[i];
            }
            rgb.rgbRed   = (BYTE)(sumR >> 4);
            rgb.rgbGreen = (BYTE)(sumG >> 4);
            rgb.rgbBlue  = (BYTE)(sumB >> 4);
            idx = g_lpInvPal[RGB555(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue)];
            rgb = g_rgbqOut[idx];

            aTry8[0] = rgb;
            memcpy(&aTry8[1], &aTry8[0], 15 * sizeof(RGBQUAD));    /* overlapping forward copy */

            if (BlockDiff(aCur8, aTry8) <= dwThreshold) {
                if (lpbiPrev && BlockDiff(aTry8, aPrev8) <= dwThresholdTemporal)
                    goto SkipApprox;
                FLUSH_SKIPS();
                goto Solid;                 /* with idx */
            }

            /* ---- 2-colour ---- */
            lumaAvg[0] = (UINT)(dwLumaSum >> 4);

            cnt[0][0] = cnt[1][0] = 0;
            aSumR[0][0] = aSumR[1][0] = 0;
            aSumG[0][0] = aSumG[1][0] = 0;
            aSumB[0][0] = aSumB[1][0] = 0;
            mask = 0;

            for (i = 0; i < 16; i++) {
                if (luma[i] < lumaAvg[0]) {
                    cnt[0][0]++;
                    aSumR[0][0] += aCur8[i].rgbRed;
                    aSumG[0][0] += aCur8[i].rgbGreen;
                    aSumB[0][0] += aCur8[i].rgbBlue;
                } else {
                    mask |= 1 << i;
                    cnt[1][0]++;
                    aSumR[1][0] += aCur8[i].rgbRed;
                    aSumG[1][0] += aCur8[i].rgbGreen;
                    aSumB[1][0] += aCur8[i].rgbBlue;
                }
            }

            if (cnt[1][0]) {
                avg[1].r[0] = aSumR[1][0] / cnt[1][0];
                avg[1].g[0] = aSumG[1][0] / cnt[1][0];
                avg[1].b[0] = aSumB[1][0] / cnt[1][0];
            } else
                avg[1].b[0] = avg[1].g[0] = avg[1].r[0] = 0;

            if (cnt[0][0]) {
                avg[0].r[0] = aSumR[0][0] / cnt[0][0];
                avg[0].g[0] = aSumG[0][0] / cnt[0][0];
                avg[0].b[0] = aSumB[0][0] / cnt[0][0];
            } else
                avg[0].b[0] = avg[0].g[0] = avg[0].r[0] = 0;

            rgbLo.rgbRed   = (BYTE)avg[0].r[0];
            rgbLo.rgbGreen = (BYTE)avg[0].g[0];
            rgbLo.rgbBlue  = (BYTE)avg[0].b[0];
            idxLo = g_lpInvPal[RGB555(rgbLo.rgbRed, rgbLo.rgbGreen, rgbLo.rgbBlue)];
            rgbLo = g_rgbqOut[idxLo];

            rgbHi.rgbRed   = (BYTE)avg[1].r[0];
            rgbHi.rgbGreen = (BYTE)avg[1].g[0];
            rgbHi.rgbBlue  = (BYTE)avg[1].b[0];
            idxHi = g_lpInvPal[RGB555(rgbHi.rgbRed, rgbHi.rgbGreen, rgbHi.rgbBlue)];
            rgbHi = g_rgbqOut[idxHi];

            for (i = 0; i < 16; i++)
                aTry8[i] = (luma[i] < lumaAvg[0]) ? rgbLo : rgbHi;

            if (BlockDiff(aCur8, aTry8) <= dwThreshold) {
                if (lpbiPrev && BlockDiff(aTry8, aPrev8) <= dwThresholdTemporal)
                    goto SkipApprox;
                FLUSH_SKIPS();

                if (mask == 0) {
                    idx = idxLo;
                    goto Solid;
                }
                if (mask == 0xFFFF || idxHi == idxLo) {
                    idx = idxHi;
                    goto Solid;
                }

                if (mask & 0x8000) {            /* keep bit 15 clear */
                    mask = ~mask;
                    t = idxLo; idxLo = idxHi; idxHi = t;
                }
                *pOut++ = mask;
                *pOut++ = MAKEWORD(idxHi, idxLo);
                dwSize += 4;
                g_c2Color++;
                continue;
            }

            /* ---- 8-colour ---- */
            FLUSH_SKIPS();
            g_c8Color++;

            lumaAvg[0] = (luma[0]  + luma[1]  + luma[4]  + luma[5])  >> 2;
            lumaAvg[1] = (luma[2]  + luma[3]  + luma[6]  + luma[7])  >> 2;
            lumaAvg[2] = (luma[8]  + luma[9]  + luma[12] + luma[13]) >> 2;
            lumaAvg[3] = (luma[10] + luma[11] + luma[14] + luma[15]) >> 2;

            _fmemset(cnt, 0, sizeof(cnt));
            _fmemset(aSumR, 0, sizeof(aSumR));
            _fmemset(aSumG, 0, sizeof(aSumG));
            _fmemset(aSumB, 0, sizeof(aSumB));
            mask = 0;

            for (i = 0; i < 16; i++) {
                q = g_abQuadrant[i];
                if (luma[i] < lumaAvg[q]) {
                    cnt[0][q]++;
                    aSumR[0][q] += aCur8[i].rgbRed;
                    aSumG[0][q] += aCur8[i].rgbGreen;
                    aSumB[0][q] += aCur8[i].rgbBlue;
                } else {
                    mask |= 1 << i;
                    cnt[1][q]++;
                    aSumR[1][q] += aCur8[i].rgbRed;
                    aSumG[1][q] += aCur8[i].rgbGreen;
                    aSumB[1][q] += aCur8[i].rgbBlue;
                }
            }

            /*
             * A CRAM-8 8-colour block needs a flag word >= 0x9000.  Bits 15 and 13 are
             * forced on by inverting all of quadrant 3 (pixels 10,11,14,15 = 0xCC00) and/or
             * quadrant 2 (pixels 8,9,12,13 = 0x3300); the index pair of an inverted
             * quadrant is swapped below to compensate.
             */
            w = mask;
            if (!(mask & 0x8000))
                w ^= 0xCC00;
            if (!(mask & 0x2000))
                w ^= 0x3300;
            *pOut++ = w;
            dwSize += 10;
            g_c8ColorQuads += 4;

            for (q = 0; q < 4; q++) {
                if (cnt[1][q]) {
                    avg[1].r[q] = aSumR[1][q] / cnt[1][q];
                    avg[1].g[q] = aSumG[1][q] / cnt[1][q];
                    avg[1].b[q] = aSumB[1][q] / cnt[1][q];
                } else
                    avg[1].r[q] = avg[1].g[q] = avg[1].b[q] = 0;

                if (cnt[0][q]) {
                    avg[0].r[q] = aSumR[0][q] / cnt[0][q];
                    avg[0].g[q] = aSumG[0][q] / cnt[0][q];
                    avg[0].b[q] = aSumB[0][q] / cnt[0][q];
                } else
                    avg[0].b[q] = avg[0].g[q] = avg[0].r[q] = 0;

                rgbLo.rgbRed   = (BYTE)avg[0].r[q];
                rgbLo.rgbGreen = (BYTE)avg[0].g[q];
                rgbLo.rgbBlue  = (BYTE)avg[0].b[q];
                idxLo = g_lpInvPal[RGB555(rgbLo.rgbRed, rgbLo.rgbGreen, rgbLo.rgbBlue)];

                rgbHi.rgbRed   = (BYTE)avg[1].r[q];
                rgbHi.rgbGreen = (BYTE)avg[1].g[q];
                rgbHi.rgbBlue  = (BYTE)avg[1].b[q];
                idxHi = g_lpInvPal[RGB555(rgbHi.rgbRed, rgbHi.rgbGreen, rgbHi.rgbBlue)];

                if (q == 3 && !(mask & 0x8000)) {
                    t = idxLo; idxLo = idxHi; idxHi = t;
                }
                if (q == 2 && !(mask & 0x2000)) {
                    t = idxLo; idxLo = idxHi; idxHi = t;
                }
                *pOut++ = MAKEWORD(idxHi, idxLo);
            }
            continue;

SkipApprox:
            g_cSkippedApprox++;
Skip:
            g_cSkipped++;
            nSkip++;
            continue;

Solid:
            *pOut++ = MAKEWORD(idx, 0x80);
            g_cSolid++;
            dwSize += 2;
        }

        pIn += cbIn * 4;
        if (pPrev)
            pPrev += cbPrev * 4;
    }

    FLUSH_SKIPS();
    *pOut = 0;
    return dwSize + 2;
}
