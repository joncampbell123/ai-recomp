/*
 * dec386.c - Microsoft Video 1 (CRAM) decompressors for 386+ CPUs.
 *
 * Reconstruction of MSVIDC.DRV segment 2 (file offset 0x2E00, 0x1E83 bytes
 * in the file, minalloc 0x1E84; the last byte, the high half of the final
 * RETF immediate, comes from the loader's zero fill).
 *
 * The original is hand-written 386 assembly in a USE32 code segment (NE
 * segment flag 0x2000).  It is reached only through the decompressor
 * dispatch table in the data segment (DS:0748, 16 far pointers indexed by
 * ((inBpp/8-1)*2 + stretch)*4 + (outBpp/8-1), selected in seg1:0406 when
 * the CPU is not a 286):
 *
 *   slot  0  DS:0748  seg2:1208  Decompress8To8_386     CRAM8  -> 8bpp
 *   slot  4  DS:0758  seg2:14D8  Decompress8To8x2_386   CRAM8  -> 8bpp, 2x
 *   slot  8  DS:0768  seg2:18FE  Decompress16To8_386    CRAM16 -> 8bpp dither
 *   slot  9  DS:076C  seg2:0024  Decompress16To16_386   CRAM16 -> 16bpp
 *   slot 13  DS:077C  seg2:0338  Decompress16To16x2_386 CRAM16 -> 16bpp, 2x
 *
 * Two more complete routines in the segment, Decompress16To32_386
 * (seg2:077A) and Decompress16To32x2_386 (seg2:0BB2), are not referenced by
 * anything: slots 11 and 15 of the table are zero.
 *
 * All routines are FAR PASCAL with six DWORD-sized arguments and return
 * with "o16 retf 18h"; the return value (EAX) is meaningless and the caller
 * (seg1:07C9) ignores it.  The frame is built as "push bp / movzx ebp,sp".
 *
 * USE32 fix-up stub
 * -----------------
 * Every entry point is preceded by the same 16-bit stub (seg2:0000-0023,
 * 0316-0337, 0756-0779, 0B90-0BB1, 11E4-1207, 14B6-14D7, 18DA-18FD):
 *
 *   stub:   push  di                    ; runs as USE16
 *           sub   sp,8
 *           mov   bx,cs
 *           mov   ax,ss
 *           mov   es,ax
 *           mov   di,sp
 *           mov   ax,000Bh              ; DPMI Get Descriptor (BX) -> ES:DI
 *           int   31h
 *           or    byte ptr es:[di+6],40h ; set D (default operand size 32)
 *           mov   ax,000Ch              ; DPMI Set Descriptor
 *           int   31h                   ; IRET reloads CS: now USE32
 *           add   sp,8                  ; 66 83 C4 08 / 66 5F: the same
 *           pop   di                    ; meaning when decoded as USE32
 *           xchg  ebx,ebx / nop         ; padding
 *   entry:  xor   eax,eax
 *           mov   ah,80h
 *           add   eax,eax               ; USE16: 8000h+8000h sets CF
 *           jc    stub                  ; USE32: EAX=10000h, CF clear
 *
 * The test runs on every call, so the code works whether or not the loader
 * honoured the NE 32-bit flag, and also after this discardable segment has
 * been reloaded.
 *
 * Addressing
 * ----------
 * Source and destination are addressed as selector:32-bit offset with no
 * __AHINCR tiling: the code relies on the first selector of a huge global
 * block having a limit that covers the whole block.  In C that is simply a
 * huge pointer.  The routines keep separate counters for "blocks left in
 * this block row" and "block rows left", both tested only after being
 * decremented, so an input narrower or shorter than 4 pixels makes them
 * run away (the original does the same).
 *
 * Bitstream (as decoded here).  Each 4x4 block starts with a 16-bit code.
 * Bit (4*row + col) of the code selects the first (1) or second (0) colour
 * for that pixel; row 0 is the lowest scanline of the block (lowest
 * address in a bottom-up DIB).  Blocks run left to right, block rows bottom
 * to top.
 *
 *   CRAM16  code < 8000h, c0 < 8000h   2 colours  (c0, c1)
 *           code < 8000h, c0 >= 8000h  8 colours, one pair per 2x2
 *                                      quadrant; bit 15 of c0 is dropped
 *           (code & FC00h) == 8400h    skip (code & 3FFh) blocks
 *           other code >= 8000h        fill with (code & 7FFFh)
 *
 *   CRAM8   code < 8000h               2 colour bytes
 *           (code & FC00h) == 8400h    skip (code & 3FFh) blocks
 *           (code & FC00h) == 8000h    fill with (code & 00FFh)
 *           other code >= 8000h        8 colour bytes
 *
 * Quadrant q = (row/2)*2 + col/2 uses colour pair q, i.e. pairs 0 and 1
 * cover the lower two scanlines.
 */

