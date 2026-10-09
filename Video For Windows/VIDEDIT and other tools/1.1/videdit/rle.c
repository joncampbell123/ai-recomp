/*
 * rle.c - code segment 13 of VIDEDIT.EXE (0x047E bytes): decompressing
 * RLE8 DIBs (BI_RLE8) onto an 8-bit DIB, and checking whether an RLE
 * frame covers the whole image.
 *
 * RleDecompress picks one of three decoders: on a 386 or better a
 * hand-written assembly version with 32-bit offsets (RleDecompress386),
 * on a 286 another assembly version with 16-bit offsets for images
 * under 64K (RleDecompress286), and a C version with huge pointers for
 * larger images on a 286 (RleDecompressHuge).  The assembly versions are
 * rendered here as C.  They differ slightly: the assembly versions go to
 * the start of the next line at end-of-line and keep the source word
 * aligned after an absolute run, the C version counts the pixels of the
 * line and skips the pad byte of an odd absolute run; none of them
 * checks the image bounds.
 */

#include "videdit.h"
#include "vemain.h"

#define RLE_ESCAPE  0
#define RLE_EOL     0
#define RLE_EOB     1
#define RLE_DELTA   2

#define DibBits(lpbi) \
    ((LPBYTE)(lpbi) + (WORD)(lpbi)->biSize + (WORD)(lpbi)->biClrUsed * 4)

static void NEAR PASCAL RleDecompressHuge(LPBITMAPINFOHEADER lpbi, BYTE _huge *pb, BYTE _huge *prle);
static void NEAR PASCAL RleDecompress386(LPBITMAPINFOHEADER lpbi, LPBYTE pbDst, LPBYTE pbSrc);
static void NEAR PASCAL RleDecompress286(LPBITMAPINFOHEADER lpbi, LPBYTE pbDst, LPBYTE pbSrc);

/*
 * s13:0000-003F  RleDecompressDib
 *
 * Both DIBs are packed (header, colour table, bits) at offset 0 of
 * their selectors; the size comes from the destination's header.
 */
void FAR RleDecompressDib(WORD selDst, WORD selSrc)
{
    LPBITMAPINFOHEADER lpbiSrc = (LPBITMAPINFOHEADER)MAKELP(selSrc, 0);
    LPBITMAPINFOHEADER lpbiDst = (LPBITMAPINFOHEADER)MAKELP(selDst, 0);

    RleDecompress(lpbiDst, DibBits(lpbiDst), DibBits(lpbiSrc));
}

/*
 * s13:0040-008E  RleDecompress
 */
void FAR RleDecompress(LPBITMAPINFOHEADER lpbi, LPVOID lpDst, LPVOID lpSrc)
{
    if (!(GetWinFlags() & WF_CPU286))
        RleDecompress386(lpbi, (LPBYTE)lpDst, (LPBYTE)lpSrc);
    else if ((DWORD)(WORD)lpbi->biWidth * (WORD)lpbi->biHeight >= 0x10000L)
        RleDecompressHuge(lpbi, (BYTE _huge *)lpDst, (BYTE _huge *)lpSrc);
    else
        RleDecompress286(lpbi, (LPBYTE)lpDst, (LPBYTE)lpSrc);
}

/*
 * s13:0090-01C3  RleDecompressHuge  (NEAR PASCAL)
 */
static void NEAR PASCAL RleDecompressHuge(LPBITMAPINFOHEADER lpbi, BYTE _huge *pb, BYTE _huge *prle)
{
    WORD    cbLine;
    WORD    x;
    WORD    dx, dy;
    BYTE    cnt, b;

    cbLine = ((WORD)lpbi->biWidth + 3) & ~3;
    x = 0;
    for (;;) {
        cnt = *prle++;
        b = *prle++;
        if (cnt) {
            x += cnt;
            while (cnt--)
                *pb++ = b;
            continue;
        }
        switch (b) {
        case RLE_EOL:
            pb += (WORD)(cbLine - x);
            x = 0;
            break;
        case RLE_EOB:
            return;
        case RLE_DELTA:
            dx = *prle++;
            dy = *prle++;
            pb += (DWORD)dy * cbLine + dx;
            x += dx;
            break;
        default:
            cnt = b;
            x += cnt;
            while (cnt--)
                *pb++ = *prle++;
            if (b & 1)
                prle++;
            break;
        }
    }
}

/*
 * s13:01C6-0350  RleCheckFrame  (FAR PASCAL)
 *
 * Walks the RLE data of a packed DIB.  Returns FALSE if it runs past
 * the right or bottom edge (and then leaves the DIB locked).  Otherwise
 * sets *lpfPartial if the frame leaves pixels untouched: a delta, a line
 * ending short of the width, or the data ending before the last pixel.
 * The header is read again after GlobalUnlock.
 */
