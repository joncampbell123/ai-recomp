/*
 * dib.c - VIDEDIT.EXE code segment 4, offsets 2EAC-4B29: DIB utilities.
 *
 * This is the DIB.C that came with the Windows 3.0 SDK samples (ShowDIB)
 * and later VfW tools, in a version where a DIB "handle" is the selector
 * of a locked global block: GlobalLock is replaced by MAKELP(h, 0) and
 * allocations go through the gmem macros (edit.h).  Several functions
 * are not used by VidEdit; they were linked because they are in the same
 * module.
 *
 * All functions are FAR _cdecl except lread and lwrite (NEAR PASCAL).
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "vemain.h"
#include "edit.h"

#define BFT_BITMAP          0x4D42      /* 'BM' */
#define MAXREAD             0x8000

#define DibLock(h)          ((LPBITMAPINFOHEADER)MAKELP(h, 0))

/* standard VGA colours, RGBQUADs as DWORDs (DS:1244) */
static DWORD rgbStd[16] = {
    0x00000000L, 0x00800000L, 0x00008000L, 0x00808000L,
    0x00000080L, 0x00800080L, 0x00008080L, 0x00C0C0C0L,
    0x00808080L, 0x00FF0000L, 0x0000FF00L, 0x00FFFF00L,
    0x000000FFL, 0x00FF00FFL, 0x0000FFFFL, 0x00FFFFFFL
};

static DWORD NEAR PASCAL lread(int fh, LPVOID pv, DWORD ul);
static DWORD NEAR PASCAL lwrite(int fh, LPVOID pv, DWORD ul);

/*
 * s04:2EAC-2F93  OpenDIB  (unreferenced)
 *
 * Reads a DIB file.  A "file name" with a zero selector is an open file
 * handle (in the offset), which is left open.
 */
HANDLE FAR OpenDIB(LPSTR szFile)
{
    int                 fh;
    BITMAPINFOHEADER    bi;
    LPBITMAPINFOHEADER  lpbi;
    DWORD               dwLen;
    HANDLE              hdib, h;
    OFSTRUCT            of;

    if (SELECTOROF(szFile) == 0)
        fh = OFFSETOF(szFile);
    else
        fh = OpenFile(szFile, &of, OF_READ);
    if (fh == -1)
        return NULL;

    hdib = ReadDibBitmapInfo(fh);
    if (hdib == NULL)
        return NULL;
    DibInfo(hdib, &bi);

    dwLen = bi.biSizeImage;
    h = (ghMemTemp = GlobalReAlloc(hdib, dwLen + PaletteSize(&bi) + bi.biSize, 0)) != 0 ?
        SELECTOROF(GlobalLock(ghMemTemp)) : 0;
    if (h == 0) {
        GlobalUnlock(hdib);
        GlobalFree(hdib);
        hdib = NULL;
    } else {
        hdib = h;
    }

    if (hdib) {
        lpbi = DibLock(hdib);
        lread(fh, (LPSTR)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi), dwLen);
    }

    if (SELECTOROF(szFile))
        _lclose(fh);
    return hdib;
}

/*
 * s04:2F94-3077  WriteDIB  (unreferenced)
 */
BOOL FAR WriteDIB(LPSTR szFile, HANDLE hdib)
{
    BITMAPFILEHEADER    hdr;
    LPBITMAPINFOHEADER  lpbi;
    BITMAPINFOHEADER    bi;
    int                 fh;
    OFSTRUCT            of;
    DWORD               dwSize;

    if (!hdib)
        return FALSE;

    if (SELECTOROF(szFile) == 0)
        fh = OFFSETOF(szFile);
    else
        fh = OpenFile(szFile, &of, OF_CREATE | OF_READWRITE);
    if (fh == -1)
        return FALSE;

    DibInfo(hdib, &bi);
    dwSize = bi.biSize + PaletteSize(&bi) + bi.biSizeImage;

    lpbi = DibLock(hdib);
    hdr.bfType = BFT_BITMAP;
    hdr.bfSize = dwSize + sizeof(BITMAPFILEHEADER);
    hdr.bfReserved1 = 0;
    hdr.bfReserved2 = 0;
    hdr.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + lpbi->biSize + PaletteSize(lpbi);

    _lwrite(fh, (LPSTR)&hdr, sizeof(BITMAPFILEHEADER));
    lwrite(fh, (LPVOID)lpbi, dwSize);

    if (SELECTOROF(szFile))
        _lclose(fh);
    return TRUE;
}

/*
 * s04:3078-316D  DibInfo
 *
 * The header of a DIB as a BITMAPINFOHEADER, with biSizeImage and
 * biClrUsed filled in.  The core header's width and height are sign
 * extended.
 */
BOOL FAR DibInfo(HANDLE hbi, LPBITMAPINFOHEADER lpbi)
{
    BITMAPCOREHEADER bc;

    if (hbi) {
        *lpbi = *DibLock(hbi);

        if (lpbi->biSize == sizeof(BITMAPCOREHEADER)) {
            bc = *(LPBITMAPCOREHEADER)lpbi;
            lpbi->biSize = sizeof(BITMAPINFOHEADER);
            lpbi->biWidth = (LONG)(int)bc.bcWidth;
            lpbi->biHeight = (LONG)(int)bc.bcHeight;
            lpbi->biPlanes = bc.bcPlanes;
            lpbi->biBitCount = bc.bcBitCount;
            lpbi->biCompression = BI_RGB;
            lpbi->biSizeImage = 0;
            lpbi->biXPelsPerMeter = 0;
            lpbi->biYPelsPerMeter = 0;
            lpbi->biClrUsed = 0;
            lpbi->biClrImportant = 0;
        }

        if (lpbi->biSizeImage == 0)
            lpbi->biSizeImage = (LONG)(int)((((WORD)lpbi->biWidth * lpbi->biBitCount + 31) & ~31) >> 3)
                                * lpbi->biHeight;

        if (lpbi->biClrUsed == 0)
            lpbi->biClrUsed = DibNumColors(lpbi);
        return TRUE;
    }
    return FALSE;
}

