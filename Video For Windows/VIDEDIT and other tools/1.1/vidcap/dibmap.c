/*
 * dibmap.c - code segment 6 of VIDCAP.EXE: the histogram and optimal
 * palette code used by Capture Palette.
 *
 * The histogram counts RGB555 colors: 32768 DWORD counters in one 128 KB
 * global block.  The counters are reached through FS with 32-bit offsets
 * (UseHistogram loads FS, GetHistogram/IncHistogram use it), so this
 * module needs a 386.
 *
 * HistogramPalette builds the palette with a median cut: the RGB555 cube
 * is split box by box, always the box with the largest variance, at the
 * point along one axis that maximises the between-class variance (Otsu),
 * until there are enough boxes or no box has any variance left.  Each box
 * becomes one palette entry and fills its part of the 16-to-8 inverse
 * table.
 *
 * Except for the three helpers, all functions are FAR CDECL.  DibReduce is
 * unreferenced.
 */

#include <windows.h>
#include "vidcap.h"

#define RGB16(r, g, b)  ((((WORD)(r) >> 3) << 10) | (((WORD)(g) >> 3) << 5) | ((WORD)(b) >> 3))

#define MAXBOXES        236             /* colors left with 20 system colors */

typedef struct tagBOX {
    struct tagBOX NEAR *next;           /* +00 */
    int         rmin;                   /* +02 inclusive, 0..31 */
    int         rmax;                   /* +04 */
    int         gmin;                   /* +06 */
    int         gmax;                   /* +08 */
    int         bmin;                   /* +0A */
    int         bmax;                   /* +0C */
    long        variance;               /* +0E sum over the three axes */
    long        count;                  /* +12 pixels in the box */
    BYTE        abUnused[12];           /* +16 never used */
} BOX;                                  /* 0x22 bytes */

typedef BOX NEAR *PBOX;

/* the result of FindSplit; only two fields are ever set */
typedef struct {
    WORD        wUnused0[2];            /* +00 */
    int         iSplit;                 /* +04 last value of the low box */
    WORD        wUnused6[2];            /* +06 */
    int         iAxis;                  /* +0A 0 red, 1 green, 2 blue */
    WORD        wUnusedC[8];            /* +0C */
} CUT;                                  /* 0x1C bytes */

static struct {
    WORD            palVersion;
    WORD            palNumEntries;
    PALETTEENTRY    palPalEntry[256];
} gpal;                                 /* DS:0FDA */

static PBOX         gpBoxList;          /* DS:25FA sorted by variance */
static LPBYTE       glp16to8;           /* DS:2EDA inverse table being built */
static PBOX         gpFreeBoxes;        /* DS:2EFE */
static HLOCAL       ghBoxes;            /* DS:2F48 */
/* DS:301A is MSC's static return buffer for FindSplit's CUT */

static void FAR SortBoxes(void);
static BOOL FAR AllocBoxes(int nBoxes);
static void FAR FreeBoxes(void);
static void FAR SplitBox(PBOX pBox);
static CUT FAR FindSplit(PBOX pBox);
static BOOL FAR CutBox(PBOX pBox, CUT cut);
static void FAR BoxStats(PBOX pBox);
static DWORD FAR BoxColor(PBOX pBox, int iColor);
static void FAR Histogram24(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram);
static void FAR Histogram16(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram);
static void FAR Histogram8(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram, WORD FAR *lpColors);
static void FAR Histogram4(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram, WORD FAR *lpColors);
static void FAR Histogram1(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram, WORD FAR *lpColors);
static void FAR Reduce24(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lp16to8);
static void FAR Reduce16(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lp16to8);
static void FAR Reduce8(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lpMap);
static void FAR Reduce4(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lpMap);
static void FAR Reduce1(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lpMap);

/*
 * The three helpers below are tiny near functions in the original
 * (05FA-0662); MSC's GetHistogram/IncHistogram take their arguments in
 * registers (_fastcall).  Here they are inline code sequences.
 */

/*
 * seg6:05FA-060A  UseHistogram  (NEAR PASCAL)
 *
 * Points FS at the histogram.
 */
void UseHistogram(LPHISTOGRAM lpHistogram);
#pragma aux UseHistogram = \
    "mov    fs,dx"          \
    parm [dx ax] modify exact [];

