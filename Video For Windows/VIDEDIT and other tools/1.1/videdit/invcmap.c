/*
 * invcmap.c - code segment 16 of VIDEDIT.EXE, s16:1BE0-269B: the inverse
 * colour map
 *
 * BuildInverseMap fills a 32K table that maps every RGB555 colour to the
 * nearest entry of a palette.  It uses Spencer W. Thomas' incremental
 * algorithm (Graphics Gems II, "Efficient Inverse Color Map Computation",
 * the version with min/max edge tracking): for each palette colour the
 * cells of the 32x32x32 cube are visited outwards from the colour's own
 * cell, keeping a distance buffer, and each scan line stops as soon as
 * the colour is no longer the closest one.
 *
 * The distance buffer (128K, DWORD huge) is walked with huge pointers;
 * the map itself (32K) with far pointers.  BuildInverseMap is FAR PASCAL;
 * the others are FAR _cdecl.  The module has no initialised data.
 */

#include "videdit.h"
#include "dibmap.h"

static void inv_cmap(int colors, BYTE (*colormap)[256], int bits, DWORD huge *dist_buf,
                     BYTE FAR *rgbmap);
static int  redloop(void);
static int  greenloop(int restart);
static int  blueloop(int restart);
static void maxfill(DWORD huge *buffer, long side);

static struct {                                 /* DS:25E6 */
    WORD            palVersion;
    WORD            palNumEntries;
    PALETTEENTRY    palPalEntry[256];
} palMap;

static BYTE         abColormap[3][256];         /* DS:2D5C red, green, blue */

/* state shared by the loops */
static int          bcenter;                    /* DS:29EA */
static int          gcenter;                    /* DS:29EC */
static int          rcenter;                    /* DS:29EE */
static long         gdist;                      /* DS:29F0 */
static long         rdist;                      /* DS:29F4 */
static long         cdist;                      /* DS:29F8 */
static long         cbinc;                      /* DS:29FC */
static long         cginc;                      /* DS:2A00 */
static long         crinc;                      /* DS:2A04 */
static DWORD huge  *gdp;                        /* DS:2A08 */
static DWORD huge  *rdp;                        /* DS:2A0C */
static DWORD huge  *cdp;                        /* DS:2A10 */
static BYTE FAR    *grgbp;                      /* DS:2A14 */
static BYTE FAR    *rrgbp;                      /* DS:2A18 */
static BYTE FAR    *crgbp;                      /* DS:2A1C */
static int          gstride;                    /* DS:2A20 */
static int          rstride;                    /* DS:2A22 */
static long         x;                          /* DS:2A24 width of a cell in 8 bit units */
static long         xsqr;                       /* DS:2A28 */
static long         colormax;                   /* DS:2A2C cells per axis */
static int          cindex;                     /* DS:2A30 palette index being entered */

/*
 * s16:1BE0-1C82  BuildInverseMap  (FAR PASCAL)
 *
 * lpMap receives the index of the nearest colour of hpal for every
 * RGB555 colour.  The distance buffer is freed by selector, unlocked.
 */
BOOL FAR PASCAL BuildInverseMap(HPALETTE hpal, LPBYTE lpMap)
{
    DWORD huge *lpDist;
    int         i;

    lpDist = (DWORD huge *)GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 0x20000L));
    if (lpDist == NULL)
        return FALSE;

    palMap.palVersion = 0x300;
    GetObject(hpal, sizeof(int), (LPVOID)&palMap.palNumEntries);
    GetPaletteEntries(hpal, 0, palMap.palNumEntries, palMap.palPalEntry);

    for (i = 0; i < (int)palMap.palNumEntries; i++) {
        abColormap[0][i] = palMap.palPalEntry[i].peRed;
        abColormap[1][i] = palMap.palPalEntry[i].peGreen;
        abColormap[2][i] = palMap.palPalEntry[i].peBlue;
    }

    inv_cmap(palMap.palNumEntries, abColormap, 5, lpDist, lpMap);

    GlobalFree((HGLOBAL)SELECTOROF(lpDist));
    return TRUE;
}

/*
 * s16:1C84-1F56  inv_cmap
 *
 * The distances of a palette colour from the centre of its cell are kept
 * in rdist and gdist while cdist is computed (both are reset by the loops
 * before they are used as running distances).
 */
