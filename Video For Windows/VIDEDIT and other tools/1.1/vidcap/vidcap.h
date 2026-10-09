/*
 * vidcap.h - shared declarations for the reconstructed VIDCAP.EXE
 * (VidCap 1.1, "Video Capture app", Video for Windows 1.1, Win16 NE
 * application, MD5 0ad5d7ad8e4832d321af388ea670718f).
 *
 * Reconstructed from the binary.  Offsets in comments are:
 *   segN:xxxx  code segment N, offset xxxx  (seg1..seg8)
 *   DS:xxxx    automatic data segment (seg9) offset
 *
 * Built with Microsoft C 7 (LINK 5.50, 386 code, inline 8087 code with
 * WIN87EM), medium model: far code, near data.  Every far function has
 * the Windows prologue (mov ax,ds / nop / inc bp ...); the few near
 * helpers say so.  Unless a prototype says otherwise, functions are FAR
 * _cdecl.  Only 14 functions are exported; the dialog procedures of
 * dialogs.c and mcicap.c are not, and work because DS is DGROUP when
 * Windows calls them.  The Watcom build's functions load no DS, which
 * holds for window and dialog procedures; the three callbacks that are
 * called with someone else's DS (the COMMDLG hook, the WH_MSGFILTER hook
 * and the arrow's timer procedure) are _loadds there.
 */

#ifndef _VIDCAP_H_
#define _VIDCAP_H_

/* ------------------------------------------------------------------ */
/* resources                                                          */
/* ------------------------------------------------------------------ */

#define IDB_TOOLBAR             100     /* BITMAP, the toolbar buttons */
#define IDB_INTRO               402     /* DIB, the start-up/About picture */

#define IDR_FILTER_AVI          301     /* RCDATA file filters */
#define IDR_FILTER_DIB          302
#define IDR_FILTER_AVI2         303
#define IDR_FILTER_PAL          304

/* string table */
#define IDS_TITLE               100     /* "VidCap" */
#define IDS_COPYRIGHT           101
#define IDS_VERSION             102     /* "1.1" */
#define IDS_EXPIRED             106     /* beta expiry, unused */
#define IDS_APPNAME             126     /* "VidCap" */
#define IDS_UNTITLED            152     /* "(Untitled)", unused */
#define IDS_APPNAME2            400     /* "VidCap" */
#define IDS_PALEXT              402     /* "pal" */
#define IDS_ERR_NOMEM           406     /* "Out of memory" */
#define IDS_OUTOFMEMORY         406
#define IDS_OVERWRITE           407     /* "File '%s' exists -- overwrite it?" */
#define IDS_ERR_OPENPAL         409
#define IDS_ERR_SAVEPAL         410
#define IDS_ERR_SAVEFRAME       412
#define IDS_LOADPAL_TITLE       413
#define IDS_SAVEPAL_TITLE       414
#define IDS_SAVEFRAME_TITLE     416
#define IDS_AVIEXT              420     /* "avi" */
#define IDS_SETCAPFILE_TITLE    421
#define IDS_ABOUT               426     /* "About VidCap" */
#define IDS_SAVEAS_TITLE        427
#define IDS_ERR_CANTOPEN        428     /* "Cannot open '%s'" */
#define IDS_CONFIRMFRAMES       429
#define IDS_CONFIRMVIDEO        430
#define IDS_CLOSEFRAMES         431     /* "C&lose" */
#define IDS_HITESCAPE           432
#define IDS_OVERWRITECAPFILE    433
#define IDS_CLOSE               434     /* "&Close" */
#define IDS_ERR_VIDEDIT         435
#define IDS_ERR_READONLY        436
#define IDS_ERR_DISKFULL        437
#define IDS_ERR_NOSPACE         438
#define IDS_SETFILESIZE_TITLE   439
#define IDS_SAVEAS_PROGRESS     440     /* "SaveAs: %2ld%%  Hit Escape to abort." */
#define IDS_SAVEAS_WAIT         441
                                        /* 500-585 menu help (= IDM_*) */
#define IDS_ERR_NOHARDWARE      600
#define IDS_ERR_WAVEOPEN        610
#define IDS_ERR_WAVEMEM         611
#define IDS_ERR_WAVEPREPARE     612
#define IDS_ERR_WAVEADD         613
#define IDS_ERR_WAVESIZE        614
#define IDS_ERR_VIDEOOPEN       620
#define IDS_ERR_VIDEOMEM        621
#define IDS_ERR_VIDEOPREPARE    622
#define IDS_ERR_VIDEOADD        623
#define IDS_ERR_VIDEOSIZE       624
#define IDS_ERR_FILEOPEN        630
#define IDS_ERR_FILEWRITE       631
#define IDS_ERR_DATARATE        640
#define IDS_ERR_RECORDING       641
#define IDS_ERR_INIT            642
#define IDS_WARN_NOFRAMES       650
#define IDS_WARN_DEFAULTPAL     655
#define IDS_ERR_MCI             660
#define IDS_ERR_MCISTEP         661
#define IDS_ERR_NOAUDIO         662
#define IDS_ERR_COMPRESSOR      663
#define IDS_ERR_AUDIOLOST       664
#define IDS_LIVEWINDOW          670
#define IDS_OVERLAYWINDOW       671
#define IDS_STATUS_SETUP        672     /* "Setting up for capture - Please wait" */
#define IDS_STATUS_FINISHED     673
#define IDS_BUILDPALMAP         674
#define IDS_OPTIMALPAL          675
#define IDS_NFRAMES             676     /* "%d frames" */
#define IDS_STATUS_LNFRAMES     677     /* "%ld frames" */
#define IDS_STATUS_CAPTURED     678
#define IDS_STATUS_AUDIO        679
#define IDS_STATUS_CAPTURING    680
#define IDS_STATUS_RESULTAUDIO  681
#define IDS_STATUS_RESULT       682
#define IDS_DROPPED             683
#define IDS_START               690
#define IDS_STOP                691
#define IDS_TB_SETFILE          820     /* toolbar help */
#define IDS_TB_SINGLE           821
#define IDS_TB_FRAMES           822
#define IDS_TB_VIDEO            823
#define IDS_TB_PALETTE          824
#define IDS_TB_PREVIEW          825
#define IDS_TB_EDIT             826
#define IDS_TB_OVERLAY          827
#define IDS_HELP_FILEMENU       890     /* 890-894 the menus */
#define IDS_HELP_SYSMENU        899