/*
 * seg6:060B-0641  GetHistogram  (NEAR _fastcall: AL, DL, BL)
 *
 * Returns the count of the 5-bit color r,g,b.  The original carries an
 * unreachable "return 0" (mov ax,0 / mov dx,0) that the code jumps over.
 */
DWORD GetHistogram(BYTE r, BYTE g, BYTE b);
#pragma aux GetHistogram =  \
    "xor    ah,ah"          \
    "shl    ax,5"           \
    "or     al,dl"          \
    "shl    ax,5"           \
    "or     al,bl"          \
    "xor    ebx,ebx"        \
    "mov    bx,ax"          \
    "shl    ebx,2"          \
    "mov    dx,fs:[ebx+2]"  \
    "mov    ax,fs:[ebx]"    \
    parm [al] [dl] [bl] value [dx ax] modify exact [ax dx bx];

/*
 * seg6:0642-0662  IncHistogram  (NEAR _fastcall: AX)
 *
 * Counts one pixel of RGB555 color w; the counter sticks at 0xFFFFFFFF.
 * (The original skips the increment with cmp/jz; adding 1 and taking the
 * carry back off gives the same result without a label.)
 */
void IncHistogram(WORD w);
#pragma aux IncHistogram =  \
    "xor    ebx,ebx"        \
    "mov    bx,ax"          \
    "shl    ebx,2"          \
    "add    dword ptr fs:[ebx],1" \
    "sbb    dword ptr fs:[ebx],0" \
    parm [ax] modify exact [bx];

/*
 * seg6:0000-0035  InitHistogram
 *
 * Allocates a zeroed histogram if lpHistogram is NULL; a histogram passed
 * in is returned as it is (not cleared).
 */
LPHISTOGRAM FAR InitHistogram(LPHISTOGRAM lpHistogram)
{
    if (lpHistogram == NULL)
        lpHistogram = (LPHISTOGRAM)GlobalLock(GlobalAlloc(GHND, 32768L * sizeof(DWORD)));
    return lpHistogram;
}

/*
 * seg6:0036-004E  FreeHistogram
 *
 * Frees the block by its selector, still locked.
 */
void FAR FreeHistogram(LPHISTOGRAM lpHistogram)
{
    GlobalFree((HGLOBAL)HIWORD((DWORD)lpHistogram));
}

/*
 * seg6:0050-02B2  DibHistogram
 *
 * Adds the colors of the dx by dy pixels at x,y of a 1, 4, 8, 16 or 24
 * bit DIB to the histogram.  dx or dy < 0 (or too big) mean the full
 * width or height.  Fills in biClrUsed of a palettized DIB if it is 0.
 * Returns FALSE for a NULL DIB or histogram; otherwise the original falls
 * off the end and returns whatever the HistogramN call left in AX.
 */
BOOL FAR DibHistogram(LPBITMAPINFOHEADER lpbi, LPBYTE lpBits, int x, int y,
                      int dx, int dy, LPHISTOGRAM lpHistogram)
{
    WORD        awColor[256];
    RGBQUAD FAR *prgb;
    UINT        cbScan;
    int         i;

    if (lpbi == NULL || lpHistogram == NULL)
        return FALSE;

    if (lpbi->biClrUsed == 0 && lpbi->biBitCount <= 8)
        lpbi->biClrUsed = 1 << lpbi->biBitCount;

    if (lpBits == NULL)
        lpBits = (LPBYTE)lpbi + (UINT)lpbi->biSize + (UINT)lpbi->biClrUsed * sizeof(RGBQUAD);

    cbScan = (UINT)((lpbi->biBitCount * lpbi->biWidth + 7) / 8 + 3) & ~3;
    lpBits = (LPBYTE)((BYTE _huge *)lpBits + (long)y * cbScan + (x * (int)lpbi->biBitCount) / 8);

    if (dx < 0 || dx > (int)lpbi->biWidth)
        dx = (int)lpbi->biWidth;
    if (dy < 0 || dy > (int)lpbi->biHeight)
        dy = (int)lpbi->biHeight;

    if (lpbi->biBitCount <= 8) {
        prgb = (RGBQUAD FAR *)((LPBYTE)lpbi + (UINT)lpbi->biSize);
        for (i = 0; i < (int)lpbi->biClrUsed; i++, prgb++)
            awColor[i] = RGB16(prgb->rgbRed, prgb->rgbGreen, prgb->rgbBlue);
        for (i = (int)lpbi->biClrUsed; i < 256; i++)
            awColor[i] = 0;
    }

    switch (lpbi->biBitCount) {
    case 1:
        Histogram1(lpBits, dx, dy, cbScan, lpHistogram, awColor);
        break;
    case 4:
        Histogram4(lpBits, dx, dy, cbScan, lpHistogram, awColor);
        break;
    case 8:
        Histogram8(lpBits, dx, dy, cbScan, lpHistogram, awColor);
        break;
    case 16:
        Histogram16(lpBits, dx, dy, cbScan, lpHistogram);
        break;
    case 24:
        Histogram24(lpBits, dx, dy, cbScan, lpHistogram);
        break;
    }
    return TRUE;
}

