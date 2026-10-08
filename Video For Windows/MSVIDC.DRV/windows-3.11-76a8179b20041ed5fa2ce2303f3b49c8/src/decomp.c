/*
 * decomp.c - MSVIDC.DRV segment 3, offsets 06B8-1095: Video 1 (CRAM)
 * decompressors written in C that address the DIB through huge pointers.
 *
 *   06B8  DibXYPtr             near cdecl helper
 *   0756  DecodeBlock16To24    near cdecl, one CRAM16 block -> RGB24
 *   0A88  Decompress16To24Huge FAR PASCAL, NOT REFERENCED anywhere
 *   0B8A  Decompress8To8Huge   FAR PASCAL, chosen by seg1:0406 on a 286
 *                              for CRAM8 -> 8 bpp images over 64K
 *   0D08  DecodeBlock8To8      near cdecl, one CRAM8 block -> 8 bpp
 *
 * Compiled with 386 code generation (o32 operands, movsx, imul ecx), so
 * this "286" fallback raises an invalid-opcode fault on a real 80286.
 *
 * Known defects reproduced here:
 *
 *  1. SS != DS.  The block decoders return a pending skip count through
 *     a NEAR int pointer.  The callers pass &skip, the address of a stack
 *     local, but in a DLL DS is the driver's DGROUP and SS is the
 *     caller's stack.  The decoder therefore writes the count into
 *     DGROUP at that offset, corrupting whatever lives there, while the
 *     caller's own copy on the stack stays 0.  As a result skip codes in
 *     these paths advance by one block only.
 *
 *  2. MAKE4.  The 8 bpp decoder builds 4-pixel DWORDs with a macro that
 *     shifts plain 16-bit ints.  Only the two low bytes survive, and the
 *     value is then sign-extended to 32 bits.  Pixels 2 and 3 of every
 *     row therefore come out 0x00 or 0xFF instead of the encoded colours.
 *
 *  3. If the counts were delivered, the skip arithmetic in these paths
 *     would treat a skip code as occupying its own block plus N more
 *     (N+1 total).  The assembly decoders in dec286.c skip N.
 */

#include <windows.h>
#include "msvidc.h"

static BYTE _huge * NEAR DibXYPtr(LPBITMAPINFOHEADER lpbi, LPBYTE lp,
                                  LONG x, LONG y, int FAR *pStride);
static WORD _huge * NEAR DecodeBlock16To24(BYTE _huge *dst, WORD _huge *src,
                                           int stride, int NEAR *pSkip);
static LPBYTE NEAR DecodeBlock8To8(BYTE _huge *dst, LPBYTE src, int stride,
                                   int NEAR *pSkip);


/*
 * DS:07EA  Flag bit for each pixel of an 8-colour CRAM16 block, in the
 * order DecodeBlock16To24 visits them: quadrant by quadrant (bottom-left,
 * bottom-right, top-left, top-right), 2x2 pixels inside each.
 */
static WORD awQuadBit[16] = {
    0x0001, 0x0002, 0x0010, 0x0020,
    0x0004, 0x0008, 0x0040, 0x0080,
    0x0100, 0x0200, 0x1000, 0x2000,
    0x0400, 0x0800, 0x4000, 0x8000
};

/* RGB555 word -> RGBTRIPLE, low 3 bits of each component left zero */
#define RGB555TO24(rgb, w)                              \
    ((rgb).rgbtRed   = (BYTE)(((w) >> 7) & 0xF8),       \
     (rgb).rgbtGreen = (BYTE)(((w) >> 2) & 0xF8),       \
     (rgb).rgbtBlue  = (BYTE)((w) << 3))

/*
 * Four pixel bytes -> DWORD, as the binary computes it (defect 2 above).
 * The intended value is a | b<<8 | c<<16 | d<<24.  The original was
 * presumably ((((d << 8 | c) << 8) | b) << 8 | a) in 16-bit int
 * arithmetic, so c and d are lost and the result is sign-extended.  The
 * explicit casts keep that behaviour under any compiler.
 */
#define MAKE4(a, b, c, d) \
    ((DWORD)(LONG)(short)(((WORD)(BYTE)(b) << 8) | (BYTE)(a)))

