/*
 * msvidc.h - shared declarations for the reconstructed MSVIDC.DRV
 * (Microsoft Video 1 compressor, Video for Windows 1.1, Win16 NE DLL,
 * MD5 76a8179b20041ed5fa2ce2303f3b49c8).
 *
 * Reconstructed from the binary.  Offsets in comments are:
 *   segN:xxxx  code segment N, offset xxxx  (seg1..seg4)
 *   DS:xxxx    automatic data segment (seg5) offset
 *
 * The C parts were built with a Microsoft C 7 / Visual C++ 1.0 class
 * compiler (LINK 5.50; 386 code generation: ENTER/LEAVE, MOVSX, o32 PUSH),
 * as a small/medium model DLL (near data pointers are DS-relative).
 */

#ifndef _MSVIDC_H_
#define _MSVIDC_H_

#include <windows.h>
#include <mmsystem.h>
#include <compddk.h>            /* ICM_*, ICOPEN, ICINFO, ICCOMPRESS, ... */

/* FOURCCs as stored little-endian in the binary */
#define FOURCC_VIDC     mmioFOURCC('v','i','d','c')     /* 0x63646976 */
#define FOURCC_MSVC     mmioFOURCC('M','S','V','C')     /* 0x4356534D */
#define FOURCC_CRAM     mmioFOURCC('C','R','A','M')     /* 0x4D415243 */
#define BI_1632         mmioFOURCC('1','6','3','2')     /* 0x32333631, accepted as an output biCompression */
#define TWOCC_DC        0x6364                          /* 'dc' chunk id written to *lpckid */

#ifndef ICSTATUS_STATUS
#define ICSTATUS_STATUS 1
#endif

#ifndef MAKEWORD                /* not in the Win 3.1 windows.h */
#define MAKEWORD(lo, hi) ((WORD)(((BYTE)(lo)) | ((WORD)((BYTE)(hi))) << 8))
#endif

/* string table (block 3) */
#define IDS_DESCRIPTION 0x2A    /* "Microsoft Video 1" */
#define IDS_NAME        0x2B    /* "MS-CRAM" */
#define IDS_ABOUT       0x2C    /* "About" */

/* "Configure" dialog controls */
#define ID_SCROLL       100     /* temporal quality ratio scroll bar, range 1..100 */
#define ID_TEXT         101     /* "0.75" style readout */

#define QUALITY_DEFAULT 7500    /* ICM_GETDEFAULTQUALITY */

#ifndef RC_INVOKED              /* the rest is C only */

/* ICSTATE: the only persistent state is the temporal quality ratio
 * (percent, 1..100; default 75 meaning 0.75). */
typedef WORD ICSTATE;

/*
 * Decompressor entry point.  Every table entry (seg2 USE32 code, seg4
 * 16-bit code, seg3 C fallback) has this FAR PASCAL signature and pops
 * 0x18 bytes.  Decompress() ignores the return value; the assembly
 * routines leave nothing meaningful in DX:AX, so they are declared void
 * and the DWORD-returning C routines are cast when stored in the tables.
 */
typedef void (FAR PASCAL *DECOMPPROC)(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                      LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                      LONG x, LONG y);

typedef LONG (FAR PASCAL *STATUSPROC)(LPARAM lParam, UINT message, LONG l);

/* per-open instance, LocalAlloc(LPTR, sizeof(INSTINFO)) == 0x1C bytes */
typedef struct tagINSTINFO {
    DWORD       dwFlags;            /* +00 ICOPEN.dwFlags */
    DECOMPPROC  lpfnDecompress;     /* +04 decompressor in use (set by DecompressBegin) */
    DECOMPPROC  lpfnDecompressTest; /* +08 decompressor chosen by DecompressQuery */
    ICSTATE     CurrentState;       /* +0C temporal quality ratio */
    int         nCompress;          /* +0E compression begun (0/1) */
    int         nDecompress;        /* +10 decompression begun (0/1) */
    int         nDraw;              /* +12 never set; ICM_DRAW_* unsupported */
    STATUSPROC  Status;             /* +14 ICM_SET_STATUS_PROC */
    LPARAM      lParam;             /* +18 ICM_SET_STATUS_PROC */
} INSTINFO, NEAR *PINSTINFO;

