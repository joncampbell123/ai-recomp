/*
 * dibconv.c - code segment 16 of VIDEDIT.EXE, s16:269C-3451: DIB bit
 * depth conversion
 *
 * DibConvertBitCount changes a DIB to 8, 16 or 24 bits (Video/Video
 * Format).  A DIB going to 8 bits is reduced to the movie's palette, or to
 * a 64 level gray palette if there is none, through a 32K RGB555 -> index
 * map that is kept for the last palette used (BuildInverseMap, or a
 * luminance table for the gray palette).  4 bit DIBs are first expanded
 * to 8 bits in place.  The other converters make a new DIB with CreateDib
 * (s04) and free the old one.
 *
 * Most converters reach the DIB by using its global handle as a selector
 * (MAKELP(hdib, 0)) without locking it, then unlock and free it anyway;
 * they don't check that CreateDib succeeded.  Dib4To8 and Dib8To16 were
 * compiled without optimisation (Dib4To8 calls the C runtime's long
 * multiply).  All functions are FAR _cdecl except where noted.
 *
 * Module data: DS:0C7C-109B (the "AnimSTrackBar"/"GrayBlob" class names
 * every module carries, then the variables below, all initialised to 0).
 */

#include "videdit.h"
#include "dibmap.h"

/*
 * VfW-style allocation: ghMemTemp (shared by several modules) receives the
 * handle, and the pointer is the block's selector with offset 0.
 */
#define GAllocPtr(flags, cb) \
    ((ghMemTemp = (WORD)GlobalAlloc((flags), (cb))) != 0 \
        ? (LPVOID)MAKELP(SELECTOROF(GlobalLock((HGLOBAL)ghMemTemp)), 0) : (LPVOID)NULL)

/* initialised data */
static int          gnMapColors = 0;            /* DS:0C94 palette size of glpMap */
static LPBYTE       glpMap = NULL;              /* DS:0C96 RGB555 -> index for gapeMap */
static PALETTEENTRY gapeMap[256] = { 0 };       /* DS:0C9A palette glpMap was built for */
static HPALETTE     ghpalGray = NULL;           /* DS:109A 64 gray levels */

static void Dib4To8(HANDLE FAR *phdib, HPALETTE FAR *phpal);
static void Dib8To16(HANDLE FAR *phdib, HPALETTE FAR *phpal);
static void Dib8To24(HANDLE FAR *phdib, HPALETTE FAR *phpal);
static void DibTo8(HANDLE FAR *phdib, HPALETTE FAR *phpal);
static void Dib16To24(HANDLE FAR *phdib, HPALETTE FAR *phpal);
static void Dib24To8(HANDLE FAR *phdib, HPALETTE FAR *phpal);
static void Dib24To16(HANDLE FAR *phdib, HPALETTE FAR *phpal);

/*
 * s16:269C-269F  DibGetGrayPalette  (s03 deletes it at exit)
 */
HPALETTE FAR DibGetGrayPalette(void)
{
    return ghpalGray;
}

/*
 * s16:26A0-26A7  DibGetPaletteMap  (s03 frees it at exit)
 */
LPBYTE FAR DibGetPaletteMap(void)
{
    return glpMap;
}

/*
 * s16:26A8-26EC  CreateGrayPalette
 *
 * 64 grays, entry i = i * 255 / 63.
 */
static HPALETTE CreateGrayPalette(void)
{
    struct {
        WORD            palVersion;
        WORD            palNumEntries;
        PALETTEENTRY    palPalEntry[64];
    } pal;
    WORD    i;

    pal.palVersion = 0x300;
    pal.palNumEntries = 64;

    for (i = 0; i < 64; i++) {
        pal.palPalEntry[i].peRed =
        pal.palPalEntry[i].peGreen =
        pal.palPalEntry[i].peBlue = (BYTE)(i * 255 / 63);
        pal.palPalEntry[i].peFlags = 0;
    }

    return CreatePalette((LPLOGPALETTE)&pal);
}