/* menu commands (also their help strings) */
#define IDM_EXIT                500
#define IDM_LOADPALETTE         501
#define IDM_SETCAPFILE          502
#define IDM_EDITCAPTURE         503
#define IDM_SAVEAS              504
#define IDM_SAVEPALETTE         505
#define IDM_SAVEFRAME           506
#define IDM_COPY                520
#define IDM_PASTEPALETTE        521
#define IDM_PREFERENCES         522
#define IDM_AUDIOFORMAT         540
#define IDM_VIDEOSOURCE         541
#define IDM_VIDEOFORMAT         542
#define IDM_VIDEODISPLAY        543
#define IDM_PREVIEW             544
#define IDM_OVERLAY             545
#define IDM_COMPRESSION         546
#define IDM_CAPSINGLE           560
#define IDM_CAPFRAMES           561
#define IDM_CAPVIDEO            562
#define IDM_CAPPALETTE          563
#define IDM_HELPCONTENTS        580
#define IDM_ABOUT               585

/* help contexts (gidHelpContext); dialogs use their own ID */
#define IDH_LOADPALETTE         650
#define IDH_SETCAPFILE          651
#define IDH_SAVEAS              653
#define IDH_SAVEPALETTE         654
#define IDH_SAVEFRAME           655
#define IDH_VIDEOSOURCE         659
#define IDH_VIDEOFORMAT         660
#define IDH_VIDEODISPLAY        661
#define IDH_COMPRESSION         669

/* dialogs */
#define IDD_FILESIZE            652
#define IDD_PREFERENCES         656
#define IDD_INITERROR           657
#define IDD_AUDIOFORMAT         658
#define IDD_CAPFRAMES           662
#define IDD_CAPSEQUENCE         663
#define IDD_CAPPALETTE          664
#define IDD_MCISETUP            665
#define IDD_MCISTEP             666
#define IDD_LEVELMONO           667
#define IDD_LEVELSTEREO         668

/* Capture Frames */
#define IDC_CAPFRAMESTEXT       600
#define IDC_CAPFRAMESCAPTURE    601
#define IDC_CAPFRAMESCOUNT      603

/* Capture Video Sequence */
#define IDC_FRAMERATE           610
#define IDC_TIMELIMIT           611
#define IDC_SECONDSLABEL        612
#define IDC_SECONDS             613
#define IDC_CAPAUDIO            614
#define IDC_FRAMERATEARROW      615
#define IDC_SECONDSARROW        616
#define IDC_AUDIOSETUP          625
#define IDC_VIDEOSETUP          626
#define IDC_TODISK              627
#define IDC_TOMEMORY            628
#define IDC_MCISETUP            629
#define IDC_MCICONTROL          630
#define IDC_COMPRESSSETUP       631

/* Capture Palette */
#define IDC_PALFRAME            630
#define IDC_PALSTART            631
#define IDC_PALNUMFRAMES        632
#define IDC_PALCOLORS           633
#define IDC_PALCOLORSARROW      634

/* Audio Format */
#define IDC_8BIT                700
#define IDC_16BIT               701
#define IDC_MONO                702
#define IDC_STEREO              703
#define IDC_11KHZ               704
#define IDC_22KHZ               705
#define IDC_44KHZ               706
#define IDC_LEVEL               707

/* Set File Size */
#define IDC_FILESIZE            725
#define IDC_FILESIZEARROW       726
#define IDC_SPACEAVAIL          727

/* Recording Level */
#define IDC_METER1              740
#define IDC_METER2              741

/* Initialization Error */
#define IDC_CONTINUE            760
#define IDC_EXIT                761
#define IDC_INITERRTEXT         890

/* Set Capture File (SETFILE, a COMMDLG template) */
#define IDC_NEWSIZE             763

/* Preferences */
#define IDC_STATUSBAR           840
#define IDC_TOOLBARCHK          841
#define IDC_CENTERIMAGE         843
#define IDC_BKDEFAULT           845
#define IDC_BKLTGRAY            846
#define IDC_BKDKGRAY            847
#define IDC_BKBLACK             848
#define IDC_INDEX32K            850
#define IDC_INDEX324K           851

/* MCI Settings */
#define IDC_MCI2XSPATIAL        855
#define IDC_MCIAVERAGE          856
#define IDC_MCIDEVICE           860
#define IDC_MCIPLAY             861
#define IDC_MCISTEP             862
#define IDC_MCISTARTTIME        865
#define IDC_MCISTOPTIME         866
#define IDC_MCISETSTART         867
#define IDC_MCISETSTOP          868