/*
 * s04:316E-3231  CreateBIPalette
 */
HPALETTE FAR CreateBIPalette(LPBITMAPINFOHEADER lpbi)
{
    LOGPALETTE     *pPal;
    HPALETTE        hpal = NULL;
    int             nNumColors;
    int             i;
    RGBQUAD FAR    *pRgb;
    BOOL            fCore;

    if (!lpbi)
        return NULL;

    fCore = (lpbi->biSize == sizeof(BITMAPCOREHEADER));
    pRgb = (RGBQUAD FAR *)((LPSTR)lpbi + (WORD)lpbi->biSize);
    nNumColors = DibNumColors(lpbi);

    if (nNumColors) {
        pPal = (LOGPALETTE *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + nNumColors * sizeof(PALETTEENTRY));
        if (!pPal)
            return hpal;

        pPal->palNumEntries = nNumColors;
        pPal->palVersion = 0x300;
        for (i = 0; i < nNumColors; i++) {
            pPal->palPalEntry[i].peRed = pRgb->rgbRed;
            pPal->palPalEntry[i].peGreen = pRgb->rgbGreen;
            pPal->palPalEntry[i].peBlue = pRgb->rgbBlue;
            pPal->palPalEntry[i].peFlags = 0;
            pRgb = (RGBQUAD FAR *)((LPBYTE)pRgb + (fCore ? sizeof(RGBTRIPLE) : sizeof(RGBQUAD)));
        }
        hpal = CreatePalette(pPal);
        LocalFree((HANDLE)pPal);
    }
    return hpal;
}

/*
 * s04:3232-324F  CreateDibPalette
 */
HPALETTE FAR CreateDibPalette(HANDLE hdib)
{
    if (!hdib)
        return NULL;
    return CreateBIPalette(DibLock(hdib));
}

/*
 * s04:3250-32FB  CreateColorPalette  (unreferenced)
 *
 * A 6x6x6 colour cube.  The levels are 255 * i / 6, so the brightest is
 * 212, not 255.
 */
HPALETTE FAR CreateColorPalette(void)
{
    LOGPALETTE     *pPal;
    PALETTEENTRY   *ppe;
    HPALETTE        hpal = NULL;
    BYTE            r, g, b;

    pPal = (LOGPALETTE *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + 216 * sizeof(PALETTEENTRY));
    if (pPal) {
        pPal->palNumEntries = 216;
        pPal->palVersion = 0x300;
        ppe = pPal->palPalEntry;
        for (r = 0; r < 6; r++)
            for (g = 0; g < 6; g++)
                for (b = 0; b < 6; b++) {
                    ppe->peRed = (BYTE)((WORD)(r * 255) / 6);
                    ppe->peGreen = (BYTE)((WORD)(g * 255) / 6);
                    ppe->peBlue = (BYTE)((WORD)(b * 255) / 6);
                    ppe->peFlags = 0;
                    ppe++;
                }
        hpal = CreatePalette(pPal);
        LocalFree((HANDLE)pPal);
    }
    return hpal;
}

/*
 * s04:32FC-33CD  CreateSystemPalette  (unreferenced)
 *
 * A copy of the system palette with the non-static entries marked
 * PC_NOCOLLAPSE.
 */
HPALETTE FAR CreateSystemPalette(void)
{
    HDC         hdc;
    int         nColors, nStatic, i;
    LOGPALETTE *pPal;
    HPALETTE    hpal = NULL;

    hdc = GetDC(NULL);
    nColors = GetDeviceCaps(hdc, SIZEPALETTE);
    if (GetSystemPaletteUse(hdc) == SYSPAL_STATIC)
        nStatic = GetDeviceCaps(hdc, NUMRESERVED);
    else
        nStatic = 2;

    pPal = (LOGPALETTE *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + nColors * sizeof(PALETTEENTRY));
    if (pPal) {
        pPal->palVersion = 0x300;
        pPal->palNumEntries = nColors;
        GetSystemPaletteEntries(hdc, 0, nColors, pPal->palPalEntry);
        for (i = nStatic / 2; i < nColors - nStatic / 2; i++)
            pPal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
        hpal = CreatePalette(pPal);
        LocalFree((HANDLE)pPal);
    }
    ReleaseDC(NULL, hdc);
    return hpal;
}

/*
 * s04:33CE-342F  CopyPalette
 */
HPALETTE FAR CopyPalette(HPALETTE hpal)
{
    LOGPALETTE *pPal;
    int         nNumEntries;

    GetObject(hpal, sizeof(int), (LPSTR)&nNumEntries);
    pPal = (LOGPALETTE *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + nNumEntries * sizeof(PALETTEENTRY));
    if (!pPal)
        return NULL;
    pPal->palVersion = 0x300;
    pPal->palNumEntries = nNumEntries;
    GetPaletteEntries(hpal, 0, nNumEntries, pPal->palPalEntry);
    hpal = CreatePalette(pPal);
    LocalFree((HANDLE)pPal);
    return hpal;
}

