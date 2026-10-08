/*
 * invcmap.c - code segment 3, offsets 0x1096-0x1D2B
 *
 * Compression setup for 8-bit CRAM output: the 32K RGB555 -> palette
 * index table, built with Spencer W. Thomas' incremental-distance
 * inverse colormap algorithm ("Efficient Inverse Color Map
 * Computation", Graphics Gems II).  The function and variable layout in
 * the binary matches his inv_cmap.c (inv_cmap, redloop, greenloop,
 * blueloop, maxfill, in that order), adapted to a huge distance buffer
 * and a far result table.  The names below are his.
 *
 * Also here: the quality -> error threshold mapping and the
 * CompressBegin/End helpers.
 */

#include <windows.h>
#include <string.h>
#include "msvidc.h"

BYTE        g_ab5to8[32] = { 0xFF };    /* DS:081A, 0xFF in [0] = not built yet */
RGBQUAD     g_rgbqIn[256];              /* DS:0A74 */
RGBQUAD     g_rgbqOut[256];             /* DS:0E74 */
BYTE FAR   *g_lpInvPal;                 /* DS:1274 */

static BYTE abColormap[3][256];         /* DS:141E red, DS:151E green, DS:161E blue */

static double dThresholdScale = 3.2768e-12;     /* DS:09EA, == 32768 / 10000^4 */

/* inv_cmap() state shared with the loops */
static int      bcenter, gcenter, rcenter;      /* DS:09FC, 09FE, 0A00 */
static long     gdist, rdist, cdist;            /* DS:0A02, 0A06, 0A0A */
static long     cbinc, cginc, crinc;            /* DS:0A0E, 0A12, 0A16 */
static DWORD _huge *gdp, _huge *rdp, _huge *cdp;/* DS:0A1A, 0A1E, 0A22 */
static BYTE FAR *grgbp, FAR *rrgbp, FAR *crgbp; /* DS:0A26, 0A2A, 0A2E */
static int      gstride, rstride;               /* DS:0A32, 0A34 */
static long     x, xsqr, colormax;              /* DS:0A36, 0A3A, 0A3E */
static int      cindex;                         /* DS:0A42 */

static int  NEAR redloop(void);
static int  NEAR greenloop(int restart);
static int  NEAR blueloop(int restart);
static void NEAR maxfill(DWORD _huge *buffer, long side);

static void NEAR inv_cmap(int colors, BYTE (NEAR *colormap)[256], int bits,
                          DWORD _huge *dist_buf, BYTE FAR *rgbmap);

/*
 * seg3:1096-1156  BuildInverseTable  (FAR PASCAL)
 *
 * Returns a locked 32K table indexed by (r5 << 10) | (g5 << 5) | b5.
 * If the 128K distance buffer can't be allocated, it returns NULL and
 * leaks the 32K table.
 */
LPBYTE FAR PASCAL BuildInverseTable(LPRGBQUAD prgb, int nColors)
{
    LPBYTE          lpInv;
    DWORD _huge    *lpDist;
    int             i;

    lpInv = (LPBYTE)GlobalLock(GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE | GMEM_ZEROINIT, 32768L));
    if (lpInv == NULL)
        return NULL;

#ifdef __WATCOMC__
    /*
     * Not in the original.  The loops form pointers up to about one red
     * plane before and after the distance buffer without dereferencing
     * them.  MSC keeps huge pointers in memory, so that is harmless;
     * Open Watcom keeps them in ES:BX, and loading a selector outside
     * the block faults in protected mode.  Pad by one 64K tile on each
     * side.
     */
    lpDist = (DWORD _huge *)GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                                   32L * 32 * 32 * sizeof(DWORD) + 0x20000L));
    if (lpDist == NULL)
        return NULL;
    lpDist += 0x10000L / sizeof(DWORD);
#else
    lpDist = (DWORD _huge *)GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 32L * 32 * 32 * sizeof(DWORD)));
    if (lpDist == NULL)
        return NULL;
#endif

    for (i = 0; i < nColors; i++) {
        abColormap[0][i] = prgb[i].rgbRed;
        abColormap[1][i] = prgb[i].rgbGreen;
        abColormap[2][i] = prgb[i].rgbBlue;
    }

    inv_cmap(nColors, abColormap, 5, lpDist, lpInv);

#ifdef __WATCOMC__
    lpDist -= 0x10000L / sizeof(DWORD);
