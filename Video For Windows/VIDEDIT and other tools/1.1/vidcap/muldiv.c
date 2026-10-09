/*
 * muldiv.c - code segment 1 of VIDCAP.EXE, 8CDC-8D6E: a hand-written 386
 * assembly module of 32-bit multiply-then-divide helpers with a 64-bit
 * intermediate, after the C runtime.  Rendered as C with inline assembly;
 * all are FAR PASCAL without the Windows prologue.
 *
 * Only muldiv32 (rounded) is used.
 */

#include <windows.h>
#include "vidcap.h"


/*
 * seg1:8CDC-8CF7  muldivt32  (unreferenced)
 *
 * Signed a * b / c, truncated.  The original name is unknown; it can't
 * be MulDiv32, which links as the same PASCAL name as muldiv32.
 */
LONG FAR PASCAL muldivt32(LONG a, LONG b, LONG c)
{
    LONG l;

    _asm {
        mov     eax, a
        mov     ebx, b
        mov     ecx, c
        imul    ebx
        idiv    ecx
        mov     l, eax
    }
    return l;
}

/*
 * seg1:8CFA-8D22  muldiv32
 *
 * Unsigned a * b / c, rounded to nearest.
 */
DWORD FAR PASCAL muldiv32(DWORD a, DWORD b, DWORD c)
{
    DWORD dw;

    _asm {
        mov     eax, a
        mov     ebx, b
        mov     ecx, c
        mul     ebx
        mov     ebx, ecx
        shr     ebx, 1
        add     eax, ebx
        adc     edx, 0
        div     ecx
        mov     dw, eax
    }
    return dw;
}

/*
 * seg1:8D26-8D4D  muldivru32
 *
 * Unsigned a * b / c, rounded up.
 */
DWORD FAR PASCAL muldivru32(DWORD a, DWORD b, DWORD c)
{
    DWORD dw;

    _asm {
        mov     eax, a
        mov     ebx, b
        mov     ecx, c
        mul     ebx
        mov     ebx, ecx
        dec     ebx
        add     eax, ebx
        adc     edx, 0
        div     ecx
        mov     dw, eax
    }
    return dw;
}

/*
 * seg1:8D50-8D6B  muldivrd32
 *
 * Unsigned a * b / c, truncated.
 */
DWORD FAR PASCAL muldivrd32(DWORD a, DWORD b, DWORD c)
{
    DWORD dw;

    _asm {
        mov     eax, a
        mov     ebx, b
        mov     ecx, c
        mul     ebx
        div     ecx
        mov     dw, eax
    }
    return dw;
}