/* MCI Step Capture; 870, 874, 875 are only posted */
#define IDC_STEPSTART           870
#define IDC_STEPSTOP            871
#define IDC_STEPCANCEL          872
#define IDC_STEPAUDIO           874
#define IDC_STEPFRAME           875
#define IDC_STEPSTATUS          876
#define IDC_STEPSTARTTIME       877
#define IDC_STEPCURTIME         878
#define IDC_STEPSTOPTIME        879

#define IDT_PALETTE             900     /* Capture Palette timer */

/* the toolbar */
#define IDC_TOOLBAR             189     /* its control ID */

#define BTN_SETFILE             0       /* buttons = bitmap columns */
#define BTN_EDIT                1
#define BTN_PREVIEW             2
#define BTN_SINGLE              3
#define BTN_FRAMES              4
#define BTN_VIDEO               5
#define BTN_PALETTE             6
#define BTN_OVERLAY             7

#define BTNST_GRAYED            0       /* states = bitmap rows */
#define BTNST_UP                1
#define BTNST_DOWN              2
#define BTNST_FOCUSUP           3
#define BTNST_FOCUSDOWN         4
#define BTNST_FULLDOWN          5

#define BTNTYPE_PUSH            0
#define BTNTYPE_CHECKBOX        1
#define BTNTYPE_RADIO           2       /* and up: radio groups */

#define BTNACT_MOUSEDOWN        0
#define BTNACT_MOUSEUP          1
#define BTNACT_MOUSEMOVEOFF     2
#define BTNACT_MOUSEMOVEON      3
#define BTNACT_MOUSEDBLCLK      4
#define BTNACT_KEYDOWN          5
#define BTNACT_KEYUP            6

#ifndef RC_INVOKED

#include <string.h>
#include <time.h>
#include <mmsystem.h>
#include <msvideo.h>
#include <avifmt.h>

/* ------------------------------------------------------------------ */
/* types                                                              */
/* ------------------------------------------------------------------ */

#ifndef MAKEWORD
#define MAKEWORD(lo, hi)        ((WORD)(((BYTE)(lo)) | ((WORD)((BYTE)(hi))) << 8))
#endif

typedef struct tagTOOLBUTTON {
    RECT    rc;                         /* 00 */
    int     iButton;                    /* 08 bitmap column, BTN_* */
    int     iState;                     /* 0A BTNST_* */
    int     iPrevState;                 /* 0C */
    int     iType;                      /* 0E BTNTYPE_* */
    int     iActivity;                  /* 10 BTNACT_* */
    int     iString;                    /* 12 help string */
} TOOLBUTTON, NEAR *PTOOLBUTTON, FAR *LPTOOLBUTTON;     /* 0x14 bytes */

typedef DWORD _huge *LPHISTOGRAM;       /* 32768 RGB555 counters */

typedef struct tagAVERAGE FAR *LPAVERAGE;   /* average.c */

/* an INFO chunk for the file header (AVICAP's CAPINFOCHUNK) */
typedef struct {
    FOURCC  fccInfoID;
    LPVOID  lpData;
    LONG    cbData;
} CAPINFOCHUNK, FAR *LPCAPINFOCHUNK;

/* ------------------------------------------------------------------ */
/* DGROUP (seg9, 0x3072 bytes; heap 0x1000, stack 0x2000)             */
/*                                                                    */
/* Initialised data is in link order: misc, intro, path, arrow,       */
/* vidcap, profile, help, init, dialogs, frame, data, dosutil, init,  */
/* video, capframe, mcicap, file, toolbar, status, util, average,     */
/* then the C runtime (0624-0EFF).  The rest (0F02-3071) is           */
/* uninitialised.                                                     */
/* ------------------------------------------------------------------ */

/* misc.c */
extern char         gachMsg[128];           /* DS:0F02 */

/* arrow.c */
extern char         gszArrowClass[];        /* DS:00EE "ComArrow" */

/* vidcap.c */
extern char         gszIniFile[];           /* DS:00F8 "mmtools.ini" */
extern char         gszAppName[];           /* DS:0104 "VidCap" */
extern char         gszEditClass[];         /* DS:010C */
extern FARPROC      ghhkMsgFilter;          /* DS:011E */
extern FARPROC      glpfnMsgFilter;         /* DS:0122 */
extern HINSTANCE    ghInst;                 /* DS:0126 */
extern HACCEL       ghAccel;                /* DS:0128 */
extern HDC          ghdcUnused;             /* DS:012A */
extern char         gszRegisterPenApp[];    /* DS:012C */
extern BOOL         gfInitDone;             /* DS:013C */

/* help.c */
extern char         gachHelpFile[80];       /* DS:263C */