/*
 * seg6:02B4-05F8  DibReduce  (unreferenced)
 *
 * Returns a new 8-bit packed DIB of lpbiIn mapped to hpal through the
 * 16-to-8 table (24 and 16 bit) or through a map of the DIB's own color
 * table built from it (8, 4 and 1 bit).
 */
HANDLE FAR DibReduce(LPBITMAPINFOHEADER lpbiIn, LPBYTE pbIn, HPALETTE hpal, LPBYTE lp16to8)
{
    BYTE                abMap[256];
    PALETTEENTRY        pe;
    LPBITMAPINFOHEADER  lpbiOut;
    RGBQUAD FAR         *prgb;
    LPBYTE              pbOut;
    HANDLE              hdib;
    DWORD               dwSize;
    int                 nPalColors;
    int                 nDibColors;
    UINT                cbIn;
    UINT                cbOut;
    int                 dx;
    int                 dy;
    int                 i;

    dx = (int)lpbiIn->biWidth;
    dy = (int)lpbiIn->biHeight;
    cbIn = ((UINT)dx * lpbiIn->biBitCount + 7) / 8 + 3 & ~3;
    cbOut = (dx + 3) & ~3;

    GetObject(hpal, sizeof(int), (LPSTR)&nPalColors);

    nDibColors = (int)lpbiIn->biClrUsed;
    if (nDibColors == 0 && lpbiIn->biBitCount <= 8)
        nDibColors = 1 << lpbiIn->biBitCount;

    if (pbIn == NULL)
        pbIn = (LPBYTE)lpbiIn + (UINT)lpbiIn->biSize + nDibColors * sizeof(RGBQUAD);

    dwSize = (long)dy * cbOut;
    hdib = GlobalAlloc(GMEM_MOVEABLE, dwSize + sizeof(BITMAPINFOHEADER) + nPalColors * sizeof(RGBQUAD));
    if (hdib == NULL)
        return NULL;

    lpbiOut = (LPBITMAPINFOHEADER)GlobalLock(hdib);
    lpbiOut->biSize = sizeof(BITMAPINFOHEADER);
    lpbiOut->biWidth = lpbiIn->biWidth;
    lpbiOut->biHeight = lpbiIn->biHeight;
    lpbiOut->biPlanes = 1;
    lpbiOut->biBitCount = 8;
    lpbiOut->biCompression = BI_RGB;
    lpbiOut->biSizeImage = dwSize;
    lpbiOut->biXPelsPerMeter = 0;
    lpbiOut->biYPelsPerMeter = 0;
    lpbiOut->biClrUsed = nPalColors;
    lpbiOut->biClrImportant = 0;

    pbOut = (LPBYTE)lpbiOut + sizeof(BITMAPINFOHEADER) + nPalColors * sizeof(RGBQUAD);
    prgb = (RGBQUAD FAR *)((LPBYTE)lpbiOut + sizeof(BITMAPINFOHEADER));

    for (i = 0; i < nPalColors; i++, prgb++) {
        GetPaletteEntries(hpal, i, 1, &pe);
        prgb->rgbRed = pe.peRed;
        prgb->rgbGreen = pe.peGreen;
        prgb->rgbReserved = 0;
        prgb->rgbBlue = pe.peBlue;
    }

    if (lpbiIn->biBitCount <= 8) {
        prgb = (RGBQUAD FAR *)((LPBYTE)lpbiIn + (UINT)lpbiIn->biSize);
        for (i = 0; i < nDibColors; i++, prgb++)
            abMap[i] = lp16to8[RGB16(prgb->rgbRed, prgb->rgbGreen, prgb->rgbBlue)];
        for (; i < 256; i++)
            abMap[i] = 0;
    }

    switch (lpbiIn->biBitCount) {
    case 1:
        Reduce1(pbIn, dx, dy, cbIn, pbOut, cbOut, abMap);
        break;
    case 4:
        Reduce4(pbIn, dx, dy, cbIn, pbOut, cbOut, abMap);
        break;
    case 8:
        Reduce8(pbIn, dx, dy, cbIn, pbOut, cbOut, abMap);
        break;
    case 16:
        Reduce16(pbIn, dx, dy, cbIn, pbOut, cbOut, lp16to8);
        break;
    case 24:
        Reduce24(pbIn, dx, dy, cbIn, pbOut, cbOut, lp16to8);
        break;
    }
    return hdib;
}