/*
 * s16:26EE-278B  MakeGrayMap  (FAR PASCAL)
 *
 * RGB555 -> gray palette index: luminance (0.299 R + 0.587 G + 0.114 B,
 * from the lower edge of each cell) scaled to 0-63.
 */
static void FAR PASCAL MakeGrayMap(LPBYTE lpMap)
{
    WORD r;
    WORD g;
    WORD b;

    for (r = 0; r < 256; r += 8)
        for (g = 0; g < 256; g += 8)
            for (b = 0; b < 256; b += 8)
                *lpMap++ = (BYTE)((WORD)(((DWORD)r * 299 + (DWORD)g * 587 + (DWORD)b * 114) / 1000) * 63 / 255);
}

/*
 * s16:278E-28AC  GetPaletteMap  (NEAR PASCAL)
 *
 * The map for hpal: the cached one if the palette has the same colours,
 * else rebuilt.  The 32K block is allocated once and kept.
 */
static LPBYTE NEAR PASCAL GetPaletteMap(HPALETTE hpal)
{
    PALETTEENTRY    ape[256];
    int             nColors = 0;
    BOOL            fSame = TRUE;
    WORD            i;

    if (!GetObject(hpal, sizeof(int), (LPVOID)&nColors))
        return NULL;

    if (nColors == gnMapColors) {
        GetPaletteEntries(hpal, 0, nColors, ape);
        for (i = 0; i < (WORD)nColors && fSame; i++) {
            if (ape[i].peRed != gapeMap[i].peRed ||
                ape[i].peGreen != gapeMap[i].peGreen ||
                ape[i].peBlue != gapeMap[i].peBlue)
                fSame = FALSE;
        }
        if (fSame)
            return glpMap;
    }

    if (glpMap == NULL) {
        glpMap = (LPBYTE)GAllocPtr(GMEM_MOVEABLE, 0x8000L);
        if (glpMap == NULL)
            return NULL;
    }

    if (!GetObject(hpal, sizeof(int), (LPVOID)&nColors))
        return NULL;

    gnMapColors = nColors;
    GetPaletteEntries(hpal, 0, nColors, gapeMap);

    if (hpal == ghpalGray)
        MakeGrayMap(glpMap);
    else
        BuildInverseMap(hpal, glpMap);

    return glpMap;
}

/*
 * s16:28AE-2B4F  Dib4To8
 *
 * Expands the pixels in place, from the end backwards, after growing the
 * block (GMEM_ZEROINIT).  The colour table is kept.
 */
static void Dib4To8(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    LPBITMAPINFOHEADER  lpbi;
    long                x;
    long                y;
    long                dx;
    long                dy;
    long                cbIn;
    long                cbImageIn;
    long                cbHeader;
    long                cbSkipIn;
    long                cbOut;
    long                cbImageOut;
    long                cbSkipOut;
    long                nColors;
    BYTE huge          *pbIn;
    BYTE huge          *pbOut;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(*phdib);
    dx = lpbi->biWidth;
    dy = lpbi->biHeight;
    cbIn = (((dx + 1) >> 1) + 3) & ~3L;
    cbOut = (dx + 3) & ~3L;
    cbImageIn = cbIn * dy;
    cbImageOut = cbOut * dy;
    cbSkipIn = cbIn - ((dx + 1) >> 1);
    cbSkipOut = cbOut - dx;
    nColors = lpbi->biClrUsed ? lpbi->biClrUsed : 16;
    cbHeader = nColors * sizeof(RGBQUAD) + lpbi->biSize;
    GlobalUnlock(*phdib);

    *phdib = GlobalReAlloc(*phdib, cbImageOut + cbHeader, GMEM_ZEROINIT);
    lpbi = (LPBITMAPINFOHEADER)GlobalLock(*phdib);

    pbIn = (BYTE huge *)lpbi + (cbHeader + cbImageIn - 1);
    pbOut = (BYTE huge *)lpbi + (cbImageOut + cbHeader - 1);

    for (y = dy - 1; y >= 0; y--) {
        pbIn -= cbSkipIn;
        pbOut -= cbSkipOut;
        for (x = dx - 1; x >= 0; x--) {
            if (x & 1)
                *pbOut-- = *pbIn & 0x0F;
            else
                *pbOut-- = *pbIn-- >> 4;
        }
    }

    lpbi->biPlanes = 1;
    lpbi->biBitCount = 8;
    GlobalUnlock(*phdib);
}