/* init.c */
extern char         gszRLMeterClass[];      /* DS:016D "VCRLMeter" */
extern BOOL         gfToolBar;              /* DS:02F2 */
extern BOOL         gfStatusBar;            /* DS:02F4 */
extern HWND         ghwndApp;               /* DS:02F6 */
extern int          gcxVScroll;             /* DS:1532 */
extern int          gcyHScroll;             /* DS:1536 */
extern UINT         gwmFileNameOK;          /* DS:153C */
extern COLORREF     grgbBtnShadow;          /* DS:153E */
extern HBRUSH       ghbrBtnHighlight;       /* DS:1550 */
extern HBRUSH       ghbrBtnShadow;          /* DS:1556 */
extern HBRUSH       ghbrBtnText;            /* DS:1558 */
extern RECT         grcApp;                 /* DS:15F0 */
extern int          gcyScreen;              /* DS:25A8 */
extern BOOL         gfInitializing;         /* DS:25AA */
extern CHANNEL_CAPS gChannelCaps;           /* DS:25AC */
extern int          gnDevice;               /* DS:25D0 */
extern COLORREF     grgbBtnFace;            /* DS:25FE */
extern int          gcxClient;              /* DS:2606 */
extern HBRUSH       ghbrBtnFace;            /* DS:2ECE */
extern BOOL         gfWin31;                /* DS:2ED4 */
extern HFONT        ghfontApp;              /* DS:2EEC */
extern BOOL         gfAudioDevice;          /* DS:2EEE */
extern RECT         grcCtrl;                /* DS:2F04 */
extern COLORREF     grgbBtnText;            /* DS:2F0C */
extern int          gcyStatus;              /* DS:2F14 */
extern int          gcyBorder;              /* DS:2F2E */
extern int          gcxBorder;              /* DS:2F32 */
extern BOOL         gfYield;                /* DS:2F34 */
extern HWND         ghwndToolbar;           /* DS:2F38 */
extern BOOL         gfHasOverlay;           /* DS:2F46 */
extern BOOL         gfVideoDevice;          /* DS:2FE8 */
extern HBRUSH       ghbrUnused;             /* DS:3016 */
extern HWND         ghwndStatusBar;         /* DS:303A */
extern COLORREF     grgbBtnHighlight;       /* DS:3040 */
extern int          gcxScreen;              /* DS:3048 */
extern int          gcxStatusField;         /* DS:3052 */
extern DWORD        gdwWinFlags;            /* DS:3056 */
extern int          gcyClient;              /* DS:305E */

/* frame.c */
extern char         gszShowClass[];         /* DS:01C8 "ShowWindow" */
extern HWND         ghwndSizeBox;           /* DS:1542 */
extern HWND         ghwndFrame;             /* DS:2EE2 */
extern HWND         ghwndShow;              /* DS:2EE4 */
extern HWND         ghwndVScroll;           /* DS:2EF0 */
extern int          gnHScrollPos;           /* DS:304A */
extern int          gnVScrollPos;           /* DS:304C */
extern HWND         ghwndHScroll;           /* DS:3060 */

/* ctrl.c */
extern int          gcyFrame;               /* DS:1530 */
extern int          gcyExtra;               /* DS:25F6 */
extern int          gcxFrame;               /* DS:2F26 */
extern BOOL         gfAppActive;            /* DS:2F36 */
extern int          gxFrame;                /* DS:3044 */
extern int          gyFrame;                /* DS:3046 */
extern int          gcyMinApp;              /* DS:3054 */

/* data.c: capture settings and devices */
extern COMPVARS     gCompVars;              /* DS:01D4 Video Compression */
extern DWORD        gdwMicroSecPerFrame;    /* DS:0212 = 66666 */
extern BOOL         gfCaptureAudio;         /* DS:0216 = 1 */
extern BOOL         gfPalettized;           /* DS:0218 = 1 */
extern UINT         gwFrequency;            /* DS:021A = 11025 */
extern UINT         gwChannels;             /* DS:021C = 1 */
extern UINT         gwBits;                 /* DS:021E = 8 */
extern BOOL         gfLimitEnabled;         /* DS:0220 */
extern UINT         gwTimeLimit;            /* DS:0222 = 100 s */
extern HCURSOR      ghcurWait;              /* DS:0224 */
extern HCURSOR      ghcurOld;               /* DS:0226 */
extern BOOL         gfCaptureToMemory;      /* DS:0228 */
extern HVIDEO       ghVideoIn;              /* DS:022A VIDEO_IN */
extern HVIDEO       ghVideoExtIn;           /* DS:022C VIDEO_EXTERNALIN */
extern HVIDEO       ghVideoExtOut;          /* DS:022E VIDEO_EXTERNALOUT */
extern BOOL         gfMCIControl;           /* DS:0230 */
extern char         gachMCIDevice[80];      /* DS:0232 */
extern UINT         gidTimer;               /* DS:0282 preview timer */
extern BOOL         gfWarnedDefaultPal;     /* DS:0284 */

/* data.c: uninitialised, owner unknown */
extern BOOL         gfOverlay;              /* DS:155A */
extern BOOL         gfPreview;              /* DS:25F8 */
extern BOOL         gfCenterImage;          /* DS:25FC */
extern UINT         gidHelpContext;         /* DS:2F30 */
extern char         gachCaptureFile[144];   /* DS:2F4A */
extern VIDEOHDR     gvhFrame;               /* DS:2FEE the last frame */
extern HDRAWDIB     ghdd;                   /* DS:3018 */

/* video.c */
extern HPALETTE     ghpalCurrent;           /* DS:0348 */
extern BOOL         gfDefaultPal;           /* DS:034A = 1 */
extern int          gnPalFrames;            /* DS:034C */
extern int          gnPalColors;            /* DS:034E = 256 */
extern HGLOBAL      ghFrameBits;            /* DS:0354 */
extern HGLOBAL      ghbiCapture;            /* DS:0356 */
extern LPBYTE       glpFrameBits;           /* DS:0358 */
extern LPBITMAPINFOHEADER glpbiCapture;     /* DS:035C */
extern int          gcxCapture;             /* DS:0360 */
extern int          gcyCapture;             /* DS:0362 */

