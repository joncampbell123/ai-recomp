/*
 * wrkbench.h - stand-in for the Workbench (WRKBENCH.DLL) interface used
 * by VidEdit, written for the Open Watcom build.
 *
 * No header for WRKBENCH.DLL was ever published.  The argument sizes
 * come from the RETF of each VfW 1.1 export; the parameter types and
 * names were inferred from VidEdit's call sites and are provisional
 * where marked "?".  Imported by ordinal.
 */

#ifndef _INC_WRKBENCH
#define _INC_WRKBENCH

/* WrkGetToolInfo: what a registered tool (handler) can do */
typedef struct {
    WORD    wVersion;                   /* 00 set to 0x0100 by the caller */
    WORD    w02;
    WORD    wFlags;                     /* 04 WTF_* */
    WORD    w06;
    MEDTYPE medtypePhysical;            /* 08 */
    MEDTYPE medtypeLogical;             /* 0C */
    WORD    wExtString;                 /* 10 passed to WrkGetExtString */
} WRKTOOLINFO, FAR *LPWRKTOOLINFO;      /* 0x12 bytes */

#define WTF_LOAD                0x0001  /* can open files of its type */
#define WTF_SAVE                0x0002  /* can save files of its type */

void FAR PASCAL WrkClientInit(void);                                            /* 10 */
void FAR PASCAL WrkClientExit(void);                                            /* 11 */
WORD FAR PASCAL WrkGetExtString(WORD wTool, LPSTR lpszBuf, WORD cbBuf);         /* 42 */
WORD FAR PASCAL WrkRegisterEditor(LPSTR lpszName, MEDTYPE medtype, LPSTR lpszTitle,
                                  WORD wFlags);                                 /* 52 */
WORD FAR PASCAL WrkGetToolTitle(WORD wTool, LPSTR lpszBuf, WORD cbBuf);         /* 55 */
WORD FAR PASCAL WrkIterTools(WORD wTool, WORD wFlags);                          /* 57: next tool after wTool (0 = first), 0 at the end */
BOOL FAR PASCAL WrkGetToolInfo(WORD wTool, LPWRKTOOLINFO lpInfo);               /* 58 */
BOOL FAR PASCAL WrkVerifyHandler(MEDTYPE medtype, LPSTR lpszHandler);           /* 62 */
WORD FAR PASCAL WrkCreateToolArray(WORD wCount);                                /* 80 */
void FAR PASCAL WrkDestroyToolArray(WORD hArray);                               /* 81 */
BOOL FAR PASCAL WrkAddToToolArray(WORD hArray, WORD wTool, WORD wFlags);        /* 82 */
WORD FAR PASCAL WrkGetToolArrayEntry(WORD hArray, WORD wIndex);                 /* 84 */
WORD FAR PASCAL WrkAddInstance(WORD wEditor, HWND hwnd, LPSTR lpszName, WORD wFlags); /* 90 */
void FAR PASCAL WrkRemoveInstance(WORD wInstance);                              /* 91 */
void FAR PASCAL WrkBroadcastMessage(DWORD dwMsg, HANDLE hInst, MEDID medid);    /* 93 ? */
void FAR PASCAL WrkShowResError(HWND hwnd, LPSTR lpszTitle);                    /* 111 */
void FAR PASCAL WrkShowAboutDialog(HINSTANCE hInst, HWND hwnd, WORD idsTitle,
                                   WORD idsName, WORD idsVersion, WORD idsCopyright,
                                   WORD idDIB);                                 /* 114 */
WORD FAR PASCAL WrkOpenFileName(LPSTR lpszFile, FPMedReturn lpmr, MEDTYPE medtype,
                                WORD wFlags, HWND hwnd, LPSTR lpszTitle);       /* 130: 1 = OK; fills *lpmr (8 bytes) */
WORD FAR PASCAL WrkOpenDialog(FPMedReturn lpmr, MEDTYPE medtype, WORD wFlags, HWND hwnd,
                              LPSTR lpszTitle);                                 /* 131: 1 = OK; fills *lpmr */
WORD FAR PASCAL WrkProgBarCB(WORD wmsg, FPMedDisk fpDisk, LONG lParam,
                             WORD wPercentDone, LPSTR lpszTextStatus);          /* 500: a MedDiskInfoCallback, passed to medSaveAs */

#endif /* _INC_WRKBENCH */
