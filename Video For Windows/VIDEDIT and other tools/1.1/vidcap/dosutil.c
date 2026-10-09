/*
 * dosutil.c - code segment 1 of VIDCAP.EXE, 2694-2A75: a hand-written
 * assembly module of DOS, XMS and BIOS helpers, rendered here as C with
 * inline assembly.
 *
 * Only DosDiskFreeSpace is called (by the Set File Size dialog); the rest
 * came along with the object file: find first/next, directories, drives,
 * the volume label, memory sizes, the DOS version and even a warm reboot.
 * All are FAR PASCAL with the usual Windows prologue unless noted.
 */

#include <windows.h>
#include "vidcap.h"

/*
 * DS:0286 an extended FCB searching for the volume label ("???????????",
 * attribute 8), DS:02B2 the DTA it is found in (the name at DS:02BA).
 */
static BYTE gabVolFCB[44] = {
    0xFF, 0, 0, 0, 0, 0, 0x08, 0,
    '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?',
    0, 0, 0, 0, 0,
    '?', '?', '?', '?', '?', '?', '?', '?', '?', '?', '?',
    0, 0, 0, 0, 0, 0, 0, 0, 0
};
static BYTE gabVolDTA[64];

static void NEAR XmsCall(void);
static void NEAR CmosRead(void);

/*
 * seg1:2694-26C0  DosFindFirst
 *
 * INT 21h AH=4Eh into lpDTA.  TRUE if a file was found.
 */
BOOL FAR PASCAL DosFindFirst(LPVOID lpDTA, LPSTR lpszPath, UINT wAttr)
{
    BOOL f;

    _asm {
        push    ds
        lds     dx, lpDTA
        mov     ah, 1Ah
        int     21h
        mov     cx, wAttr
        lds     dx, lpszPath
        mov     ah, 4Eh
        int     21h
        mov     ax, 1
        jnc     found
        xor     ax, ax
    found:
        pop     ds
        mov     f, ax
    }
    return f;
}

/*
 * seg1:26C3-26EA  DosFindNext
 */
BOOL FAR PASCAL DosFindNext(LPVOID lpDTA)
{
    BOOL f;

    _asm {
        push    ds
        lds     dx, lpDTA
        mov     ah, 1Ah
        int     21h
        pop     ds
        mov     ah, 4Fh
        int     21h
        mov     ax, 1
        jnc     found
        xor     ax, ax
    found:
        mov     f, ax
    }
    return f;
}

/*
 * seg1:26ED-2708  DosMkDir
 *
 * Returns 0 or the DOS error code.
 */
UINT FAR PASCAL DosMkDir(LPSTR lpszDir)
{
    UINT err;

    _asm {
        push    ds
        lds     dx, lpszDir
        mov     ah, 39h
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     ds
        mov     err, ax
    }
    return err;
}

/*
 * seg1:270B-272B  DosRename
 */
UINT FAR PASCAL DosRename(LPSTR lpszOld, LPSTR lpszNew)
{
    UINT err;

    _asm {
        push    ds
        push    di
        lds     dx, lpszOld
        les     di, lpszNew
        mov     ah, 56h
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     di
        pop     ds
        mov     err, ax
    }
    return err;
}

/*
 * seg1:272E-2749  DosDelete
 */
UINT FAR PASCAL DosDelete(LPSTR lpszFile)
{
    UINT err;

    _asm {
        push    ds
        lds     dx, lpszFile
        mov     ah, 41h
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     ds
        mov     err, ax
    }
    return err;
}

/*
 * seg1:274C-2763  DosExit
 *
 * INT 21h AH=4Ch: ends the task.
 */
void FAR PASCAL DosExit(int nCode)
{
    _asm {
        mov     al, byte ptr nCode
        mov     ah, 4Ch
        int     21h
    }
}

/*
 * seg1:2766-277C  DosGetDrive
 *
 * The current drive, 0 for A:.
 */
int FAR PASCAL DosGetDrive(void)
{
    int n;

    _asm {
        mov     ah, 19h
        int     21h
        sub     ah, ah
        mov     n, ax
    }
    return n;
}

/*
 * seg1:277D-2799  DosGetCodePage
 *
 * The active code page (INT 21h AX=6601h), or -1.
 */
int FAR PASCAL DosGetCodePage(void)
{
    int n;

    _asm {
        mov     ax, 6601h
        int     21h
        mov     ax, bx
        jnc     ok
        mov     ax, 0FFFFh
    ok:
        mov     n, ax
    }
    return n;
}

/*
 * seg1:279A-27B3  DosSetDrive
 *
 * Returns the number of drives.
 */