/* capframe.c */
extern UINT         gidBkColor;             /* DS:0364 = IDC_BKDEFAULT */
extern LPAVERAGE    glpAverage;             /* DS:0366 */
extern int          gnAverageFrames;        /* DS:036A = 1 */
extern BOOL         gf2xSpatial;            /* DS:036C */
extern BOOL         gfCapFramesOK;          /* DS:144E */
extern BOOL         gfCapFramesStarted;     /* DS:1450 */

/* mcicap.c */
extern int          gnMCIVideodiscs;        /* DS:1552 */
extern BOOL         gfMCIPlay;              /* DS:1554 */
extern BOOL         gfCapturing;            /* DS:2FDE */
extern DWORD        gdwMCIStartTime;        /* DS:2F3A */
extern DWORD        gdwMCIStopTime;         /* DS:2FE0 */
extern int          gnMCIVCRs;              /* DS:3062 */

/* file.c */
extern BOOL         gfCaptureFileHasData;   /* DS:04FC */
extern BOOL         gfCapFileExisted;       /* DS:15EC */
extern DWORD        gdwCapFileSize;         /* DS:2F3E */

/* toolbar.c */
extern char         gszToolBarClass[];      /* DS:054A "ToolBarClass" */

/* status.c */
extern char         gszStatusClass[];       /* DS:0558 "StatusClass" */
extern char         gszSTextClass[];        /* DS:0564 "SText" */
extern BOOL         gfStatusDefault;        /* DS:056A */
extern HWND         ghwndStatusText;        /* DS:2F24 */

/* capavi.c */
extern HGLOBAL      ghIndex;                /* DS:1534 */
extern DWORD        gdwIndex;               /* DS:1548 */
extern DWORD        gdwIndexSize;           /* DS:154C [VidCap] IndexSize */
extern LPVIDEOHDR   galpVHdr[];             /* DS:1608 [1000] */
extern int          giVideoBuffer;          /* DS:260E */
extern DWORD        gdwAVIHdrSize;          /* DS:2634 */
extern LPBYTE       glpInfoChunks;          /* DS:2638 */
extern int          gnVideoBuffers;         /* DS:26B8 */
extern LPWAVEHDR    galpwh[];               /* DS:2EBE [4] */
extern int          giWaveBuffer;           /* DS:2EE6 */
extern BOOL         gfUsingDOSMemory;       /* DS:2EEA */
extern DWORD        gdwAVIHdrPos;           /* DS:2EF2 */
extern HMMIO        ghmmio;                 /* DS:2EF6 */
extern BOOL         gfWaveMapped;           /* DS:2F12 */
extern BOOL         gfVariableFrames;       /* DS:2F16 */
extern DWORD        gdwActualMicroSecPerFrame;  /* DS:2F1A */
extern DWORD        gdwVideoChunks;         /* DS:2F20 */
extern time_t       gtimeCapture;           /* DS:2F42 */
extern DWORD        gcbInfoChunks;          /* DS:2FEA */
extern DWORD        gdwWaveBytes;           /* DS:3036 */
extern DWORD        gdwVideoBufferSize;     /* DS:303C */
extern HWAVEIN      ghWaveIn;               /* DS:3050 */
extern DWORD        gdwFramesDropped;       /* DS:3064 */
extern BOOL         gfAudioLost;            /* DS:3068 */
extern DWORD        gdwWaveBufferSize;      /* DS:306A */
extern DWORD        gdwWaveChunks;          /* DS:306E */

/* ------------------------------------------------------------------ */
/* seg1                                                               */
/* ------------------------------------------------------------------ */

/* misc.c */
int     FAR PASCAL GetEditInt(HWND hwnd, int *pfOK);
void    FAR PASCAL SetEditInt(HWND hwnd, int n);
void    FAR PASCAL EnableDlgItem(HWND hDlg, int id, BOOL fEnable);
void    FAR PASCAL SetDlgItemLong(HWND hDlg, int id, LONG l, BOOL fUnsigned);
char NEAR * FAR PASCAL GetProfileKeys(LPCSTR lpszFile, LPCSTR lpszSection);
char NEAR * FAR PASCAL StrDupLocal(char NEAR *psz);
DWORD   FAR PASCAL FrameRateToUSec(char NEAR *psz);
char NEAR * FAR PASCAL USecToFrameRate(char NEAR *psz, DWORD dwUSec);
LONG    FAR PASCAL ArrowEditChangeStep(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax, UINT wStep);
LONG    FAR PASCAL ArrowEditChangeStep2(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax, UINT wStepUp, UINT wStepDown);
int     FAR PASCAL DoDialog(int id, HWND hwndParent, FARPROC lpfn);
BOOL    FAR PASCAL IsSystemPalette(HPALETTE hpal);
int     FAR CDECL  ErrMsg(LPSTR lpszFormat, ...);
int     FAR CDECL  MsgBoxID(HWND hwnd, HINSTANCE hInst, UINT fuStyle, UINT idsTitle, UINT idsFormat, ...);

/* intro.c */
LRESULT FAR PASCAL IntroWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL IntroBoxDone(HWND hwnd);
BOOL    FAR IntroRegister(void);
HWND    FAR PASCAL IntroBox(HINSTANCE hInst, UINT idsTitle, UINT idsVersion, UINT idsCopyright, UINT idDIB);
void    FAR PASCAL AboutBox(HINSTANCE hInst, HWND hwndParent, UINT idsCaption, UINT idsTitle,
                            UINT idsVersion, UINT idsCopyright, UINT idDIB);