/*
 * s16:2B50-2D54  Dib8To16
 *
 * The pixel is read as a WORD and masked to its low byte.
 */
static void Dib8To16(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    LPBITMAPINFOHEADER  lpbi;
    LPBITMAPINFOHEADER  lpbiNew;
    HANDLE              hdibNew;
    LPBYTE              pColors;
    int                 nColors;
    int                 dx;
    int                 dy;
    int                 x;
    int                 y;
    int                 i;
    WORD                aw[256];
    BYTE huge          *pbIn;
    BYTE huge          *pbOut;
    WORD                cbIn;
    WORD                cbOut;
    WORD                cbSkipIn;
    WORD                cbSkipOut;

    lpbi = (LPBITMAPINFOHEADER)MAKELP(*phdib, 0);
    nColors = (int)lpbi->biClrUsed;
    if (nColors == 0)
        nColors = 256;
    dx = (int)lpbi->biWidth;
    dy = (int)lpbi->biHeight;
    cbIn = ((dx * 8 + 7) / 8 + 3) & ~3;
    cbOut = ((dx * 16 + 7) / 8 + 3) & ~3;

    hdibNew = CreateDib(16, dx, dy);

    pColors = (LPBYTE)lpbi + (WORD)lpbi->biSize;
    for (i = 0; i < nColors; i++)
        aw[i] = RGB16(pColors[i * 4 + 2], pColors[i * 4 + 1], pColors[i * 4]);

    lpbiNew = (LPBITMAPINFOHEADER)MAKELP(hdibNew, 0);
    pbIn = (BYTE huge *)(pColors + nColors * 4);
    pbOut = (BYTE huge *)lpbiNew + (WORD)lpbiNew->biSize;
    cbSkipIn = cbIn - dx;
    cbSkipOut = cbOut - dx * 2;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            *(WORD huge *)pbOut = aw[*(WORD huge *)pbIn & 0xFF];
            pbIn++;
            pbOut += 2;
        }
        pbIn += cbSkipIn;
        pbOut += cbSkipOut;
    }

    GlobalUnlock(*phdib);
    GlobalFree(*phdib);
    *phdib = hdibNew;
    *phpal = NULL;
}

/*
 * s16:2D55-2F31  Dib8To24
 */
static void Dib8To24(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    LPBITMAPINFOHEADER  lpbi;
    HANDLE              hdibNew;
    LPBYTE              pColors;
    int                 nColors;
    int                 dx;
    int                 dy;
    int                 x;
    int                 y;
    int                 i;
    BYTE                argb[256 * 3];
    BYTE huge          *pbIn;
    BYTE huge          *pbOut;
    WORD                cbIn;
    WORD                cbOut;
    WORD                cbSkipIn;
    WORD                cbSkipOut;

    lpbi = (LPBITMAPINFOHEADER)MAKELP(*phdib, 0);
    nColors = (int)lpbi->biClrUsed;
    if (nColors == 0)
        nColors = 256;
    dx = (int)lpbi->biWidth;
    cbIn = ((dx * 8 + 7) / 8 + 3) & ~3;
    cbOut = ((dx * 24 + 7) / 8 + 3) & ~3;
    dy = (int)lpbi->biHeight;

    hdibNew = CreateDib(24, dx, dy);

    pColors = (LPBYTE)lpbi + (WORD)lpbi->biSize;
    for (i = 0; i < nColors; i++) {
        argb[i * 3 + 0] = pColors[i * 4 + 2];
        argb[i * 3 + 1] = pColors[i * 4 + 1];
        argb[i * 3 + 2] = pColors[i * 4 + 0];
    }

    pbIn = (BYTE huge *)(pColors + nColors * 4);
    pbOut = (BYTE huge *)MAKELP(hdibNew, 0) + (WORD)((LPBITMAPINFOHEADER)MAKELP(hdibNew, 0))->biSize;
    cbSkipIn = cbIn - dx;
    cbSkipOut = cbOut - dx * 3;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            i = *(WORD huge *)pbIn & 0xFF;
            *pbOut++ = argb[i * 3 + 2];
            *pbOut++ = argb[i * 3 + 1];
            *pbOut++ = argb[i * 3 + 0];
            pbIn++;
        }
        pbIn += cbSkipIn;
        pbOut += cbSkipOut;
    }

    GlobalUnlock(*phdib);
    GlobalFree(*phdib);
    *phdib = hdibNew;
    *phpal = NULL;
}

