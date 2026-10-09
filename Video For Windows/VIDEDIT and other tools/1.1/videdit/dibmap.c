/*
 * dibmap.c - code segment 16 of VIDEDIT.EXE, s16:0000-1BDF: colour
 * histograms, the optimal palette and colour reduction
 *
 * This is the DIBMAP module of the Video for Windows 1.1 DK's PALMAP
 * sample (Create Palette uses it): a histogram of 15 bit RGB colours is
 * built from the frames, a median-cut style box splitter ("cluster.c")
 * picks the palette, and DibReduce maps a DIB onto a palette through a
 * 32K table of RGB555 -> palette index.  VidEdit's copy differs from the
 * DK sample in one place: when the display has no palette, the 20 static
 * colours come from the default palette.
 *
 * The functions are FAR _cdecl.  HistogramPalette was compiled without
 * optimisation (its calls to the other functions of the module are far
 * calls with relocations).  The module has no initialised data.
 *
 * The original reaches the 128K histogram through FS: UseHistogram loads
 * the selector into FS and GetHistogram / IncHistogram (hand-written 386
 * code) index it with EBX = rgb555 * 4, which relies on that selector's
 * limit covering the whole block.  The rebuild uses a huge pointer.
 */

#include "videdit.h"
#include "dibmap.h"

/* the boxes ("cluster.c") */

#define IN_DEPTH        5                       /* bits per component kept */
#define IN_SIZE         (1 << IN_DEPTH)

typedef enum { red, green, blue } color;

typedef struct tagCut {                         /* 0x1C bytes */
    long            lvariance;                  /* 00 unused */
    int             cutpoint;                   /* 04 */
    unsigned long   rem;                        /* 06 unused */
    color           cutaxis;                    /* 0A */
    long            w1, w2;                     /* 0C unused */
    double          variance;                   /* 14 unused */
} Cut;

typedef struct tagColorBox {                    /* 0x22 bytes */
    struct tagColorBox *next;                   /* 00 */
    int             rmin, rmax;                 /* 02 */
    int             gmin, gmax;                 /* 06 */
    int             bmin, bmax;                 /* 0A */
    long            variance;                   /* 0E */
    long            wt;                         /* 12 number of pixels */
    long            sum[3];                     /* 16 unused */
} ColorBox;

static struct {                                 /* DS:21D2 */
    WORD            palVersion;
    WORD            palNumEntries;
    PALETTEENTRY    palPalEntry[256];
} pal;

static ColorBox    *UsedBoxes;                  /* DS:3060 sorted by variance, largest first */
static LPBYTE       glp16to8;                   /* DS:318E */
static ColorBox    *FreeBoxes;                  /* DS:31AC */
static HLOCAL       hBoxes;                     /* DS:31F2 */

static LPHISTOGRAM  glpHistogram;               /* the rebuild's stand-in for FS */

static BOOL     InitBoxes(int nBoxes);
static void     DeleteBoxes(void);
static void     SortBoxes(void);
static void     SplitBox(ColorBox *box);
static Cut      FindSplitAxis(ColorBox *box);
static int      SplitBoxAxis(ColorBox *box, Cut cutaxis);
static void     ShrinkBox(ColorBox *box);
static COLORREF DetermineRepresentative(ColorBox *box, int palIndex);

static void Histogram24(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram);
static void Histogram16(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram);
static void Histogram8(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram, LPWORD lpColors);
static void Histogram4(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram, LPWORD lpColors);
static void Histogram1(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram, LPWORD lpColors);
static void Reduce24(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut, LPBYTE lp16to8);
static void Reduce16(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut, LPBYTE lp16to8);
static void Reduce8(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut, LPBYTE lp8to8);
static void Reduce4(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut, LPBYTE lp8to8);
static void Reduce1(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut, LPBYTE lp8to8);

/*
 * s16:0000-002A  InitHistogram
 *
 * Allocates a zeroed histogram (32768 DWORD counts) unless one is given.
 */