/* path.c */
int     FAR PASCAL SplitPath(LPSTR lpszPath, LPSTR lpszDrive, LPSTR lpszDir, LPSTR lpszName, LPSTR lpszExt);

/* arrow.c (seg1) and arrowini.c (seg4): the "ComArrow" spin control */
LONG    FAR PASCAL ArrowEditChange(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax);
void    FAR PASCAL _loadds ArrowTimerProc(HWND hwnd, UINT msg, UINT idTimer, DWORD dwTime);
void    FAR ArrowInvert(HWND hwnd, int iPart);
LRESULT FAR PASCAL ArrowControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL ArrowInit(HINSTANCE hInst);

/* vidcap.c */
int     PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow);
BOOL    FAR TabMessage(MSG msg);
LRESULT FAR InitMenuPopup(HWND hwnd, HMENU hmenu, int iMenu, BOOL fSystemMenu);
LRESULT FAR MenuCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* dosutil.c */
BOOL    FAR PASCAL DosFindFirst(LPVOID lpDTA, LPSTR lpszPath, UINT wAttr);
BOOL    FAR PASCAL DosFindNext(LPVOID lpDTA);
UINT    FAR PASCAL DosMkDir(LPSTR lpszDir);
UINT    FAR PASCAL DosRename(LPSTR lpszOld, LPSTR lpszNew);
UINT    FAR PASCAL DosDelete(LPSTR lpszFile);
void    FAR PASCAL DosExit(int nCode);
int     FAR PASCAL DosGetDrive(void);
int     FAR PASCAL DosGetCodePage(void);
int     FAR PASCAL DosSetDrive(int nDrive);
DWORD   FAR PASCAL DosDiskFreeSpace(int nDrive);
UINT    FAR PASCAL DosGetCurDir(LPSTR lpszDir);
int     FAR PASCAL GetFixedDrives(int NEAR *anDrives);
BOOL    FAR PASCAL IsRemovableDrive(int chDrive);
int     FAR PASCAL GetVolumeLabel(LPSTR lpsz);
UINT    FAR PASCAL DosChDir(LPSTR lpszDir);
BOOL    FAR PASCAL IsValidDir(LPSTR lpszDir);
UINT    FAR PASCAL XmsQueryFree(void);
UINT    FAR PASCAL ExtMemSize(void);
UINT    FAR PASCAL DosGetVersion(void);
void    FAR PASCAL Reboot(void);
LPSTR   FAR PASCAL DosGetEnvironment(void);
DWORD   FAR PASCAL XmsGetVersion(void);
UINT    FAR PASCAL CmosExtMem(void);

/* video.c */
void    FAR FreeCapturePalette(void);
int     FAR GetCapturePalette(void);
int     FAR InitCapturePalette(void);
void    FAR TermCapturePalette(void);
BOOL    FAR SetCapturePalette(HPALETTE hpal, LPVOID lpMap);
HPALETTE FAR CopyPalette(HPALETTE hpal);
void    FAR CapturePalette(void);
BOOL    FAR PASCAL CapPalDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR InitBitmapInfoHeader(LPBITMAPINFOHEADER lpbi);
DWORD   FAR AllocCaptureFormat(LPBITMAPINFOHEADER lpbi);
DWORD   FAR AllocFrameBuffer(LPBITMAPINFOHEADER lpbi);
int     FAR InitVideoFormat(void);
void    FAR TermVideoFormat(void);
DWORD   FAR SetDriverFormat(LPBITMAPINFOHEADER lpbi, DWORD cb);
DWORD   FAR SetCaptureFormat(LPBITMAPINFOHEADER lpbi);
int     FAR GetCaptureFormat(void);
void    FAR MapBits(BYTE _huge *lpBits, DWORD cb, BYTE FAR *lpMap);
BOOL    FAR SetFormatPalette(HPALETTE hpal);
void    FAR DrawFrame(HWND hwnd, HDC hdc);
HANDLE  FAR FrameToDIB(void);
BOOL    FAR HaveFrame(void);

/* file.c */
BOOL    FAR PASCAL WriteDIBFile(HMMIO hmmio);
BOOL    FAR PASCAL AllocCaptureFile(HWND hwnd, LPSTR lpszFile);
BOOL    FAR SetCaptureFile(void);
BOOL    FAR EditCapture(void);
BOOL    FAR LoadPalette(void);
BOOL    FAR SaveCaptureAs(void);
BOOL    FAR SavePalette(void);
BOOL    FAR SaveFrame(void);
UINT    FAR PASCAL _loadds SetFileDlgHook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* util.c */
WPARAM  FAR FlushKeys(BOOL fWait);
int     FAR DoDialogParam(int id, HWND hwndParent, FARPROC lpfn, LPARAM lParam);
BOOL    FAR PositionDialog(HWND hDlg, HWND hwnd);
LONG    FAR PASCAL ArrowEditChangeRate(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax, UINT wStep);

/* muldiv.c */
LONG    FAR PASCAL muldivt32(LONG a, LONG b, LONG c);
DWORD   FAR PASCAL muldiv32(DWORD a, DWORD b, DWORD c);
DWORD   FAR PASCAL muldivru32(DWORD a, DWORD b, DWORD c);
DWORD   FAR PASCAL muldivrd32(DWORD a, DWORD b, DWORD c);

/* ------------------------------------------------------------------ */
/* seg2                                                               */
/* ------------------------------------------------------------------ */

