/*
 * fullfrm.c - code segment 6 of VIDEDIT.EXE, s06:6108-70AB: full frames
 *
 * A frame that is an RLE delta, or covers only part of the movie frame,
 * builds on the frames before it.  The full frame of frame n is made by
 * going back to a complete frame and drawing the frames from there up to
 * n into a DIB of the movie's size.  The result is kept (a locked DIB
 * known by its selector, gselFullFrame) together with the next frame to
 * draw into it, so that stepping forward costs one frame.  FramesChanged
 * marks it stale.
 *
 * RLE delta frames are drawn with their skipped pixels transparent: the
 * RLE data is drawn once in a single colour over white to get a mask,
 * once over black for the image, and the two are combined with the
 * destination.
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include <string.h>
#include "frames.h"

/* DS:129C-12C7 */
BOOL            gfPalIndexReady;        /* DS:129C framedrw.c's gbmiPalIndex filled in */
BOOL            gfFullFrameStale;       /* DS:129E */
static WORD     gselFullFrame;          /* DS:12A0 the full frame (a DIB, locked) */
static HPALETTE ghpalFullFrame;         /* DS:12A2 its palette */
static UINT     giFullNext;             /* DS:12A4 next frame to draw into it */
static UINT     giFullFrame;            /* DS:12A6 frame it shows */
                                        /* DS:12A8 "Constructing Full Frame: %u", DS:12C4 "dib" */

static BOOL     gfDIBDriverDC;          /* DS:317C StretchRLEDIBits draws on a DIB.DRV DC */

static BOOL NEAR PASCAL IsDIBDriverDC(HDC hdc);
static void NEAR PASCAL TransparentStretchBlt(HDC hdcDst, int xd, int yd, int wd, int hd,
                                              HDC hdcSrc, HDC hdcMask, int xs, int ys,
                                              int ws, int hs);

/*
 * s06:6108-6283  FullFrameSetColors  (NEAR)
 *
 * Gives the full frame, if palette-based, as many colours as the movie
 * (gnMovieColors), moving the bits and resizing the DIB, and fills its colour
 * table from its palette.
 */
static BOOL NEAR FullFrameSetColors(void)
{
    LPBITMAPINFOHEADER  lpbi;
    LPBYTE              lpBits;
    int                 cbDelta;

    lpbi = (LPBITMAPINFOHEADER)MAKELP(gselFullFrame, 0);
    if (lpbi->biBitCount > 8)
        return TRUE;

    if (lpbi->biClrUsed != (DWORD)gnMovieColors) {
        cbDelta = (gnMovieColors - (WORD)lpbi->biClrUsed) << 2;

        if (cbDelta > 0) {
            gselFullFrame = GReAllocSelF(gselFullFrame,
                                         GlobalSize((HGLOBAL)gselFullFrame) + (LONG)cbDelta, 0);
            if (gselFullFrame == 0)
                return FALSE;
            lpbi = (LPBITMAPINFOHEADER)MAKELP(gselFullFrame, 0);
        }

        lpBits = (LPBYTE)lpbi + (WORD)lpbi->biClrUsed * 4 + (WORD)lpbi->biSize;
        hmemmove(lpBits + cbDelta, lpBits, lpbi->biSizeImage);

        if (cbDelta < 0) {
            gselFullFrame = GReAllocSelF(gselFullFrame,
                                         GlobalSize((HGLOBAL)gselFullFrame) + (LONG)cbDelta, 0);
            if (gselFullFrame == 0)
                return FALSE;
            lpbi = (LPBITMAPINFOHEADER)MAKELP(gselFullFrame, 0);
        }
    }

    lpbi->biClrUsed = gnMovieColors;
    SetDibUsageFill((HANDLE)gselFullFrame, ghpalFullFrame, DIB_RGB_COLORS);
    return TRUE;
}

/*
 * s06:6284-62D0  FindCompleteFrame  (NEAR PASCAL)
 *
 * Moves *piFrame back to the nearest complete frame (frame 0 at the
 * latest).  With GFF_ESCAPE, Escape cancels (FALSE).
 */
static BOOL NEAR PASCAL FindCompleteFrame(UINT NEAR *piFrame, WORD wFlags)
{
    if (wFlags & GFF_ESCAPE)
        GetAsyncKeyState(VK_ESCAPE);

    while (*piFrame != 0 && GetFrameInfo(*piFrame, FI_COMPLETE) == 0) {
        (*piFrame)--;
        if ((wFlags & GFF_ESCAPE) && (GetAsyncKeyState(VK_ESCAPE) & 1))
            return FALSE;
    }
    return TRUE;
}