/*
 * s04:3430-367D  ReadDibBitmapInfo
 *
 * Reads the headers and colour table of a DIB file into a new block
 * (allocated for the header and colours only).
 */
HANDLE FAR ReadDibBitmapInfo(int fh)
{
    DWORD               off;
    HANDLE              hbi;
    int                 size;
    int                 i;
    WORD                nNumColors;
    RGBQUAD FAR        *pRgb;
    RGBQUAD             rgb;
    BITMAPINFOHEADER    bi;
    BITMAPCOREHEADER    bc;
    LPBITMAPINFOHEADER  lpbi;
    BITMAPFILEHEADER    bf;

    if (fh == -1)
        return NULL;

    off = _llseek(fh, 0L, SEEK_CUR);
    if (_lread(fh, (LPSTR)&bf, sizeof(bf)) != sizeof(bf))
        return NULL;

    if (bf.bfType != BFT_BITMAP) {
        bf.bfOffBits = 0L;
        _llseek(fh, off, SEEK_SET);
    }

    if (_lread(fh, (LPSTR)&bi, sizeof(bi)) != sizeof(bi))
        return NULL;

    nNumColors = DibNumColors(&bi);

    switch (size = (int)bi.biSize) {
    case sizeof(BITMAPINFOHEADER):
        break;

    case sizeof(BITMAPCOREHEADER):
        bc = *(BITMAPCOREHEADER *)&bi;
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = (LONG)(int)bc.bcWidth;
        bi.biHeight = (LONG)(int)bc.bcHeight;
        bi.biPlanes = bc.bcPlanes;
        bi.biBitCount = bc.bcBitCount;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = 0;
        bi.biYPelsPerMeter = 0;
        bi.biClrUsed = nNumColors;
        bi.biClrImportant = nNumColors;
        _llseek(fh, (LONG)sizeof(BITMAPCOREHEADER) - sizeof(BITMAPINFOHEADER), SEEK_CUR);
        break;

    default:
        return NULL;
    }

    if (bi.biSizeImage == 0)
        bi.biSizeImage = (LONG)(int)((((WORD)bi.biWidth * bi.biBitCount + 31) & ~31) >> 3)
                         * bi.biHeight;
    if (bi.biXPelsPerMeter == 0)
        bi.biXPelsPerMeter = 0;
    if (bi.biYPelsPerMeter == 0)
        bi.biYPelsPerMeter = 0;
    if (bi.biClrUsed == 0)
        bi.biClrUsed = DibNumColors(&bi);

    hbi = GAllocSelF(GMEM_MOVEABLE, bi.biSize + (DWORD)(nNumColors * sizeof(RGBQUAD)));
    if (!hbi)
        return NULL;
    lpbi = DibLock(hbi);
    *lpbi = bi;

    pRgb = (RGBQUAD FAR *)((LPSTR)lpbi + bi.biSize);
    if (nNumColors) {
        if (size == sizeof(BITMAPCOREHEADER)) {
            /* expand the RGBTRIPLEs in place, from the end */
            _lread(fh, (LPSTR)pRgb, nNumColors * sizeof(RGBTRIPLE));
            for (i = nNumColors - 1; i >= 0; i--) {
                rgb.rgbRed = ((RGBTRIPLE FAR *)pRgb)[i].rgbtRed;
                rgb.rgbBlue = ((RGBTRIPLE FAR *)pRgb)[i].rgbtBlue;
                rgb.rgbGreen = ((RGBTRIPLE FAR *)pRgb)[i].rgbtGreen;
                rgb.rgbReserved = (BYTE)0;
                pRgb[i] = rgb;
            }
        } else {
            _lread(fh, (LPSTR)pRgb, nNumColors * sizeof(RGBQUAD));
        }
    }

    if (bf.bfOffBits != 0L)
        _llseek(fh, off + bf.bfOffBits, SEEK_SET);

    return hbi;
}

/*
 * s04:367E-36A9  PaletteSize
 */
WORD FAR PaletteSize(LPBITMAPINFOHEADER lpbi)
{
    WORD nNumColors;

    nNumColors = DibNumColors(lpbi);
    if (lpbi->biSize == sizeof(BITMAPCOREHEADER))
        return nNumColors * sizeof(RGBTRIPLE);
    return nNumColors * sizeof(RGBQUAD);
}

/*
 * s04:36AA-36F7  DibNumColors
 */
WORD FAR DibNumColors(LPBITMAPINFOHEADER lpbi)
{
    WORD bits;

    if (lpbi->biSize != sizeof(BITMAPCOREHEADER)) {
        if (lpbi->biClrUsed != 0)
            return (WORD)lpbi->biClrUsed;
        bits = lpbi->biBitCount;
    } else {
        bits = ((LPBITMAPCOREHEADER)lpbi)->bcBitCount;
    }

    switch (bits) {
    case 1:
        return 2;
    case 4:
        return 16;
    case 8:
        return 256;
    default:
        return 0;
    }
}

/*
 * s04:36F8-3967  DibFromBitmap
 *
 * A DIB of a DDB.  Unlike the SDK version, the colour count of a
 * palettized result is filled in, and the block stays locked.
 */