LPHISTOGRAM FAR InitHistogram(LPHISTOGRAM lpHistogram)
{
    if (lpHistogram == NULL)
        lpHistogram = (LPHISTOGRAM)GlobalLock(GlobalAlloc(GHND, 32768L * sizeof(DWORD)));

    return lpHistogram;
}

/*
 * s16:002C-0038  FreeHistogram
 *
 * Frees the block by its selector, without unlocking it.
 */
void FAR FreeHistogram(LPHISTOGRAM lpHistogram)
{
    GlobalFree((HGLOBAL)SELECTOROF(lpHistogram));
}

/*
 * s16:003A-0279  DibHistogram
 *
 * Counts the colours of the dx x dy area at (x, y) of a 1, 4, 8, 16 or 24
 * bit DIB (dx or dy < 0, or too large: the whole width/height).  Fills in
 * biClrUsed if it is 0.  The original returns whatever the counting
 * routine left in AX (0 for missing arguments); the callers ignore it.
 */
BOOL FAR DibHistogram(LPBITMAPINFOHEADER lpbi, LPBYTE lpBits, int x, int y, int dx, int dy,
                      LPHISTOGRAM lpHistogram)
{
    int             i;
    WORD            WidthBytes;
    RGBQUAD FAR    *prgbq;
    WORD            argb16[256];

    if (lpbi == NULL || lpHistogram == NULL)
        return FALSE;

    if (lpbi->biClrUsed == 0 && lpbi->biBitCount <= 8)
        lpbi->biClrUsed = 1 << lpbi->biBitCount;

    if (lpBits == NULL)
        lpBits = (LPBYTE)lpbi + (WORD)lpbi->biSize + (WORD)lpbi->biClrUsed * sizeof(RGBQUAD);

    WidthBytes = ((WORD)((lpbi->biBitCount * lpbi->biWidth + 7) / 8) + 3) & ~3;

    lpBits = (LPBYTE)((BYTE huge *)lpBits + ((DWORD)y * WidthBytes + (x * (int)lpbi->biBitCount) / 8));

    if (dx < 0 || dx > (int)lpbi->biWidth)
        dx = (int)lpbi->biWidth;

    if (dy < 0 || dy > (int)lpbi->biHeight)
        dy = (int)lpbi->biHeight;

    if ((int)lpbi->biBitCount <= 8) {
        prgbq = (RGBQUAD FAR *)((LPBYTE)lpbi + (WORD)lpbi->biSize);

        for (i = 0; i < (int)lpbi->biClrUsed; i++)
            argb16[i] = RGB16(prgbq[i].rgbRed, prgbq[i].rgbGreen, prgbq[i].rgbBlue);

        for (i = (int)lpbi->biClrUsed; i < 256; i++)
            argb16[i] = 0;
    }

    switch (lpbi->biBitCount) {
    case 24:
        Histogram24(lpBits, dx, dy, WidthBytes, lpHistogram);
        break;
    case 16:
        Histogram16(lpBits, dx, dy, WidthBytes, lpHistogram);
        break;
    case 8:
        Histogram8(lpBits, dx, dy, WidthBytes, lpHistogram, argb16);
        break;
    case 4:
        Histogram4(lpBits, dx, dy, WidthBytes, lpHistogram, argb16);
        break;
    case 1:
        Histogram1(lpBits, dx, dy, WidthBytes, lpHistogram, argb16);
        break;
    }

    return TRUE;
}

/*
 * s16:027A-0571  DibReduce
 *
 * Makes an 8 bit copy of a DIB using the colours of hpal.  16 and 24 bit
 * pixels go through lp16to8 (RGB555 -> index); 1, 4 and 8 bit pixels
 * through a table built from their colour table.  The source row size is
 * computed in 16 bits (biBitCount * width), so it is wrong for 24 bit
 * DIBs wider than 2730 pixels.  The new DIB is returned locked.
 */