/*
 * seg6:0663-090E  HistogramPalette
 *
 * Makes a palette of up to nColors colors for the histogram and fills
 * lp16to8 (32 KB) with the palette index of every RGB555 color.  The
 * counts are first halved until they fit in 16 bits (the histogram is
 * changed).  With more than 236 colors the palette gets the 20 system
 * colors at 0-9 and 246-255, unused entries black, and 256 entries.
 * This function is compiled without optimisation in the original.
 */
HPALETTE FAR HistogramPalette(LPHISTOGRAM lpHistogram, LPBYTE lp16to8, int nColors)
{
    PBOX        pBox;
    DWORD       dw;
    DWORD       dwMax;
    HPALETTE    hpal;
    HDC         hdc;
    WORD        w;
    int         i;

    dwMax = 0;
    for (w = 0; w < 0x8000; w++)
        dwMax = max(dwMax, lpHistogram[w]);

    while (dwMax > 0xFFFFL) {
        for (w = 0; w < 0x8000; w++)
            lpHistogram[w] /= 2;
        dwMax /= 2;
    }

    if (!AllocBoxes(min(nColors, MAXBOXES)))
        return NULL;

    UseHistogram(lpHistogram);
    glp16to8 = lp16to8;

    i = 0;
    do {
        i++;
        SplitBox(gpBoxList);
    } while (gpFreeBoxes != NULL && gpBoxList->variance != 0);

    SortBoxes();

    i = 0;
    if (nColors > MAXBOXES) {
        hdc = GetDC(NULL);
        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            GetSystemPaletteEntries(hdc, 0, 10, &gpal.palPalEntry[0]);
            GetSystemPaletteEntries(hdc, 246, 10, &gpal.palPalEntry[246]);
            i = 10;
        } else {
            hpal = GetStockObject(DEFAULT_PALETTE);
            GetPaletteEntries(hpal, 0, 10, &gpal.palPalEntry[0]);
            GetPaletteEntries(hpal, 10, 10, &gpal.palPalEntry[246]);
            i = 10;
        }
        ReleaseDC(NULL, hdc);
    }

    for (pBox = gpBoxList; pBox != NULL; pBox = pBox->next, i++) {
        dw = BoxColor(pBox, i);
        gpal.palPalEntry[i].peRed   = GetRValue(dw);
        gpal.palPalEntry[i].peGreen = GetGValue(dw);
        gpal.palPalEntry[i].peBlue  = GetBValue(dw);
        gpal.palPalEntry[i].peFlags = 0;
    }

    FreeBoxes();

    if (nColors > MAXBOXES) {
        for (; i < 246; i++) {
            gpal.palPalEntry[i].peRed   = 0;
            gpal.palPalEntry[i].peGreen = 0;
            gpal.palPalEntry[i].peBlue  = 0;
            gpal.palPalEntry[i].peFlags = 0;
        }
        i = 256;
    }

    glp16to8 = NULL;
    gpal.palVersion = 0x300;
    gpal.palNumEntries = i;
    return CreatePalette((LPLOGPALETTE)&gpal);
}