HANDLE FAR DibFromBitmap(HBITMAP hbm, DWORD biStyle, WORD biBits, HPALETTE hpal, UINT wUsage)
{
    BITMAP              bm;
    BITMAPINFOHEADER    bi;
    LPBITMAPINFOHEADER  lpbi;
    DWORD               dwLen;
    int                 nColors;
    HANDLE              hdib, h;
    HDC                 hdc;

    if (wUsage == 0)
        wUsage = DIB_RGB_COLORS;
    if (!hbm)
        return NULL;
    if (hpal == NULL)
        hpal = GetStockObject(DEFAULT_PALETTE);

    GetObject(hbm, sizeof(bm), (LPSTR)&bm);
    GetObject(hpal, sizeof(nColors), (LPSTR)&nColors);

    if (biBits == 0)
        biBits = bm.bmPlanes * bm.bmBitsPixel;

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = bm.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = biBits;
    bi.biCompression = biStyle;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    dwLen = bi.biSize + PaletteSize(&bi);

    hdc = CreateCompatibleDC(NULL);
    hpal = SelectPalette(hdc, hpal, FALSE);
    RealizePalette(hdc);

    hdib = GAllocSelF(GMEM_MOVEABLE, dwLen);
    if (hdib) {
        lpbi = DibLock(hdib);
        *lpbi = bi;
        GetDIBits(hdc, hbm, 0, (WORD)bi.biHeight, NULL, (LPBITMAPINFO)lpbi, wUsage);
        bi = *lpbi;

        if (bi.biSizeImage == 0) {
            bi.biSizeImage = (DWORD)(WORD)((((WORD)bm.bmWidth * biBits + 31) & ~31) >> 3)
                             * (LONG)bm.bmHeight;
            if (biStyle != BI_RGB)
                bi.biSizeImage = (bi.biSizeImage * 3) / 2;
        }

        dwLen = bi.biSize + PaletteSize(&bi) + bi.biSizeImage;
        h = (ghMemTemp = GlobalReAlloc(hdib, dwLen, 0)) != 0 ? SELECTOROF(GlobalLock(ghMemTemp)) : 0;
        if (h) {
            hdib = h;
            lpbi = DibLock(hdib);
            GetDIBits(hdc, hbm, 0, (WORD)bi.biHeight,
                      (LPSTR)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi),
                      (LPBITMAPINFO)lpbi, wUsage);
            if (lpbi->biBitCount <= 8 && lpbi->biClrUsed == 0)
                lpbi->biClrUsed = 1 << lpbi->biBitCount;
        } else {
            GlobalUnlock(hdib);
            GlobalFree(hdib);
            hdib = NULL;
        }
    }

    SelectPalette(hdc, hpal, FALSE);
    DeleteDC(hdc);
    return hdib;
}

/*
 * s04:3968-3A13  BitmapFromDib
 */
HBITMAP FAR BitmapFromDib(HANDLE hdib, HPALETTE hpal, UINT wUsage)
{
    LPBITMAPINFOHEADER  lpbi;
    HPALETTE            hpalT;
    HDC                 hdc;
    HBITMAP             hbm;

    if (!hdib)
        return NULL;
    if (wUsage == 0)
        wUsage = DIB_RGB_COLORS;

    lpbi = DibLock(hdib);
    if (!lpbi)
        return NULL;

    hdc = GetDC(NULL);
    if (hpal) {
        hpalT = SelectPalette(hdc, hpal, FALSE);
        RealizePalette(hdc);
    }

    hbm = CreateDIBitmap(hdc, lpbi, (LONG)CBM_INIT,
                         (LPSTR)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi),
                         (LPBITMAPINFO)lpbi, wUsage);

    if (hpal && hpalT)
        SelectPalette(hdc, hpalT, FALSE);
    ReleaseDC(NULL, hdc);
    return hbm;
}

/*
 * s04:3A14-3AB3  DibFromDib  (unreferenced)
 *
 * Converts a DIB to another format through a DDB.
 */
HANDLE FAR DibFromDib(HANDLE hdib, DWORD biStyle, WORD biBits, HPALETTE hpal, UINT wUsage)
{
    BITMAPINFOHEADER    bi;
    HBITMAP             hbm;
    BOOL                fKillPalette = FALSE;
    HANDLE              hdibNew;

    if (!hdib)
        return NULL;

    DibInfo(hdib, &bi);
    if (bi.biCompression == biStyle && bi.biBitCount == biBits)
        return hdib;

    if (hpal == NULL) {
        hpal = CreateDibPalette(hdib);
        fKillPalette = TRUE;
    }

    hbm = BitmapFromDib(hdib, hpal, wUsage);
    if (hbm == NULL) {
        hdibNew = NULL;
    } else {
        hdibNew = DibFromBitmap(hbm, biStyle, biBits, hpal, wUsage);
        DeleteObject(hbm);
    }

    if (fKillPalette && hpal)
        DeleteObject(hpal);
    return hdibNew;
}

/*
 * s04:3AB4-3DA5  DibFromBitmapPalette  (unreferenced)
 *
 * A 4 or 8-bit DIB of a DDB whose colour indices are indices into hpal:
 * the bits are read with DIB_PAL_COLORS into a scratch header, each
 * pixel is translated through the returned index table, and the colour
 * table is filled from the palette.
 */