/* profile.c */
void    FAR PASCAL WriteProfileInt(LPCSTR lpszSection, LPCSTR lpszKey, int nDefault, int nValue);
void    FAR SaveSettings(void);
void    NEAR ReadSettings(void);

/* help.c */
DWORD   FAR PASCAL _loadds HelpMsgFilter(int nCode, WORD wParam, LPMSG lpMsg);
BOOL    FAR InitHelp(void);
void    FAR TermHelp(void);

/* init.c */
BOOL    FAR AppTerm(void);
BOOL    FAR CloseVideo(void);
BOOL    FAR OpenVideo(int nDevice);
BOOL    FAR PASCAL AppInit(HINSTANCE hInstance, HINSTANCE hPrevInstance, int nCmdShow, LPSTR lpszCmdLine);
LONG    FAR PASCAL CreateBars(HWND hwndParent);
BOOL    FAR PASCAL StatusInit(HINSTANCE hInst, HINSTANCE hPrev);

/* ------------------------------------------------------------------ */
/* seg3                                                               */
/* ------------------------------------------------------------------ */

/* frame.c */
void    FAR PASCAL SizeShowWindow(BOOL fKeepPos);
void    FAR PASCAL DrawFrameBorder(HWND hwnd, HDC hdc);
void    FAR PaintFrameBackground(HWND hwnd, HDC hdc);
BOOL    FAR PASCAL ScrollShow(int dx, int dy);
BOOL    FAR PASCAL StartAutoScroll(POINT pt);
void    FAR StopAutoScroll(void);
LRESULT FAR PASCAL EditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL ScrollPtToClient(LPPOINT lppt);
void    FAR PASCAL ScrollPtToImage(LPPOINT lppt);
void    FAR PASCAL ScrollRectToClient(LPRECT lprc);
void    FAR PASCAL ScrollRectToImage(LPRECT lprc);
BOOL    FAR PASCAL ShowWndInit(HINSTANCE hInst, HINSTANCE hPrev);

/* show.c */
HDC     FAR GetShowDC(void);
void    FAR SetOverlayRect(HWND hwnd);
void    FAR UpdateOverlay(HWND hwnd, HDC hdc, BOOL fForce);
LRESULT FAR PASCAL ShowWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ctrl.c */
void    FAR LayoutWindows(void);
void    FAR PASCAL ShowBars(BOOL fToolBar, BOOL fStatusBar);
LRESULT FAR PASCAL CtrlWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* toolbar.c */
BOOL    FAR PASCAL toolbarInit(HINSTANCE hInst, HINSTANCE hPrev);
void    FAR PASCAL toolbarSetBitmap(HWND hwnd, HINSTANCE hInst, int ibmp, POINT ptSize);
int     FAR PASCAL toolbarGetNumButtons(HWND hwnd);
int     FAR PASCAL toolbarButtonFromIndex(HWND hwnd, int iBtnPos);
int     FAR PASCAL toolbarIndexFromButton(HWND hwnd, int iButton);
int     FAR PASCAL toolbarPrevStateFromButton(HWND hwnd, int iButton);
int     FAR PASCAL toolbarActivityFromButton(HWND hwnd, int iButton);
int     FAR PASCAL toolbarIndexFromPoint(HWND hwnd, POINT pt);
BOOL    FAR PASCAL toolbarRectFromIndex(HWND hwnd, int iBtnPos, LPRECT lprc);
int     FAR PASCAL toolbarStateFromButton(HWND hwnd, int iButton);
int     FAR PASCAL toolbarFullStateFromButton(HWND hwnd, int iButton);
int     FAR PASCAL toolbarStringFromIndex(HWND hwnd, int iBtnPos);
int     FAR PASCAL toolbarTypeFromIndex(HWND hwnd, int iBtnPos);
BOOL    FAR PASCAL toolbarAddTool(HWND hwnd, TOOLBUTTON tb);
BOOL    FAR PASCAL toolbarRetrieveTool(HWND hwnd, int iButton, LPTOOLBUTTON lptb);
BOOL    FAR PASCAL toolbarRemoveTool(HWND hwnd, int iButton);
BOOL    FAR PASCAL toolbarModifyString(HWND hwnd, int iButton, int iString);
BOOL    FAR PASCAL toolbarModifyState(HWND hwnd, int iButton, int iState);
BOOL    FAR PASCAL toolbarModifyPrevState(HWND hwnd, int iButton, int iPrevState);
BOOL    FAR PASCAL toolbarModifyActivity(HWND hwnd, int iButton, int iActivity);
BOOL    FAR PASCAL toolbarFixFocus(HWND hwnd);
BOOL    FAR PASCAL toolbarExclusiveRadio(HWND hwnd, int iType, int iButton);
BOOL    FAR PASCAL toolbarMoveFocus(HWND hwnd, BOOL fBackward);
BOOL    FAR PASCAL toolbarSetFocus(HWND hwnd, int iButton);
HBITMAP FAR PASCAL LoadUIBitmap(HINSTANCE hInst, LPCSTR lpszName, COLORREF rgbText, COLORREF rgbFace,
                                COLORREF rgbShadow, COLORREF rgbHighlight, COLORREF rgbWindow,
                                COLORREF rgbFrame);
LRESULT FAR PASCAL toolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* seg5: dialogs.c                                                    */
/* ------------------------------------------------------------------ */