/*
 * seg6:090F-0990  SortBoxes
 *
 * Re-sorts the box list by pixel count, largest first (insertion sort).
 */
static void FAR SortBoxes(void)
{
    PBOX    pHead;
    PBOX    pBox;
    PBOX    pNext;
    PBOX    p;

    pHead = gpBoxList;
    pBox = pHead->next;
    pHead->next = NULL;

    for (; pBox != NULL; pBox = pNext) {
        pNext = pBox->next;
        if (pBox->count > pHead->count) {
            pBox->next = pHead;
            pHead = pBox;
        } else {
            for (p = pHead; p->next != NULL && p->next->count > pBox->count; p = p->next)
                ;
            pBox->next = p->next;
            p->next = pBox;
        }
    }
    gpBoxList = pHead;
}

/*
 * seg6:0992-0A24  AllocBoxes
 *
 * Allocates nBoxes boxes: the list starts with one box holding the whole
 * cube (with a made-up variance of 9999999 so it gets split), the rest
 * are free.
 */
static BOOL FAR AllocBoxes(int nBoxes)
{
    int     i;

    ghBoxes = LocalAlloc(LHND, nBoxes * sizeof(BOX));
    if (ghBoxes == NULL)
        return FALSE;

    gpBoxList = (PBOX)LocalLock(ghBoxes);
    gpFreeBoxes = gpBoxList + 1;
    gpBoxList->next = NULL;

    for (i = 0; i < nBoxes - 1; i++)
        gpFreeBoxes[i].next = &gpFreeBoxes[i + 1];
    gpFreeBoxes[nBoxes - 2].next = NULL;

    gpBoxList->rmin = gpBoxList->gmin = gpBoxList->bmin = 0;
    gpBoxList->rmax = gpBoxList->gmax = gpBoxList->bmax = 31;
    gpBoxList->variance = 9999999;
    return TRUE;
}

/*
 * seg6:0A26-0A4E  FreeBoxes
 */
static void FAR FreeBoxes(void)
{
    LocalUnlock(ghBoxes);
    LocalFree(ghBoxes);
    ghBoxes = NULL;
}

/*
 * seg6:0A50-0B58  SplitBox
 *
 * Splits pBox (the head of the list) into itself and the first free box,
 * then puts both back into the list by variance, largest first.
 */
static void FAR SplitBox(PBOX pBox)
{
    CUT     cut;
    PBOX    pNew;
    PBOX    pNext;
    PBOX    p;

    cut = FindSplit(pBox);
    if (CutBox(pBox, cut))
        return;

    BoxStats(pBox);
    BoxStats(gpFreeBoxes);

    if (pBox->next != NULL && pBox->next->variance > pBox->variance) {
        gpBoxList = pBox->next;
        for (p = pBox; p->next != NULL && p->next->variance > pBox->variance; p = p->next)
            ;
        pBox->next = p->next;
        p->next = pBox;
    }

    pNew = gpFreeBoxes;
    if (pNew->variance >= gpBoxList->variance) {
        gpFreeBoxes = pNew->next;
        pNew->next = gpBoxList;
        gpBoxList = pNew;
    } else {
        for (p = gpBoxList; p->next != NULL && p->next->variance > pNew->variance; p = p->next)
            ;
        pNext = pNew->next;
        pNew->next = p->next;
        p->next = pNew;
        gpFreeBoxes = pNext;
    }
}

/*
 * seg6:0B5A-118B  FindSplit
 *
 * Projects the box onto each axis and finds the cut that maximises the
 * between-class variance (mean - mean of the low part)^2 * n / (N - n).
 * Ties keep the first cut found, red before green before blue.  If no
 * cut gives a positive value the CUT is returned uninitialised; CutBox
 * only gets that far when the box has variance, so it does not happen.
 */