/*
 * s06:62D4-6AE8  GetFullFrame
 *
 * The full frame of frame iFrame (the selector of the DIB, which stays
 * owned here), with its palette in *phpal; 0 on error, 1 if cancelled
 * with Escape.  GFF_REBUILD: don't take a complete frame's own DIB as it
 * is; GFF_CONVERT: convert to the movie's bit depth; GFF_STATUS: always
 * show progress.  A movie-sized uncompressed frame is copied, an empty
 * frame repeats the previous full frame, and otherwise the frames from
 * the last complete one (or from where the kept full frame got to) are
 * drawn: identical-size frames by copying the bits or decompressing the
 * RLE, others through DIB.DRV (or StretchDIB if it isn't there).
 */
WORD FAR PASCAL GetFullFrame(UINT iFrame, HPALETTE NEAR *phpal, WORD wFlags)
{
    char                ach[50];
    LPBITMAPINFOHEADER  lpbi;
    LPBITMAPINFOHEADER  lpbiFull;
    HPFRAME             pfr;
    HPALETTE            hpal;
    HCURSOR             hcurOld = 0;
    HANDLE              hdib;
    HDC                 hdcDIB;
    BOOL                fRLE;
    UINT                iKey;
    UINT                i;
    int                 cxSrc;
    int                 cySrc;

    if (iFrame >= gwLengthShown)
        return 0;

    if (gfFullFrameStale) {
        if (gselFullFrame) {
            GlobalUnlock((HGLOBAL)gselFullFrame);
            GlobalFree((HGLOBAL)gselFullFrame);
            gselFullFrame = 0;
        }
        ghpalFullFrame = 0;
        giFullNext = 0;
        giFullFrame = 0;
        gfFullFrameStale = FALSE;
    }

    if (iFrame - giFullNext == 0xFFFF) {
        /* it is already there */
        if (wFlags & GFF_CONVERT) {
            if (DibConvertBitCount((HANDLE FAR *)&gselFullFrame, (HPALETTE FAR *)&ghpalFullFrame,
                                   gwVideoBits ? gwVideoBits : 8))
                gfFullFrameStale = TRUE;
        }
        if (phpal)
            *phpal = ghpalFullFrame;
        return gselFullFrame;
    }

    if ((wFlags & GFF_REBUILD) && GetFrameInfo(iFrame, FI_COMPLETE) == 0)
        goto Build;
    if (iFrame - giFullNext == 0xFFFF)
        goto Build;

    hdib = GetFrameDIB(iFrame, &hpal, 0);
    if (hdib == 0)
        goto NoDIB;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
    if ((wFlags & GFF_CONVERT) && lpbi->biBitCount != gwVideoBits)
        wFlags |= GFF_REBUILD;
    fRLE = (lpbi->biCompression == BI_RLE8);
    GlobalUnlock(hdib);

    if (!fRLE && lpbi->biWidth >= (LONG)gcxMovie && lpbi->biHeight >= (LONG)gcyMovie) {
        /* a whole uncompressed frame: draw just it */
        giFullNext = iFrame;
        giFullFrame = iFrame;
        ghpalFullFrame = hpal;
        goto Composite;
    }

    if (wFlags & GFF_REBUILD)
        goto Build;

    pfr = &gpFrames[iFrame];
    if (pfr->rcDst.left == 0 && pfr->rcDst.right == (int)gcxMovie &&
        pfr->rcDst.top == 0 && pfr->rcDst.bottom == (int)gcyMovie &&
        pfr->rcSrc.left == 0 && pfr->rcSrc.right == (int)gcxMovie &&
        (WORD)lpbi->biWidth == gcxMovie &&
        pfr->rcSrc.top == 0 && pfr->rcSrc.bottom == (int)gcyMovie &&
        (WORD)lpbi->biHeight == gcyMovie &&
        pfr->selRemap == 0) {
        /* a movie-sized frame: a copy of it will do (not kept for the next frame) */
        ghpalFullFrame = hpal;
        gfFullFrameStale = TRUE;
        if (gselFullFrame) {
            GlobalUnlock((HGLOBAL)gselFullFrame);
            GlobalFree((HGLOBAL)gselFullFrame);
            gselFullFrame = 0;
        }
        gselFullFrame = CopyHandle((HANDLE)SELECTOROF(GlobalLock(hdib)));
        goto Finish;
    }
    goto Build;

NoDIB:
    if (iFrame < gnFrames && !(gpFrames[iFrame].medid == 0 && gpFrames[iFrame].wFrame == 0))
        return 0;
    if (gselFullFrame && giFullFrame == iFrame) {
        /* an empty frame shows the frame before it */
        giFullFrame++;
        goto Finish;
    }

Build:
    if (iFrame + 1 < giFullNext) {
        giFullNext = 0;
        giFullFrame = 0;
        if (gselFullFrame)
            GlobalFree((HGLOBAL)gselFullFrame);
        gselFullFrame = 0;
    }

    iKey = iFrame;
    if (!FindCompleteFrame(&iKey, wFlags))
        return 1;
    if (iKey > giFullNext)
        giFullNext = iKey;

    if ((wFlags & GFF_STATUS) || giFullNext + 2 < iFrame)
        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    GetAsyncKeyState(VK_ESCAPE);

    while (giFullNext <= iFrame) {
        giFullFrame = giFullNext;

        if ((wFlags & GFF_STATUS) || (iFrame != 0 && iFrame - 1 > giFullNext)) {
            wsprintf(ach, "Constructing Full Frame: %u", giFullNext);
            StatusSetMessage(ach);
        }
        if ((wFlags & GFF_ESCAPE) && (GetAsyncKeyState(VK_ESCAPE) & 1))
            goto Cancel;

        hdib = GetFrameDIB(giFullNext, &hpal, GFD_NOCACHE);
        if (hdib == 0) {
            i = giFullNext;
            if (i < gnFrames && !(gpFrames[i].medid == 0 && gpFrames[i].wFrame == 0))
                return 0;
            goto Next;
        }

        ghpalFullFrame = hpal;
        fRLE = (((LPBITMAPINFOHEADER)GlobalLock(hdib))->biCompression == BI_RLE8);
        GlobalUnlock(hdib);

Composite:
        lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
        if (gselFullFrame == 0) {
            gselFullFrame = CreateDib(lpbi->biBitCount, gcxMovie, gcyMovie);
            if (gselFullFrame == 0)
                return 0;
        }
        if (((LPBITMAPINFOHEADER)MAKELP(gselFullFrame, 0))->biBitCount != lpbi->biBitCount) {
            if (gselFullFrame) {
                GlobalUnlock((HGLOBAL)gselFullFrame);
                GlobalFree((HGLOBAL)gselFullFrame);
            }
            gselFullFrame = CreateDib(lpbi->biBitCount, gcxMovie, gcyMovie);
            if (gselFullFrame == 0)
                return 0;
        }

        lpbiFull = (LPBITMAPINFOHEADER)MAKELP(gselFullFrame, 0);
        if (!FullFrameSetColors())
            return 0;

        pfr = &gpFrames[giFullNext];
        if (pfr->rcDst.left == 0 && pfr->rcDst.right == (int)gcxMovie &&
            pfr->rcDst.top == 0 && pfr->rcDst.bottom == (int)gcyMovie &&
            pfr->rcSrc.left == 0 && pfr->rcSrc.right == (int)gcxMovie &&
            (WORD)lpbi->biWidth == gcxMovie &&
            pfr->rcSrc.top == 0 && pfr->rcSrc.bottom == (int)gcyMovie &&
            (WORD)lpbi->biHeight == gcyMovie &&
            pfr->selRemap == 0) {
            if (fRLE)
                RleDecompressDib(gselFullFrame, (WORD)SELECTOROF(GlobalLock(hdib)));
            else {
                if (lpbi->biSizeImage != lpbiFull->biSizeImage)
                    lpbi->biSizeImage = lpbiFull->biSizeImage;
                hmemmove((LPBYTE)lpbiFull + (WORD)lpbiFull->biClrUsed * 4 + (WORD)lpbiFull->biSize,
                         (LPBYTE)lpbi + (WORD)lpbi->biClrUsed * 4 + (WORD)lpbi->biSize,
                         lpbi->biSizeImage);
            }
        } else {
            hdcDIB = CreateDC("dib", NULL, NULL, lpbiFull);
            if (hdcDIB) {
                pfr = &gpFrames[giFullNext];
                DrawFrameDIB(hdcDIB, hdib, ghpalFullFrame, (RECT FAR *)&pfr->rcSrc,
                             (RECT FAR *)&pfr->rcDst, 2, pfr->wFlags);
                DeleteDC(hdcDIB);
            } else {
                pfr = &gpFrames[giFullNext];
                cxSrc = pfr->rcSrc.right - pfr->rcSrc.left;
                cySrc = pfr->rcSrc.bottom - pfr->rcSrc.top;
                StretchDIB(lpbiFull, DibXY(lpbiFull, 0, 0),
                           pfr->rcDst.left, (int)lpbiFull->biHeight - pfr->rcDst.bottom,
                           pfr->rcDst.right - pfr->rcDst.left, pfr->rcDst.bottom - pfr->rcDst.top,
                           lpbi, DibXY(lpbi, 0, 0),
                           pfr->rcSrc.left, (int)lpbi->biHeight - pfr->rcSrc.bottom,
                           cxSrc, cySrc);
            }
        }

        GlobalUnlock(hdib);
        i = giFullNext;
Next:
        giFullNext = i + 1;
    }

Finish:
    if (!(wFlags & GFF_STATUS))
        StatusSetMessage(NULL);
    if (hcurOld)
        SetCursor(hcurOld);

    if (gselFullFrame == 0) {
        gselFullFrame = CreateDib(gwVideoBits ? gwVideoBits : 8, gcxMovie, gcyMovie);
        if (gselFullFrame == 0)
            return 0;
        hpal = 0;
        for (i = iFrame + 1; i < gwLength && hpal == 0; i++)
            GetFrameDIB(i, &hpal, GFD_PALETTE);
        if (hpal)
            ghpalFullFrame = hpal;
    }

    if (!FullFrameSetColors())
        return 0;

    if (wFlags & GFF_CONVERT) {
        if (DibConvertBitCount((HANDLE FAR *)&gselFullFrame, (HPALETTE FAR *)&ghpalFullFrame,
                               gwVideoBits ? gwVideoBits : 8)) {
            gfFullFrameStale = TRUE;
            if (!FullFrameSetColors())
                return 0;
        }
    }

    if (phpal)
        *phpal = ghpalFullFrame;

    if ((wFlags & GFF_ESCAPE) && (GetAsyncKeyState(VK_ESCAPE) & 1))
        goto Cancel;
    return gselFullFrame;

Cancel:
    if (!(wFlags & GFF_STATUS))
        StatusSetMessage(NULL);
    if (hcurOld)
        SetCursor(hcurOld);
    return 1;
}