/* ------------------------------------------------------------------ */
/* DGROUP (seg5) objects, in address order                             */
/* ------------------------------------------------------------------ */

extern WORD         gfCPU286;               /* DS:0000 GetWinFlags() & WF_CPU286         drvproc.c */
extern ICSTATE      DefaultState;           /* DS:0002 = 75                               drvproc.c */
extern BYTE         abDitherIndex[7][7][5]; /* DS:0004 (R,G,B level) -> palette index     data.c */
                                            /* DS:00F9-034D unreferenced tables           data.c */
extern BYTE         argbDitherPal[256][3];  /* DS:034E dither palette, R,G,B order        data.c */
extern int          aiDitherR[40];          /* DS:064E 0..39 -> R level 0..6              data.c */
extern int          aiDitherG[40];          /* DS:069E 0..39 -> G level 0..6              data.c */
extern int          aiDitherB[40];          /* DS:06EE 0..39 -> B level 0..4              data.c */
extern char         szConfigure[];          /* DS:073E "Configure"                        compress.c */
extern DECOMPPROC   aDecompress386[2][2][4];/* DS:0748 [src bpp/8-1][2x][dst bpp/8-1]     data.c */
extern DECOMPPROC   aDecompress286[2][2][4];/* DS:0788 same, used when gfCPU286           data.c */
                                            /* DS:07C8-07E9 dialog strings                compress.c */
                                            /* DS:07EA awQuadBit[16]                      decomp.c */
extern BYTE         g_abQuadrant[16];       /* DS:080A pixel -> 2x2 quadrant              data.c */
extern BYTE         g_ab5to8[32];           /* DS:081A 5 -> 8 bit, i*255/31, built lazily invcmap.c */
                                            /* DS:083A-09E9 C runtime data */
                                            /* DS:09EA dThresholdScale                    invcmap.c */
                                            /* DS:09FA pinstConfig                        compress.c */
                                            /* DS:09FC-0A72 inv_cmap state                invcmap.c */
extern RGBQUAD      g_rgbqIn[256];          /* DS:0A74 palette of an 8 bpp source         invcmap.c */
extern RGBQUAD      g_rgbqOut[256];         /* DS:0E74 palette of 8-bit CRAM output       invcmap.c */
extern BYTE FAR    *g_lpInvPal;             /* DS:1274 32K RGB555 -> g_rgbqOut index      invcmap.c */
                                            /* DS:1278-13F7 encoder block buffers         encode.c */
extern LPVOID       glpDitherTable;         /* DS:1410 0x1FA00-byte 16->8 dither tables   drvproc.c */
extern HMODULE      ghModule;               /* DS:1414                                    drvproc.c */
                                            /* DS:141E abColormap[3][256]                 invcmap.c */
extern PINSTINFO    gpinstCompress;         /* DS:172A owner of the 8-bit tables          compress.c */

/*
 * Encoder statistics (data.c), reset at the start of every frame.  Only
 * g_cSkipped is read elsewhere: Compress() marks a frame as a key frame
 * when no block was skipped.
 */
extern DWORD g_c8Color;                     /* DS:1416 8-colour blocks */
extern DWORD g_cSkipped;                    /* DS:141A blocks coded as skipped */
extern DWORD g_c8ColorQuads;                /* DS:171E CRAM-8 only: += 4 per 8-colour block */
extern DWORD g_dwStat1722;                  /* DS:1722 reset only */
extern DWORD g_cSolid;                      /* DS:1726 solid blocks */
extern DWORD g_cSkipCodes;                  /* DS:172C skip code words written */
extern DWORD g_cSolidFixups;                /* DS:1730 CRAM-16 fills moved out of the skip range */
extern DWORD g_c2Color;                     /* DS:1734 2-colour blocks */
extern DWORD g_cSkippedApprox;              /* DS:1740 skipped because the approximation matched */

/* ------------------------------------------------------------------ */
/* drvproc.c (seg1)                                                    */
/* ------------------------------------------------------------------ */

LONG FAR PASCAL _loadds DriverProc(DWORD dwDriverID, HDRVR hDriver, UINT uiMessage,
                                   LPARAM lParam1, LPARAM lParam2);