HANDLE FAR DibFromBitmapPalette(HBITMAP hbm, WORD biBits, HPALETTE hpal)
{
    BITMAP              bm;
    BITMAPINFOHEADER    bi;
    LPBITMAPINFOHEADER  lpbi, lpbiT;
    WORD FAR           *pwIdx;
    RGBQUAD FAR        *pRgb;
    BYTE huge          *pb;
    PALETTEENTRY        pe;
    HANDLE              hdib;
    HPALETTE            hpalT;
    HDC                 hdc;
    int                 nColors, i;
    DWORD               dw;
    BYTE                b;

    if (hpal == NULL)
        hpal = GetStockObject(DEFAULT_PALETTE);
    if (hbm == NULL)
        hbm = NULL;

    GetObject(hpal, sizeof(nColors), (LPSTR)&nColors);
    GetObject(hbm, sizeof(bm), (LPSTR)&bm);

    if (biBits == 0)
        biBits = (nColors > 16) ? 8 : 4;

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bm.bmWidth;
    bi.biPlanes = 1;
    bi.biBitCount = biBits;
    bi.biCompression = BI_RGB;
    bi.biHeight = bm.bmHeight;
    bi.biSizeImage = (DWORD)(WORD)((((WORD)bm.bmWidth * biBits + 31) & ~31) >> 3) * bi.biHeight;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = (LONG)nColors;
    bi.biClrImportant = 0;

    hdib = GAllocSelF(GMEM_MOVEABLE, bi.biSize + PaletteSize(&bi) + bi.biSizeImage);
    if (!hdib)
        return NULL;

    lpbiT = (LPBITMAPINFOHEADER)GAllocPtrF(GMEM_MOVEABLE, bi.biSize + 1024);
    if (lpbiT == NULL) {
        GlobalUnlock(hdib);
        GlobalFree(hdib);
        return NULL;
    }

    hdc = GetDC(NULL);
    hpalT = SelectPalette(hdc, hpal, FALSE);
    RealizePalette(hdc);

    *lpbiT = bi;
    lpbi = DibLock(hdib);
    *lpbi = bi;
    pwIdx = (WORD FAR *)((LPSTR)lpbiT + (WORD)lpbiT->biSize);
    pRgb = (RGBQUAD FAR *)((LPSTR)lpbi + (WORD)lpbi->biSize);
    pb = (BYTE huge *)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi);

    GetDIBits(hdc, hbm, 0, (WORD)bi.biHeight, (LPSTR)pb, (LPBITMAPINFO)lpbiT, DIB_PAL_COLORS);

    if (biBits == 8) {
        for (dw = 0; dw < bi.biSizeImage; dw++) {
            *pb = (BYTE)pwIdx[*pb];
            pb++;
        }
    } else {
        for (dw = 0; dw < bi.biSizeImage; dw++) {
            b = *pb;
            *pb = (BYTE)(((BYTE)pwIdx[b >> 4] << 4) | (BYTE)pwIdx[b & 0x0F]);
            pb++;
        }
    }

    for (i = 0; i < nColors; i++) {
        GetPaletteEntries(hpal, i, 1, &pe);
        pRgb[i].rgbRed = pe.peRed;
        pRgb[i].rgbGreen = pe.peGreen;
        pRgb[i].rgbBlue = pe.peBlue;
        pRgb[i].rgbReserved = 0;
    }

    GFreePtr(lpbiT);
    SelectPalette(hdc, hpalT, FALSE);
    ReleaseDC(NULL, hdc);
    return hdib;
}

/*
 * s04:3DA6-3DF7  MapBits8
 */
void FAR MapBits8(BYTE huge *pb, DWORD cb, LPBYTE xlat)
{
    DWORD dw;

    for (dw = 0; dw < cb; dw++) {
        *pb = xlat[*pb];
        pb++;
    }
}

/*
 * s04:3DF8-3E5F  MapBits4
 */
void FAR MapBits4(BYTE huge *pb, DWORD cb, LPBYTE xlat)
{
    DWORD dw;
    BYTE  b;

    for (dw = 0; dw < cb; dw++) {
        b = *pb;
        *pb = xlat[b & 0x0F] | (BYTE)(xlat[b >> 4] << 4);
        pb++;
    }
}

/*
 * s04:3E60-3F1B  MapBitsRLE8
 *
 * Translates the pixels of RLE8 data in place; cb is not used.
 */
void FAR MapBitsRLE8(BYTE huge *pb, DWORD cb, LPBYTE xlat)
{
    BYTE cnt, b;

    for (;;) {
        cnt = *pb++;
        b = *pb;
        if (cnt == 0) {
            pb++;
            switch (b) {
            case 0:                     /* end of line */
                break;
            case 1:                     /* end of bitmap */
                return;
            case 2:                     /* delta */
                pb += 2;
                break;
            default:                    /* absolute run, word aligned */
                cnt = b;
                for (b = cnt; b; b--) {
                    *pb = xlat[*pb];
                    pb++;
                }
                if (cnt & 1)
                    pb++;
                break;
            }
        } else {
            *pb = xlat[b];
            pb++;
        }
    }
}

/*
 * s04:3F1C-3F1D  MapBitsRLE4
 *
 * Not implemented: an empty function.
 */
void FAR MapBitsRLE4(BYTE huge *pb, DWORD cb, LPBYTE xlat)
{
}

/*
 * s04:3F1E-410D  MapDibToPalette  (unreferenced)
 *
 * Maps the pixels of a DIB to the nearest colours of hpal and replaces
 * its colour table with the palette (zeroing the colours beyond it).
 */