static CUT FAR FindSplit(PBOX pBox)
{
    DWORD   ahr[32];
    DWORD   ahg[32];
    DWORD   ahb[32];
    CUT     cut;
    DWORD   dwTotal;
    DWORD   dwSum;
    DWORD   n;
    DWORD   s;
    DWORD   dw;
    double  dTotal;
    double  dMean;
    double  dBest;
    double  dN;
    double  dDiff;
    double  d;
    int     iSplit;
    int     iAxis;
    int     r;
    int     g;
    int     b;

    _fmemset(ahr, 0, sizeof(ahr));
    _fmemset(ahg, 0, sizeof(ahg));
    _fmemset(ahb, 0, sizeof(ahb));
    dwTotal = 0;

    for (r = pBox->rmin; r <= pBox->rmax; r++) {
        for (g = pBox->gmin; g <= pBox->gmax; g++) {
            for (b = pBox->bmin; b <= pBox->bmax; b++) {
                dw = GetHistogram((BYTE)r, (BYTE)g, (BYTE)b);
                ahr[r] += dw;
                ahg[g] += dw;
                ahb[b] += dw;
            }
        }
        dwTotal += ahr[r];
    }

    dBest = 0.0;
    dDiff = 0.0;
    dN = 0.0;

    /* red */
    dwSum = 0;
    for (r = pBox->rmin; r <= pBox->rmax; r++)
        dwSum += ahr[r] * r;
    dTotal = (double)dwTotal;
    dMean = (double)dwSum / dTotal;

    n = 0;
    s = 0;
    for (r = pBox->rmin; r <= pBox->rmax; r++) {
        dw = ahr[r];
        n += dw;
        if (n == 0)
            continue;
        if (n == dwTotal)
            break;
        s += r * dw;
        dN = (double)n;
        dDiff = dMean - (double)s / dN;
        d = dDiff * (dN / (double)(dwTotal - n)) * dDiff;
        if (d > dBest) {
            iSplit = r;
            iAxis = 0;
            dBest = d;
        }
    }

    /* green */
    dwSum = 0;
    for (g = pBox->gmin; g <= pBox->gmax; g++)
        dwSum += ahg[g] * g;
    dMean = (double)dwSum / dTotal;

    n = 0;
    s = 0;
    for (g = pBox->gmin; g <= pBox->gmax; g++) {
        dw = ahg[g];
        n += dw;
        if (n == 0)
            continue;
        if (n == dwTotal)
            break;
        s += g * dw;
        dN = (double)n;
        dDiff = dMean - (double)s / dN;
        d = dDiff * (dN / (double)(dwTotal - n)) * dDiff;
        if (d > dBest) {
            iSplit = g;
            iAxis = 1;
            dBest = d;
        }
    }

    /* blue */
    dwSum = 0;
    for (b = pBox->bmin; b <= pBox->bmax; b++)
        dwSum += ahb[b] * b;
    dMean = (double)dwSum / dTotal;

    n = 0;
    s = 0;
    for (b = pBox->bmin; b <= pBox->bmax; b++) {
        dw = ahb[b];
        n += dw;
        if (n == 0)
            continue;
        if (n == dwTotal)
            break;
        s += b * dw;
        dN = (double)n;
        dDiff = dMean - (double)s / dN;
        d = dDiff * (dN / (double)(dwTotal - n)) * dDiff;
        if (d > dBest) {
            iSplit = b;
            iAxis = 2;
            dBest = d;
        }
    }

    cut.iAxis = iAxis;
    cut.iSplit = iSplit;
    return cut;
}

/*
 * seg6:118C-1217  CutBox
 *
 * Returns TRUE if the box has no variance; otherwise copies it into the
 * first free box (keeping the free link) and cuts the two apart.
 */
static BOOL FAR CutBox(PBOX pBox, CUT cut)
{
    PBOX    pNew;
    PBOX    pNext;

    if (pBox->variance == 0)
        return TRUE;

    pNew = gpFreeBoxes;
    pNext = pNew->next;
    *pNew = *pBox;
    pNew->next = pNext;

    switch (cut.iAxis) {
    case 0:
        pBox->rmax = cut.iSplit;
        pNew->rmin = cut.iSplit + 1;
        break;
    case 1:
        pBox->gmax = cut.iSplit;
        pNew->gmin = cut.iSplit + 1;
        break;
    case 2:
        pBox->bmax = cut.iSplit;
        pNew->bmin = cut.iSplit + 1;
        break;
    }
    return FALSE;
}

/*
 * seg6:1218-1594  BoxStats
 *
 * Sets the box's pixel count and variance: per axis sum(v^2 c) - S^2/N,
 * with S^2/N worked out as (S/N)*S + ((S%N)*S)/N to stay in 32 bits.
 */