/*
 * s06:6AEC-6EFC  StretchRLEDIBits
 *
 * StretchDIBits for an RLE8 delta frame, leaving the skipped pixels of
 * the destination alone.  Without stretching StretchDIBits does that by
 * itself.  Otherwise the RLE data is drawn into a bitmap of the DIB's
 * size twice, in one colour over white (the mask) and in its colours
 * over black (the image), both are stretched and combined with the
 * destination.  On a DIB.DRV DC the colours are DIB indices and the
 * mask is blitted with the NEWTRANSPARENT background mode.
 *
 * When a bitmap or DC can't be made nothing is drawn or cleaned up, and
 * the original returns whatever is in AX (here the image bitmap).
 */
int FAR PASCAL StretchRLEDIBits(HDC hdc, int xd, int yd, int wd, int hd, int xs, int ys,
                                int ws, int hs, LPVOID lpBits, LPBITMAPINFOHEADER lpbi,
                                UINT wUsage, DWORD rop)
{
    struct {
        BITMAPINFOHEADER    bmiHeader;
        RGBQUAD             bmiColors[256];
    } bmi;
    struct {
        WORD                palVersion;
        WORD                palNumEntries;
        PALETTEENTRY        palPalEntry[256];
    } lp;
    HPALETTE    hpalOld;
    HPALETTE    hpalExplicit;
    HPALETTE    hpalImageOld;
    HPALETTE    hpalMaskOld;
    HPALETTE    hpalFullOld;
    HDC         hdcFull;
    HDC         hdcMask;
    HDC         hdcImage;
    HBITMAP     hbmFull;
    HBITMAP     hbmMask;
    HBITMAP     hbmImage;
    HBITMAP     hbmImageOld;
    HBITMAP     hbmMaskOld;
    HBITMAP     hbmFullOld;
    HBRUSH      hbrOld;
    int         cx;
    int         cy;
    int         i;

    if (lpbi->biSizeImage <= 2)
        return 1;

    if (ws == wd && hs == hd)
        return StretchDIBits(hdc, xd, yd, wd, hd, xs, ys, ws, hs, lpBits, (LPBITMAPINFO)lpbi,
                             wUsage, rop);

    gfDIBDriverDC = IsDIBDriverDC(hdc);
    SetStretchBltMode(hdc, COLORONCOLOR);
    hpalOld = SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
    SelectPalette(hdc, hpalOld, FALSE);

    /* a palette of indices into the hardware palette */
    lp.palVersion = 0x300;
    lp.palNumEntries = 256;
    for (i = 0; i < 256; i++) {
        lp.palPalEntry[i].peRed = (BYTE)i;
        lp.palPalEntry[i].peGreen = 0;
        lp.palPalEntry[i].peBlue = 0;
        lp.palPalEntry[i].peFlags = PC_EXPLICIT;
    }
    hpalExplicit = CreatePalette((LPLOGPALETTE)&lp);

    cx = (int)lpbi->biWidth;
    cy = (int)lpbi->biHeight;

    hdcFull = CreateCompatibleDC(hdc);
    hbmFull = CreateCompatibleBitmap(hdc, cx, cy);
    hdcMask = CreateCompatibleDC(hdc);
    hbmMask = CreateCompatibleBitmap(hdc, wd, hd);
    hdcImage = CreateCompatibleDC(hdc);
    hbmImage = CreateCompatibleBitmap(hdc, wd, hd);
    if (hbmImage == 0 || hdcImage == 0 || hbmMask == 0 || hdcMask == 0)
        return (int)hbmImage;

    hbmImageOld = SelectObject(hdcImage, hbmImage);
    hbmMaskOld = SelectObject(hdcMask, hbmMask);
    hbmFullOld = SelectObject(hdcFull, hbmFull);
    hpalImageOld = SelectPalette(hdcImage, hpalExplicit, FALSE);
    RealizePalette(hdcImage);
    hpalMaskOld = SelectPalette(hdcMask, hpalExplicit, FALSE);
    RealizePalette(hdcMask);
    hpalFullOld = SelectPalette(hdcFull, hpalExplicit, FALSE);
    RealizePalette(hdcFull);

    /* the mask: the RLE pixels in one colour over the background */
    if (gfDIBDriverDC) {
        hbrOld = SelectObject(hdcFull, CreateSolidBrush(0x10FF0001L));
        PatBlt(hdcFull, 0, 0, cx, cy, PATCOPY);
        DeleteObject(SelectObject(hdcFull, hbrOld));
    } else
        PatBlt(hdcFull, 0, 0, cx, cy, WHITENESS);

    bmi.bmiHeader = *lpbi;
    if (gfDIBDriverDC) {
        for (i = 0; i < 256; i++) {
            bmi.bmiColors[i].rgbRed = 0;
            bmi.bmiColors[i].rgbGreen = 0;
            bmi.bmiColors[i].rgbBlue = 0xFF;
            bmi.bmiColors[i].rgbReserved = 0x10;
        }
        SetDIBits(hdc, hbmFull, 0, cy, lpBits, (LPBITMAPINFO)&bmi, DIB_RGB_COLORS);
    } else {
        SelectPalette(hdc, hpalExplicit, FALSE);
        RealizePalette(hdc);
        _fmemset(bmi.bmiColors, 0, 256 * sizeof(WORD));
        SetDIBits(hdc, hbmFull, 0, cy, lpBits, (LPBITMAPINFO)&bmi, DIB_PAL_COLORS);
        SelectPalette(hdc, hpalOld, FALSE);
        RealizePalette(hdc);
    }

    SetStretchBltMode(hdcMask, COLORONCOLOR);
    StretchBlt(hdcMask, 0, 0, wd, hd, hdcFull, xs, (int)lpbi->biHeight - hs - ys, ws, hs, SRCCOPY);

    /* the image: the RLE pixels over black */
    PatBlt(hdcFull, 0, 0, cx, cy, BLACKNESS);
    SetDIBits(hdc, hbmFull, 0, cy, lpBits, (LPBITMAPINFO)lpbi, wUsage);
    SetStretchBltMode(hdcImage, COLORONCOLOR);
    StretchBlt(hdcImage, 0, 0, wd, hd, hdcFull, xs, (int)lpbi->biHeight - hs - ys, ws, hs, SRCCOPY);

    TransparentStretchBlt(hdc, xd, yd, wd, hd, hdcImage, hdcMask, 0, 0, wd, hd);

    SelectPalette(hdcFull, hpalFullOld, FALSE);
    SelectPalette(hdcImage, hpalImageOld, FALSE);
    SelectPalette(hdcMask, hpalMaskOld, FALSE);
    SelectPalette(hdc, hpalOld, FALSE);
    DeleteObject(hpalExplicit);

    SelectObject(hdcMask, hbmMaskOld);
    DeleteDC(hdcMask);
    DeleteObject(hbmMask);
    SelectObject(hdcImage, hbmImageOld);
    DeleteDC(hdcImage);
    DeleteObject(hbmImage);
    SelectObject(hdcFull, hbmFullOld);
    DeleteDC(hdcFull);
    return DeleteObject(hbmFull);
}