BOOL FAR MapDibToPalette(HANDLE hdib, HPALETTE hpal)
{
    LPBITMAPINFOHEADER  lpbi;
    RGBQUAD FAR        *pRgb;
    BYTE huge          *pb;
    PALETTEENTRY        pe;
    int                 nPalColors, nDibColors, i;
    DWORD               dwSize;
    BYTE                xlat[256];

    if (!hpal || !hdib)
        return FALSE;

    lpbi = DibLock(hdib);
    pRgb = (RGBQUAD FAR *)((LPSTR)lpbi + (WORD)lpbi->biSize);
    pb = DibXY(lpbi, 0, 0);

    GetObject(hpal, sizeof(int), (LPSTR)&nPalColors);
    nDibColors = DibNumColors(lpbi);
    if (nPalColors > nDibColors)
        nPalColors = nDibColors;

    for (i = 0; i < nDibColors; i++)
        xlat[i] = (BYTE)GetNearestPaletteIndex(hpal,
                        RGB(pRgb[i].rgbRed, pRgb[i].rgbGreen, pRgb[i].rgbBlue));

    dwSize = lpbi->biSizeImage;
    if (dwSize == 0)
        dwSize = (LONG)(int)((((WORD)lpbi->biWidth * lpbi->biBitCount + 31) & ~31) >> 3)
                 * lpbi->biHeight;

    switch ((WORD)lpbi->biCompression) {
    case BI_RGB:
        if (lpbi->biBitCount == 8)
            MapBits8(pb, dwSize, xlat);
        else
            MapBits4(pb, dwSize, xlat);
        break;
    case BI_RLE8:
        MapBitsRLE8(pb, dwSize, xlat);
        break;
    case BI_RLE4:
        MapBitsRLE4(pb, dwSize, xlat);
        break;
    }

    for (i = 0; i < nPalColors; i++) {
        GetPaletteEntries(hpal, i, 1, &pe);
        pRgb[i].rgbRed = pe.peRed;
        pRgb[i].rgbGreen = pe.peGreen;
        pRgb[i].rgbReserved = 0;
        pRgb[i].rgbBlue = pe.peBlue;
    }
    for (i = nPalColors; i < nDibColors; i++) {
        pRgb[i].rgbRed = 0;
        pRgb[i].rgbGreen = 0;
        pRgb[i].rgbReserved = 0;
        pRgb[i].rgbBlue = 0;
    }
    return TRUE;
}

/*
 * s04:410E-41A7  StretchBitmap  (unreferenced)
 */
BOOL FAR StretchBitmap(HDC hdc, int x, int y, int dx, int dy, HBITMAP hbm,
                       int x0, int y0, int dx0, int dy0, DWORD rop)
{
    HDC         hdcBits;
    HPALETTE    hpal, hpalT;
    BOOL        f;

    if (!hdc || !hbm)
        return FALSE;

    hpal = SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
    SelectPalette(hdc, hpal, FALSE);

    hdcBits = CreateCompatibleDC(hdc);
    SelectObject(hdcBits, hbm);
    hpalT = SelectPalette(hdcBits, hpal, FALSE);
    RealizePalette(hdcBits);
    f = StretchBlt(hdc, x, y, dx, dy, hdcBits, x0, y0, dx0, dy0, rop);
    SelectPalette(hdcBits, hpalT, FALSE);
    DeleteDC(hdcBits);
    return f;
}

/*
 * s04:41A8-420B  DrawBitmap  (unreferenced)
 */
BOOL FAR DrawBitmap(HDC hdc, int x, int y, HBITMAP hbm, DWORD rop)
{
    HDC     hdcBits;
    BITMAP  bm;
    BOOL    f;

    if (!hdc || !hbm)
        return FALSE;

    hdcBits = CreateCompatibleDC(hdc);
    GetObject(hbm, sizeof(BITMAP), (LPSTR)&bm);
    SelectObject(hdcBits, hbm);
    f = BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, hdcBits, 0, 0, rop);
    DeleteDC(hdcBits);
    return f;
}


/*
 * s04:420C-4265  DrawDib  (unreferenced)
 */
BOOL FAR DrawDib(HDC hdc, int x, int y, HANDLE hdib, HPALETTE hpal, UINT wUsage)
{
    HPALETTE hpalT;

    if (hpal) {
        hpalT = SelectPalette(hdc, hpal, FALSE);
        RealizePalette(hdc);
    }
    DibBlt(hdc, x, y, -1, -1, hdib, 0, 0, SRCCOPY, wUsage);
    if (hpal)
        SelectPalette(hdc, hpalT, FALSE);
    return TRUE;
}

/*
 * s04:4266-437D  SetDibUsageFill
 *
 * SetDibUsage, but for DIB_RGB_COLORS the colours the palette doesn't
 * have are set to black.
 */
BOOL FAR SetDibUsageFill(HANDLE hdib, HPALETTE hpal, UINT wUsage)
{
    PALETTEENTRY        ape[256];
    LPBITMAPINFOHEADER  lpbi;
    RGBQUAD FAR        *pRgb;
    WORD FAR           *pw;
    int                 nColors, n, i;

    if (hpal == NULL)
        hpal = GetStockObject(DEFAULT_PALETTE);
    if (!hdib)
        return FALSE;
    lpbi = DibLock(hdib);
    if (!lpbi)
        return FALSE;

    nColors = DibNumColors(lpbi);
    if (nColors > 0) {
        pRgb = (RGBQUAD FAR *)((LPSTR)lpbi + (WORD)lpbi->biSize);
        switch (wUsage) {
        case DIB_PAL_COLORS:
            for (pw = (WORD FAR *)pRgb, i = 0; i < nColors; i++, pw++)
                *pw = i;
            break;

        default:
            nColors = min(nColors, 256);
            n = GetPaletteEntries(hpal, 0, nColors, ape);
            for (i = 0; i < nColors; i++) {
                if (i < n) {
                    pRgb[i].rgbRed = ape[i].peRed;
                    pRgb[i].rgbGreen = ape[i].peGreen;
                    pRgb[i].rgbReserved = 0;
                    pRgb[i].rgbBlue = ape[i].peBlue;
                } else {
                    pRgb[i].rgbRed = 0;
                    pRgb[i].rgbGreen = 0;
                    pRgb[i].rgbReserved = 0;
                    pRgb[i].rgbBlue = 0;
                }
            }
            break;
        }
    }
    return TRUE;
}