static void FAR BoxStats(PBOX pBox)
{
    DWORD   ahr[32];
    DWORD   ahg[32];
    DWORD   ahb[32];
    DWORD   dwTotal;
    DWORD   dwVar;
    DWORD   s;
    DWORD   sq;
    DWORD   t;
    DWORD   dw;
    int     r;
    int     g;
    int     b;

    dwTotal = 0;
    _fmemset(ahr, 0, sizeof(ahr));
    _fmemset(ahg, 0, sizeof(ahg));
    _fmemset(ahb, 0, sizeof(ahb));

    for (r = pBox->rmin; r <= pBox->rmax; r++) {
        for (g = pBox->gmin; g <= pBox->gmax; g++) {
            for (b = pBox->bmin; b <= pBox->bmax; b++) {
                dw = GetHistogram((BYTE)r, (BYTE)g, (BYTE)b);
                ahr[r] += dw;
                ahg[g] += dw;
                ahb[b] += dw;
            }
        }
        dwTotal += ahr[r];
    }
    pBox->count = dwTotal;

    s = 0;
    sq = 0;
    for (r = pBox->rmin; r <= pBox->rmax; r++) {
        t = ahr[r] * r;
        sq += r * t;
        s += t;
    }
    dwVar = sq - ((s % dwTotal) * s) / dwTotal - (s / dwTotal) * s;

    s = 0;
    sq = 0;
    for (g = pBox->gmin; g <= pBox->gmax; g++) {
        t = ahg[g] * g;
        sq += g * t;
        s += t;
    }
    dwVar += sq - ((s % dwTotal) * s) / dwTotal - (s / dwTotal) * s;

    s = 0;
    sq = 0;
    for (b = pBox->bmin; b <= pBox->bmax; b++) {
        t = ahb[b] * b;
        sq += b * t;
        s += t;
    }
    dwVar += sq - ((s % dwTotal) * s) / dwTotal - (s / dwTotal) * s;

    pBox->variance = dwVar;
}

/*
 * seg6:1596-177C  BoxColor
 *
 * Fills the box's cells of the 16-to-8 table (if any) with iColor and
 * returns the box's mean color as an RGB.  Components are scaled with
 * (mean * 255) / 32, so white comes out as 247.
 */
static DWORD FAR BoxColor(PBOX pBox, int iColor)
{
    DWORD   dw;
    DWORD   rsum;
    DWORD   gsum;
    DWORD   bsum;
    DWORD   n;
    int     r;
    int     g;
    int     b;

    rsum = gsum = bsum = n = 0;

    for (r = pBox->rmin; r <= pBox->rmax; r++) {
        for (g = pBox->gmin; g <= pBox->gmax; g++) {
            for (b = pBox->bmin; b <= pBox->bmax; b++) {
                if (glp16to8)
                    glp16to8[(r << 10) | (g << 5) | b] = (BYTE)iColor;

                dw = GetHistogram((BYTE)r, (BYTE)g, (BYTE)b);
                if (dw) {
                    rsum += r * dw;
                    gsum += g * dw;
                    bsum += b * dw;
                    n += dw;
                }
            }
        }
    }

    return RGB((BYTE)(((rsum + n / 2) * 255 / n) >> 5),
               (BYTE)(((gsum + n / 2) * 255 / n) >> 5),
               (BYTE)(((bsum + n / 2) * 255 / n) >> 5));
}

/*
 * seg6:177E-1834  Histogram24
 */
static void FAR Histogram24(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram)
{
    BYTE    r;
    BYTE    g;
    BYTE    b;
    int     i;
    int     j;

    UseHistogram(lpHistogram);
    cbScan -= dx * 3;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++) {
            b = *pb++;
            g = *pb++;
            r = *pb++;
            IncHistogram(RGB16(r, g, b));
        }
        pb += cbScan;
    }
}

/*
 * seg6:1836-18A8  Histogram16
 */
static void FAR Histogram16(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram)
{
    WORD _huge *pw;
    int     i;
    int     j;

    pw = (WORD _huge *)pb;
    UseHistogram(lpHistogram);
    cbScan -= dx * 2;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++)
            IncHistogram(*pw++ & 0x7FFF);
        pw = (WORD _huge *)((BYTE _huge *)pw + cbScan);
    }
}

