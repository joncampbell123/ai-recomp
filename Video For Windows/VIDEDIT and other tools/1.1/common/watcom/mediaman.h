/*
 * mediaman.h - stand-in for the Media Manager (MEDIAMAN.DLL) header,
 * written for the Open Watcom build of VidEdit.
 *
 * Video for Windows 1.1 shipped no MediaMan header.  The declarations
 * follow MEDIAMAN.H of the Windows 3.0 Multimedia Extensions beta SDK
 * (1990); the argument sizes of every function used here were checked
 * against the RETF of the VfW 1.1 MEDIAMAN.DLL exports and all match.
 * Ordinals are in the comments; the functions are imported by ordinal in
 * the linker directives.
 */

#ifndef _INC_MEDIAMAN
#define _INC_MEDIAMAN

typedef DWORD   MEDID;
typedef WORD    MEDGID;
#ifndef _FOURCC_DEFINED
typedef DWORD   FOURCC;
#define _FOURCC_DEFINED
#endif
typedef FOURCC  MEDTYPE;
typedef WORD    MEDMSG;
typedef WORD    MEDUSER;

#define medFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#define medMEDTYPE(ch0, ch1, ch2, ch3)  medFOURCC(ch0, ch1, ch2, ch3)

/* instance block passed to handlers */
typedef struct _MedInfoStruct {
    DWORD   wFlags;
    WORD    wAccessCount;
    WORD    wLockCount;
    DWORD   dwAccessRet;
    DWORD   dwLockRet;
    char    achInst[2];
} MedInfoStruct;
typedef MedInfoStruct FAR *MEDINFO;

#define medInfoInstance(medinfo)        ((LPSTR)&((medinfo)->achInst[0]))
#define medInfoAccessCount(medinfo)     ((medinfo)->wAccessCount)
#define medInfoLockCount(medinfo)       ((medinfo)->wLockCount)

typedef DWORD (FAR PASCAL *FPMedHandler)(MEDID medid, MEDMSG medmsg, MEDINFO medinfo,
                                         LONG lParam1, LONG lParam2);

/* handler messages */
#define MED_INIT                0x0010
#define MED_UNLOAD              0x0011
#define MED_LOCK                0x0012
#define MED_UNLOCK              0x0013
#define MED_EXPEL               0x0014
#define MED_DESTROY             0x0015
#define MED_CREATE              0x0016
#define MED_TYPEINIT            0x0020
#define MED_TYPEUNLOAD          0x0021
#define MED_SETPHYSICAL         0x0022
#define MED_COPY                0x0023
#define MED_NEWNAME             0x0024
#define MED_PAINT               0x002A
#define MED_REALIZEPALETTE      0x002B
#define MED_GETPAINTCAPS        0x002C
#define MED_GETCLIPBOARDDATA    0x002D
#define MED_GETLOADPARAM        0x0030
#define MED_PRELOAD             0x0031
#define MED_LOAD                0x0032
#define MED_POSTLOAD            0x0033
#define MED_FREELOADPARAM       0x0034
#define MED_GETSAVEPARAM        0x0035
#define MED_PRESAVE             0x0036
#define MED_SAVE                0x0037
#define MED_POSTSAVE            0x0038
#define MED_FREESAVEPARAM       0x0039
#define MED_CHANGE              0x0060
#define MED_DISKCBBEGIN         0x0065
#define MED_DISKCBUPDATE        0x0066
#define MED_DISKCBEND           0x0067
#define MED_DISKCBNEWCONV       0x0068
#define MED_USER                0x0200

typedef struct _MedDisk {
    WORD    wFlags;                     /* 00 MEDF_DISK* */
    DWORD   dwMessageData;              /* 02 */
    DWORD   dwRetVal;                   /* 06 */
    DWORD   dwInstance1;                /* 0A */
    DWORD   dwInstance2;                /* 0E */
    DWORD   dwParam1;                   /* 12 */
    DWORD   dwParam2;                   /* 16 */
    DWORD   dwCbInstance1;              /* 1A */
    DWORD   dwCbInstance2;              /* 1E */
    HWND    hwndParentWindow;           /* 22 */
    MEDID   medid;                      /* 24 */
    DWORD   dwReserved1;
    DWORD   dwReserved2;
    DWORD   dwReserved3;
    DWORD   dwReserved4;
} MedDisk;
typedef MedDisk FAR *FPMedDisk;
typedef MedDisk NEAR *NPMedDisk;