BOOL    FAR PASCAL FileSizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL CapSequenceDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT FAR PASCAL RLMeterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL LevelDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL InitErrDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL GetAudioFormatDlg(HWND hDlg);
BOOL    FAR PASCAL AudioFormatDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
LONG    FAR StrToLong(LPSTR lpsz);
void    FAR DispatchPending(HWND hDlg);
BOOL    FAR OpenLevelWaveIn(HWND hwnd);
int     FAR CloseLevelWaveIn(void);
void    FAR RecordLevel(HWND hwndParent);

/* ------------------------------------------------------------------ */
/* seg6: dibmap.c                                                     */
/* ------------------------------------------------------------------ */

LPHISTOGRAM FAR InitHistogram(LPHISTOGRAM lpHistogram);
void    FAR FreeHistogram(LPHISTOGRAM lpHistogram);
BOOL    FAR DibHistogram(LPBITMAPINFOHEADER lpbi, LPBYTE lpBits, int x, int y, int dx, int dy,
                         LPHISTOGRAM lpHistogram);
HANDLE  FAR DibReduce(LPBITMAPINFOHEADER lpbiIn, LPBYTE pbIn, HPALETTE hpal, LPBYTE lp16to8);
HPALETTE FAR HistogramPalette(LPHISTOGRAM lpHistogram, LPBYTE lp16to8, int nColors);

/* ------------------------------------------------------------------ */
/* seg7                                                               */
/* ------------------------------------------------------------------ */

/* capavi.c */
BOOL    FAR InitIndex(DWORD dwVideoBufferSize, DWORD dwWaveBufferSize);
void    FAR FiniIndex(void);
BOOL    FAR IndexVideo(DWORD dwSize, BOOL fKeyFrame);
BOOL    FAR IndexAudio(DWORD dwSize);
DWORD   FAR GetFreePhysicalMemory(void);
WORD    FAR PASCAL SmartDrv(char chDrive, WORD w);
HMMIO   FAR AVIFileInit(LPBITMAPINFOHEADER lpbi);
BOOL    FAR AVIFileFini(LPBITMAPINFOHEADER lpbi, HMMIO hmmio, BOOL fSound, BOOL fJunkChunkWritten);
int     FAR AVIAudioInit(HWND hwnd);
int     FAR AVIAudioPrepare(void);
int     FAR AVIAudioFini(void);
void    FAR AVIFini(void);
DWORD   FAR CalcWaveBufferSize(void);
int     FAR AVIInit(LPBITMAPINFOHEADER lpbi);
DWORD   FAR GetWaveInPosition(HWAVEIN hwi);
BOOL    FAR AVIWriteDummyFrames(int nCount);
BOOL    FAR AVIWriteVideoFrame(LPVIDEOHDR lpvh);
BOOL    FAR PASCAL SetInfoChunk(LPCAPINFOCHUNK lpcic);
void    FAR CaptureVideo(void);
BOOL    FAR WaveInMapped(HWAVEIN hwi);

/* status.c */
void    FAR PASCAL StatusText(LPCSTR lpsz);
LRESULT FAR PASCAL StatusWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT FAR PASCAL fnText(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* seg8                                                               */
/* ------------------------------------------------------------------ */

/* capframe.c */
void    FAR PASCAL MCIMsToSMPTE(DWORD dwMS, LPSTR lpsz);
BOOL    FAR CapFramesInit(LPBITMAPINFOHEADER lpbi);
void    FAR CapFramesFini(LPBITMAPINFOHEADER lpbi, BOOL fSound, BOOL fJunkChunkWritten);
void    FAR CaptureFrames(void);
BOOL    FAR PASCAL CapFrameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* mcicap.c */
UINT    FAR MCICountDevices(UINT wDeviceType);
void    FAR MCIDeviceClose(void);
BOOL    FAR MCIDeviceOpen(LPSTR lpszDevice);
BOOL    FAR PASCAL MCIDeviceGetPosition(LPDWORD lpdwPos);
BOOL    FAR PASCAL MCIDeviceSetPosition(DWORD dwPos);
BOOL    FAR MCIDevicePlay(void);
BOOL    FAR MCIDevicePause(void);
BOOL    FAR MCIDeviceStop(void);
BOOL    FAR PASCAL MCIDeviceStep(BOOL fForward);
void    FAR MCIDeviceFreeze(BOOL fFreeze);
BOOL    FAR MCIOpen(void);
void    FAR MCIClose(void);
void    FAR PASCAL MCIMsToTime(DWORD dwMS, LPSTR lpsz);
BOOL    FAR GetMCIDevice(HWND hDlg, int FAR *lpiSel, LPSTR lpszDevice);
BOOL    FAR PASCAL MCIDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR MCIStepCapture(void);
BOOL    FAR PASCAL MCIStepDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* average.c */
BOOL    FAR AverageFrameInit(LPAVERAGE NEAR *plpAvg, LPBITMAPINFOHEADER lpbi, HPALETTE hpal);
BOOL    FAR AverageFrameFini(LPAVERAGE lpAvg);
BOOL    FAR AverageFrameStart(LPAVERAGE lpAvg);
BOOL    FAR AverageFrameAdd(LPAVERAGE lpAvg, BYTE _huge *pb);
BOOL    FAR AverageFrameEnd(LPAVERAGE lpAvg, BYTE _huge *pb);
BOOL    FAR AverageShrink2x(LPAVERAGE lpAvg, LPBITMAPINFOHEADER lpbiSrc, BYTE _huge *pbSrc,
                            LPBITMAPINFOHEADER lpbiDst, BYTE _huge *pbDst);

#endif /* RC_INVOKED */

#endif /* _VIDCAP_H_ */
