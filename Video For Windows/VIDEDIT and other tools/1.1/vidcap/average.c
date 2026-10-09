/*
 * average.c - code segment 8 of VIDCAP.EXE, 1DB8-2FF7: frame averaging
 * for MCI step capture.  Several grabs of a still frame are summed per
 * colour component in 16-bit counters and averaged back; 2x spatial
 * averaging grabs at double size and averages 2x2 blocks down.
 * Uncompressed 8, 16, 24 and 32 bit frames; 8-bit frames go back to
 * palette indices through a 32K RGB555 table built with
 * GetNearestPaletteIndex.
 *
 * Quirks: the sum, add and average loops (--n) all leave out the last
 * pixel; the sum buffer is sized biSizeImage * 6 whatever the depth;
 * the 32-bit shrink reads its source pixels 3 bytes apart; the 5-to-8
 * bit table the shrink builds for 16-bit frames is never used.
 *
 * Functions are FAR CDECL.
 */

#include <windows.h>
#include "vidcap.h"

#define WIDTHBYTES(i)   ((((UINT)(i) + 31) & ~31U) >> 3)

typedef struct tagAVERAGE {
    BITMAPINFOHEADER bi;                /* 000 the frame format */
    RGBQUAD         argb[256];          /* 028 its colours (8-bit) */
    BYTE            abUnused[4];        /* 428 */
    LPBYTE          lp16to8;            /* 42C RGB555 to index (8-bit) */
    WORD _huge      *lpwSum;            /* 430 B,G,R sums per pixel */
    WORD            nFrames;            /* 434 frames summed */
} AVERAGE;                              /* 0x436 bytes */

static BYTE gab5to8[32] = { 0xFF };     /* DS:0604 filled on first use */

/*
 * seg8:1DB8-1F73  AverageFrameInit
 *
 * Sets *plpAvg to a new AVERAGE for frames of lpbi (NULL if it can't).
 */
BOOL FAR AverageFrameInit(LPAVERAGE NEAR *plpAvg, LPBITMAPINFOHEADER lpbi, HPALETTE hpal)
{
    LPAVERAGE   lpAvg;
    LPBYTE      lp;
    UINT        r;
    UINT        g;
    UINT        b;
    int         i;
    int         n;

    *plpAvg = NULL;

    if (lpbi->biCompression != BI_RGB)
        return FALSE;
    if (lpbi->biBitCount != 8 && lpbi->biBitCount != 16 &&
        lpbi->biBitCount != 24 && lpbi->biBitCount != 32)
        return FALSE;

    if (gab5to8[0]) {
        for (i = 0, n = 0; n < 32 * 255; i++, n += 255)
            gab5to8[i] = (BYTE)(n / 31);
    }

    lpAvg = (LPAVERAGE)GlobalLock(GlobalAlloc(GHND, sizeof(AVERAGE)));
    if (lpAvg == NULL)
        return FALSE;

    lpAvg->bi = *lpbi;

    if (lpbi->biBitCount == 8) {
        hmemcpy(lpAvg->argb, (LPBYTE)lpbi + sizeof(BITMAPINFOHEADER),
                lpbi->biClrUsed * sizeof(RGBQUAD));
        lpAvg->lp16to8 = lp = (LPBYTE)GlobalLock(GlobalAlloc(GHND, 0x8000L));
        for (r = 0; r < 256; r += 8)
            for (g = 0; g < 256; g += 8)
                for (b = 0; b < 256; b += 8)
                    *lp++ = (BYTE)GetNearestPaletteIndex(hpal, RGB(r, g, b));
    }

    lpAvg->lpwSum = (WORD _huge *)GlobalLock(GlobalAlloc(GHND, lpbi->biSizeImage * 6));
    if (lpAvg->lpwSum == NULL) {
        AverageFrameFini(lpAvg);
        return FALSE;
    }

    *plpAvg = lpAvg;
    return TRUE;
}

/*
 * seg8:1F74-201C  AverageFrameFini
 */