#endif
    GlobalUnlock(GlobalHandle(SELECTOROF(lpDist)));
    GlobalFree(GlobalHandle(SELECTOROF(lpDist)));

    return lpInv;
}

/*
 * seg3:115A-1429  inv_cmap  (NEAR CDECL)
 */
static void NEAR inv_cmap(int colors, BYTE (NEAR *colormap)[256], int bits,
                          DWORD _huge *dist_buf, BYTE FAR *rgbmap)
{
    int nbits = 8 - bits;

    colormax = 1 << bits;
    x        = 1 << nbits;
    xsqr     = 1 << (2 * nbits);

    gstride  = (int)colormax;
    rstride  = (int)colormax * (int)colormax;

    maxfill(dist_buf, colormax);

    for (cindex = 0; cindex < colors; cindex++) {
        /*
         * Distances are measured from the centre of each quantised cell,
         * and the increments are kept incrementally (see Thomas).
         */
        rcenter = colormap[0][cindex] >> nbits;
        gcenter = colormap[1][cindex] >> nbits;
        bcenter = colormap[2][cindex] >> nbits;

        rdist = colormap[0][cindex] - (rcenter * x + x / 2);
        gdist = colormap[1][cindex] - (gcenter * x + x / 2);
        cdist = colormap[2][cindex] - (bcenter * x + x / 2);
        cdist = rdist * rdist + gdist * gdist + cdist * cdist;

        crinc = 2 * ((rcenter + 1) * xsqr - (colormap[0][cindex] * x));
        cginc = 2 * ((gcenter + 1) * xsqr - (colormap[1][cindex] * x));
        cbinc = 2 * ((bcenter + 1) * xsqr - (colormap[2][cindex] * x));

        cdp   = dist_buf + rcenter * rstride + gcenter * gstride + bcenter;
        crgbp = rgbmap   + rcenter * rstride + gcenter * gstride + bcenter;

        (void)redloop();
    }
}

/*
 * seg3:142A-15A8  redloop  (NEAR)
 */
static int NEAR redloop(void)
{
    int         detect;
    int         r;
    int         first;
    long        txsqr = xsqr + xsqr;
    static long rxx;                            /* DS:0A44 */

    detect = 0;

    /* basic loop up */
    for (r = rcenter, rdist = cdist, rxx = crinc,
         rdp = cdp, rrgbp = crgbp, first = 1;
         r < (int)colormax;
         r++, rdp += rstride, rrgbp += rstride,
         rdist += rxx, rxx += txsqr,
         first = 0)
    {
        if (greenloop(first))
            detect = 1;
        else if (detect)
            break;
    }

    /* basic loop down */
    for (r = rcenter - 1, rxx = crinc - txsqr, rdist = cdist - rxx,
         rdp = cdp - rstride, rrgbp = crgbp - rstride, first = 1;
         r >= 0;
         r--, rdp -= rstride, rrgbp -= rstride,
         rxx -= txsqr, rdist -= rxx,
         first = 0)
    {
        if (greenloop(first))
            detect = 1;
        else if (detect)
            break;
    }

    return detect;
}

/*
 * seg3:15AA-1880  greenloop  (NEAR CDECL)
 *
 * The "gc" variables keep the values for the bcenter position, even
 * though blueloop changes gdist, gdp and grgbp.
 */