#include <windows.h>
#include "msvidc.h"

/* bit for pixel (row, col) of a block */
#define BLOCKBIT(code, r, c)    (((code) >> ((r) * 4 + (c))) & 1)

/* colour pair used by pixel (row, col) in an 8-colour block */
#define QUAD(r, c)              ((((r) >> 1) << 1) | ((c) >> 1))

/*
 * Common prologue: point dst at pixel (x, y) of the output DIB and return
 * the signed scanline stride.  For biHeight < 0 the walk starts on the last
 * scanline in memory (located with biSizeImage) and goes backwards.
 */
#define SETUP_DST(dst, lpbiOut, bpp, x, y, stride)                          \
    stride = ((lpbiOut)->biWidth * (bpp) + 3) & ~3L;                        \
    dst += (x) * (bpp);                                                     \
    if ((lpbiOut)->biHeight < 0) {                                          \
        stride = -stride;                                                   \
        dst += (lpbiOut)->biSizeImage + stride;                             \
    }                                                                       \
    dst += (y) * stride

/*
 * Skip code: advance over n blocks, possibly into later block rows, without
 * counting the code itself as a block.  "Moving up one block row" keeps the
 * column, so it consumes exactly blocksPerRow blocks; n may go negative.
 * Returns FALSE when the last block row was left (the decoder then exits).
 */
#define DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, blockBytes) \
    {                                                                       \
        while (blocksLeft <= n) {                                           \
            if (--blockRows == 0)                                           \
                return;                                                     \
            dst += blockRowBytes;                                           \
            n -= blocksPerRow;                                              \
        }                                                                   \
        blocksLeft -= n;                                                    \
        dst += n * (blockBytes);                                            \
    }

/*
 * RGB555 -> 32bpp exactly as the 386 code does it with rotates in a
 * register holding both colour words: R, G and B are shifted up 3 bits,
 * and the top byte is NOT cleared; it gets bits 5-12 of the other colour
 * word of the pair (FFh for a fill, whose other half is the sign extension).
 */
#define RGB555TO32(c, other)                                                \
    (((DWORD)(((other) >> 5) & 0xFF) << 24) |                               \
     ((DWORD)((c) & 0x7C00) << 9) |                                         \
     ((DWORD)((c) & 0x03E0) << 6) |                                         \
     ((DWORD)((c) & 0x001F) << 3))


/*
 * seg2:0024-0315 (stub 0000-0023)
 * CRAM16 -> 16bpp RGB555, 1:1.  Dispatch slot 9.
 * FAR PASCAL, o16 retf 18h.  ES:ESI = source, DS:EDI = destination.
 * Locals: [ebp-14h] stride, [-10h] 2*stride-8, [-18h] 4*stride,
 * [-1Ch] row advance, [-8] blocks/row, [-4] block rows, [-0Ch] blocks left.
 */