BOOL FAR AverageFrameFini(LPAVERAGE lpAvg)
{
    if (lpAvg == NULL)
        return FALSE;

    if (lpAvg->lp16to8) {
        GlobalUnlock(GlobalHandle(SELECTOROF(lpAvg->lp16to8)));
        GlobalFree(GlobalHandle(SELECTOROF(lpAvg->lp16to8)));
    }
    if (lpAvg->lpwSum) {
        GlobalUnlock(GlobalHandle(SELECTOROF(lpAvg->lpwSum)));
        GlobalFree(GlobalHandle(SELECTOROF(lpAvg->lpwSum)));
    }
    GlobalUnlock(GlobalHandle(SELECTOROF(lpAvg)));
    GlobalFree(GlobalHandle(SELECTOROF(lpAvg)));
    return TRUE;
}

/*
 * seg8:201E-20A0  AverageFrameStart
 *
 * Clears the sums.
 */
BOOL FAR AverageFrameStart(LPAVERAGE lpAvg)
{
    WORD _huge  *lpw;
    DWORD       n;

    if (lpAvg == NULL)
        return FALSE;

    lpw = lpAvg->lpwSum;
    n = lpAvg->bi.biSizeImage * 3;
    while (--n)
        *lpw++ = 0;
    lpAvg->nFrames = 0;
    return TRUE;
}

/*
 * seg8:20A2-2369  AverageFrameAdd
 *
 * Adds a frame to the sums.
 */
BOOL FAR AverageFrameAdd(LPAVERAGE lpAvg, BYTE _huge *pb)
{
    WORD _huge  *lpw;
    RGBQUAD FAR *prgb;
    DWORD       n;
    WORD        w;

    if (lpAvg == NULL)
        return FALSE;

    lpw = lpAvg->lpwSum;

    if (lpAvg->bi.biBitCount == 8) {
        n = lpAvg->bi.biSizeImage;
        while (--n) {
            prgb = &lpAvg->argb[*pb++];
            lpw[0] += prgb->rgbBlue;
            lpw[1] += prgb->rgbGreen;
            lpw[2] += prgb->rgbRed;
            lpw += 3;
        }
    } else if (lpAvg->bi.biBitCount == 16) {
        n = lpAvg->bi.biSizeImage / 2;
        while (--n) {
            w = *(WORD _huge *)pb;
            pb += 2;
            lpw[0] += gab5to8[w & 0x1F];
            lpw[1] += gab5to8[(w & 0x03E0) >> 5];
            lpw[2] += gab5to8[(w & 0x7C00) >> 10];
            lpw += 3;
        }
    } else if (lpAvg->bi.biBitCount == 24) {
        n = lpAvg->bi.biSizeImage;
        while (--n)
            *lpw++ += *pb++;
    } else if (lpAvg->bi.biBitCount == 32) {
        n = lpAvg->bi.biSizeImage / 4;
        while (--n) {
            lpw[0] += pb[0];
            lpw[1] += pb[1];
            lpw[2] += pb[2];
            lpw += 3;
            pb += 4;
        }
    }

    lpAvg->nFrames++;
    return TRUE;
}

/*
 * seg8:236A-2672  AverageFrameEnd
 *
 * Writes the average of the frames added into pb.
 */
BOOL FAR AverageFrameEnd(LPAVERAGE lpAvg, BYTE _huge *pb)
{
    WORD _huge  *lpw;
    DWORD       n;
    UINT        r;
    UINT        g;
    UINT        b;

    if (lpAvg == NULL || lpAvg->nFrames == 0)
        return FALSE;

    lpw = lpAvg->lpwSum;

    if (lpAvg->bi.biBitCount == 8) {
        n = lpAvg->bi.biSizeImage;
        while (--n) {
            b = *lpw++ / lpAvg->nFrames;
            g = *lpw++ / lpAvg->nFrames;
            r = *lpw++ / lpAvg->nFrames;
            *pb++ = lpAvg->lp16to8[((r >> 3) << 10 | (g & 0xF8) << 2 | b >> 3) & 0x7FFF];
        }
    } else if (lpAvg->bi.biBitCount == 16) {
        n = lpAvg->bi.biSizeImage / 2;
        while (--n) {
            b = *lpw++ / lpAvg->nFrames;
            g = *lpw++ / lpAvg->nFrames;
            r = *lpw++ / lpAvg->nFrames;
            *(WORD _huge *)pb = (r >> 3) << 10 | (g & 0xF8) << 2 | b >> 3;
            pb += 2;
        }
    } else if (lpAvg->bi.biBitCount == 24) {
        n = lpAvg->bi.biSizeImage;
        while (--n)
            *pb++ = (BYTE)(*lpw++ / lpAvg->nFrames);
    } else if (lpAvg->bi.biBitCount == 32) {
        n = lpAvg->bi.biSizeImage / 4;
        while (--n) {
            *pb++ = (BYTE)(*lpw++ / lpAvg->nFrames);
            *pb++ = (BYTE)(*lpw++ / lpAvg->nFrames);
            *pb++ = (BYTE)(*lpw++ / lpAvg->nFrames);
            pb++;
        }
    }
    return TRUE;
}