#define MEDF_DISKSAVE           0x0001
#define MEDF_DISKLOAD           0x0002
#define MEDF_DISKVERIFY         0x0004

/* handler results for MED_LOAD and MED_SAVE */
#define MEDF_NOTPROCESSED       0L
#define MEDF_OK                 1
#define MEDF_ABORT              2
#define MEDF_ERROR              3
#define MEDF_BADFORMAT          4

typedef WORD (FAR PASCAL *FPMedDiskInfoCallback)(WORD wmsg, FPMedDisk fpDisk, LONG lParam,
                                                 WORD wPercentDone, LPSTR lpszTextStatus);

typedef struct _MedReturn {
    MEDID   medid;
    DWORD   dwReturn;
} MedReturn;
typedef MedReturn FAR *FPMedReturn;

/* medRegisterType flags */
#define MEDTYPE_LOGICAL         0x0001
#define MEDTYPE_PHYSICAL        0x0002

/* medLocate flags */
#define MEDF_LOCATE             0x0001
#define MEDF_MAKEFILE           0x0002
#define MEDF_MEMORYFILE         0x0004
#define MEDF_NONSHARED          0x0008

BOOL    FAR PASCAL medRegisterType(MEDTYPE medtype, FPMedHandler lpfnHandler, WORD wFlags); /* 10 */
BOOL    FAR PASCAL medUnregisterType(MEDTYPE medtype);                          /* 11 */
MEDTYPE FAR PASCAL medGetLogicalType(MEDID medid);                              /* 12 */
MEDTYPE FAR PASCAL medGetPhysicalType(MEDID medid);                             /* 17 */
DWORD   FAR PASCAL medGetError(void);                                           /* 30 */
WORD    FAR PASCAL medGetErrorText(DWORD wErrno, LPSTR lpszBuf, WORD wSize);    /* 31 */
void    FAR PASCAL medSetExtError(WORD wErrno, HANDLE hInst);                   /* 32 */
WORD    FAR PASCAL medAccess(MEDID medid, LONG lParam, FPMedReturn medReturn, BOOL fYield,
                             FPMedDiskInfoCallback lpfnCb, LONG lParamCb);      /* 60 */
void    FAR PASCAL medRelease(MEDID medid, LONG lParam);                        /* 61 */
DWORD   FAR PASCAL medLock(MEDID medid, WORD wFlags, LONG lParam);              /* 62 */
void    FAR PASCAL medUnlock(MEDID medid, WORD wFlags, DWORD dwChangeInfo, LONG lParam); /* 63 */
MEDID   FAR PASCAL medLocate(LPSTR lpszMedName, MEDTYPE medtype, WORD wFlags,
                             LPSTR lpszMedPath);                                /* 65 */
DWORD   FAR PASCAL medSendMessage(MEDID medid, MEDMSG medmsg, LONG lParam1, LONG lParam2); /* 66 */
WORD    FAR PASCAL medSaveAs(MEDID medid, FPMedReturn medReturn, LPSTR lpszName, LONG lParam,
                             BOOL fYield, FPMedDiskInfoCallback lpfnCb, LONG lParamCb); /* 69 */
BOOL    FAR PASCAL medCreate(FPMedReturn medReturn, MEDTYPE medtype, LONG lParam); /* 70 */
BOOL    FAR PASCAL medSetPhysicalType(MEDID medid, MEDTYPE medtype);            /* 71 */
WORD    FAR PASCAL medIsAccessed(MEDID medid);                                  /* 77 */
WORD    FAR PASCAL medGetFileName(MEDID medid, LPSTR lpszBuf, WORD wBufSize);   /* 83 */
HANDLE  FAR PASCAL medGetAliases(MEDID medid);                                  /* 180 */

#endif /* _INC_MEDIAMAN */