/*
 * s16:2F32-2FEE  DibTo8
 *
 * 16 or 24 bits to 8 with *phpal (the gray palette, made on first use, if
 * there is none).  If the gray palette can't be made the DIB is freed.
 */
static void DibTo8(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    LPBITMAPINFOHEADER  lpbi;
    HCURSOR             hcurOld;
    HANDLE              hdibNew;

    lpbi = (LPBITMAPINFOHEADER)MAKELP(*phdib, 0);

    if (*phpal == NULL) {
        if (ghpalGray == NULL)
            ghpalGray = CreateGrayPalette();
        *phpal = ghpalGray;
        if (*phpal == NULL) {
            GlobalUnlock(*phdib);
            GlobalFree(*phdib);
            *phdib = NULL;
            return;
        }
    }

    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    hdibNew = DibReduce(lpbi, NULL, *phpal, GetPaletteMap(*phpal));
    SetCursor(hcurOld);

    GlobalUnlock(*phdib);
    GlobalFree(*phdib);
    *phdib = hdibNew;
}

/*
 * s16:2FF0-3175  Dib16To24
 *
 * RGB555 to 24 bits; the low three bits of each component stay 0.  Any
 * colour table entries (biClrUsed) are skipped.
 */
static void Dib16To24(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    LPBITMAPINFOHEADER  lpbi;
    HANDLE              hdibNew;
    int                 nColors;
    int                 dx;
    int                 dy;
    int                 x;
    int                 y;
    BYTE huge          *pbIn;
    BYTE huge          *pbOut;
    WORD                cbIn;
    WORD                cbOut;
    WORD                cbSkipIn;
    WORD                cbSkipOut;

    lpbi = (LPBITMAPINFOHEADER)MAKELP(*phdib, 0);
    nColors = (int)lpbi->biClrUsed;
    dx = (int)lpbi->biWidth;
    cbIn = ((dx * 16 + 7) / 8 + 3) & ~3;
    cbOut = ((dx * 24 + 7) / 8 + 3) & ~3;
    dy = (int)lpbi->biHeight;

    hdibNew = CreateDib(24, dx, dy);

    pbIn = (BYTE huge *)((LPBYTE)lpbi + (WORD)(nColors * 4 + (WORD)lpbi->biSize));
    pbOut = (BYTE huge *)MAKELP(hdibNew, 0) + (WORD)((LPBITMAPINFOHEADER)MAKELP(hdibNew, 0))->biSize;
    cbSkipIn = cbIn - dx * 2;
    cbSkipOut = cbOut - dx * 3;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            *pbOut++ = (BYTE)(*pbIn << 3);
            *pbOut++ = (BYTE)((*(WORD huge *)pbIn >> 2) & 0xF8);
            *pbOut++ = (BYTE)((*(WORD huge *)pbIn >> 7) & 0xF8);
            pbIn += 2;
        }
        pbIn += cbSkipIn;
        pbOut += cbSkipOut;
    }

    GlobalUnlock(*phdib);
    GlobalFree(*phdib);
    *phdib = hdibNew;
    *phpal = NULL;
}