static void inv_cmap(int colors, BYTE (*colormap)[256], int bits, DWORD huge *dist_buf,
                     BYTE FAR *rgbmap)
{
    int     rcolor;
    int     gcolor;
    int     bcolor;
    long    bxx;
    int     nbits = 8 - bits;

    colormax = 1 << bits;
    x = 1 << nbits;
    xsqr = 1 << (2 * nbits);

    gstride = (int)colormax;
    rstride = gstride * gstride;

    maxfill(dist_buf, colormax);

    for (cindex = 0; cindex < colors; cindex++) {
        /* start in the cell that holds the palette colour */
        rcolor = colormap[0][cindex];
        rcenter = rcolor >> nbits;
        gcolor = colormap[1][cindex];
        gcenter = gcolor >> nbits;
        bcolor = colormap[2][cindex];
        bcenter = bcolor >> nbits;

        /* squared distance to the centre of that cell */
        rdist = rcolor - (rcenter * x + x / 2);
        gdist = gcolor - (gcenter * x + x / 2);
        bxx = bcolor - (bcenter * x + x / 2);
        cdist = rdist * rdist + gdist * gdist + bxx * bxx;

        /* increments for one cell step, kept as 2 * x * (distance + x) */
        crinc = 2 * ((rcenter + 1) * xsqr - (rcolor * x));
        cginc = 2 * ((gcenter + 1) * xsqr - (gcolor * x));
        cbinc = 2 * ((bcenter + 1) * xsqr - (bcolor * x));

        cdp = dist_buf + rcenter * rstride + gcenter * gstride + bcenter;
        crgbp = rgbmap + rcenter * rstride + gcenter * gstride + bcenter;

        redloop();
    }
}

/*
 * s16:1F58-20D6  redloop
 *
 * Steps up and then down the red axis from the centre cell until green
 * loops stop finding cells.  The loop bound is compared as a 16 bit int.
 */
static int redloop(void)
{
    int         detect;
    int         r;
    int         first;
    long        txsqr = xsqr + xsqr;
    static long rxx;                            /* DS:2A32 */

    detect = 0;

    for (r = rcenter, rdist = cdist, rxx = crinc, rdp = cdp, rrgbp = crgbp, first = 1;
         r < (int)colormax;
         r++, rdp += rstride, rrgbp += rstride, rdist += rxx, rxx += txsqr, first = 0) {
        if (greenloop(first))
            detect = 1;
        else if (detect)
            break;
    }

    for (r = rcenter - 1, rxx = crinc - txsqr, rdist = cdist - rxx,
         rdp = cdp - rstride, rrgbp = crgbp - rstride, first = 1;
         r >= 0;
         r--, rdp -= rstride, rrgbp -= rstride, rxx -= txsqr, rdist -= rxx, first = 0) {
        if (greenloop(first))
            detect = 1;
        else if (detect)
            break;
    }

    return detect;
}

/*
 * s16:20D8-23B2  greenloop
 *
 * Same along green for the current red plane.  The gc... copies keep the
 * values for the blue centre position, since blueloop moves gdist, gdp
 * and grgbp.  Where the colour was found is remembered across red planes
 * (here, ginc), and the min/max range shrinks with the run of found
 * cells.
 */
static int greenloop(int restart)
{
    int                 detect;
    int                 g;
    int                 first;
    int                 thismin;
    int                 thismax;
    long                txsqr = xsqr + xsqr;
    static int          here;                   /* DS:2A36 */
    static int          gmin;                   /* DS:2A38 */
    static int          gmax;                   /* DS:2A3A */
    static int          prevmax;                /* DS:2A3C */
    static int          prevmin;                /* DS:2A3E */
    static long         ginc;                   /* DS:2A40 */
    static long         gxx;                    /* DS:2A44 */
    static long         gcdist;                 /* DS:2A48 */
    static DWORD huge  *gcdp;                   /* DS:2A4C */
    static BYTE FAR    *gcrgbp;                 /* DS:2A50 */

    if (restart) {
        here = gcenter;
        gmax = (int)colormax - 1;
        ginc = cginc;
        gmin = 0;
        prevmax = 0;
        prevmin = (int)colormax;
    }

    thismin = gmin;
    thismax = gmax;
    detect = 0;

    for (g = here, gcdist = gdist = rdist, gxx = ginc,
         gcdp = gdp = rdp, gcrgbp = grgbp = rrgbp, first = 1;
         g <= gmax;
         g++, gdp += gstride, gcdp += gstride, grgbp += gstride, gcrgbp += gstride,
         gdist += gxx, gcdist += gxx, gxx += txsqr, first = 0) {
        if (blueloop(first)) {
            if (!detect) {
                if (g > here) {
                    here = g;
                    rdp = gcdp;
                    rrgbp = gcrgbp;
                    rdist = gcdist;
                    ginc = gxx;
                    thismin = here;
                }
                detect = 1;
            }
        } else if (detect) {
            thismax = g - 1;
            break;
        }
    }

    for (g = here - 1, gxx = ginc - txsqr, gcdist = gdist = rdist - gxx,
         gcdp = gdp = rdp - gstride, gcrgbp = grgbp = rrgbp - gstride, first = 1;
         g >= gmin;
         g--, gdp -= gstride, gcdp -= gstride, grgbp -= gstride, gcrgbp -= gstride,
         gxx -= txsqr, gdist -= gxx, gcdist -= gxx, first = 0) {
        if (blueloop(first)) {
            if (!detect) {
                here = g;
                rdp = gcdp;
                rrgbp = gcrgbp;
                rdist = gcdist;
                ginc = gxx;
                thismax = here;
                detect = 1;
            }
        } else if (detect) {
            thismin = g + 1;
            break;
        }
    }

    /* only shrinking edges are tracked */
    if (detect) {
        if (thismax < prevmax)
            gmax = thismax;
        prevmax = thismax;
        if (thismin > prevmin)
            gmin = thismin;
        prevmin = thismin;
    }

    return detect;
}