BOOL FAR PASCAL RleCheckFrame(HANDLE hdib, BOOL FAR *lpfPartial)
{
    LPBITMAPINFOHEADER  lpbi;
    BYTE _huge         *prle;
    BOOL                fPartial;
    WORD                x, y, xEnd;
    BYTE                cnt, b;

    fPartial = FALSE;
    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
    prle = (BYTE _huge *)lpbi + (lpbi->biClrUsed * 4 + lpbi->biSize);
    x = 0;
    y = 0;
    for (;;) {
        cnt = *prle++;
        b = *prle++;
        if (cnt) {
            x += cnt;
            if ((WORD)lpbi->biWidth < x)
                return FALSE;
            continue;
        }
        switch (b) {
        case RLE_EOL:
            if ((WORD)lpbi->biWidth > x)
                fPartial = TRUE;
            y++;
            if ((WORD)lpbi->biHeight < y)
                return FALSE;
            x = 0;
            break;

        case RLE_EOB:
            xEnd = x;
            GlobalUnlock(hdib);
            if ((WORD)lpbi->biHeight - 1 > y)
                fPartial = TRUE;
            else if ((WORD)lpbi->biHeight - y - 1 == 0 && (WORD)lpbi->biWidth > xEnd)
                fPartial = TRUE;
            if (lpfPartial)
                *lpfPartial = fPartial;
            return TRUE;

        case RLE_DELTA:
            fPartial = TRUE;
            x += *prle++;
            y += *prle++;
            if ((WORD)lpbi->biWidth < x)
                return FALSE;
            if ((WORD)lpbi->biHeight < y)
                return FALSE;
            break;

        default:
            prle += b;
            x += b;
            if ((WORD)lpbi->biWidth < x)
                return FALSE;
            if (b & 1)
                prle++;
            break;
        }
    }
}

/*
 * s13:0352-0403  RleDecompress386  (NEAR PASCAL, assembly)
 *
 * 32-bit offsets from the two selectors; the source offset is rounded
 * up to even before the first code and after every absolute run.
 */
static void NEAR PASCAL RleDecompress386(LPBITMAPINFOHEADER lpbi, LPBYTE pbDst, LPBYTE pbSrc)
{
    BYTE _huge *pb = (BYTE _huge *)pbDst;
    BYTE _huge *pbLine;
    BYTE _huge *prle = (BYTE _huge *)pbSrc;
    DWORD       cbLine;
    BYTE        cnt, b, dx, dy;

    cbLine = ((WORD)lpbi->biWidth + 3) & ~3;
    pbLine = pb;
    prle = (BYTE _huge *)MAKELP(SELECTOROF(prle), (OFFSETOF(prle) + 1) & ~1);
    for (;;) {
        cnt = *prle++;
        b = *prle++;
        if (cnt) {
            while (cnt--)
                *pb++ = b;
            continue;
        }
        switch (b) {
        case RLE_EOL:
            pb = pbLine + cbLine;
            pbLine = pb;
            break;
        case RLE_EOB:
            return;
        case RLE_DELTA:
            dx = *prle++;
            dy = *prle++;
            while (dy--) {
                pb += cbLine;
                pbLine += cbLine;
            }
            pb += dx;
            break;
        default:
            cnt = b;
            while (cnt--)
                *pb++ = *prle++;
            prle = (BYTE _huge *)MAKELP(SELECTOROF(prle), (OFFSETOF(prle) + 1) & ~1);
            break;
        }
    }
}

/*
 * s13:0404-047D  RleDecompress286  (NEAR PASCAL, assembly)
 *
 * The same with 16-bit offsets (the image is under 64K).
 */
static void NEAR PASCAL RleDecompress286(LPBITMAPINFOHEADER lpbi, LPBYTE pbDst, LPBYTE pbSrc)
{
    LPBYTE  pb = pbDst;
    LPBYTE  pbLine;
    LPBYTE  prle = pbSrc;
    WORD    cbLine;
    BYTE    cnt, b, dx, dy;

    cbLine = ((WORD)lpbi->biWidth + 3) & ~3;
    pbLine = pb;
    for (;;) {
        cnt = *prle++;
        b = *prle++;
        if (cnt) {
            while (cnt--)
                *pb++ = b;
            continue;
        }
        switch (b) {
        case RLE_EOL:
            pb = pbLine + cbLine;
            pbLine = pb;
            break;
        case RLE_EOB:
            return;
        case RLE_DELTA:
            dx = *prle++;
            dy = *prle++;
            while (dy--) {
                pb += cbLine;
                pbLine += cbLine;
            }
            pb += dx;
            break;
        default:
            cnt = b;
            while (cnt--)
                *pb++ = *prle++;
            prle = (LPBYTE)MAKELP(SELECTOROF(prle), (OFFSETOF(prle) + 1) & ~1);
            break;
        }
    }
}