/* ------------------------------------------------------------------ */
/* compress.c (seg3:0000-06B7)                                         */
/* ------------------------------------------------------------------ */

LONG FAR PASCAL CompressQuery(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
LONG FAR PASCAL CompressGetFormat(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
LONG FAR PASCAL CompressBegin(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
LONG FAR PASCAL CompressGetSize(PINSTINFO pinst, LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
LONG FAR PASCAL Compress(PINSTINFO pinst, ICCOMPRESS FAR *icinfo, DWORD dwSize);
LONG FAR PASCAL CompressEnd(PINSTINFO pinst);
BOOL FAR PASCAL _loadds ConfigureDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* decomp.c (seg3:06B8-1095), huge-pointer C decompressors             */
/* ------------------------------------------------------------------ */

DWORD FAR PASCAL Decompress16To24Huge(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                      LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                      LONG x, LONG y);      /* seg3:0A88, unreferenced */
DWORD FAR PASCAL Decompress8To8Huge(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                    LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                    LONG x, LONG y);        /* seg3:0B8A */

/* ------------------------------------------------------------------ */
/* invcmap.c (seg3:1096-1D2B)                                          */
/* ------------------------------------------------------------------ */

LPBYTE FAR PASCAL BuildInverseTable(LPRGBQUAD prgb, int nColors);
LONG   FAR CDECL  QualityToThreshold(DWORD dwBadness);
LONG   FAR CDECL  CompressSetup(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
LONG   FAR        CompressCleanup(void);
void   FAR        FreeCompressTables(void);

/* ------------------------------------------------------------------ */
/* encode.c (seg3:1D2C-34B9)                                           */
/* ------------------------------------------------------------------ */

DWORD FAR _cdecl CompressFrame16(LPBITMAPINFOHEADER lpbiIn, BYTE _huge *pIn, WORD _huge *pOut,
                                 DWORD dwThreshold, DWORD dwThresholdTemporal,
                                 LPBITMAPINFOHEADER lpbiPrev, BYTE _huge *pPrev,
                                 STATUSPROC Status, LPARAM lParam);
DWORD FAR _cdecl CompressFrame8(LPBITMAPINFOHEADER lpbiIn, BYTE _huge *pIn, WORD _huge *pOut,
                                DWORD dwThreshold, DWORD dwThresholdTemporal,
                                LPBITMAPINFOHEADER lpbiPrev, BYTE _huge *pPrev,
                                STATUSPROC Status, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* Microsoft C runtime (seg3:34BA-4BC8)                                */
/* ------------------------------------------------------------------ */

/* WIN87EM glue: install the runtime's FP signal handler, and restore */
void (FAR *_FPInit(void))();
void FAR _FPTerm(void (FAR *lpfnOld)());

/* ------------------------------------------------------------------ */
/* dec386.c (seg2, USE32 assembly)                                     */
/* ------------------------------------------------------------------ */

void FAR PASCAL Decompress16To16_386  (LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG);
void FAR PASCAL Decompress16To16x2_386(LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG);
void FAR PASCAL Decompress16To32_386  (LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG); /* unreferenced */
void FAR PASCAL Decompress16To32x2_386(LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG); /* unreferenced */
void FAR PASCAL Decompress8To8_386    (LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG);
void FAR PASCAL Decompress8To8x2_386  (LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG);
void FAR PASCAL Decompress16To8_386   (LPBITMAPINFOHEADER, LPVOID, LPBITMAPINFOHEADER, LPVOID, LONG, LONG);

/* ------------------------------------------------------------------ */
/* dec286.c (seg4, 16-bit assembly)                                    */
/* ------------------------------------------------------------------ */

DWORD FAR PASCAL Decompress8To8_286(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                    LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                    LONG x, LONG y);        /* seg4:0000 */
DWORD FAR PASCAL Decompress16To16_286(LPBITMAPINFOHEADER lpbiIn, LPVOID pIn,
                                      LPBITMAPINFOHEADER lpbiOut, LPVOID pOut,
                                      LONG x, LONG y);      /* seg4:025C */

#endif /* RC_INVOKED */

#endif /* _MSVIDC_H_ */