/*
 * seg8:2674-2FF6  AverageShrink2x
 *
 * Averages each 2x2 block of the double size frame (lpbiSrc, pbSrc)
 * into one pixel of the frame (lpbiDst, pbDst).  Also fills in a missing
 * biClrUsed of the source and copies its colour table to an 8-bit
 * destination.  The depths must match.
 */
BOOL FAR AverageShrink2x(LPAVERAGE lpAvg, LPBITMAPINFOHEADER lpbiSrc, BYTE _huge *pbSrc,
                         LPBITMAPINFOHEADER lpbiDst, BYTE _huge *pbDst)
{
    struct {
        WORD            palVersion;
        WORD            palNumEntries;
        PALETTEENTRY    palPalEntry[256];
    } pal;
    WORD            aw5to8[32];
    RGBTRIPLE       t00;
    RGBTRIPLE       t01;
    RGBTRIPLE       t10;
    RGBTRIPLE       t11;
    RGBQUAD FAR     *prgb;
    PALETTEENTRY    *pe;
    BYTE _huge      *pd;
    LPBYTE          lp16to8;
    DWORD           rgb;
    WORD            w00;
    WORD            w01;
    WORD            w10;
    WORD            w11;
    UINT            cbDst;
    UINT            cbSrc;
    UINT            cx;
    UINT            cy;
    UINT            x;
    UINT            y;
    BYTE            r;
    BYTE            g;
    UINT            b;
    UINT            i00;
    UINT            i01;
    UINT            i10;
    UINT            i11;
    int             i;

    if (lpbiSrc->biCompression != BI_RGB)
        return FALSE;

    if (lpbiSrc->biBitCount == 16) {
        for (i = 0; i < 32; i++)
            aw5to8[i] = (WORD)(i * 255) / 31;
    }

    cbDst = WIDTHBYTES((UINT)lpbiDst->biWidth * lpbiDst->biBitCount);
    cbSrc = WIDTHBYTES((UINT)lpbiSrc->biWidth * lpbiSrc->biBitCount);
    cx = (UINT)lpbiSrc->biWidth & ~1;
    cy = (UINT)lpbiSrc->biHeight & ~1;

    if (lpbiSrc->biClrUsed == 0 && lpbiSrc->biBitCount <= 8)
        lpbiSrc->biClrUsed = 1 << lpbiSrc->biBitCount;

    pal.palVersion = 0x300;
    pal.palNumEntries = (WORD)lpbiSrc->biClrUsed;
    prgb = (RGBQUAD FAR *)((LPBYTE)lpbiSrc + sizeof(BITMAPINFOHEADER));
    for (i = 0; i < (int)pal.palNumEntries; i++, prgb++) {
        pal.palPalEntry[i].peRed = prgb->rgbRed;
        pal.palPalEntry[i].peGreen = prgb->rgbGreen;
        pal.palPalEntry[i].peBlue = prgb->rgbBlue;
        pal.palPalEntry[i].peFlags = 0;
    }

    if (lpbiDst->biBitCount == 8)
        _fmemcpy((LPBYTE)lpbiDst + sizeof(BITMAPINFOHEADER),
                 (LPBYTE)lpbiSrc + sizeof(BITMAPINFOHEADER),
                 (UINT)lpbiSrc->biClrUsed * sizeof(RGBQUAD));

    if (lpbiDst->biBitCount != lpbiSrc->biBitCount)
        return FALSE;

    pe = pal.palPalEntry;

    switch (lpbiSrc->biBitCount) {
    case 8:
        for (y = 0; y < cy; y += 2) {
            pd = pbDst;
            for (x = 0; x < cx; x += 2) {
                lp16to8 = lpAvg->lp16to8;
                i00 = pbSrc[x];
                i01 = pbSrc[x + 1];
                i10 = pbSrc[(DWORD)cbSrc + x];
                i11 = pbSrc[(DWORD)cbSrc + x + 1];
                r = (BYTE)((pe[i00].peRed + pe[i01].peRed + pe[i10].peRed + pe[i11].peRed) >> 2);
                g = (BYTE)((pe[i00].peGreen + pe[i01].peGreen + pe[i10].peGreen + pe[i11].peGreen) >> 2);
                b = (pe[i00].peBlue + pe[i01].peBlue + pe[i10].peBlue + pe[i11].peBlue) >> 2;
                *pd++ = lp16to8[(BYTE)b >> 3 | ((UINT)(r >> 3) << 10 | (g & 0xF8) << 2)];
            }
            pbSrc += (DWORD)(cbSrc * 2);
            pbDst += cbDst;
        }
        break;

    case 16:
        for (y = 0; y < cy; y += 2) {
            pd = pbDst;
            for (x = 0; x < cx; x += 2) {
                w00 = *(WORD _huge *)(pbSrc + (DWORD)x * 2);
                w01 = *(WORD _huge *)(pbSrc + (DWORD)(x + 1) * 2);
                w10 = *(WORD _huge *)(pbSrc + (DWORD)x * 2 + cbSrc);
                w11 = *(WORD _huge *)(pbSrc + (DWORD)(x + 1) * 2 + cbSrc);
                r = (BYTE)((int)(((w00 >> 10) & 0x1F) + ((w01 >> 10) & 0x1F) +
                                 ((w10 >> 10) & 0x1F) + ((w11 >> 10) & 0x1F)) >> 2);
                g = (BYTE)((int)(((w11 >> 5) & 0x1F) + ((w10 >> 5) & 0x1F) +
                                 ((w01 >> 5) & 0x1F) + ((w00 >> 5) & 0x1F)) >> 2);
                b = (BYTE)((int)((w11 & 0x1F) + (w10 & 0x1F) + (w01 & 0x1F) + (w00 & 0x1F)) >> 2);
                *(WORD _huge *)pd = b | (UINT)g << 5 | (UINT)r << 10;
                pd += 2;
            }
            pbSrc += (DWORD)(cbSrc * 2);
            pbDst += cbDst;
        }
        break;

    case 24:
        for (y = 0; y < cy; y += 2) {
            pd = pbDst;
            for (x = 0; x < cx; x += 2) {
                t00 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)x * 3);
                t01 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)(x + 1) * 3);
                t10 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)x * 3 + cbSrc);
                t11 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)(x + 1) * 3 + cbSrc);
                rgb = RGB((t11.rgbtRed + t10.rgbtRed + t01.rgbtRed + t00.rgbtRed) >> 2,
                          (t11.rgbtGreen + t10.rgbtGreen + t01.rgbtGreen + t00.rgbtGreen) >> 2,
                          (t11.rgbtBlue + t10.rgbtBlue + t01.rgbtBlue + t00.rgbtBlue) >> 2);
                *pd++ = GetBValue(rgb);
                *pd++ = GetGValue(rgb);
                *pd++ = GetRValue(rgb);
            }
            pbSrc += (DWORD)(cbSrc * 2);
            pbDst += cbDst;
        }
        break;

    case 32:
        for (y = 0; y < cy; y += 2) {
            pd = pbDst;
            for (x = 0; x < cx; x += 2) {
                t00 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)x * 3);
                t01 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)(x + 1) * 3);
                t10 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)x * 3 + cbSrc);
                t11 = *(RGBTRIPLE _huge *)(pbSrc + (DWORD)(x + 1) * 3 + cbSrc);
                rgb = RGB((t11.rgbtRed + t10.rgbtRed + t01.rgbtRed + t00.rgbtRed) >> 2,
                          (t11.rgbtGreen + t10.rgbtGreen + t01.rgbtGreen + t00.rgbtGreen) >> 2,
                          (t11.rgbtBlue + t10.rgbtBlue + t01.rgbtBlue + t00.rgbtBlue) >> 2);
                *pd++ = GetBValue(rgb);
                *pd++ = GetGValue(rgb);
                *pd++ = GetRValue(rgb);
                pd++;
            }
            pbSrc += (DWORD)(cbSrc * 2);
            pbDst += cbDst;
        }
        break;

    default:
        return FALSE;
    }
    return TRUE;
}
