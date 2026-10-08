/*
 * libmain.c - adapter between Open Watcom's DLL startup and the
 * reconstructed LibMain, for the Open Watcom build.
 *
 * The original MSVIDC.DRV has an MSC-style LibEntry (documented at the
 * top of drvproc.c) that calls LibMain(hModule, cbHeap, lpszCmdLine).
 * This build uses Watcom's libentry.obj instead, because the Watcom
 * runtime (huge pointer and floating point helpers) needs its startup
 * to run.  Watcom's startup calls a FAR LibMain with an extra wDataSeg
 * argument; drvproc.c is compiled with -dLibMain=MsvidcLibMain and this
 * file lives in the same code segment (SEG1_TEXT) for the near call.
 */

#include <windows.h>

int NEAR PASCAL MsvidcLibMain(HMODULE hModule, WORD cbHeap, LPSTR lpszCmdLine);

int FAR PASCAL LibMain(HINSTANCE hInstance, WORD wDataSeg, WORD cbHeap, LPSTR lpszCmdLine)
{
    (void)wDataSeg;
    return MsvidcLibMain(hInstance, cbHeap, lpszCmdLine);
}