/*
 * s06:6F00-6F6C  RleHasSkip  (NEAR PASCAL, unreferenced)
 *
 * TRUE if RLE8 data contains a delta (skip) before its end.
 */
static BOOL NEAR PASCAL RleHasSkip(BYTE _huge *pb)
{
    BYTE    b0;
    BYTE    b1;

    for (;;) {
        b0 = *pb++;
        b1 = *pb++;
        if (b0 != 0)
            continue;
        switch (b1) {
        case 0:                         /* end of line */
            continue;
        case 1:                         /* end of bitmap */
            return FALSE;
        case 2:                         /* delta */
            return TRUE;
        default:                        /* absolute run, word aligned */
            pb += (b1 + 1) & ~1;
            break;
        }
    }
}

/*
 * s06:6F70-704C  TransparentStretchBlt  (NEAR PASCAL)
 *
 * Draws hdcSrc on hdcDst where hdcMask is black.  On a DIB.DRV DC the
 * mask colour is made transparent with NEWTRANSPARENT and the image
 * XORed on; otherwise the destination is ANDed into the mask, the image
 * XORed onto that and the result copied.
 *
 * Bug of the original: in the second case the mask DC is addressed with
 * the destination's coordinates and the destination with the source's,
 * which only works when xd and yd are 0.
 */
