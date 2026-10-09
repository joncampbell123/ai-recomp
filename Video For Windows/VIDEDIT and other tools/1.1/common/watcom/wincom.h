/*
 * wincom.h - stand-in for the WINCOM.DLL helpers used by VidEdit,
 * written for the Open Watcom build.
 *
 * The signatures are those documented in the Windows 3.0 Multimedia
 * Extensions beta reference (MMREF.HLP); the argument sizes match the
 * RETF of the VfW 1.1 WINCOM.DLL exports.  Imported by ordinal.
 */

#ifndef _INC_WINCOM
#define _INC_WINCOM

BOOL FAR PASCAL AddExtension(LPSTR lpszPath, LPSTR lpszExt, WORD wBufLen);     /* 50 */
void FAR PASCAL MakePath(LPSTR lpPath, LPSTR lpDrive, LPSTR lpDir, LPSTR lpFname,
                         LPSTR lpExt);                                          /* 52 */
void FAR PASCAL lmemcpy(LPSTR lpDst, LPSTR lpSrc, WORD wLen);                   /* 70 */

#endif /* _INC_WINCOM */