HANDLE FAR DibReduce(LPBITMAPINFOHEADER lpbiIn, LPBYTE pbIn, HPALETTE hpal, LPBYTE lp16to8)
{
    HANDLE              hdib;
    int                 nPalColors;
    int                 nDibColors;
    WORD                cbOut;
    WORD                cbIn;
    BYTE                xlat[256];
    BYTE huge          *pbOut;
    RGBQUAD FAR        *prgb;
    DWORD               dwSize;
    int                 i;
    int                 dx;
    int                 dy;
    PALETTEENTRY        pe;
    LPBITMAPINFOHEADER  lpbiOut;

    dx = (int)lpbiIn->biWidth;
    dy = (int)lpbiIn->biHeight;
    cbIn = (((WORD)(lpbiIn->biBitCount * dx) + 7) / 8 + 3) & ~3;
    cbOut = (dx + 3) & ~3;

    GetObject(hpal, sizeof(int), (LPVOID)&nPalColors);

    nDibColors = (int)lpbiIn->biClrUsed;
    if (nDibColors == 0 && lpbiIn->biBitCount <= 8)
        nDibColors = 1 << lpbiIn->biBitCount;

    if (pbIn == NULL)
        pbIn = (LPBYTE)lpbiIn + (WORD)lpbiIn->biSize + nDibColors * sizeof(RGBQUAD);

    dwSize = (DWORD)cbOut * (long)dy;

    hdib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + nPalColors * sizeof(RGBQUAD) + dwSize);
    if (!hdib)
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

    pbOut = (BYTE huge *)lpbiOut + sizeof(BITMAPINFOHEADER) + nPalColors * sizeof(RGBQUAD);
    prgb = (RGBQUAD FAR *)((LPBYTE)lpbiOut + sizeof(BITMAPINFOHEADER));

    for (i = 0; i < nPalColors; i++) {
        GetPaletteEntries(hpal, i, 1, &pe);
        prgb[i].rgbRed = pe.peRed;
        prgb[i].rgbGreen = pe.peGreen;
        prgb[i].rgbReserved = 0;
        prgb[i].rgbBlue = pe.peBlue;
    }

    if ((int)lpbiIn->biBitCount <= 8) {
        prgb = (RGBQUAD FAR *)((LPBYTE)lpbiIn + (WORD)lpbiIn->biSize);

        for (i = 0; i < nDibColors; i++)
            xlat[i] = lp16to8[RGB16(prgb[i].rgbRed, prgb[i].rgbGreen, prgb[i].rgbBlue)];

        for (; i < 256; i++)
            xlat[i] = 0;
    }

    switch (lpbiIn->biBitCount) {
    case 24:
        Reduce24(pbIn, dx, dy, cbIn, pbOut, cbOut, lp16to8);
        break;
    case 16:
        Reduce16(pbIn, dx, dy, cbIn, pbOut, cbOut, lp16to8);
        break;
    case 8:
        Reduce8(pbIn, dx, dy, cbIn, pbOut, cbOut, xlat);
        break;
    case 4:
        Reduce4(pbIn, dx, dy, cbIn, pbOut, cbOut, xlat);
        break;
    case 1:
        Reduce1(pbIn, dx, dy, cbIn, pbOut, cbOut, xlat);
        break;
    }

    return hdib;
}

/*
 * s16:0572-0581  UseHistogram  (NEAR PASCAL)
 *
 * Original: loads FS with SELECTOROF(lpHistogram).
 */
static void NEAR PASCAL UseHistogram(LPHISTOGRAM lpHistogram)
{
    glpHistogram = lpHistogram;
}

/*
 * s16:0583-05B9  GetHistogram  (NEAR, register arguments AL, DL, BL;
 * result DX:AX; assembly)
 *
 * The count of a colour, r, g and b being 0-31.  (The assembly starts with
 * an unreachable "return 0" the compiler kept for the C wrapper.)
 */
static DWORD NEAR GetHistogram(BYTE r, BYTE g, BYTE b)
{
    return glpHistogram[((((WORD)r << 5) | g) << 5) | b];
}

