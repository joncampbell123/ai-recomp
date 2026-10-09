/*
 * init.c - code segment 1 of MSADPCM.ACM (0x0092 bytes, PRELOAD)
 *
 * Library entry, LibMain and WEP.
 */

#include <windows.h>
#include "msadpcm.h"

HINSTANCE   ghinst;                     /* DS:01B0 */

/*
 * seg1:0000-0041  stub in front of LibEntry  (assembly, not referenced)
 *
 * The same 66-byte block also sits in front of the C runtime's own
 * LibEntry at seg2:0F48.  Nothing jumps to it: the NE entry point is
 * seg1:0042.  It ends in a signature and three offsets into the block
 * ('CDD', 1, 0016h, 001Eh, 0042h), presumably so that a tool can find it
 * and redirect the entry point here.
 *
 * 0000 push  ax, bx, cx, dx, es
 * 0005 mov   ax,__WINFLAGS     ; KERNEL.178
 *      or    ax,ax
 *      jns   001Eh             ; bit 15 (WF_WLO) clear
 * 000C pop   es, dx, cx, bx, ax
 * 0011 call  far InitTask-5    ; additive fixup: KERNEL.91 plus FFFBh
 * 0016 jmp   001Ch
 * 0019 xor   ax,ax             ; only if the callee skips the jmp
 *      retf
 * 001C jmp   0042h             ; LibEntry
 * 001E jmp   0030h             ; patchable; 0022h would run LibEntry only
 * 0020 jmp   0030h             ; for the first use of the module:
 * 0022 push  di
 *      call  far GetModuleUsage
 *      dec   ax
 *      jz    0030h
 *      inc   ax
 *      add   sp,10             ; return the usage count
 *      retf
 * 0030 pop   es, dx, cx, bx, ax
 * 0035 jmp   0042h             ; LibEntry
 * 0037 db    'CDD'
 *      dw    1, 0016h, 001Eh, 0042h
 */

/*
 * seg1:0042-0073  LibEntry  (assembly, FAR, the NE entry point CS:IP)
 *
 * The Windows SDK's LIBENTRY.ASM.  Windows enters with DI = hInstance,
 * DS = DGROUP, CX = local heap size, ES:SI = command line.
 *
 *      mov   ax,ds             ; Windows FAR prolog
 *      nop
 *      inc   bp
 *      push  bp
 *      mov   bp,sp
 *      push  ds
 *      mov   ds,ax
 *      push  di                ; LibMain(hInstance,
 *      push  ds                ;         wDataSeg,
 *      push  cx                ;         cbHeapSize,
 *      push  es                ;         pszCmdLine)
 *      push  si
 *      jcxz  @f
 *      xor   ax,ax
 *      push  ds
 *      push  ax
 *      push  cx
 *      call  LOCALINIT         ; LocalInit(DS, 0, cbHeap)
 *      or    ax,ax
 *      jz    fail
 * @@:  call  far LibMain       ; FAR PASCAL, pops its own 10 bytes
 *      jmp   short done
 * fail:pop   si, es, cx, ds, di
 * done:lea   sp,[bp-2]
 *      pop   ds
 *      pop   bp
 *      dec   bp
 *      retf
 *
 * The C runtime's initialisation (its own LibEntry at seg2:0F8A, which
 * calls _cinit) is never run.
 */

/*
 * seg1:0074-0083  LibMain  (FAR PASCAL, retf 0Ah; DS is already DGROUP)
 */
int FAR PASCAL LibMain(HINSTANCE hinst, WORD wDataSeg, WORD cbHeapSize, LPSTR pszCmdLine)
{
    ghinst = hinst;
    return (TRUE);
}

/*
 * seg1:0084-0090  WEP  (exported ordinal 1, FAR PASCAL _loadds)
 */
int FNEXPORT WEP(int nParam)
{
    return (1);
}