void FAR PASCAL Decompress16To16_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                     LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                     LONG x, LONG y)
{
    WORD _huge *src = (WORD _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code, c[8];
    int  r, col, q, i;

    SETUP_DST(dst, lpbiOut, 2, x, y, stride);

    blockRowBytes = stride * 4;
    /* QUIRK: blocks advance dst by (width/4)*8 bytes, but this subtracts
     * (width*2) & ~3, which differs when width % 4 is 2 or 3. */
    rowAdvance   = blockRowBytes - ((lpbiIn->biWidth * 2) & ~3L);
    blocksPerRow = (DWORD)lpbiIn->biWidth >> 2;
    blockRows    = (DWORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *src++;
            if (code & 0x8000) {
                if ((code & 0xFC00) == 0x8400) {
                    n = code & 0x3FF;
                    DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 8);
                    continue;
                }
                /* solid fill */
                c[0] = code & 0x7FFF;
                for (r = 0; r < 4; r++)
                    for (col = 0; col < 4; col++)
                        ((WORD _huge *)(dst + r * stride))[col] = c[0];
            }
            else {
                c[0] = src[0];
                c[1] = src[1];
                src += 2;
                if (c[0] & 0x8000) {
                    /* 8 colours; only the very first word loses bit 15 */
                    c[0] &= 0x7FFF;
                    for (i = 2; i < 8; i++)
                        c[i] = *src++;
                    for (r = 0; r < 4; r++)
                        for (col = 0; col < 4; col++) {
                            q = QUAD(r, col) * 2;
                            ((WORD _huge *)(dst + r * stride))[col] =
                                BLOCKBIT(code, r, col) ? c[q] : c[q + 1];
                        }
                }
                else {
                    for (r = 0; r < 4; r++)
                        for (col = 0; col < 4; col++)
                            ((WORD _huge *)(dst + r * stride))[col] =
                                BLOCKBIT(code, r, col) ? c[0] : c[1];
                }
            }
            dst += 8;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}


/*
 * seg2:0338-0755 (stub 0316-0337)
 * CRAM16 -> 16bpp RGB555, each source pixel written as a 2x2 square.
 * Dispatch slot 13.  FAR PASCAL, o16 retf 18h.  x, y are in output pixels.
 * Locals as above with [-10h] = 6*stride-16, [-18h] = 8*stride.
 */
void FAR PASCAL Decompress16To16x2_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                       LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                       LONG x, LONG y)
{
    WORD _huge *src = (WORD _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code, v, c[8];
    int  r, col, q, i;

    SETUP_DST(dst, lpbiOut, 2, x, y, stride);

    blockRowBytes = stride * 8;
    /* QUIRK: blocks advance (width/4)*16 bytes; this subtracts width*4. */
    rowAdvance   = blockRowBytes - lpbiIn->biWidth * 4;
    blocksPerRow = (DWORD)lpbiIn->biWidth >> 2;
    blockRows    = (DWORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *src++;
            if (code & 0x8000) {
                if ((code & 0xFC00) == 0x8400) {
                    n = code & 0x3FF;
                    DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 16);
                    continue;
                }
                c[0] = c[1] = code & 0x7FFF;
                code = 0;                       /* fill: every pixel c[1] */
                i = 0;
            }
            else {
                c[0] = src[0];
                c[1] = src[1];
                src += 2;
                i = (c[0] & 0x8000) != 0;
                if (i) {
                    c[0] &= 0x7FFF;
                    for (q = 2; q < 8; q++)
                        c[q] = *src++;
                }
            }
            for (r = 0; r < 4; r++)
                for (col = 0; col < 4; col++) {
                    q = i ? QUAD(r, col) * 2 : 0;
                    v = BLOCKBIT(code, r, col) ? c[q] : c[q + 1];
                    ((WORD _huge *)(dst + (2 * r)     * stride))[2 * col]     = v;
                    ((WORD _huge *)(dst + (2 * r)     * stride))[2 * col + 1] = v;
                    ((WORD _huge *)(dst + (2 * r + 1) * stride))[2 * col]     = v;
                    ((WORD _huge *)(dst + (2 * r + 1) * stride))[2 * col + 1] = v;
                }
            dst += 16;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}


/*
 * seg2:077A-0B8F (stub 0756-0779)
 * CRAM16 -> 32bpp, 1:1.  NOT REFERENCED: dispatch slot 11 (DS:0774) is 0.
 * FAR PASCAL, o16 retf 18h.  [-10h] = 2*stride-16, [-18h] = 4*stride.
 */
void FAR PASCAL Decompress16To32_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                     LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                     LONG x, LONG y)
{
    WORD _huge *src = (WORD _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code, c[8];
    DWORD v[8];
    int  r, col, q, i;

    SETUP_DST(dst, lpbiOut, 4, x, y, stride);

    blockRowBytes = stride * 4;
    /* QUIRK: blocks advance (width/4)*16 bytes; this subtracts width*4. */
    rowAdvance   = blockRowBytes - lpbiIn->biWidth * 4;
    blocksPerRow = (DWORD)lpbiIn->biWidth >> 2;
    blockRows    = (DWORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *src++;
            if (code & 0x8000) {
                if ((code & 0xFC00) == 0x8400) {
                    n = code & 0x3FF;
                    DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 16);
                    continue;
                }
                v[0] = v[1] = RGB555TO32(code, 0xFFFF);
                code = 0;
                i = 0;
            }
            else {
                c[0] = src[0];
                c[1] = src[1];
                src += 2;
                i = (c[0] & 0x8000) != 0;
                if (i) {
                    c[0] &= 0x7FFF;
                    for (q = 2; q < 8; q++)
                        c[q] = *src++;
                }
                for (q = 0; q < (i ? 8 : 2); q += 2) {
                    v[q]     = RGB555TO32(c[q], c[q + 1]);
                    v[q + 1] = RGB555TO32(c[q + 1], c[q]);
                }
            }
            for (r = 0; r < 4; r++)
                for (col = 0; col < 4; col++) {
                    q = i ? QUAD(r, col) * 2 : 0;
                    ((DWORD _huge *)(dst + r * stride))[col] =
                        BLOCKBIT(code, r, col) ? v[q] : v[q + 1];
                }
            dst += 16;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}


/*
 * seg2:0BB2-11E3 (stub 0B90-0BB1)
 * CRAM16 -> 32bpp, 2x2 stretch.  NOT REFERENCED: slot 15 (DS:0784) is 0.
 * FAR PASCAL, o16 retf 18h.  [-10h] = 6*stride-32, [-18h] = 8*stride.
 */
void FAR PASCAL Decompress16To32x2_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                       LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                       LONG x, LONG y)
{
    WORD _huge *src = (WORD _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code, c[8];
    DWORD v[8], p;
    int  r, col, q, i;

    SETUP_DST(dst, lpbiOut, 4, x, y, stride);

    blockRowBytes = stride * 8;
    /* QUIRK: blocks advance (width/4)*32 bytes; this subtracts width*8. */
    rowAdvance   = blockRowBytes - lpbiIn->biWidth * 8;
    blocksPerRow = (DWORD)lpbiIn->biWidth >> 2;
    blockRows    = (DWORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *src++;
            if (code & 0x8000) {
                if ((code & 0xFC00) == 0x8400) {
                    n = code & 0x3FF;
                    DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 32);
                    continue;
                }
                v[0] = v[1] = RGB555TO32(code, 0xFFFF);
                code = 0;
                i = 0;
            }
            else {
                c[0] = src[0];
                c[1] = src[1];
                src += 2;
                i = (c[0] & 0x8000) != 0;
                if (i) {
                    c[0] &= 0x7FFF;
                    for (q = 2; q < 8; q++)
                        c[q] = *src++;
                }
                for (q = 0; q < (i ? 8 : 2); q += 2) {
                    v[q]     = RGB555TO32(c[q], c[q + 1]);
                    v[q + 1] = RGB555TO32(c[q + 1], c[q]);
                }
            }
            for (r = 0; r < 4; r++)
                for (col = 0; col < 4; col++) {
                    q = i ? QUAD(r, col) * 2 : 0;
                    p = BLOCKBIT(code, r, col) ? v[q] : v[q + 1];
                    ((DWORD _huge *)(dst + (2 * r)     * stride))[2 * col]     = p;
                    ((DWORD _huge *)(dst + (2 * r)     * stride))[2 * col + 1] = p;
                    ((DWORD _huge *)(dst + (2 * r + 1) * stride))[2 * col]     = p;
                    ((DWORD _huge *)(dst + (2 * r + 1) * stride))[2 * col + 1] = p;
                }
            dst += 32;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}


/*
 * seg2:1208-14B5 (stub 11E4-1207)
 * CRAM8 -> 8bpp, 1:1.  Dispatch slot 0.
 * FAR PASCAL, o16 retf 18h.  ES:ESI = source, DS:EDI = destination.
 * [-10h] = 2*stride-4, [-18h] = 4*stride.  Unlike the CRAM16 routines the
 * source width and height are read as 16-bit WORDs (movzx).
 */
void FAR PASCAL Decompress8To8_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                   LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                   LONG x, LONG y)
{
    BYTE _huge *src = (BYTE _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code;
    BYTE c[8];
    int  r, col, q, i;

    SETUP_DST(dst, lpbiOut, 1, x, y, stride);

    blockRowBytes = stride * 4;
    rowAdvance   = blockRowBytes - (LONG)((WORD)lpbiIn->biWidth & 0xFFFC);
    blocksPerRow = (WORD)lpbiIn->biWidth >> 2;
    blockRows    = (WORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *(WORD _huge *)src;
            src += 2;
            if (!(code & 0x8000)) {
                c[0] = src[0];
                c[1] = src[1];
                src += 2;
                i = 0;
            }
            else if ((code & 0xFC00) == 0x8400) {
                n = code & 0x3FF;
                DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 4);
                continue;
            }
            else if ((code & 0xFC00) == 0x8000) {
                /* fill with the low byte of the code */
                c[0] = c[1] = (BYTE)code;
                code = 0;
                i = 0;
            }
            else {
                /* code 8800h-FFFFh: 8 colours */
                for (q = 0; q < 8; q++)
                    c[q] = *src++;
                i = 1;
            }
            for (r = 0; r < 4; r++)
                for (col = 0; col < 4; col++) {
                    q = i ? QUAD(r, col) * 2 : 0;
                    dst[r * stride + col] = BLOCKBIT(code, r, col) ? c[q] : c[q + 1];
                }
            dst += 4;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}


/*
 * seg2:14D8-18D9 (stub 14B6-14D7)
 * CRAM8 -> 8bpp, each source pixel written as a 2x2 square.  Slot 4.
 * FAR PASCAL, o16 retf 18h.  [-10h] = 6*stride-8, [-18h] = 8*stride.
 * The code word and the following two bytes are fetched as one DWORD and
 * the source is backed up by 2 for fills and skips, so the routine can
 * read 2 bytes past the end of the input.
 */
void FAR PASCAL Decompress8To8x2_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                     LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                     LONG x, LONG y)
{
    BYTE _huge *src = (BYTE _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code;
    BYTE c[8], v;
    int  r, col, q, i;

    SETUP_DST(dst, lpbiOut, 1, x, y, stride);

    blockRowBytes = stride * 8;
    rowAdvance   = blockRowBytes - 2 * (LONG)((WORD)lpbiIn->biWidth & 0xFFFC);
    blocksPerRow = (WORD)lpbiIn->biWidth >> 2;
    blockRows    = (WORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *(WORD _huge *)src;
            c[0] = src[2];
            c[1] = src[3];
            src += 4;
            if (!(code & 0x8000)) {
                i = 0;
            }
            else if ((code & 0xFC00) == 0x8400) {
                src -= 2;
                n = code & 0x3FF;
                DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 8);
                continue;
            }
            else if ((code & 0xFC00) == 0x8000) {
                src -= 2;
                c[0] = c[1] = (BYTE)code;
                code = 0;
                i = 0;
            }
            else {
                for (q = 2; q < 8; q++)
                    c[q] = *src++;
                i = 1;
            }
            for (r = 0; r < 4; r++)
                for (col = 0; col < 4; col++) {
                    q = i ? QUAD(r, col) * 2 : 0;
                    v = BLOCKBIT(code, r, col) ? c[q] : c[q + 1];
                    dst[(2 * r)     * stride + 2 * col]     = v;
                    dst[(2 * r)     * stride + 2 * col + 1] = v;
                    dst[(2 * r + 1) * stride + 2 * col]     = v;
                    dst[(2 * r + 1) * stride + 2 * col + 1] = v;
                }
            dst += 8;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}


/*
 * Ordered-dither offsets for Decompress16To8_386, added to the
 * (r*40+g)*40+b index before the palette lookup.  In the binary these are
 * displacements in the gs:[bx+disp] / gs:[ebx+disp] operands.
 */
#define DOFS(r, g, b)   ((r) * 1600 + (g) * 40 + (b))

static WORD aDither[4][4] = {
    { DOFS(4,4,6), DOFS(3,3,5), DOFS(2,2,2), DOFS(1,1,1) },  /* 19A6 133D 0CD2 0669 */
    { DOFS(2,2,3), DOFS(0,0,0), DOFS(5,5,7), DOFS(3,3,4) },  /* 0CD3 0000 200F 133C */
    { DOFS(4,4,5), DOFS(4,4,6), DOFS(1,1,1), DOFS(1,1,2) },  /* 19A5 19A6 0669 066A */
    { DOFS(0,0,0), DOFS(2,2,3), DOFS(3,3,4), DOFS(5,5,7) },  /* 0000 0CD3 133C 200F */
};

/*
 * seg2:18FE-1E83 (stub 18DA-18FD)
 * CRAM16 -> 8bpp palettised with a 4x4 ordered dither.  Slot 8.
 * FAR PASCAL, o16 retf 18h.  DS:ESI = source, ES:EDI = destination,
 * FS = selector of glpDitherTable (read from DS:1412 on entry), GS = FS +
 * __AHINCR (the only relocation in the segment, at 1929h); FS and GS are
 * zeroed before returning.  [-10h] = 2*stride-4, [-18h] = 4*stride,
 * [-20h] = row advance (frame is 20h bytes, [-1Ch] unused).
 * All colour words, including those of 8-colour pairs 1-3, are masked to
 * 15 bits before the lookup.
 */
void FAR PASCAL Decompress16To8_386(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                    LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                    LONG x, LONG y)
{
    WORD _huge *src = (WORD _huge *)pIn;
    BYTE _huge *dst = (BYTE _huge *)pOut;
    WORD FAR   *lpIndex;            /* FS:0 */
    BYTE FAR   *lpPal;              /* GS:0 */
    LONG stride, blockRowBytes, rowAdvance;
    LONG blocksPerRow, blockRows, blocksLeft, n;
    WORD code, c[8], t;
    int  r, col, q, i;

    /* only the selector of glpDitherTable is used; both tables start at offset 0 */
    lpIndex = (WORD FAR *)MAKELP(SELECTOROF(glpDitherTable), 0);
    lpPal   = (BYTE FAR *)((BYTE _huge *)lpIndex + 0x10000L);

    SETUP_DST(dst, lpbiOut, 1, x, y, stride);

    blockRowBytes = stride * 4;
    rowAdvance   = blockRowBytes - (lpbiIn->biWidth & ~3L);
    blocksPerRow = (DWORD)lpbiIn->biWidth >> 2;
    blockRows    = (DWORD)lpbiIn->biHeight >> 2;

    for (;;) {
        blocksLeft = blocksPerRow;
        for (;;) {
            code = *src++;
            if (code & 0x8000) {
                if ((code & 0xFC00) == 0x8400) {
                    n = code & 0x3FF;
                    DO_SKIP(n, blocksLeft, blocksPerRow, blockRows, dst, blockRowBytes, 4);
                    continue;
                }
                c[0] = c[1] = code & 0x7FFF;
                code = 0;
                i = 0;
            }
            else {
                c[0] = src[0];
                c[1] = src[1];
                src += 2;
                i = (c[0] & 0x8000) != 0;
                if (i)
                    for (q = 2; q < 8; q++)
                        c[q] = *src++;
                for (q = 0; q < (i ? 8 : 2); q++)
                    c[q] &= 0x7FFF;
            }
            for (r = 0; r < 4; r++)
                for (col = 0; col < 4; col++) {
                    q = i ? QUAD(r, col) * 2 : 0;
                    t = lpIndex[BLOCKBIT(code, r, col) ? c[q] : c[q + 1]];
                    dst[r * stride + col] = lpPal[t + aDither[r][col]];
                }
            dst += 4;
            if (--blocksLeft == 0)
                break;
        }
        dst += rowAdvance;
        if (--blockRows == 0)
            break;
    }
}
