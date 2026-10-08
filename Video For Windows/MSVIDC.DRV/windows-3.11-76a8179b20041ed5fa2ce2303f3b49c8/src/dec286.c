/*
 * dec286.c - MSVIDC.DRV segment 4: 16-bit Video 1 (CRAM) decompressors.
 *
 * C reconstruction of hand-written 186+ assembly (segment 4 has no
 * relocations and no prologue/epilogue beyond push bp/mov bp,sp).  These
 * two routines are the only non-NULL entries of the 286 dispatch table at
 * DS:0788 that seg1:0406 selects when GetWinFlags() reports WF_CPU286:
 *
 *   DS:0788  slot 0  CRAM8  -> 8 bpp,  1:1   seg4:0000  Decompress8To8_286
 *   DS:07AC  slot 9  CRAM16 -> 16 bpp, 1:1   seg4:025C  Decompress16To16_286
 *
 * Both walk the destination with 16-bit offsets, so they only work for
 * images up to 64K; for larger CRAM8 images seg1:0406 substitutes the C
 * routine Decompress8To8Huge (seg3:0B8A, decomp.c).
 *
 * The asm is fully unrolled per block and builds pixels with
 * ror/sbb/and/xor mask tricks; it is written here as plain loops that
 * produce the same bytes.
 *
 * Common behaviour of both routines:
 *  - x and y are ignored: decoding always starts at pOut, and a top-down
 *    output DIB (biHeight < 0) is not handled.
 *  - The output stride comes from lpbiOut->biWidth, the block counts from
 *    lpbiIn->biWidth/biHeight (low words only, unsigned >> 2).
 *  - There is no end-of-stream code; decoding stops when the block count
 *    is exhausted.  The row and column counters are tested only after
 *    decrementing, so a source under 4 pixels wide or high wraps the
 *    16-bit counter and runs on for 65535 rows/blocks.
 *  - A skip code (flags & 0xFC00) == 0x8400 skips (flags & 0x3FF) blocks
 *    and does not occupy a block position of its own.
 *  - AX:DX are not set on return; the only caller (seg1:075C) ignores the
 *    result.
 *  - The routines load DS from the far pointers and restore it on exit.
 */

#include <windows.h>
#include "msvidc.h"


/*
 * seg4:0000-0259  Decompress8To8_286
 * FAR PASCAL, retf 18h.  Frame: [bp+1A] lpbiIn, [bp+16] pIn, [bp+12] lpbiOut,
 * [bp+0E] pOut, [bp+0A] x, [bp+06] y.
 *
 * Decodes an 8-bit CRAM frame into an 8 bpp DIB.  Block types by flag word:
 *   bit 15 clear          2 colours: one word (A = low byte, B = high byte),
 *                         flag bit set selects A
 *   0x8400-0x87FF         skip (flags & 0x3FF) blocks
 *   0x8000-0x83FF         solid block of colour LOBYTE(flags)
 *   any other bit-15 code 8 colours: four words (A,B) per quadrant in the
 *                         order bottom-left, bottom-right, top-left,
 *                         top-right.  Codes 0x88xx-0x8Fxx fall into this
 *                         case too.
 * Flag bit (y*4 + x) controls pixel x of block row y, with row 0 the
 * bottom scan line.
 */
DWORD FAR PASCAL Decompress8To8_286(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                    LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                    LONG x, LONG y)
{
    WORD FAR *src;
    BYTE FAR *dst;
    BYTE FAR *p;
    int  stride;        /* [bp-0A] output scan line, bytes */
    int  stride4;       /* [bp-0C] four scan lines */
    int  rowSkip;       /* [bp-0E] end of one block row -> start of next */
    int  xBlocks;       /* [bp-04] */
    int  yBlocks;       /* [bp-02] block rows left */
    int  n;             /* [bp-06] blocks left in this row, incl. current */
    int  skip;
    int  i, j, q;
    WORD flags, c;
    WORD aw[4];

    stride  = ((WORD)lpbiOut->biWidth + 3) & ~3;
    stride4 = stride * 4;
    rowSkip = stride4 - ((WORD)lpbiIn->biWidth & ~3);
    xBlocks = (WORD)lpbiIn->biWidth >> 2;
    yBlocks = (WORD)lpbiIn->biHeight >> 2;

    dst = (BYTE FAR *)pOut;
    src = (WORD FAR *)pIn;

    n = xBlocks;
    for (;;) {
        flags = *src++;

        if (!(flags & 0x8000)) {
            /* 2 colours */
            c = *src++;
            for (i = 0, p = dst; i < 4; i++, p += stride, flags >>= 4)
                for (j = 0; j < 4; j++)
                    p[j] = (flags & (1 << j)) ? LOBYTE(c) : HIBYTE(c);
        }
        else if ((flags & 0xFC00) == 0x8400) {
            /* skip: the code itself does not consume a block */
            skip = flags & 0x3FF;
            while (n <= skip) {                 /* signed compares */
                if (--yBlocks == 0)
                    return 0;
                dst  += stride4;                /* same column, next block row */
                skip -= xBlocks;
            }
            n   -= skip;
            dst += skip * 4;
            continue;                           /* jmp 005C: no --n */
        }
        else if ((flags & 0xFC00) == 0x8000) {
            /* solid */
            for (i = 0, p = dst; i < 4; i++, p += stride)
                for (j = 0; j < 4; j++)
                    p[j] = LOBYTE(flags);
        }
        else {
            /* 8 colours, one (A,B) word per quadrant */
            aw[0] = *src++;     /* bottom-left  */
            aw[1] = *src++;     /* bottom-right */
            aw[2] = *src++;     /* top-left     */
            aw[3] = *src++;     /* top-right    */
            for (i = 0, p = dst; i < 4; i++, p += stride)
                for (j = 0; j < 4; j++) {
                    q = (i >> 1) * 2 + (j >> 1);
                    p[j] = (flags & (1 << (i * 4 + j))) ? LOBYTE(aw[q])
                                                       : HIBYTE(aw[q]);
                }
        }

        dst += 4;
        if (--n == 0) {                         /* 0240 */
            dst += rowSkip;
            if (--yBlocks == 0)
                return 0;   /* asm returns with AX:DX undefined */
            n = xBlocks;
        }
    }
}


