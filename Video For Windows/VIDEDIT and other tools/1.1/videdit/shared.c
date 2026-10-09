/*
 * shared.c - definitions of the globals declared in shared.h, with the
 * initial values from VIDEDIT.EXE's data segment (seg21).
 */

#include "videdit.h"

HINSTANCE ghInst = 0;                     /* DS:006A */
HBITMAP   ghbmMenuCheck = 0;              /* DS:009E */
HBITMAP   ghbmMenuUncheck = 0;            /* DS:00A0 */
BOOL      gfToolBar = 1;                  /* DS:01AA */
BOOL      gfStatusBar = 1;                /* DS:01AC */
WORD      gwCurFrame = 0x0000;            /* DS:01B0 */
WORD      gwLengthShown = 0x0000;         /* DS:01B2 */
WORD      gwLength = 0x0000;              /* DS:01B6 */
HWND      ghwndApp = 0;                   /* DS:01BA */
BOOL      gfDirty = 0;                    /* DS:0288 */
WORD      gwFrameUpdate = 0x0033;         /* DS:02AA */
WORD      gnMovieColors = 0x0001;         /* DS:02AC */
WORD      gnMovieColorsUndo = 0x0000;     /* DS:02AE */
UINT      gcfDibRange = 0;                /* DS:02BE */
BOOL      gfEditVideo = 1;                /* DS:04C0 */
BOOL      gfEditAudio = 1;                /* DS:04C2 */
BOOL      gfEditMidi = 0;                 /* DS:04C4 */
WORD      gwZoom = 0x0002;                /* DS:0570 */
BOOL      gfFrameNumInTitle = 0;          /* DS:058C */
BOOL      gfTimeFormat = 0;               /* DS:058E */
BOOL      gfInsertMode = 1;               /* DS:0590 */
BOOL      gfInMemory = 0;                 /* DS:111C */
WORD      gfPreviewing = 0x0000;          /* DS:111E */
WORD      gfPreviewDrawing = 0x0000;      /* DS:1120 */
UINT      gwUndoType = 0;                 /* DS:12E0 */
UINT      gwRedoType = 0;                 /* DS:12E2 */
MEDID     gmedidMovie = 0;                /* DS:1376 */
BOOL      gfInFileDialog = 0;             /* DS:137A */
HGLOBAL   ghDibRangeList;                /* DS:1BFA */
WORD      gcDibRangeList;                /* DS:1C00 */
UINT      gcfWave;                       /* DS:1C26 */
int       gcxVScroll;                    /* DS:2D02 */
int       gcyHScroll;                    /* DS:2D04 */
int       gcyDefault;                    /* DS:2D06 */
HBRUSH    ghbrBtnHighlight;              /* DS:2D14 */
WORD      gidBackground;                 /* DS:2D16 */
HBRUSH    ghbrBtnShadow;                 /* DS:2D18 */
HBRUSH    ghbrBtnText;                   /* DS:2D1A */
HINSTANCE ghMedWave;                     /* DS:2D1E */
int       gcyScreen;                     /* DS:2D50 */
LPVOID    glpfnMediaWaveFormatHWAVE;     /* DS:306C */
BOOL      gfCenterImage;                 /* DS:3070 */
LPVOID    glpfnMediaWaveReadHWAVE;       /* DS:3078 */
WORD      gcxMovie;                      /* DS:307C */
BOOL      gfResizeOnOpen;                /* DS:3124 */
HBRUSH    ghbrBtnFace;                   /* DS:3180 */
HGLOBAL   ghMemTemp;                     /* DS:318C */
HWND      ghwndView;                     /* DS:3192 */
HWND      ghwndFrame;                    /* DS:3196 */
HFONT     ghfontApp;                     /* DS:319A */
LPVOID    glpfnMediaWaveFreeHWAVE;       /* DS:31A0 */
int       gcyStatus;                     /* DS:31C0 */
HWND      ghwndTrackBar;                 /* DS:31CA */
UINT      gwCacheKB;                     /* DS:31CE */
int       gcxDefault;                    /* DS:31D2 */
HWND      ghwndTracks;                   /* DS:31DC */
int       gcyBorder;                     /* DS:31E0 */
WORD      gidHelpContext;                /* DS:31E2 */
int       gcxBorder;                     /* DS:31E8 */
HWND      ghwndMarks;                    /* DS:31EA */
HWND      ghwndToolBar;                  /* DS:31EC */
LPVOID    glpfnMediaWaveSizeHWAVE;       /* DS:31F8 */
HBRUSH    ghbrFillPat;                   /* DS:31FE */
HDRAWDIB  ghdd;                          /* DS:3200 */
int       gcxScreen;                     /* DS:3228 */
WORD      gcyMovie;                      /* DS:324E */
int       gxScroll;                      /* DS:3250 */
int       gyScroll;                      /* DS:3252 */
int       gcxMinWindow;                  /* DS:3256 */
HWND      ghwndTransport;                /* DS:3274 */

RECT      grcApp;                          /* DS:31AE */
COMPOPTIONS gCompOptions;                   /* DS:322A */
COMPOPTIONS gCompOptionsSaved;              /* DS:2D2A */