static int NEAR greenloop(int restart)
{
    int                 detect;
    int                 g;
    int                 first;
    long                txsqr = xsqr + xsqr;
    static int          here, min, max;         /* DS:0A48, 0A4A, 0A4C */
    static int          prevmax, prevmin;       /* DS:0A4E, 0A50 */
    static long         ginc, gxx, gcdist;      /* DS:0A52, 0A56, 0A5A */
    static DWORD _huge *gcdp;                   /* DS:0A5E */
    static BYTE FAR    *gcrgbp;                 /* DS:0A62 */
    int                 thismax, thismin;

    if (restart) {
        here    = gcenter;
        min     = 0;
        max     = (int)colormax - 1;
        ginc    = cginc;
        prevmax = 0;
        prevmin = (int)colormax;
    }

    thismin = min;
    thismax = max;
    detect  = 0;

    /* basic loop up */
    for (g = here, gcdist = gdist = rdist, gxx = ginc,
         gcdp = gdp = rdp, gcrgbp = grgbp = rrgbp, first = 1;
         g <= max;
         g++, gdp += gstride, gcdp += gstride, grgbp += gstride, gcrgbp += gstride,
         gdist += gxx, gcdist += gxx, gxx += txsqr, first = 0)
    {
        if (blueloop(first)) {
            if (!detect) {
                /* remember here and associated data */
                if (g > here) {
                    here    = g;
                    rdp     = gcdp;
                    rrgbp   = gcrgbp;
                    rdist   = gcdist;
                    ginc    = gxx;
                    thismin = here;
                }
                detect = 1;
            }
        }
        else if (detect) {
            thismax = g - 1;
            break;
        }
    }

    /* basic loop down */
    for (g = here - 1, gxx = ginc - txsqr, gcdist = gdist = rdist - gxx,
         gcdp = gdp = rdp - gstride, gcrgbp = grgbp = rrgbp - gstride,
         first = 1;
         g >= min;
         g--, gdp -= gstride, gcdp -= gstride, grgbp -= gstride, gcrgbp -= gstride,
         gxx -= txsqr, gdist -= gxx, gcdist -= gxx, first = 0)
    {
        if (blueloop(first)) {
            if (!detect) {
                /* remember here */
                here    = g;
                rdp     = gcdp;
                rrgbp   = gcrgbp;
                rdist   = gcdist;
                ginc    = gxx;
                thismax = here;
                detect  = 1;
            }
        }
        else if (detect) {
            thismin = g + 1;
            break;
        }
    }

    /* track only edges that are shrinking (min rising, max falling) */
    if (detect) {
        if (thismax < prevmax)
            max = thismax;
        prevmax = thismax;

        if (thismin > prevmin)
            min = thismin;
        prevmin = thismin;
    }

    return detect;
}

/*
 * seg3:1882-1B23  blueloop  (NEAR CDECL)
 */
static int NEAR blueloop(int restart)
{
    int                 detect;
    DWORD _huge        *dp;
    BYTE FAR           *rgbp;
    DWORD               bdist;
    long                bxx;
    int                 b, i = cindex;
    long                txsqr = xsqr + xsqr;
    int                 lim;
    static int          here, min, max;         /* DS:0A66, 0A68, 0A6A */
    static int          prevmin, prevmax;       /* DS:0A6C, 0A6E */
    static long         binc;                   /* DS:0A70 */
    int                 thismin, thismax;

    if (restart) {
        here    = bcenter;
        min     = 0;
        max     = (int)colormax - 1;
        binc    = cbinc;
        prevmin = (int)colormax;
        prevmax = 0;
    }

    detect  = 0;
    thismin = min;
    thismax = max;

    /* basic loop up; the first loop only finds the first closer cell */
    for (b = here, bdist = gdist, bxx = binc, dp = gdp, rgbp = grgbp, lim = max;
         b <= lim;
         b++, dp++, rgbp++, bdist += bxx, bxx += txsqr)
    {
        if (*dp > bdist) {
            /* remember new 'here' and associated data */
            if (b > here) {
                here    = b;
                gdp     = dp;
                grgbp   = rgbp;
                gdist   = bdist;
                binc    = bxx;
                thismin = here;
            }
            detect = 1;
            break;
        }
    }

    /* the second loop fills in a run of closer cells */
    for (; b <= lim; b++, dp++, rgbp++, bdist += bxx, bxx += txsqr) {
        if (*dp > bdist) {
            *dp   = bdist;
            *rgbp = (BYTE)i;
        }
        else {
            thismax = b - 1;
            break;
        }
    }

    /* basic loop down; initialise here since the find loop may not run */
    lim   = min;
    b     = here - 1;
    bxx   = binc - txsqr;
    bdist = gdist - bxx;
    dp    = gdp - 1;
    rgbp  = grgbp - 1;

    /* the find loop runs only if nothing was found going up */
    if (!detect) {
        for (; b >= lim; b--, dp--, rgbp--, bxx -= txsqr, bdist -= bxx) {
            if (*dp > bdist) {
                /* remember here; b < here by definition */
                here    = b;
                gdp     = dp;
                grgbp   = rgbp;
                gdist   = bdist;
                binc    = bxx;
                thismax = here;
                detect  = 1;
                break;
            }
        }
    }

    /* the update loop */
    for (; b >= lim; b--, dp--, rgbp--, bxx -= txsqr, bdist -= bxx) {
        if (*dp > bdist) {
            *dp   = bdist;
            *rgbp = (BYTE)i;
        }
        else {
            thismin = b + 1;
            break;
        }
    }

    /* track only edges that are shrinking (min rising, max falling) */
    if (detect) {
        if (thismax < prevmax)
            max = thismax;
        if (thismin > prevmin)
            min = thismin;
        prevmax = thismax;
        prevmin = thismin;
    }

    return detect;
}