int FAR PASCAL DosSetDrive(int nDrive)
{
    int n;

    _asm {
        mov     dx, nDrive
        mov     ah, 0Eh
        int     21h
        sub     ah, ah
        mov     n, ax
    }
    return n;
}

/*
 * seg1:27B6-27DB  DosDiskFreeSpace
 *
 * Free bytes on a drive (0 = current, 1 = A:), -1 for a bad drive.
 */
DWORD FAR PASCAL DosDiskFreeSpace(int nDrive)
{
    DWORD dw;

    _asm {
        mov     dx, nDrive
        mov     ah, 36h
        int     21h
        cmp     ax, 0FFFFh
        je      bad
        mul     cx
        mul     bx
        jmp     done
    bad:
        mov     dx, ax
    done:
        mov     word ptr dw, ax
        mov     word ptr dw + 2, dx
    }
    return dw;
}

/*
 * seg1:27DE-281E  DosGetCurDir
 *
 * "X:\path" of the current drive.  Returns 0 or the DOS error code.
 */
UINT FAR PASCAL DosGetCurDir(LPSTR lpszDir)
{
    int     nDrive;
    UINT    err;

    nDrive = DosGetDrive();
    _asm {
        push    ds
        push    si
        push    di
        les     di, lpszDir
        push    es
        pop     ds
        cld
        mov     ax, nDrive
        inc     al
        mov     dl, al
        add     al, 40h
        stosb
        mov     al, ':'
        stosb
        mov     al, 5Ch             ; '\\'
        stosb
        mov     byte ptr es:[di], 0
        mov     si, di
        mov     ah, 47h
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     di
        pop     si
        pop     ds
        mov     err, ax
    }
    return err;
}

/*
 * seg1:2821-288F  GetFixedDrives
 *
 * Lists the local, non-removable drives (0 = A:) by making each drive
 * current in turn.  Returns how many were stored in anDrives.
 */
int FAR PASCAL GetFixedDrives(int NEAR *anDrives)
{
    int     nDriveOld;
    int     nDrive;
    int     n;
    BOOL    f;

    nDriveOld = DosGetDrive();
    n = 0;
    for (nDrive = 0; nDrive != 26; nDrive++) {
        DosSetDrive(nDrive);
        if (DosGetDrive() != nDrive)
            continue;
        _asm {
            mov     word ptr f, 0
            mov     ax, 4409h
            mov     bx, nDrive
            inc     bx
            int     21h
            jc      no
            and     dx, 1000h
            jnz     no
            mov     ax, 4408h
            mov     bx, nDrive
            inc     bx
            int     21h
            jc      no
            or      al, al
            jz      no
            mov     word ptr f, 1
        no:
        }
        if (f)
            anDrives[n++] = nDrive;
    }
    DosSetDrive(nDriveOld);
    return n;
}

/*
 * seg1:2892-28BB  IsRemovableDrive
 *
 * chDrive is the drive letter.
 */
BOOL FAR PASCAL IsRemovableDrive(int chDrive)
{
    BOOL f;

    _asm {
        mov     bx, chDrive
        or      bx, 20h
        sub     bx, 60h
        mov     ax, 4408h
        int     21h
        jc      no
        or      al, al
        jnz     no
        inc     ax
        jnz     done
    no:
        xor     ax, ax
    done:
        mov     f, ax
    }
    return f;
}

/*
 * seg1:28BE-2900  GetVolumeLabel
 *
 * "[LABEL]" of the current drive, with an FCB search.  Returns 0 if
 * found; otherwise lpsz is untouched and the result is 0xFF.
 *
 * Quirk of the original: the copy loop sets only CL to 11, so with CH
 * not zero it runs on until it finds a space.
 */
int FAR PASCAL GetVolumeLabel(LPSTR lpsz)
{
    int n;

    _asm {
        push    si
        push    di
        push    ds
        mov     dx, offset gabVolDTA
        mov     ah, 1Ah
        int     21h
        mov     dx, offset gabVolFCB
        mov     ah, 11h
        int     21h
        test    al, al
        jnz     done
        les     di, lpsz
        mov     al, 5Bh             ; '['
        stosb
        cld
        mov     si, offset gabVolDTA + 8
        mov     cl, 0Bh
    copy:
        lodsb
        cmp     al, ' '
        je      copied
        stosb
        loop    copy
    copied:
        mov     al, 5Dh             ; ']'
        stosb
        xor     ax, ax
        stosb
    done:
        pop     ds
        pop     di
        pop     si
        mov     n, ax
    }
    return n;
}

/*
 * seg1:2903-2937  DosChDir
 *
 * Changes the drive too if the path has one.  Returns 0 or the DOS error
 * code.
 */