/*
 * s16:05BA-05DA  IncHistogram  (NEAR, argument in AX; assembly)
 *
 * Counts one pixel, saturating at 0xFFFFFFFF.
 */
static void NEAR IncHistogram(WORD rgb16)
{
    if (glpHistogram[rgb16] != 0xFFFFFFFFL)
        glpHistogram[rgb16]++;
}

/*
 * s16:05DB-08A1  HistogramPalette
 *
 * Reduces the histogram to an optimal palette of nColors colours (at most
 * 236 are computed).  If lp16to8 is given it receives the RGB555 -> index
 * table; it may be the histogram itself.  With more than 236 colours the
 * palette gets the 20 static colours at 0-9 and 246-255: from the system
 * palette on a palette device, else from the default palette.
 */
HPALETTE FAR HistogramPalette(LPHISTOGRAM lpHistogram, LPBYTE lp16to8, int nColors)
{
    WORD        w;
    DWORD       dwMax;
    COLORREF    rgb;
    ColorBox   *box;
    int         i;
    HDC         hdc;
    HPALETTE    hpalDef;

    /* the box code can't take counts above 64K: halve until they fit */
    for (dwMax = 0, w = 0; w < 0x8000; w++)
        dwMax = max(dwMax, lpHistogram[w]);

    while (dwMax > 0xFFFFL) {
        for (w = 0; w < 0x8000; w++)
            lpHistogram[w] /= 2;
        dwMax /= 2;
    }

    if (!InitBoxes(min(nColors, 236)))
        return NULL;

    UseHistogram(lpHistogram);
    glp16to8 = lp16to8;

    /* split the box with the largest variance while boxes are left */
    i = 0;
    do {
        i++;
        SplitBox(UsedBoxes);
    } while (FreeBoxes && UsedBoxes->variance);

    SortBoxes();

    i = 0;

    if (nColors > 236) {
        hdc = GetDC(NULL);

        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            GetSystemPaletteEntries(hdc, 0, 10, &pal.palPalEntry[0]);
            GetSystemPaletteEntries(hdc, 246, 10, &pal.palPalEntry[246]);
            i = 10;
        } else {
            hpalDef = (HPALETTE)GetStockObject(DEFAULT_PALETTE);
            GetPaletteEntries(hpalDef, 0, 10, &pal.palPalEntry[0]);
            GetPaletteEntries(hpalDef, 10, 10, &pal.palPalEntry[246]);
            i = 10;
        }

        ReleaseDC(NULL, hdc);
    }

    for (box = UsedBoxes; box; box = box->next, i++) {
        rgb = DetermineRepresentative(box, i);
        pal.palPalEntry[i].peRed = GetRValue(rgb);
        pal.palPalEntry[i].peGreen = GetGValue(rgb);
        pal.palPalEntry[i].peBlue = GetBValue(rgb);
        pal.palPalEntry[i].peFlags = 0;
    }

    DeleteBoxes();

    if (nColors > 236) {
        for (; i < 246; i++) {
            pal.palPalEntry[i].peRed = 0;
            pal.palPalEntry[i].peGreen = 0;
            pal.palPalEntry[i].peBlue = 0;
            pal.palPalEntry[i].peFlags = 0;
        }
        i = 256;
    }

    glp16to8 = NULL;

    pal.palVersion = 0x300;
    pal.palNumEntries = i;
    return CreatePalette((LPLOGPALETTE)&pal);
}

/*
 * s16:08A2-0905  SortBoxes
 *
 * Insertion sort of the used boxes by pixel count, largest first.
 */
static void SortBoxes(void)
{
    ColorBox *box;
    ColorBox *newList;
    ColorBox *insBox;
    ColorBox *nextBox;

    newList = UsedBoxes;
    nextBox = newList->next;
    newList->next = NULL;

    for (box = nextBox; box; box = nextBox) {
        nextBox = box->next;
        if (box->wt > newList->wt) {
            box->next = newList;
            newList = box;
        } else {
            for (insBox = newList; insBox->next && box->wt < insBox->next->wt; insBox = insBox->next)
                ;
            box->next = insBox->next;
            insBox->next = box;
        }
    }

    UsedBoxes = newList;
}