/*
 * s16:3176-3186  Dib24To8
 */
static void Dib24To8(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    DibTo8(phdib, phpal);
}

/*
 * s16:3188-331E  Dib24To16
 */
static void Dib24To16(HANDLE FAR *phdib, HPALETTE FAR *phpal)
{
    LPBITMAPINFOHEADER  lpbi;
    HANDLE              hdibNew;
    int                 nColors;
    int                 dx;
    int                 dy;
    int                 x;
    int                 y;
    BYTE huge          *pbIn;
    BYTE huge          *pbOut;
    WORD                cbIn;
    WORD                cbOut;
    WORD                cbSkipIn;
    WORD                cbSkipOut;

    lpbi = (LPBITMAPINFOHEADER)MAKELP(*phdib, 0);
    nColors = (int)lpbi->biClrUsed;
    dx = (int)lpbi->biWidth;
    cbIn = ((dx * 24 + 7) / 8 + 3) & ~3;
    cbOut = ((dx * 16 + 7) / 8 + 3) & ~3;
    dy = (int)lpbi->biHeight;

    hdibNew = CreateDib(16, dx, dy);

    pbIn = (BYTE huge *)((LPBYTE)lpbi + (WORD)(nColors * 4 + (WORD)lpbi->biSize));
    pbOut = (BYTE huge *)MAKELP(hdibNew, 0) + (WORD)((LPBITMAPINFOHEADER)MAKELP(hdibNew, 0))->biSize;
    cbSkipIn = cbIn - dx * 3;
    cbSkipOut = cbOut - dx * 2;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            *(WORD huge *)pbOut = RGB16(pbIn[2], pbIn[1], pbIn[0]);
            pbOut += 2;
            pbIn += 3;
        }
        pbIn += cbSkipIn;
        pbOut += cbSkipOut;
    }

    GlobalUnlock(*phdib);
    GlobalFree(*phdib);
    *phdib = hdibNew;
    *phpal = NULL;
}

/*
 * s16:3320-3451  DibConvertBitCount  (called from s06)
 *
 * Converts *phdib (1, 4, 8, 16 or 24 bits) to wBitCount (8, 16 or 24).
 * The switch value is (old & ~7) + new / 8, so 1 and 4 bit DIBs both take
 * the 4 bit paths.  Returns FALSE if there is no DIB, the depth is already
 * right, or the combination is not handled; TRUE otherwise (also when a
 * conversion failed).  The new palette is NULL except after a conversion
 * to 8 bits.
 */
BOOL FAR DibConvertBitCount(HANDLE FAR *phdib, HPALETTE FAR *phpal, WORD wBitCount)
{
    if (*phdib == NULL)
        return FALSE;

    switch ((((LPBITMAPINFOHEADER)MAKELP(*phdib, 0))->biBitCount & ~7) + (wBitCount >> 3)) {
    case 0 + 1:                 /* 4 -> 8 */
        Dib4To8(phdib, phpal);
        break;
    case 0 + 2:                 /* 4 -> 16 */
        Dib4To8(phdib, phpal);
        Dib8To16(phdib, phpal);
        break;
    case 0 + 3:                 /* 4 -> 24 */
        Dib4To8(phdib, phpal);
        Dib8To24(phdib, phpal);
        break;
    case 8 + 2:
        Dib8To16(phdib, phpal);
        break;
    case 8 + 3:
        Dib8To24(phdib, phpal);
        break;
    case 16 + 1:
        DibTo8(phdib, phpal);
        break;
    case 16 + 3:
        Dib16To24(phdib, phpal);
        break;
    case 24 + 1:
        Dib24To8(phdib, phpal);
        break;
    case 24 + 2:
        Dib24To16(phdib, phpal);
        break;
    case 8 + 1:                 /* same depth */
    case 16 + 2:
    case 24 + 3:
        return FALSE;
    default:
        return FALSE;
    }

    return TRUE;
}
