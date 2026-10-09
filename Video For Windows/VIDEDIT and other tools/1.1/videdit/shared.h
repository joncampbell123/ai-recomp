/*
 * shared.h - globals of the reconstructed VIDEDIT.EXE that are used by
 * more than one code segment.  The definitions, with the initial values
 * from the data segment, are in shared.c.  The comment gives the DS offset
 * and the code segments that use the global.
 *
 * Two more are structures declared with their types: gCompOptions and
 * gCompOptionsSaved (compress.h).
 */

#ifndef _SHARED_H_
#define _SHARED_H_

extern HINSTANCE ghInst;                   /* DS:006A s1:22 s2:6 s4:15 s5:6 s6:7 s11:6 s12:2 s17:1 s19:5 s20:1 */
extern HBITMAP   ghbmMenuCheck;            /* DS:009E s2:12 s3:2 */
extern HBITMAP   ghbmMenuUncheck;          /* DS:00A0 s2:11 s3:2 */
extern BOOL      gfToolBar;                /* DS:01AA s2:2 s5:6 */
extern BOOL      gfStatusBar;              /* DS:01AC s2:2 s5:8 */
extern WORD      gwCurFrame;               /* DS:01B0 s1:3 s4:12 s5:24 s6:9 s19:8 */
extern WORD      gwLengthShown;            /* DS:01B2 s1:4 s4:6 s5:18 s6:2 s7:1 */
extern WORD      gwLength;                 /* DS:01B6 s1:8 s5:4 s6:2 s19:2 */
extern HWND      ghwndApp;                 /* DS:01BA s1:20 s4:12 s5:16 s6:3 s11:7 s19:5 s20:1 */
extern BOOL      gfDirty;                  /* DS:0288 s1:8 s6:1 s19:1 */
extern WORD      gwFrameUpdate;            /* DS:02AA s1:3 s2:4 s6:1 s18:2 s19:2 */
extern WORD      gnMovieColors;            /* DS:02AC s1:3 s6:15 */
extern WORD      gnMovieColorsUndo;        /* DS:02AE s1:3 s6:1 */
extern UINT      gcfDibRange;              /* DS:02BE s2:1 s6:4 */
extern BOOL      gfEditVideo;              /* DS:04C0 s1:7 s2:4 s4:9 s19:4 */
extern BOOL      gfEditAudio;              /* DS:04C2 s1:7 s2:2 s4:7 s19:4 */
extern BOOL      gfEditMidi;               /* DS:04C4 s1:1 s4:1 */
extern WORD      gwZoom;                   /* DS:0570 s1:4 s2:2 s4:9 s5:6 s6:15 */
extern BOOL      gfFrameNumInTitle;        /* DS:058C s1:1 s2:2 s5:1 */
extern BOOL      gfTimeFormat;             /* DS:058E s2:2 s5:4 */
extern BOOL      gfInsertMode;             /* DS:0590 s1:7 s2:2 s4:5 s6:22 s7:5 */
extern BOOL      gfInMemory;               /* DS:111C s1:1 s19:1 */
extern WORD      gfPreviewing;             /* DS:111E s1:1 s5:6 s19:8 */
extern WORD      gfPreviewDrawing;         /* DS:1120 s5:1 s6:1 s19:6 */
extern UINT      gwUndoType;               /* DS:12E0 s1:6 s2:1 */
extern UINT      gwRedoType;               /* DS:12E2 s1:5 s2:1 */
extern MEDID     gmedidMovie;              /* DS:1376 s1:11 s6:1 */
extern BOOL      gfInFileDialog;           /* DS:137A s1:2 s5:1 */
extern HGLOBAL   ghDibRangeList;           /* DS:1BFA s2:1 s6:10 */
extern WORD      gcDibRangeList;           /* DS:1C00 s2:1 s6:7 */
extern UINT      gcfWave;                  /* DS:1C26 s2:1 s7:4 */
extern int       gcxVScroll;               /* DS:2D02 s2:1 s4:6 */
extern int       gcyHScroll;               /* DS:2D04 s2:1 s4:5 */
extern int       gcyDefault;               /* DS:2D06 s1:1 s2:2 */
extern HBRUSH    ghbrBtnHighlight;         /* DS:2D14 s2:1 s3:2 s4:2 s5:3 */
extern WORD      gidBackground;            /* DS:2D16 s2:8 s4:3 */
extern HBRUSH    ghbrBtnShadow;            /* DS:2D18 s2:2 s3:2 s4:3 s5:1 */
extern HBRUSH    ghbrBtnText;              /* DS:2D1A s2:2 s3:2 s5:4 */
extern HINSTANCE ghMedWave;                /* DS:2D1E s2:5 s3:1 */
extern int       gcyScreen;                /* DS:2D50 s2:1 s5:2 */
extern LPVOID    glpfnMediaWaveFormatHWAVE;/* DS:306C s2:1 s7:4 */
extern BOOL      gfCenterImage;            /* DS:3070 s2:2 s4:1 */
extern LPVOID    glpfnMediaWaveReadHWAVE;  /* DS:3078 s2:1 s7:1 */
extern WORD      gcxMovie;                 /* DS:307C s1:3 s2:1 s4:8 s5:5 s6:30 s11:1 s18:3 s20:1 */
extern BOOL      gfResizeOnOpen;           /* DS:3124 s2:2 s5:1 */
extern HBRUSH    ghbrBtnFace;              /* DS:3180 s2:2 s3:2 s4:3 s6:1 */
extern HGLOBAL   ghMemTemp;                /* DS:318C s1:1 s4:5 s6:17 s7:1 s16:1 */
extern HWND      ghwndView;                /* DS:3192 s1:2 s4:14 s5:7 s19:3 */
extern HWND      ghwndFrame;               /* DS:3196 s4:14 s5:1 s6:2 s7:2 s18:2 s19:6 */
extern HFONT     ghfontApp;                /* DS:319A s2:3 s3:3 */
extern LPVOID    glpfnMediaWaveFreeHWAVE;  /* DS:31A0 s2:1 s7:11 */
extern int       gcyStatus;                /* DS:31C0 s2:1 s5:4 */
extern HWND      ghwndTrackBar;            /* DS:31CA s1:1 s5:12 */
extern UINT      gwCacheKB;                /* DS:31CE s2:2 s6:2 */
extern int       gcxDefault;               /* DS:31D2 s1:1 s2:2 */
extern HWND      ghwndTracks;              /* DS:31DC s1:4 s5:1 */
extern int       gcyBorder;                /* DS:31E0 s2:1 s5:9 */
extern WORD      gidHelpContext;           /* DS:31E2 s1:2 s2:1 s4:2 s5:3 */
extern int       gcxBorder;                /* DS:31E8 s2:1 s5:3 */
extern HWND      ghwndMarks;               /* DS:31EA s1:4 s5:8 */
extern HWND      ghwndToolBar;             /* DS:31EC s1:5 s5:3 */
extern LPVOID    glpfnMediaWaveSizeHWAVE;  /* DS:31F8 s2:1 s7:3 */
extern HBRUSH    ghbrFillPat;              /* DS:31FE s3:2 s5:1 */
extern HDRAWDIB  ghdd;                     /* DS:3200 s3:2 s4:2 s6:3 */
extern int       gcxScreen;                /* DS:3228 s2:1 s5:1 */
extern WORD      gcyMovie;                 /* DS:324E s1:3 s2:1 s4:8 s5:6 s6:30 s11:1 s18:3 s20:1 */
extern int       gxScroll;                 /* DS:3250 s4:16 s18:1 */
extern int       gyScroll;                 /* DS:3252 s4:15 s18:1 */
extern int       gcxMinWindow;             /* DS:3256 s2:1 s5:2 */
extern HWND      ghwndTransport;           /* DS:3274 s1:4 s5:11 s19:5 */

extern RECT      grcApp;                   /* DS:31AE s1 s2 s5: main window position, mmtools.ini */

/*
 * The VfW "gmem" macros: the handle of the last allocation goes through
 * the global ghMemTemp, the pointer is built from the selector alone
 * (offset 0), and pointers are unlocked and freed through their
 * selector.
 */
#define GAllocSelF(flags, cb) \
    ((ghMemTemp = GlobalAlloc((flags), (cb))) != 0 ? SELECTOROF(GlobalLock(ghMemTemp)) : 0)
#define GAllocPtrF(flags, cb)   MAKELP(GAllocSelF(flags, cb), 0)
#define GReAllocSelF(sel, cb, flags) \
    ((ghMemTemp = GlobalReAlloc((HGLOBAL)(sel), (cb), (flags))) != 0 ? SELECTOROF(GlobalLock(ghMemTemp)) : 0)
#define GUnlockPtr(lp)          GlobalUnlock((HGLOBAL)SELECTOROF(lp))
#define GFreePtr(lp)            (GUnlockPtr(lp), GlobalFree((HGLOBAL)SELECTOROF(lp)))

#endif /* _SHARED_H_ */