/*
 * s16:0906-0988  InitBoxes
 *
 * One used box covering the whole colour cube, nBoxes - 1 free ones.
 */
static BOOL InitBoxes(int nBoxes)
{
    int i;

    hBoxes = LocalAlloc(LHND, nBoxes * sizeof(ColorBox));
    if (!hBoxes)
        return FALSE;

    UsedBoxes = (ColorBox *)LocalLock(hBoxes);
    FreeBoxes = UsedBoxes + 1;
    UsedBoxes->next = NULL;

    for (i = 0; i < nBoxes - 1; i++)
        FreeBoxes[i].next = &FreeBoxes[i + 1];
    FreeBoxes[nBoxes - 2].next = NULL;

    UsedBoxes->bmin = UsedBoxes->gmin = UsedBoxes->rmin = 0;
    UsedBoxes->bmax = UsedBoxes->gmax = UsedBoxes->rmax = IN_SIZE - 1;
    UsedBoxes->variance = 9999999L;

    return TRUE;
}

/*
 * s16:098A-09A2  DeleteBoxes
 */
static void DeleteBoxes(void)
{
    LocalUnlock(hBoxes);
    LocalFree(hBoxes);
    hBoxes = NULL;
}

/*
 * s16:09A4-0A87  SplitBox
 *
 * Splits a box in two and keeps the used list sorted by variance.
 */
static void SplitBox(ColorBox *box)
{
    Cut         cutaxis;
    ColorBox   *temp;
    ColorBox   *temp2;
    ColorBox   *prev;

    cutaxis = FindSplitAxis(box);

    /* a box with a single colour is not split */
    if (SplitBoxAxis(box, cutaxis))
        return;

    ShrinkBox(box);
    ShrinkBox(FreeBoxes);

    /* move the old box down the list if it is no longer the largest */
    if (box->next && box->variance < box->next->variance) {
        UsedBoxes = box->next;
        temp = box;
        do {
            prev = temp;
            temp = temp->next;
        } while (temp && temp->variance > box->variance);
        box->next = temp;
        prev->next = box;
    }

    /* take the new box off the free list and insert it in order */
    if (FreeBoxes->variance >= UsedBoxes->variance) {
        temp = FreeBoxes;
        FreeBoxes = FreeBoxes->next;
        temp->next = UsedBoxes;
        UsedBoxes = temp;
    } else {
        temp = UsedBoxes;
        do {
            prev = temp;
            temp = temp->next;
        } while (temp && temp->variance > FreeBoxes->variance);
        temp2 = FreeBoxes->next;
        FreeBoxes->next = temp;
        prev->next = FreeBoxes;
        FreeBoxes = temp2;
    }
}

/*
 * s16:0A88-1047  FindSplitAxis
 *
 * Projects the box onto the three axes and picks the cut with the largest
 * between-class variance.  Only cutaxis and cutpoint of the result are
 * set (the original returns it in the C runtime's struct buffer, DS:3202);
 * if no cut improves on 0 they are left uninitialised.
 */