/*
 * seg6:18AA-1924  Histogram8
 */
static void FAR Histogram8(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram, WORD FAR *lpColors)
{
    int     i;
    int     j;

    UseHistogram(lpHistogram);
    cbScan -= dx;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++)
            IncHistogram(lpColors[*pb++]);
        pb += cbScan;
    }
}

/*
 * seg6:1926-19E2  Histogram4
 */
static void FAR Histogram4(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram, WORD FAR *lpColors)
{
    BYTE    bt;
    int     i;
    int     j;

    UseHistogram(lpHistogram);
    cbScan -= (dx + 1) / 2;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < (dx + 1) / 2; j++) {
            bt = *pb++;
            IncHistogram(lpColors[bt >> 4]);
            IncHistogram(lpColors[bt & 0x0F]);
        }
        pb += cbScan;
    }
}

/*
 * seg6:19E4-1AA4  Histogram1
 */
static void FAR Histogram1(BYTE _huge *pb, int dx, int dy, UINT cbScan, LPHISTOGRAM lpHistogram, WORD FAR *lpColors)
{
    BYTE    bt;
    int     i;
    int     j;
    int     k;

    UseHistogram(lpHistogram);
    cbScan -= (dx + 7) / 8;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < (dx + 7) / 8; j++) {
            bt = *pb++;
            for (k = 0; k < 8; k++) {
                IncHistogram(lpColors[bt >> 7]);
                bt <<= 1;
            }
        }
        pb += cbScan;
    }
}

/*
 * seg6:1AA6-1B86  Reduce24
 */
static void FAR Reduce24(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lp16to8)
{
    BYTE    r;
    BYTE    g;
    BYTE    b;
    int     i;
    int     j;

    cbOut -= dx;
    cbIn -= dx * 3;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++) {
            b = *pbIn++;
            g = *pbIn++;
            r = *pbIn++;
            *pbOut++ = lp16to8[RGB16(r, g, b)];
        }
        pbIn += cbIn;
        pbOut += cbOut;
    }
}

/*
 * seg6:1B88-1C21  Reduce16
 */
static void FAR Reduce16(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lp16to8)
{
    WORD _huge *pw;
    int     i;
    int     j;

    pw = (WORD _huge *)pbIn;
    cbOut -= dx;
    cbIn -= dx * 2;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++)
            *pbOut++ = lp16to8[*pw++ & 0x7FFF];
        pw = (WORD _huge *)((BYTE _huge *)pw + cbIn);
        pbOut += cbOut;
    }
}

/*
 * seg6:1C22-1CB6  Reduce8
 */
static void FAR Reduce8(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lpMap)
{
    int     i;
    int     j;

    cbIn -= dx;
    cbOut -= dx;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++)
            *pbOut++ = lpMap[*pbIn++];
        pbIn += cbIn;
        pbOut += cbOut;
    }
}

/*
 * seg6:1CB8-1D9A  Reduce4
 */
static void FAR Reduce4(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lpMap)
{
    BYTE    bt;
    int     i;
    int     j;

    cbIn -= (dx + 1) / 2;
    cbOut -= (dx + 1) & ~1;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < (dx + 1) / 2; j++) {
            bt = *pbIn++;
            *pbOut++ = lpMap[bt >> 4];
            *pbOut++ = lpMap[bt & 0x0F];
        }
        pbIn += cbIn;
        pbOut += cbOut;
    }
}

/*
 * seg6:1D9C-1E54  Reduce1
 */
static void FAR Reduce1(BYTE _huge *pbIn, int dx, int dy, UINT cbIn, BYTE _huge *pbOut, UINT cbOut, LPBYTE lpMap)
{
    BYTE    bt;
    int     i;
    int     j;

    cbIn -= (dx + 7) / 8;
    cbOut -= dx;

    for (i = 0; i < dy; i++) {
        for (j = 0; j < dx; j++) {
            if ((j & 7) == 0)
                bt = *pbIn++;
            *pbOut++ = lpMap[bt >> 7];
            bt <<= 1;
        }
        pbIn += cbIn;
        pbOut += cbOut;
    }
}