/*
 * seg3:1B24-1B71  maxfill  (NEAR CDECL)
 *
 * As in Thomas' code, 'side' is unused; the global colormax is cubed.
 */
static void NEAR maxfill(DWORD _huge *buffer, long side)
{
    DWORD           maxv = ~0UL;
    long            i;
    DWORD _huge    *bp;

    for (i = colormax * colormax * colormax, bp = buffer; i > 0; i--, bp++)
        *bp = maxv;
}

/*
 * seg3:1B72-1BB3  QualityToThreshold  (FAR CDECL)
 *
 * badness 0..10000 -> 32768 * (badness / 10000)^4, truncated.  The two
 * doubles are register variables on the 8087 stack (hence the two
 * FLDZ at entry).  _ftol is seg3:3620.
 *
 * Out-of-range results depend on MSC's _ftol, which truncates with a
 * 64-bit FISTP and returns the low DWORD: for badness above 160000 the
 * product exceeds 2^31 and the low 32 bits of the 64-bit integer come
 * back; once it exceeds 2^63 (e.g. dwBadness = 0xFFFFFFFF, which
 * Compress() passes when MulDiv overflows or dwQuality > 10000) the
 * result is 0.  Other compilers typically give 0x80000000 instead.
 */
LONG FAR CDECL QualityToThreshold(DWORD dwBadness)
{
    double d, d2;

    d  = (double)dwBadness;
    d2 = d * d;
    return (LONG)(d2 * d2 * dThresholdScale);
}

/*
 * seg3:1BB4-1CF6  CompressSetup  (FAR CDECL, called from CompressBegin)
 *
 * Captures the input palette (8 bpp input) and, for 8-bit output,
 * rebuilds the inverse table whenever the output palette changes.
 * Note that it sets the caller's biClrUsed to 256 when it was 0.
 */
LONG FAR CDECL CompressSetup(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut)
{
    int i;
    int cb;

    if (g_ab5to8[0]) {
        for (i = 0; i < 32; i++)
            g_ab5to8[i] = (BYTE)(i * 255 / 31);
    }

    if (lpbiIn->biBitCount == 8) {
        if (lpbiIn->biClrUsed == 0)
            lpbiIn->biClrUsed = 256;
        /* compiled as one REP MOVSW of biClrUsed*2 words */
        if ((int)lpbiIn->biClrUsed > 0)
            _fmemcpy(g_rgbqIn, (LPRGBQUAD)(lpbiIn + 1), (int)lpbiIn->biClrUsed * sizeof(RGBQUAD));
    }

    if (lpbiOut->biBitCount != 8)
        return ICERR_OK;

    if (lpbiOut->biClrUsed == 0)
        lpbiOut->biClrUsed = 256;

    cb = (int)lpbiOut->biClrUsed * sizeof(RGBQUAD);

    if (_fmemcmp(g_rgbqOut, (LPRGBQUAD)(lpbiOut + 1), cb) != 0) {
        for (i = 0; i < (int)lpbiOut->biClrUsed; i++)
            g_rgbqOut[i] = ((LPRGBQUAD)(lpbiOut + 1))[i];

        if (g_lpInvPal != NULL) {
            GlobalUnlock(GlobalHandle(SELECTOROF(g_lpInvPal)));
            GlobalFree(GlobalHandle(SELECTOROF(g_lpInvPal)));
        }
        g_lpInvPal = NULL;
    }

    if (g_lpInvPal == NULL)
        g_lpInvPal = BuildInverseTable(g_rgbqOut, (int)lpbiOut->biClrUsed);

    if (g_lpInvPal == NULL)
        return ICERR_MEMORY;

    return ICERR_OK;
}

/*
 * seg3:1CF8-1CFB  CompressCleanup  (FAR, called from CompressEnd)
 *
 * The inverse table is deliberately kept until DRV_FREE.
 */
LONG FAR CompressCleanup(void)
{
    return ICERR_OK;
}

/*
 * seg3:1CFC-1D2B  FreeCompressTables  (FAR, called from DRV_FREE)
 */
void FAR FreeCompressTables(void)
{
    if (g_lpInvPal != NULL) {
        GlobalUnlock(GlobalHandle(SELECTOROF(g_lpInvPal)));
        GlobalFree(GlobalHandle(SELECTOROF(g_lpInvPal)));
    }
    g_lpInvPal = NULL;
}