static Cut FindSplitAxis(ColorBox *box)
{
    DWORD   projR[IN_SIZE];
    DWORD   projG[IN_SIZE];
    DWORD   projB[IN_SIZE];
    DWORD   f;
    DWORD   w;
    DWORD   w1;
    DWORD   m;
    DWORD   m1;
    double  dMax;
    double  dMean;
    double  d1;
    double  d2;
    short   r;
    short   g;
    short   b;
    short   bestCut;
    color   bestAxis;
    Cut     cutRet;

    for (r = 0; r < IN_SIZE; r++)
        projR[r] = projG[r] = projB[r] = 0;

    w = 0;
    for (r = box->rmin; r <= box->rmax; r++) {
        for (g = box->gmin; g <= box->gmax; g++) {
            for (b = box->bmin; b <= box->bmax; b++) {
                f = GetHistogram((BYTE)r, (BYTE)g, (BYTE)b);
                projR[r] += f;
                projG[g] += f;
                projB[b] += f;
            }
        }
        w += projR[r];
    }

    dMax = 0.0;

    /* red */
    m = 0;
    for (r = box->rmin; r <= box->rmax; r++)
        m += r * projR[r];
    dMean = (double)m / (double)w;
    w1 = 0;
    m1 = 0;
    for (r = box->rmin; r <= box->rmax; r++) {
        w1 += projR[r];
        if (w1 == 0)
            continue;
        if (w1 == w)
            break;
        m1 += r * projR[r];
        d1 = dMean - (double)m1 / (double)w1;
        d2 = ((double)w1 / (double)(w - w1)) * d1 * d1;
        if (d2 > dMax) {
            bestCut = r;
            bestAxis = red;
            dMax = d2;
        }
    }

    /* green */
    m = 0;
    for (g = box->gmin; g <= box->gmax; g++)
        m += g * projG[g];
    dMean = (double)m / (double)w;
    w1 = 0;
    m1 = 0;
    for (g = box->gmin; g <= box->gmax; g++) {
        w1 += projG[g];
        if (w1 == 0)
            continue;
        if (w1 == w)
            break;
        m1 += g * projG[g];
        d1 = dMean - (double)m1 / (double)w1;
        d2 = ((double)w1 / (double)(w - w1)) * d1 * d1;
        if (d2 > dMax) {
            bestCut = g;
            bestAxis = green;
            dMax = d2;
        }
    }

    /* blue */
    m = 0;
    for (b = box->bmin; b <= box->bmax; b++)
        m += b * projB[b];
    dMean = (double)m / (double)w;
    w1 = 0;
    m1 = 0;
    for (b = box->bmin; b <= box->bmax; b++) {
        w1 += projB[b];
        if (w1 == 0)
            continue;
        if (w1 == w)
            break;
        m1 += b * projB[b];
        d1 = dMean - (double)m1 / (double)w1;
        d2 = ((double)w1 / (double)(w - w1)) * d1 * d1;
        if (d2 > dMax) {
            bestCut = b;
            bestAxis = blue;
            dMax = d2;
        }
    }

    cutRet.cutaxis = bestAxis;
    cutRet.cutpoint = bestCut;
    return cutRet;
}

/*
 * s16:1048-10CA  SplitBoxAxis
 *
 * Cuts box after cutpoint; the upper part goes into the first free box.
 * Returns 1 (no split) for a box with zero variance.
 */
static int SplitBoxAxis(ColorBox *box, Cut cutaxis)
{
    ColorBox *next;

    if (box->variance == 0)
        return 1;

    next = FreeBoxes->next;
    *FreeBoxes = *box;
    FreeBoxes->next = next;

    switch (cutaxis.cutaxis) {
    case red:
        box->rmax = cutaxis.cutpoint;
        FreeBoxes->rmin = cutaxis.cutpoint + 1;
        break;
    case green:
        box->gmax = cutaxis.cutpoint;
        FreeBoxes->gmin = cutaxis.cutpoint + 1;
        break;
    case blue:
        box->bmax = cutaxis.cutpoint;
        FreeBoxes->bmin = cutaxis.cutpoint + 1;
        break;
    }

    return 0;
}

/*
 * s16:10CC-13F0  ShrinkBox
 *
 * Computes the pixel count and the variance of a box.  (The bounds are
 * not actually shrunk.)  The variance is computed as
 * sum(x^2) - (sum(x)/n) * sum(x) - (sum(x)%n) * sum(x) / n per axis, in
 * 32 bit unsigned arithmetic.
 */