static void NEAR PASCAL TransparentStretchBlt(HDC hdcDst, int xd, int yd, int wd, int hd,
                                              HDC hdcSrc, HDC hdcMask, int xs, int ys,
                                              int ws, int hs)
{
    DWORD   crOld;
    int     nModeOld;

    if (gfDIBDriverDC) {
        nModeOld = SetBkMode(hdcDst, 3);        /* NEWTRANSPARENT */
        crOld = SetBkColor(hdcDst, 0x10FF0001L);
        StretchBlt(hdcDst, xd, yd, wd, hd, hdcMask, xs, ys, ws, hs, SRCCOPY);
        SetBkColor(hdcDst, crOld);
        SetBkMode(hdcDst, nModeOld);
        StretchBlt(hdcDst, xd, yd, wd, hd, hdcSrc, xs, ys, ws, hs, SRCINVERT);
    } else {
        StretchBlt(hdcMask, xd, yd, wd, hd, hdcDst, xs, ys, ws, hs, SRCAND);
        StretchBlt(hdcMask, xd, yd, wd, hd, hdcSrc, xs, ys, ws, hs, SRCINVERT);
        StretchBlt(hdcDst, xd, yd, wd, hd, hdcMask, xs, ys, ws, hs, SRCCOPY);
    }
}

/*
 * s06:7050-70A8  IsDIBDriverDC  (NEAR PASCAL)
 *
 * TRUE for a DC at origin 0 that can't select a bitmap: a DIB.DRV DC.
 */
static BOOL NEAR PASCAL IsDIBDriverDC(HDC hdc)
{
    HBITMAP hbm;
    HBITMAP hbmOld;

    if (GetDCOrg(hdc) != 0)
        return FALSE;

    hbm = CreateBitmap(1, 1, 1, 1, NULL);
    hbmOld = SelectObject(hdc, hbm);
    if (hbmOld)
        SelectObject(hdc, hbmOld);
    DeleteObject(hbm);
    return hbmOld == 0;
}
