/*
 * muldiv32.c - code segment 1 of VIDEDIT.EXE, s01:29DC-2FEB: the
 * Video for Windows muldiv32 assembly module, rendered here as C.
 *
 * muldiv32, muldivru32 and muldivrd32 compute a * b / c with a 64-bit
 * intermediate product, rounded to nearest, up or down, and saturate to
 * 0x7FFFFFFF (0x80000000 for a negative result) when the quotient
 * doesn't fit in 31 bits or c is 0.  Each tests __WINFLAGS: on a 386 it
 * uses 32-bit MUL and DIV, otherwise (WF_CPU086/186/286) the 16-bit
 * helpers below, with the same results.  The rounding term is |c| >> 1
 * as an arithmetic shift, so for c = 0x80000000 it is 0xC0000000.
 *
 * The 16-bit helpers use registers (DX:AX * CX:BX into DX:CX:BX:AX, and
 * DX:CX:BX:AX / SI:DI with the quotient in DX:AX and the remainder in
 * CX:BX); here they take and return values.  Two of them, the signed
 * multiply (2D28) and signed divide (2DCC), are not referenced.
 */

#include "videdit.h"
#include "vemain.h"

typedef struct {
    DWORD   hi;
    DWORD   lo;
} QWORD;

/*
 * s01:2D28-2D83  (NEAR, unreferenced) signed 32 x 32 -> 64 multiply
 */
QWORD NEAR SMul64(LONG a, LONG b)
{
    QWORD   q;
    DWORD   ua, ub;
    BOOL    fNeg;

    fNeg = (a < 0) != (b < 0);
    ua = (a < 0) ? -(DWORD)a : (DWORD)a;
    ub = (b < 0) ? -(DWORD)b : (DWORD)b;
    {
        DWORD a0 = LOWORD(ua), a1 = HIWORD(ua), b0 = LOWORD(ub), b1 = HIWORD(ub);
        DWORD p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        DWORD mid = (p00 >> 16) + LOWORD(p01) + LOWORD(p10);

        q.lo = (mid << 16) | LOWORD(p00);
        q.hi = p11 + HIWORD(p01) + HIWORD(p10) + HIWORD(mid);
    }
    if (fNeg) {
        q.lo = ~q.lo + 1;
        q.hi = ~q.hi + (q.lo == 0);
    }
    return q;
}

/*
 * s01:2D84-2DCB  (NEAR) unsigned 32 x 32 -> 64 multiply
 */
static QWORD NEAR UMul64(DWORD a, DWORD b)
{
    QWORD   q;
    DWORD   a0 = LOWORD(a), a1 = HIWORD(a), b0 = LOWORD(b), b1 = HIWORD(b);
    DWORD   p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    DWORD   mid = (p00 >> 16) + LOWORD(p01) + LOWORD(p10);

    q.lo = (mid << 16) | LOWORD(p00);
    q.hi = p11 + HIWORD(p01) + HIWORD(p10) + HIWORD(mid);
    return q;
}

/*
 * s01:2E2F-2F99  (NEAR) unsigned 64 / 32 divide; s01:2F9A-2FEB
 * normalises the divisor for it.
 *
 * Returns FALSE (the original sets the overflow flag) if the quotient
 * would not fit in 32 bits.
 */
static BOOL NEAR UDiv64(QWORD n, DWORD d, DWORD NEAR *pq, DWORD NEAR *pr)
{
    DWORD   q;
    int     i;
    BOOL    fCarry;

    if (d <= n.hi)
        return FALSE;
    q = 0;
    for (i = 0; i < 32; i++) {
        fCarry = (n.hi & 0x80000000L) != 0;
        n.hi = (n.hi << 1) | (n.lo >> 31);
        n.lo <<= 1;
        q <<= 1;
        if (fCarry || n.hi >= d) {
            n.hi -= d;
            q |= 1;
        }
    }
    *pq = q;
    *pr = n.hi;
    return TRUE;
}

/*
 * s01:2DCC-2E2E  (NEAR, unreferenced) signed 64 / 32 divide
 *
 * The quotient takes the sign of dividend ^ divisor, the remainder that
 * of the dividend; FALSE on overflow (including a quotient of 2^31 or
 * more).
 */
BOOL NEAR SDiv64(QWORD n, LONG d, LONG NEAR *pq, LONG NEAR *pr)
{
    BOOL    fNegQ, fNegR;
    DWORD   q, r, ud;

    fNegQ = fNegR = FALSE;
    ud = (DWORD)d;
    if (d < 0) {
        fNegQ = !fNegQ;
        ud = -(DWORD)d;
    }
    if (n.hi & 0x80000000L) {
        fNegQ = !fNegQ;
        fNegR = TRUE;
        n.lo = ~n.lo + 1;
        n.hi = ~n.hi + (n.lo == 0);
    }
    if (!UDiv64(n, ud, &q, &r) || (q & 0x80000000L))
        return FALSE;
    *pq = fNegQ ? -(LONG)q : (LONG)q;
    *pr = fNegR ? -(LONG)r : (LONG)r;
    return TRUE;
}

/*
 * The common part: |a| * |b| + add, divided by |c|, signed by a ^ b ^ c.
 */
static LONG NEAR MulDiv64(LONG a, LONG b, LONG c, int nRound)
{
    BOOL    fNeg;
    DWORD   ua, ub, uc, add, q, r;
    QWORD   p;

    fNeg = ((a ^ b ^ c) < 0);
    ua = (a < 0) ? -(DWORD)a : (DWORD)a;
    ub = (b < 0) ? -(DWORD)b : (DWORD)b;
    uc = (c < 0) ? -(DWORD)c : (DWORD)c;

    p = UMul64(ua, ub);
    if (nRound == 0)
        add = (DWORD)((LONG)uc >> 1);       /* nearest: sar */
    else if (nRound > 0)
        add = uc - 1;                       /* up */
    else
        add = 0;                            /* down */
    p.lo += add;
    if (p.lo < add)
        p.hi++;

    if (p.hi >= uc || !UDiv64(p, uc, &q, &r) || (q & 0x80000000L))
        return fNeg ? (LONG)0x80000000L : 0x7FFFFFFFL;
    return fNeg ? -(LONG)q : (LONG)q;
}

/*
 * s01:29DC-2B03  muldiv32  (FAR PASCAL): a * b / c rounded to nearest
 */
LONG FAR PASCAL muldiv32(LONG a, LONG b, LONG c)
{
    return MulDiv64(a, b, c, 0);
}

/*
 * s01:2B04-2C2C  muldivru32  (FAR PASCAL, unreferenced): rounded up
 */
LONG FAR PASCAL muldivru32(LONG a, LONG b, LONG c)
{
    return MulDiv64(a, b, c, 1);
}

/*
 * s01:2C2D-2D27  muldivrd32  (FAR PASCAL): rounded down
 */
LONG FAR PASCAL muldivrd32(LONG a, LONG b, LONG c)
{
    return MulDiv64(a, b, c, -1);
}