static void ShrinkBox(ColorBox *box)
{
    DWORD   n;
    DWORD   sxx;
    DWORD   sx2;
    DWORD   var;
    DWORD   quotient;
    DWORD   remainder;
    DWORD   f;
    DWORD   projR[IN_SIZE];
    DWORD   projG[IN_SIZE];
    DWORD   projB[IN_SIZE];
    int     r;
    int     g;
    int     b;

    n = 0;

    for (r = 0; r < IN_SIZE; r++)
        projR[r] = projG[r] = projB[r] = 0;

    for (r = box->rmin; r <= box->rmax; r++) {
        for (g = box->gmin; g <= box->gmax; g++) {
            for (b = box->bmin; b <= box->bmax; b++) {
                f = GetHistogram((BYTE)r, (BYTE)g, (BYTE)b);
                projR[r] += f;
                projG[g] += f;
                projB[b] += f;
            }
        }
        n += projR[r];
    }

    box->wt = n;
    var = 0;

    sxx = 0;
    sx2 = 0;
    for (r = box->rmin; r <= box->rmax; r++) {
        sxx += projR[r] * r * r;
        sx2 += projR[r] * r;
    }
    quotient = sx2 / n;
    remainder = sx2 % n;
    var += sxx - quotient * sx2 - (remainder * sx2) / n;

    sxx = 0;
    sx2 = 0;
    for (g = box->gmin; g <= box->gmax; g++) {
        sxx += projG[g] * g * g;
        sx2 += projG[g] * g;
    }
    quotient = sx2 / n;
    remainder = sx2 % n;
    var += sxx - quotient * sx2 - (remainder * sx2) / n;

    sxx = 0;
    sx2 = 0;
    for (b = box->bmin; b <= box->bmax; b++) {
        sxx += projB[b] * b * b;
        sx2 += projB[b] * b;
    }
    quotient = sx2 / n;
    remainder = sx2 % n;
    var += sxx - quotient * sx2 - (remainder * sx2) / n;

    box->variance = var;
}

/*
 * s16:13F2-15A9  DetermineRepresentative
 *
 * The weighted mean colour of a box, rounded, scaled from 0-31 to 0-255.
 * Also enters palIndex for every RGB555 colour of the box into glp16to8.
 */
static COLORREF DetermineRepresentative(ColorBox *box, int palIndex)
{
    long    f;
    long    Rval;
    long    Gval;
    long    Bval;
    DWORD   total;
    int     r;
    int     g;
    int     b;

    Rval = Gval = Bval = 0;
    total = 0;

    for (r = box->rmin; r <= box->rmax; r++) {
        for (g = box->gmin; g <= box->gmax; g++) {
            for (b = box->bmin; b <= box->bmax; b++) {
                if (glp16to8)
                    glp16to8[(WORD)b | ((WORD)g << IN_DEPTH) | ((WORD)r << (IN_DEPTH * 2))] = (BYTE)palIndex;

                f = GetHistogram((BYTE)r, (BYTE)g, (BYTE)b);
                if (f == 0L)
                    continue;

                Rval += f * (long)r;
                Gval += f * (long)g;
                Bval += f * (long)b;
                total += f;
            }
        }
    }

    /* round to nearest */
    Rval += total / 2;
    Gval += total / 2;
    Bval += total / 2;

    return RGB(Rval * 255 / total / IN_SIZE,
               Gval * 255 / total / IN_SIZE,
               Bval * 255 / total / IN_SIZE);
}

/*
 * s16:15AA-164F  Histogram24
 */
static void Histogram24(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram)
{
    int     x;
    int     y;
    BYTE    r;
    BYTE    g;
    BYTE    b;

    UseHistogram(lpHistogram);

    WidthBytes -= dx * 3;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            b = *pb++;
            g = *pb++;
            r = *pb++;
            IncHistogram(RGB16(r, g, b));
        }
        pb += WidthBytes;
    }
}

/*
 * s16:1650-16B2  Histogram16  (RGB555; bit 15 is ignored)
 */
static void Histogram16(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram)
{
    int     x;
    int     y;
    WORD    w;

    UseHistogram(lpHistogram);

    WidthBytes -= dx * 2;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            w = *(WORD huge *)pb;
            pb += 2;
            IncHistogram(w & 0x7FFF);
        }
        pb += WidthBytes;
    }
}