/* Byte mask for one row of a 2/8-colour block: flag bit i selects byte i */
#define ROWMASK(f)                                  \
    ((((f) & 8) ? 0xFF000000L : 0L) |               \
     (((f) & 1) ? 0x000000FFL : 0L) |               \
     (((f) & 2) ? 0x0000FF00L : 0L) |               \
     (((f) & 4) ? 0x00FF0000L : 0L))


/*
 * seg3:06B8-0755  DibXYPtr
 * near cdecl; args [bp+4] lpbi, [bp+8] lp, [bp+0C] x, [bp+10] y,
 * [bp+14] pStride; returns DX:AX.
 *
 * Returns a pointer to pixel (x, y) of a DIB, with y counted from the
 * bottom scan line, and stores the signed scan-line step in *pStride.
 * For a top-down DIB (biHeight < 0) it starts from the last scan line in
 * memory and the step is negative.  The x offset is a far (offset-only)
 * add; the other adjustments use huge arithmetic (__AHSHIFT).
 */
static BYTE _huge * NEAR DibXYPtr(LPBITMAPINFOHEADER lpbi, LPBYTE lp,
                                  LONG x, LONG y, int FAR *pStride)
{
    int stride;

    if (x > 0)
        lp += ((int)lpbi->biBitCount * (int)x) >> 3;

    stride = ((((int)lpbi->biWidth * (int)lpbi->biBitCount) >> 3) + 3) & ~3;

    if (lpbi->biHeight < 0) {
        stride = -stride;
        lp = (LPBYTE)((BYTE _huge *)lp + (lpbi->biSizeImage + stride));
    }

    if (y > 0)
        lp = (LPBYTE)((BYTE _huge *)lp + (LONG)stride * y);

    if (pStride != NULL)
        *pStride = stride;

    return (BYTE _huge *)lp;
}


/*
 * seg3:0756-0A86  DecodeBlock16To24
 * near cdecl; args [bp+4] dst, [bp+8] src, [bp+0C] stride, [bp+0E] pSkip
 * (near, DS-relative: see defect 1); returns the advanced src in DX:AX.
 *
 * Decodes one 4x4 CRAM16 block into 24-bit RGB at dst (bottom-left pixel).
 * While *pSkip > 0 it consumes a block position without reading any
 * input.  A skip code stores N in *pSkip and also consumes the current
 * position.
 */
static WORD _huge * NEAR DecodeBlock16To24(BYTE _huge *dst, WORD _huge *src,
                                           int stride, int NEAR *pSkip)
{
    WORD        flags;          /* [bp-22] in the 2/8-colour path */
    WORD        c;
    WORD        bit;            /* [bp-1E] */
    WORD NEAR  *pBit;           /* [bp-1C] */
    RGBTRIPLE   rgbA;           /* [bp-16] */
    RGBTRIPLE   rgbB;           /* [bp-1A] */
    BYTE _huge *blk;            /* [bp-0E] */
    BYTE _huge *quad;           /* [bp-0A] */
    BYTE _huge *row;            /* [bp-06] */
    int         qy, qx, i, j;

    if (*pSkip > 0) {
        (*pSkip)--;
        return src;
    }

    flags = *src++;

    if (flags & 0x8000) {
        if ((flags & 0xFC00) == 0x8400) {
            *pSkip = flags & 0x3FF;
            return src;
        }

        /* solid; bit 15 is dropped by the component masks */
        RGB555TO24(rgbA, flags);
        row = dst;
        for (i = 4; i; i--) {
            for (j = 0; j < 4; j++)
                *(RGBTRIPLE _huge *)(row + j * 3) = rgbA;
            row += stride;
        }
        return src;
    }

    bit  = 1;
    pBit = awQuadBit;

    if (((BYTE _huge *)src)[1] & 0x80) {
        /* colour A has bit 15 set: 8 colours, one pair per quadrant */
        blk = dst;
        for (qy = 2; qy; qy--) {
            quad = blk;
            for (qx = 2; qx; qx--) {
                c = *src++;  RGB555TO24(rgbA, c);
                c = *src++;  RGB555TO24(rgbB, c);
                row = quad;
                for (i = 2; i; i--) {
                    for (j = 0; j < 2; j++) {
                        if (flags & *pBit++)
                            *(RGBTRIPLE _huge *)(row + j * 3) = rgbA;
                        else
                            *(RGBTRIPLE _huge *)(row + j * 3) = rgbB;
                        bit <<= 1;      /* shifted but unused in this mode */
                    }
                    row += stride;
                }
                quad += 6;
            }
            blk += stride * 2;
        }
    }
    else {
        /* 2 colours */
        c = *src++;  RGB555TO24(rgbA, c);
        c = *src++;  RGB555TO24(rgbB, c);
        row = dst;
        for (i = 4; i; i--) {
            for (j = 0; j < 4; j++) {
                if (flags & bit)
                    *(RGBTRIPLE _huge *)(row + j * 3) = rgbA;
                else
                    *(RGBTRIPLE _huge *)(row + j * 3) = rgbB;
                bit <<= 1;
            }
            row += stride;
        }
    }

    return src;
}