/*
 * s04:437E-446B  SetDibUsage
 */
BOOL FAR SetDibUsage(HANDLE hdib, HPALETTE hpal, UINT wUsage)
{
    PALETTEENTRY        ape[256];
    LPBITMAPINFOHEADER  lpbi;
    RGBQUAD FAR        *pRgb;
    WORD FAR           *pw;
    int                 nColors, i;

    if (hpal == NULL)
        hpal = GetStockObject(DEFAULT_PALETTE);
    if (!hdib)
        return FALSE;
    lpbi = DibLock(hdib);
    if (!lpbi)
        return FALSE;

    nColors = DibNumColors(lpbi);
    if (nColors > 0) {
        pRgb = (RGBQUAD FAR *)((LPSTR)lpbi + (WORD)lpbi->biSize);
        switch (wUsage) {
        case DIB_PAL_COLORS:
            for (pw = (WORD FAR *)pRgb, i = 0; i < nColors; i++, pw++)
                *pw = i;
            break;

        default:
            nColors = min(nColors, 256);
            GetPaletteEntries(hpal, 0, nColors, ape);
            for (i = 0; i < nColors; i++) {
                pRgb[i].rgbRed = ape[i].peRed;
                pRgb[i].rgbGreen = ape[i].peGreen;
                pRgb[i].rgbReserved = 0;
                pRgb[i].rgbBlue = ape[i].peBlue;
            }
            break;
        }
    }
    return TRUE;
}

/*
 * s04:446C-452D  SetPalFlags  (unreferenced)
 */
BOOL FAR SetPalFlags(HPALETTE hpal, int iIndex, int cntEntries, UINT wFlags)
{
    int                 i;
    BOOL                f;
    PALETTEENTRY FAR   *lppe;

    if (!hpal)
        return FALSE;
    if (cntEntries < 0)
        GetObject(hpal, sizeof(int), (LPSTR)&cntEntries);

    lppe = (PALETTEENTRY FAR *)GAllocPtrF(GMEM_MOVEABLE, (LONG)cntEntries * sizeof(PALETTEENTRY));
    if (!lppe)
        return FALSE;

    GetPaletteEntries(hpal, iIndex, cntEntries, lppe);
    for (i = 0; i < cntEntries; i++)
        lppe[i].peFlags = (BYTE)wFlags;
    f = SetPaletteEntries(hpal, iIndex, cntEntries, lppe);
    GFreePtr(lppe);
    return f;
}

/*
 * s04:452E-4669  PalEq
 */
BOOL FAR PalEq(HPALETTE hpal1, HPALETTE hpal2)
{
    BOOL                f;
    int                 i;
    int                 nPal1, nPal2;
    PALETTEENTRY FAR   *ppe;

    if (hpal1 == hpal2)
        return TRUE;
    if (!hpal1 || !hpal2)
        return FALSE;

    GetObject(hpal1, sizeof(int), (LPSTR)&nPal1);
    GetObject(hpal2, sizeof(int), (LPSTR)&nPal2);
    if (nPal1 != nPal2)
        return FALSE;

    ppe = (PALETTEENTRY FAR *)GAllocPtrF(GMEM_MOVEABLE, (DWORD)(WORD)(nPal1 * 2 * sizeof(PALETTEENTRY)));
    if (!ppe)
        return FALSE;

    GetPaletteEntries(hpal1, 0, nPal1, ppe);
    GetPaletteEntries(hpal2, 0, nPal2, ppe + nPal1);

    for (f = TRUE, i = 0; f && i < nPal1; i++)
        f &= (ppe[i].peRed == ppe[i + nPal1].peRed &&
              ppe[i].peBlue == ppe[i + nPal1].peBlue &&
              ppe[i].peGreen == ppe[i + nPal1].peGreen);

    GFreePtr(ppe);
    return f;
}

/*
 * s04:466A-4753  StretchDibBlt  (unreferenced)
 *
 * Negative dx and dy are multiples of the DIB's size.
 */
BOOL FAR StretchDibBlt(HDC hdc, int x, int y, int dx, int dy, HANDLE hdib,
                       int x0, int y0, int dx0, int dy0, LONG rop, UINT wUsage)
{
    LPBITMAPINFOHEADER lpbi;

    if (!hdib)
        return PatBlt(hdc, x, y, dx, dy, rop);
    if (wUsage == 0)
        wUsage = DIB_RGB_COLORS;
    lpbi = DibLock(hdib);
    if (!lpbi)
        return FALSE;

    if (dx0 == -1 && dy0 == -1) {
        if (lpbi->biSize == sizeof(BITMAPCOREHEADER)) {
            dx0 = ((LPBITMAPCOREHEADER)lpbi)->bcWidth;
            dy0 = ((LPBITMAPCOREHEADER)lpbi)->bcHeight;
        } else {
            dx0 = (int)lpbi->biWidth;
            dy0 = (int)lpbi->biHeight;
        }
    }

    if (dx < 0 && dy < 0) {
        dx = dx * -dx0;
        dy = dy * -dy0;
    }

    return StretchDIBits(hdc, x, y, dx, dy, x0, y0, dx0, dy0,
                         (LPSTR)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi),
                         (LPBITMAPINFO)lpbi, wUsage, rop);
}

/*
 * s04:4754-4819  DibBlt
 */