/*
 * s16:23B4-264D  blueloop
 *
 * Along blue for the current green line: the first loop finds the first
 * cell where palette colour cindex is closer than the buffer says, the
 * second enters the run of such cells; then the same downwards.
 */
static int blueloop(int restart)
{
    int                 detect;
    DWORD huge         *dp;
    BYTE FAR           *rgbp;
    long                bdist;
    long                bxx;
    int                 b;
    int                 i = cindex;
    long                txsqr = xsqr + xsqr;
    int                 lim;
    int                 thismin;
    int                 thismax;
    static int          here;                   /* DS:2A54 */
    static int          bmin;                   /* DS:2A56 */
    static int          bmax;                   /* DS:2A58 */
    static int          prevmin;                /* DS:2A5A */
    static int          prevmax;                /* DS:2A5C */
    static long         binc;                   /* DS:2A5E */

    if (restart) {
        here = bcenter;
        bmax = (int)colormax - 1;
        binc = cbinc;
        prevmin = (int)colormax;
        bmin = 0;
        prevmax = 0;
    }

    detect = 0;
    thismin = bmin;
    thismax = bmax;

    /* up: find the first closer cell */
    for (b = here, bdist = gdist, bxx = binc, dp = gdp, rgbp = grgbp, lim = bmax;
         b <= lim;
         b++, dp++, rgbp++, bdist += bxx, bxx += txsqr) {
        if (*dp > bdist) {
            if (b > here) {
                here = b;
                gdp = dp;
                grgbp = rgbp;
                gdist = bdist;
                binc = bxx;
                thismin = here;
            }
            detect = 1;
            break;
        }
    }

    /* up: fill the run of closer cells */
    for (; b <= lim; b++, dp++, rgbp++, bdist += bxx, bxx += txsqr) {
        if (*dp > bdist) {
            *dp = bdist;
            *rgbp = (BYTE)i;
        } else {
            thismax = b - 1;
            break;
        }
    }

    /* down */
    lim = bmin;
    b = here - 1;
    bxx = binc - txsqr;
    bdist = gdist - bxx;
    dp = gdp - 1;
    rgbp = grgbp - 1;

    if (!detect) {
        for (; b >= lim; b--, dp--, rgbp--, bxx -= txsqr, bdist -= bxx) {
            if (*dp > bdist) {
                here = b;
                gdp = dp;
                grgbp = rgbp;
                gdist = bdist;
                binc = bxx;
                thismax = here;
                detect = 1;
                break;
            }
        }
    }

    for (; b >= lim; b--, dp--, rgbp--, bxx -= txsqr, bdist -= bxx) {
        if (*dp > bdist) {
            *dp = bdist;
            *rgbp = (BYTE)i;
        } else {
            thismin = b + 1;
            break;
        }
    }

    if (detect) {
        if (thismax < prevmax)
            bmax = thismax;
        if (thismin > prevmin)
            bmin = thismin;
        prevmax = thismax;
        prevmin = thismin;
    }

    return detect;
}

/*
 * s16:264E-269B  maxfill
 *
 * Sets the colormax^3 distances to the maximum.  side is not used: the
 * count comes from colormax (the caller passes the same value).
 */
static void maxfill(DWORD huge *buffer, long side)
{
    long        i;
    DWORD huge *bp;

    for (i = colormax * colormax * colormax, bp = buffer; i > 0; i--, bp++)
        *bp = 0xFFFFFFFFL;
}