/*
 * seg3:0A88-0B88  Decompress16To24Huge
 * FAR PASCAL, retf 18h; [bp+1A] lpbiIn, [bp+16] pIn, [bp+12] lpbiOut,
 * [bp+0E] pOut, [bp+0A] x, [bp+06] y.
 *
 * CRAM16 -> 24 bpp.  No code, relocation or data table refers to this
 * function: slot 10 (CRAM16 -> 24 bpp, 1:1) is zero in both the 386
 * table (DS:0770) and the 286 table (DS:07B0).  It is dead code linked in
 * with the rest of this module.  The x and y parameters are reused as the
 * loop counters.  It returns stride * yBlocks * 4, computed in 16 bits and
 * sign-extended.
 */
DWORD FAR PASCAL Decompress16To24Huge(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                      LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                      LONG x, LONG y)
{
    WORD _huge *src;        /* [bp-12] */
    int         skip;       /* [bp-20] */
    int         stride;     /* [bp-1E] */
    int         xBlocks;    /* di */
    int         yBlocks;    /* si */
    BYTE _huge *dst;        /* [bp-0E] */
    BYTE _huge *p;          /* [bp-04] */

    src     = (WORD _huge *)pIn;
    skip    = 0;
    xBlocks = (WORD)lpbiIn->biWidth >> 2;
    yBlocks = (WORD)lpbiIn->biHeight >> 2;

    dst = DibXYPtr(lpbiOut, (LPBYTE)pOut, x, y, &stride);

    for (y = 0; y < yBlocks; y++) {
        p = dst;
        for (x = 0; x < xBlocks; x++) {
            src = DecodeBlock16To24(p, src, stride, &skip);  /* defect 1 */
            p += 12;
        }
        dst += stride * 4;
    }

    return (LONG)(stride * yBlocks * 4);
}


/*
 * seg3:0B8A-0D06  Decompress8To8Huge
 * FAR PASCAL, retf 18h (frame as above).  seg1:0406 selects it on a 286
 * when the 286 table slot holds seg4:0000 and the image is over 64K.
 *
 * CRAM8 -> 8 bpp through DibXYPtr, so unlike seg4:0000 it honours x/y and
 * top-down output.  The x and y parameters are reused as 32-bit
 * down-counters.  Because of defect 1, skip is never non-zero when it is
 * tested, so the skip handling below never runs in practice.  It always
 * returns 0 (ICERR_OK).
 */