BOOL FAR DibBlt(HDC hdc, int x0, int y0, int dx, int dy, HANDLE hdib,
                int x1, int y1, LONG rop, UINT wUsage)
{
    LPBITMAPINFOHEADER lpbi;

    if (!hdib)
        return PatBlt(hdc, x0, y0, dx, dy, rop);
    if (wUsage == 0)
        wUsage = DIB_RGB_COLORS;
    lpbi = DibLock(hdib);
    if (!lpbi)
        return FALSE;

    if (dx == -1 && dy == -1) {
        if (lpbi->biSize == sizeof(BITMAPCOREHEADER)) {
            dx = ((LPBITMAPCOREHEADER)lpbi)->bcWidth;
            dy = ((LPBITMAPCOREHEADER)lpbi)->bcHeight;
        } else {
            dx = (int)lpbi->biWidth;
            dy = (int)lpbi->biHeight;
        }
    }

    return StretchDIBits(hdc, x0, y0, dx, dy, x1, y1, dx, dy,
                         (LPSTR)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi),
                         (LPBITMAPINFO)lpbi, wUsage, rop);
}

/*
 * s04:481A-482D  DibXYH  (unreferenced)
 */
LPVOID FAR DibXYH(HANDLE hdib, int x, int y)
{
    return DibXY(DibLock(hdib), x, y);
}

/*
 * s04:482E-482F  DibNothing  (unreferenced, empty)
 */
void FAR DibNothing(void)
{
}

/*
 * s04:4830-488D  DibXY
 *
 * Huge pointer to pixel (x, y) of a packed DIB.
 */
LPVOID FAR DibXY(LPBITMAPINFOHEADER lpbi, int x, int y)
{
    BYTE huge *pBits;

    pBits = (BYTE huge *)lpbi + (WORD)lpbi->biSize + PaletteSize(lpbi);
    return pBits + (LONG)(int)((((WORD)lpbi->biWidth * lpbi->biBitCount + 31) & ~31) >> 3) * y
                 + x * (int)lpbi->biBitCount / 8;
}

/*
 * s04:488E-49C3  CreateDib
 *
 * A blank DIB with the 16 standard colours (black and white for 1 bit).
 */
HANDLE FAR CreateDib(int bits, int dx, int dy)
{
    HANDLE              hdib;
    BITMAPINFOHEADER    bi;
    LPBITMAPINFOHEADER  lpbi;
    DWORD FAR          *pRgb;
    int                 i;

    if (bits <= 0)
        bits = 8;

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biPlanes = 1;
    bi.biBitCount = bits;
    bi.biWidth = dx;
    bi.biHeight = dy;
    bi.biCompression = BI_RGB;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;
    bi.biClrUsed = DibNumColors(&bi);
    bi.biSizeImage = (LONG)(int)((((WORD)bi.biWidth * bi.biBitCount + 31) & ~31) >> 3) * bi.biHeight;

    hdib = GAllocSelF(GHND, bi.biSize + bi.biClrUsed * sizeof(RGBQUAD) + bi.biSizeImage);
    if (hdib) {
        lpbi = DibLock(hdib);
        *lpbi = bi;
        pRgb = (DWORD FAR *)((LPSTR)lpbi + (WORD)lpbi->biSize);
        if (bits == 1) {
            pRgb[0] = rgbStd[0];
            pRgb[1] = rgbStd[15];
        } else {
            for (i = 0; i < (int)bi.biClrUsed; i++)
                pRgb[i] = rgbStd[i % 16];
        }
    }
    return hdib;
}

/*
 * s04:49C4-4A21  CopyHandle
 *
 * Copies a global block.  The result is the selector of the copy, left
 * locked (0 if the allocation failed).
 */
HANDLE FAR CopyHandle(HANDLE h)
{
    BYTE huge  *lpCopy;
    BYTE huge  *lp;
    HANDLE      hCopy;
    DWORD       dwLen;

    dwLen = GlobalSize(h);
    hCopy = GlobalAlloc(GMEM_MOVEABLE, dwLen);
    if (hCopy) {
        lp = (BYTE huge *)GlobalLock(h);
        lpCopy = (BYTE huge *)GlobalLock(hCopy);
        hmemcpy(lpCopy, lp, dwLen);
        GlobalUnlock(h);
        GlobalUnlock(hCopy);
    }
    return (HANDLE)SELECTOROF(GlobalLock(hCopy));
}

/*
 * s04:4A22-4AA5  lread  (NEAR PASCAL)
 *
 * _lread for more than 32 KB.
 */
static DWORD NEAR PASCAL lread(int fh, LPVOID pv, DWORD ul)
{
    DWORD       ulT = ul;
    BYTE huge  *hp = pv;

    while (ul > (DWORD)MAXREAD) {
        if (_lread(fh, (LPSTR)hp, (WORD)MAXREAD) != MAXREAD)
            return 0;
        ul -= MAXREAD;
        hp += MAXREAD;
    }
    if (_lread(fh, (LPSTR)hp, (WORD)ul) != (WORD)ul)
        return 0;
    return ulT;
}

/*
 * s04:4AA6-4B29  lwrite  (NEAR PASCAL)
 */
static DWORD NEAR PASCAL lwrite(int fh, LPVOID pv, DWORD ul)
{
    DWORD       ulT = ul;
    BYTE huge  *hp = pv;

    while (ul > (DWORD)MAXREAD) {
        if (_lwrite(fh, (LPSTR)hp, (WORD)MAXREAD) != MAXREAD)
            return 0;
        ul -= MAXREAD;
        hp += MAXREAD;
    }
    if (_lwrite(fh, (LPSTR)hp, (WORD)ul) != (WORD)ul)
        return 0;
    return ulT;
}