/*
 * seg4:025C-051A  Decompress16To16_286
 * FAR PASCAL, retf 18h (same frame as above).  The segment is 0x51B bytes
 * long, so the high byte of the retf immediate (00h at 051B) comes from
 * the zero-filled minalloc area (0x51C).
 *
 * Decodes a 16-bit CRAM frame into a 16 bpp (RGB555) DIB:
 *   bit 15 clear, colour A bit 15 clear   2 colours: words A, B
 *   bit 15 clear, colour A bit 15 set     8 colours: (A,B) word pairs for
 *                                         the four quadrants as above.  A of
 *                                         the first quadrant is stored with
 *                                         bit 15 still set.
 *   0x8400-0x87FF                         skip (flags & 0x3FF) blocks
 *   any other bit-15 code                 solid block of colour flags & 0x7FFF
 */
DWORD FAR PASCAL Decompress16To16_286(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                      LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                      LONG x, LONG y)
{
    WORD FAR *src;
    BYTE FAR *dst;
    WORD FAR *p;
    int  stride;        /* [bp-0A] output scan line, bytes */
    int  stride4;       /* [bp-0C] */
    int  rowSkip;       /* [bp-0E] */
    int  xBlocks;       /* [bp-04] */
    int  yBlocks;       /* [bp-02] */
    int  n;             /* [bp-06] */
    int  skip;
    int  i, j, q;
    WORD flags, a, b;
    WORD awA[4], awB[4];

    stride  = ((WORD)lpbiOut->biWidth * 2 + 3) & ~3;
    stride4 = stride * 4;
    rowSkip = stride4 - ((WORD)lpbiIn->biWidth & ~3) * 2;
    xBlocks = (WORD)lpbiIn->biWidth >> 2;
    yBlocks = (WORD)lpbiIn->biHeight >> 2;

    dst = (BYTE FAR *)pOut;
    src = (WORD FAR *)pIn;

    n = xBlocks;
    for (;;) {
        flags = *src++;

        if (!(flags & 0x8000)) {
            a = *src++;
            if (!(a & 0x8000)) {
                /* 2 colours */
                b = *src++;
                for (i = 0; i < 4; i++, flags >>= 4) {
                    p = (WORD FAR *)(dst + i * stride);
                    for (j = 0; j < 4; j++)
                        p[j] = (flags & (1 << j)) ? a : b;
                }
            }
            else {
                /* 8 colours */
                awA[0] = a;        awB[0] = *src++;     /* bottom-left  */
                awA[1] = *src++;   awB[1] = *src++;     /* bottom-right */
                awA[2] = *src++;   awB[2] = *src++;     /* top-left     */
                awA[3] = *src++;   awB[3] = *src++;     /* top-right    */
                for (i = 0; i < 4; i++) {
                    p = (WORD FAR *)(dst + i * stride);
                    for (j = 0; j < 4; j++) {
                        q = (i >> 1) * 2 + (j >> 1);
                        p[j] = (flags & (1 << (i * 4 + j))) ? awA[q] : awB[q];
                    }
                }
            }
        }
        else if ((flags & 0xFC00) == 0x8400) {
            skip = flags & 0x3FF;
            while (n <= skip) {
                if (--yBlocks == 0)
                    return 0;
                dst  += stride4;
                skip -= xBlocks;
            }
            n   -= skip;
            dst += skip * 8;
            continue;                           /* jmp 02BC: no --n */
        }
        else {
            /* solid */
            for (i = 0; i < 4; i++) {
                p = (WORD FAR *)(dst + i * stride);
                for (j = 0; j < 4; j++)
                    p[j] = flags & 0x7FFF;
            }
        }

        dst += 8;
        if (--n == 0) {                         /* 0500 */
            dst += rowSkip;
            if (--yBlocks == 0)
                return 0;   /* asm returns with AX:DX undefined */
            n = xBlocks;
        }
    }
}