DWORD FAR PASCAL Decompress8To8Huge(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                    LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                    LONG x, LONG y)
{
    LPBYTE      src;        /* [bp-0C] */
    int         skip;       /* [bp-10] */
    int         stride;     /* [bp-12] */
    int         xBlocks;    /* si */
    int         yBlocks;    /* di */
    int         n;
    BYTE _huge *dst;        /* [bp-08] start of the current block row */
    BYTE _huge *p;          /* [bp-04] current block */

    src     = (LPBYTE)pIn;
    skip    = 0;
    xBlocks = (WORD)lpbiIn->biWidth >> 2;
    yBlocks = (WORD)lpbiIn->biHeight >> 2;

    dst = DibXYPtr(lpbiOut, (LPBYTE)pOut, x, y, &stride);

    y = yBlocks;
    while (y-- != 0) {
        p = dst;
        x = xBlocks;
        while (x-- != 0) {
            src = DecodeBlock8To8(p, src, stride, &skip);   /* defect 1 */

            if (skip != 0) {
                x -= skip;
                if (x < 0) {
                    /* the skip runs past the end of this block row */
                    skip = -(int)x;
                    n = (skip - 1) / xBlocks + 1;
                    y   -= n;
                    dst += n * stride * 4;
                    x = skip % xBlocks;
                    if (x != 0) {
                        p = dst + (x - 1) * 4;
                        x = xBlocks - x;
                    }
                }
                else if (x != 0) {
                    p += skip * 4;
                }
                skip = 0;
            }
            p += 4;
        }
        dst += stride * 4;
    }

    return 0;
}


/*
 * seg3:0D08-1095  DecodeBlock8To8
 * near cdecl; args [bp+4] dst, [bp+8] src, [bp+0C] stride, [bp+0E] pSkip
 * (near, DS-relative: see defect 1); returns the advanced src in DX:AX.
 *
 * Decodes one 4x4 CRAM8 block into 8 bpp at dst (bottom-left pixel),
 * writing one DWORD per scan line.  The flag word is fetched with a huge
 * increment, but colour bytes use far (offset-only) increments, so a block
 * that straddles a 64K boundary of the source wraps within the segment.
 * A skip code stores N in *pSkip and returns.  It does not count down
 * pending skips; Decompress8To8Huge handles that.
 */
static LPBYTE NEAR DecodeBlock8To8(BYTE _huge *dst, LPBYTE src, int stride,
                                   int NEAR *pSkip)
{
    WORD  flags;        /* si */
    BYTE  a0, b0;       /* [bp-0D], [bp-0E] (8-colour order) */
    BYTE  a1, b1;       /* [bp-10], 4th byte */
    DWORD bg;           /* [bp-08] */
    DWORD fgxbg;        /* [bp-0C] */
    int   i;

    flags = *(WORD FAR *)src;
    /* a real huge increment (__AHINCR on carry); Open Watcom folds this
     * into an offset-only add, so go through a _huge temporary there */
    src = (LPBYTE)((BYTE _huge *)src + 2);

    if (flags & 0x8000) {
        if ((flags & 0xFC00) == 0x8400) {
            *pSkip = flags & 0x3FF;
            return src;
        }

        if ((flags & 0xFC00) == 0x8000) {
            /* solid colour LOBYTE(flags) */
            bg = MAKE4((BYTE)flags, (BYTE)flags, (BYTE)flags, (BYTE)flags);
            for (i = 4; i; i--) {
                *(DWORD _huge *)dst = bg;
                dst += stride;
            }
            return src;
        }

        /* 8 colours: (A,B) for bottom-left, bottom-right; then top pair */
        a0 = *src++;  b0 = *src++;  a1 = *src++;  b1 = *src++;
        bg    = MAKE4(b0, b0, b1, b1);
        fgxbg = MAKE4(a0, a0, a1, a1) ^ bg;
        for (i = 2; i; i--) {
            *(DWORD _huge *)dst = (ROWMASK(flags) & fgxbg) ^ bg;
            dst += stride;
            flags >>= 4;
        }

        a0 = *src++;  b0 = *src++;  a1 = *src++;  b1 = *src++;
        bg    = MAKE4(b0, b0, b1, b1);
        fgxbg = MAKE4(a0, a0, a1, a1) ^ bg;
        for (i = 2; i; i--) {
            *(DWORD _huge *)dst = (ROWMASK(flags) & fgxbg) ^ bg;
            dst += stride;
            flags >>= 4;
        }
        return src;
    }

    /* 2 colours: A (first byte) where the flag bit is set, else B */
    a0 = *src++;
    b0 = *src++;
    bg    = MAKE4(b0, b0, b0, b0);
    fgxbg = MAKE4(a0, a0, a0, a0) ^ bg;
    for (i = 4; i; i--) {
        *(DWORD _huge *)dst = (ROWMASK(flags) & fgxbg) ^ bg;
        dst += stride;
        flags >>= 4;
    }
    return src;
}