/*
 * s16:16B4-171E  Histogram8
 */
static void Histogram8(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram,
                       LPWORD lpColors)
{
    int x;
    int y;

    UseHistogram(lpHistogram);

    WidthBytes -= dx;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++)
            IncHistogram(lpColors[*pb++]);
        pb += WidthBytes;
    }
}

/*
 * s16:1720-17CB  Histogram4
 *
 * Counts both nibbles of every byte, so an odd width counts one pixel of
 * padding per row.
 */
static void Histogram4(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram,
                       LPWORD lpColors)
{
    int     x;
    int     y;
    BYTE    b;

    UseHistogram(lpHistogram);

    WidthBytes -= (dx + 1) / 2;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < (dx + 1) / 2; x++) {
            b = *pb++;
            IncHistogram(lpColors[b >> 4]);
            IncHistogram(lpColors[b & 0x0F]);
        }
        pb += WidthBytes;
    }
}

/*
 * s16:17CC-187B  Histogram1
 *
 * Counts all 8 bits of every byte, padding included.
 */
static void Histogram1(BYTE huge *pb, int dx, int dy, WORD WidthBytes, LPHISTOGRAM lpHistogram,
                       LPWORD lpColors)
{
    int     x;
    int     y;
    int     i;
    BYTE    b;

    UseHistogram(lpHistogram);

    WidthBytes -= (dx + 7) / 8;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < (dx + 7) / 8; x++) {
            b = *pb++;
            for (i = 0; i < 8; i++) {
                IncHistogram(lpColors[b >> 7]);
                b <<= 1;
            }
        }
        pb += WidthBytes;
    }
}

/*
 * s16:187C-194C  Reduce24
 */
static void Reduce24(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut,
                     LPBYTE lp16to8)
{
    int     x;
    int     y;
    BYTE    r;
    BYTE    g;
    BYTE    b;

    cbOut -= dx;
    cbIn -= dx * 3;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
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
 * s16:194E-19D9  Reduce16
 */
static void Reduce16(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut,
                     LPBYTE lp16to8)
{
    int     x;
    int     y;
    WORD    w;

    cbOut -= dx;
    cbIn -= dx * 2;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            w = *(WORD huge *)pbIn;
            pbIn += 2;
            *pbOut++ = lp16to8[w & 0x7FFF];
        }
        pbIn += cbIn;
        pbOut += cbOut;
    }
}

/*
 * s16:19DA-1A60  Reduce8
 */
static void Reduce8(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut,
                    LPBYTE lp8to8)
{
    int x;
    int y;

    cbIn -= dx;
    cbOut -= dx;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++)
            *pbOut++ = lp8to8[*pbIn++];
        pbIn += cbIn;
        pbOut += cbOut;
    }
}

/*
 * s16:1A62-1B35  Reduce4
 */
static void Reduce4(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut,
                    LPBYTE lp8to8)
{
    int     x;
    int     y;
    BYTE    b;

    cbIn -= (dx + 1) / 2;
    cbOut -= (dx + 1) & ~1;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < (dx + 1) / 2; x++) {
            b = *pbIn++;
            *pbOut++ = lp8to8[b >> 4];
            *pbOut++ = lp8to8[b & 0x0F];
        }
        pbIn += cbIn;
        pbOut += cbOut;
    }
}

/*
 * s16:1B36-1BDF  Reduce1
 */
static void Reduce1(BYTE huge *pbIn, int dx, int dy, WORD cbIn, BYTE huge *pbOut, WORD cbOut,
                    LPBYTE lp8to8)
{
    int     x;
    int     y;
    BYTE    b;

    cbIn -= (dx + 7) / 8;
    cbOut -= dx;

    for (y = 0; y < dy; y++) {
        for (x = 0; x < dx; x++) {
            if (x % 8 == 0)
                b = *pbIn++;
            *pbOut++ = lp8to8[b >> 7];
            b <<= 1;
        }
        pbIn += cbIn;
        pbOut += cbOut;
    }
}