UINT FAR PASCAL DosChDir(LPSTR lpszDir)
{
    UINT err;

    if (lpszDir[1] == ':')
        DosSetDrive((lpszDir[0] | 0x20) - 'a');
    _asm {
        push    ds
        lds     dx, lpszDir
        mov     ah, 3Bh
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     ds
        mov     err, ax
    }
    return err;
}

/*
 * seg1:293A-2978  IsValidDir
 */
BOOL FAR PASCAL IsValidDir(LPSTR lpszDir)
{
    char    ach[128];
    UINT    err;

    DosGetCurDir(ach);
    err = DosChDir(lpszDir);
    DosChDir(ach);
    return err == 0;
}

/*
 * seg1:297B-2997  XmsQueryFree
 *
 * Quirk of the original: after the XMS "query free memory" call it does
 * INT 12h, so the result is the conventional memory size in KB.
 */
UINT FAR PASCAL XmsQueryFree(void)
{
    UINT n;

    _asm {
        push    si
        mov     dx, 0FFFFh
        mov     ah, 08h
        call    XmsCall
        int     12h
        pop     si
        mov     n, ax
    }
    return n;
}

/*
 * seg1:2998-29AE  ExtMemSize
 *
 * INT 15h AH=88h: extended memory in KB.
 */
UINT FAR PASCAL ExtMemSize(void)
{
    UINT n;

    _asm {
        push    si
        mov     ah, 88h
        int     15h
        pop     si
        mov     n, ax
    }
    return n;
}

/*
 * seg1:29AF-29C1  DosGetVersion
 *
 * INT 21h AX=3000h.  The original has no DS-loading prologue.
 */
UINT FAR PASCAL DosGetVersion(void)
{
    UINT n;

    _asm {
        push    si
        mov     ax, 3000h
        int     21h
        pop     si
        mov     n, ax
    }
    return n;
}

/*
 * seg1:29C2-29D9  Reboot
 *
 * A warm boot: 1234h at 0040:0072, then a jump to FFFF:0000.
 */
void FAR PASCAL Reboot(void)
{
    _asm {
        mov     ax, 40h
        mov     ds, ax
        mov     word ptr ds:[72h], 1234h
        mov     ax, 0FFFFh
        push    ax
        xor     ax, ax
        push    ax
        retf
    }
}

/*
 * seg1:29DA-29FC  DosGetEnvironment
 *
 * A far pointer to the DOS environment, from the PSP.
 */
LPSTR FAR PASCAL DosGetEnvironment(void)
{
    WORD wSeg;

    _asm {
        push    si
        push    di
        mov     ah, 62h
        int     21h
        mov     es, bx
        mov     dx, es:[2Ch]
        pop     di
        pop     si
        mov     wSeg, dx
    }
    return (LPSTR)MAKELP(wSeg, 0);
}

/*
 * seg1:29FD-2A17  XmsGetVersion
 *
 * DX:AX = internal revision : XMS version.
 */
DWORD FAR PASCAL XmsGetVersion(void)
{
    DWORD dw;

    _asm {
        mov     ah, 00h
        call    XmsCall
        mov     word ptr dw, ax
        mov     word ptr dw + 2, bx
    }
    return dw;
}

/*
 * seg1:2A18-2A3F  XmsCall  (NEAR)
 *
 * Calls the XMS driver with AH = the function in AX, if there is one
 * (AX = 0 otherwise).
 */
static void NEAR XmsCall(void)
{
    _asm {
        push    bp
        mov     bp, sp
        sub     sp, 4
        mov     cx, ax
        mov     ax, 4300h
        int     2Fh
        cmp     al, 80h
        mov     ax, 0
        jne     none
        mov     ax, 4310h
        int     2Fh
        mov     ax, cx
        mov     [bp-2], es
        mov     [bp-4], bx
        call    dword ptr [bp-4]
    none:
        mov     sp, bp
        pop     bp
    }
}

/*
 * seg1:2A40-2A5B  CmosExtMem
 *
 * Extended memory in KB from CMOS registers 17h and 18h.
 */
UINT FAR PASCAL CmosExtMem(void)
{
    UINT n;

    _asm {
        mov     ax, 1718h
        call    CmosRead
        xchg    al, ah
        call    CmosRead
        mov     n, ax
    }
    return n;
}

/*
 * seg1:2A5C-2A75  CmosRead  (NEAR)
 *
 * Reads CMOS register AL into AL with NMI disabled, then selects register
 * 0Dh.  The original restores the flags with an IRET to itself; this
 * uses POPF.
 */
static void NEAR CmosRead(void)
{
    _asm {
        pushf
        rol     al, 1
        stc
        rcr     al, 1
        cli
        out     70h, al
        nop
        in      al, 71h
        push    ax
        mov     al, 1Ah
        rcr     al, 1
        out     70h, al
        pop     ax
        popf
    }
}
