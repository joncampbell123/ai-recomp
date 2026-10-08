/*
 * compat.c - stand-ins for the Microsoft C runtime pieces the driver
 * calls, for the Open Watcom build.
 *
 * In the original, _FPInit/_FPTerm (seg3:34BA/34EC) put the WIN87EM
 * floating point state into its initial configuration around a
 * compression call, and restore it afterwards.  The one floating point
 * function, QualityToThreshold, depends on that: it runs with 64-bit
 * (extended) precision, and at the default quality its result truncates
 * to 127 where 53-bit arithmetic gives 128.
 *
 * This build compiles invcmap.c with -fpi, which gives the same WIN87EM
 * OS fixups as the original; Watcom's DLL startup already initialises
 * WIN87EM (__init_87_emulator).  But Watcom's default control word
 * (__8087cw = 127Fh) selects 53-bit precision, so these stand-ins switch
 * precision control to 64 bits for the duration of the call.
 */

#include <windows.h>
#include <float.h>
#include "msvidc.h"

static unsigned wOldCW;

void (FAR *_FPInit(void))()
{
    wOldCW = _control87(PC_64, MCW_PC);
    return NULL;
}

void FAR _FPTerm(void (FAR *lpfnOld)())
{
    (void)lpfnOld;
    _control87(wOldCW, MCW_PC);
}
