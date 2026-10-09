/*
 * vedwnd.h - types and prototypes of the VidEdit user interface (code
 * segment 5): main window and status bar (vedwnd.c), dialog helpers
 * (dlgutil.c), intro box (intro.c), SplitPath (splitpth.c), spin arrows
 * (arrow.c), the control panel (grayblob.c), tool bars (toolbar.c), the
 * mark panel and Set Selection (selpanel.c), the frame number box
 * (framebox.c) and the trackbar (trackbar.c).
 */

#ifndef _VEDWND_H_
#define _VEDWND_H_

/* string resources used here */
#define IDS_NOLIMIT             175     /* "(No limit)" */
#define IDS_INTRO_WINDOWS       107     /* "for Microsoft Windows 3.1" */
#define IDS_INTRO_LINE2         108     /* (not in the string table) */
#define IDS_UNDO_RESIZE         311     /* "&Undo Resize\tCtrl+Z" */
#define IDS_MENU_FILE_HELP      890     /* help lines of the top level menus, 890.. */
#define IDS_MENU_TRACK_HELP     895     /* "Set tracks for editing" */
#define IDS_MENU_ZOOM_HELP      897     /* "Change zoom factor" */
#define IDS_SYSMENU_HELP        899     /* "Move, size, or close the application window" */
#define IDS_AUDIO               1065
#define IDS_VIDEO               1066
#define IDS_AUDIOVIDEO          1067
#define IDS_INS                 1068
#define IDS_OVR                 1069

/* Frame Information child dialog (901) */
#define IDC_FI_BITMAP           1020
#define IDC_FI_PALETTE          1021
#define IDC_FI_WAVE             1022
#define IDC_FI_TOTAL            1024
#define IDC_FI_OPTIMAL          1025

/* Resize Video dialog (107) */
#define IDC_RS_WIDTH            1060
#define IDC_RS_HEIGHT           1061
#define IDC_RS_DEFAULT          1062
#define IDC_RS_WIDTHARROW       1064
#define IDC_RS_HEIGHTARROW      1065

/* AnimSTrackBar messages (WM_USER based, like the later common control) */
#define TBM_GETPOS              (WM_USER + 0)
#define TBM_GETRANGEMIN         (WM_USER + 1)
#define TBM_GETRANGEMAX         (WM_USER + 2)
#define TBM_GETTICS             (WM_USER + 3)   /* the local block of tick positions */
#define TBM_SETTIC              (WM_USER + 4)
#define TBM_SETPOS              (WM_USER + 5)   /* wParam: redraw */
#define TBM_SETRANGEMIN         (WM_USER + 6)
#define TBM_SETRANGEMAX         (WM_USER + 7)
#define TBM_ERASETHUMB          (WM_USER + 8)   /* invalidates the thumb */
#define TBM_CLEARTICS           (WM_USER + 9)

/* aviframebox: set the largest frame number */
#define FBM_SETMAX              (WM_USER + 100)

/* ------------------------------------------------------------------ */
/* toolbar.c: "ToolBarClass"                                           */
/* ------------------------------------------------------------------ */

typedef struct tagTOOLBUTTON {
    RECT    rc;                 /* 00 position in the tool bar */
    int     iButton;            /* 08 button ID, also the column in the bitmap */
    int     iState;             /* 0A BTNST_* (the row in the bitmap) */
    int     iPrevState;         /* 0C state before a check box or radio button went down */
    int     iType;              /* 0E TBTYPE_*; 3 and up: radio groups */
    int     iActivity;          /* 10 TBACT_*: what happened last */
    int     iString;            /* 12 */
} TOOLBUTTON, FAR *LPTOOLBUTTON;    /* 0x14 bytes */

#define BTNST_GRAYED            0
#define BTNST_UP                1
#define BTNST_DOWN              2
#define BTNST_FOCUSUP           3
#define BTNST_FOCUSDOWN         4
#define BTNST_FULLDOWN          5

#define TBTYPE_BUTTON           0
#define TBTYPE_CHECKBOX         1
#define TBTYPE_GRAYPUSH         2       /* can be pressed while grayed */
#define TBTYPE_RADIO            3

#define TBACT_MOUSEDOWN         0
#define TBACT_MOUSEUP           1
#define TBACT_MOUSEOFF          2
#define TBACT_MOUSEON           3
#define TBACT_KEYDOWN           5
#define TBACT_KEYUP             6

#define TB_FIRST                (-1)
#define TB_LAST                 (-2)

/* toolbars post WM_COMMAND with this ID and lParam MAKELONG(hwnd, code):
 * code = button | 0x100 repeat | 0x200 right button | 0x400 double click */
#define IDC_TOOLBAR             0xBD

/* window words */
#define GWW_TB_BUTTONS          0       /* HGLOBAL TOOLBUTTON array, grown 8 at a time */
#define GWW_TB_COUNT            2
#define GWW_TB_TRACKING         4       /* mouse or key held on a button */
#define GWW_TB_KEYDOWN          6
#define GWW_TB_FOCUS            8       /* focus index */
#define GWW_TB_RIGHT            0x0A    /* right button or Shift */
#define GWW_TB_BITMAP           0x0C
#define GWW_TB_BITMAPID         0x0E
#define GWL_TB_BUTTONSIZE       0x10    /* MAKELONG(cy, cx) of one button image */
#define GWW_TB_HINST            0x14

BOOL FAR PASCAL toolbarInit(HINSTANCE hInst, HINSTANCE hPrev);
BOOL FAR PASCAL toolbarSetBitmap(HWND hwnd, HINSTANCE hInst, int ibmp, POINT ptSize);
int  FAR PASCAL toolbarGetNumButtons(HWND hwnd);
int  FAR PASCAL toolbarButtonFromIndex(HWND hwnd, int iBtnPos);
int  FAR PASCAL toolbarIndexFromButton(HWND hwnd, int iButton);
int  FAR PASCAL toolbarPrevStateFromButton(HWND hwnd, int iButton);
int  FAR PASCAL toolbarActivityFromButton(HWND hwnd, int iButton);
int  FAR PASCAL toolbarIndexFromPoint(HWND hwnd, POINT pt);
BOOL FAR PASCAL toolbarRectFromIndex(HWND hwnd, int iBtnPos, LPRECT lprc);
int  FAR PASCAL toolbarStateFromButton(HWND hwnd, int iButton);
int  FAR PASCAL toolbarFullStateFromButton(HWND hwnd, int iButton);
int  FAR PASCAL toolbarStringFromIndex(HWND hwnd, int iBtnPos);
int  FAR PASCAL toolbarTypeFromIndex(HWND hwnd, int iBtnPos);
BOOL FAR PASCAL toolbarAddTool(HWND hwnd, TOOLBUTTON tb);
BOOL FAR PASCAL toolbarRetrieveTool(HWND hwnd, int iButton, LPTOOLBUTTON lptb);
BOOL FAR PASCAL toolbarRemoveTool(HWND hwnd, int iButton);
BOOL FAR PASCAL toolbarModifyString(HWND hwnd, int iButton, int iString);
BOOL FAR PASCAL toolbarModifyState(HWND hwnd, int iButton, int iState);
BOOL FAR PASCAL toolbarModifyPrevState(HWND hwnd, int iButton, int iPrevState);
BOOL FAR PASCAL toolbarModifyActivity(HWND hwnd, int iButton, int iActivity);
BOOL FAR PASCAL toolbarFixFocus(HWND hwnd);
BOOL FAR PASCAL toolbarExclusiveRadio(HWND hwnd, int iType, int iButton);
BOOL FAR PASCAL toolbarMoveFocus(HWND hwnd, BOOL fBackward);
BOOL FAR PASCAL toolbarSetFocus(HWND hwnd, int iButton);
HBITMAP FAR PASCAL LoadUIBitmap(HINSTANCE hInst, LPCSTR szName, COLORREF rgbText,
                                COLORREF rgbFace, COLORREF rgbShadow, COLORREF rgbHighlight,
                                COLORREF rgbWindow, COLORREF rgbFrame);
LRESULT FAR PASCAL toolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* the names AppLayout etc. use */
#define toolbarGetButton(hwnd, iButton, lptb)   toolbarRetrieveTool(hwnd, iButton, lptb)
#define toolbarRemoveButton(hwnd, iButton)      toolbarRemoveTool(hwnd, iButton)
#define toolbarAddButton(hwnd, tb)              toolbarAddTool(hwnd, tb)

/* ------------------------------------------------------------------ */
/* vedwnd.c                                                            */
/* ------------------------------------------------------------------ */

extern HWND     ghwndBlob;              /* DS:01B8 (init.c) */
extern HWND     ghwndMarkBar;           /* DS:2D1C */
extern int      gcxMarkBar;             /* DS:306A */
extern int      gcyMarkBar;             /* DS:31BC */
extern int      gcyPanelBar;            /* DS:31E4 */
extern int      gcyTrackBar;            /* DS:31FC */
extern HWND     ghwndMarkOutLabel;           /* DS:31D6 */
extern HWND     ghwndMarkOutValue;         /* DS:2D4E */

void    FAR StatusUpdatePosition(void);
void    FAR PASCAL StatusSetMessage(LPCSTR lpsz);
LRESULT FAR PASCAL StatusWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT FAR PASCAL STextWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR AppSizeToFit(void);
void    FAR AppLayout(void);
void    FAR PASCAL AppShowBars(BOOL fToolBar, BOOL fStatusBar);
void    FAR UpdatePlayButtons(void);
void    FAR PASCAL SetLength(WORD wFrames, BOOL fUnused);
void    FAR UpdateLength(void);
void    FAR PASCAL SeekTo(WORD wFrame);
void    FAR PASCAL SetPosition(WORD wFrame);
LRESULT FAR PASCAL VidEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL GetFrameSizes(WORD wFrame, LPDWORD lpdwBitmap, LPDWORD lpdwWave,
                                 LPDWORD lpdwOther, LPDWORD lpdwPalette);
void    FAR FrameInfoUpdate(void);
BOOL    FAR PASCAL FrameInfoDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL SetFrameSize(WORD cx, WORD cy);
void    FAR PASCAL SetZoom(WORD wZoom);
void    FAR ResizeVideo(void);
BOOL    FAR PASCAL ResizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* dlgutil.c                                                           */
/* ------------------------------------------------------------------ */

int     FAR PASCAL EditGetInt(HWND hwnd, BOOL NEAR *pfOk);
void    FAR PASCAL EditSetInt(HWND hwnd, int n);
void    FAR PASCAL EnableDlgItem(HWND hDlg, int id, BOOL fEnable);
void    FAR PASCAL SetDlgItemLong(HWND hDlg, int id, DWORD dw, BOOL fUnsigned);
PSTR    FAR PASCAL GetProfileKeys(LPCSTR lpszFile, LPCSTR lpszSection);
PSTR    FAR PASCAL LocalStrDup(PSTR psz);
DWORD   FAR PASCAL FrameRateToUSec(PSTR psz);
PSTR    FAR PASCAL FormatFrameRate(PSTR psz, DWORD dwUSec);
LONG    FAR PASCAL ArrowEditStep(HWND hwndEdit, UINT code, LONG lMin, LONG lMax, WORD wStep);
LONG    FAR PASCAL ArrowEditStep2(HWND hwndEdit, UINT code, LONG lMin, LONG lMax,
                                  WORD wStepUp, WORD wStepDown);
int     FAR PASCAL DoDialog(WORD id, HWND hwnd, FARPROC lpfn);
BOOL    FAR PASCAL PaletteHasStaticColors(HPALETTE hpal);
int     FAR _cdecl ErrorResBox(HWND hwnd, HINSTANCE hInst, UINT flags, UINT idAppName,
                               UINT idErrorStr, ...);

/* ------------------------------------------------------------------ */
/* intro.c, splitpth.c, arrow.c, grayblob.c                            */
/* ------------------------------------------------------------------ */

LRESULT FAR PASCAL IntroWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL IntroBoxDone(HWND hwnd);
BOOL    FAR IntroRegister(void);
HWND    FAR PASCAL IntroBox(HINSTANCE hInst, UINT idsTitle, UINT idsVersion,
                            UINT idsCopyright, UINT idDIB);
void    FAR PASCAL AboutBox(HINSTANCE hInst, HWND hwndOwner, UINT idsCaption, UINT idsTitle,
                            UINT idsVersion, UINT idsCopyright, UINT idDIB);

int     FAR PASCAL SplitPath(LPSTR lpszPath, LPSTR lpszDrive, LPSTR lpszDir, LPSTR lpszName,
                             LPSTR lpszExt);

LONG    FAR PASCAL ArrowEditChange(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax);
UINT    FAR PASCAL ArrowTimerProc(HWND hwnd, UINT msg, UINT idTimer, DWORD dwTime);
void    FAR ArrowInvert(HWND hwnd, int iPart);
LRESULT FAR PASCAL ArrowControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern BOOL gfScrubSeek;                /* DS:0BA0 seek while dragging the trackbar */

BOOL    FAR PASCAL GrayBlobInit(HINSTANCE hInst, HINSTANCE hPrev);
LRESULT FAR PASCAL GrayBlobWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* ctrlbars.c, framebox.c, trackbar.c                                  */
/* ------------------------------------------------------------------ */

extern WORD     gwSelStart;             /* DS:11A8 -1 when unset */
extern WORD     gwSelEnd;               /* DS:11AA */
extern HWND     ghwndMarkInLabel;       /* DS:2D0E */
extern HWND     ghwndMarkInValue;       /* DS:3266 */
extern HWND     ghwndMarkOutLabel;      /* DS:31D6 */
extern HWND     ghwndMarkOutValue;      /* DS:2D4E */
extern HWND     ghwndStatTrack;         /* DS:31D4 */
extern HWND     ghwndStatMode;          /* DS:318A */
extern int      gcyCtrlBars;            /* DS:305E */

int     FAR PASCAL SetSelectionDialog(HWND hwnd);
BOOL    FAR PASCAL SetSelectionDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL SetSelection(WORD wStart, WORD wEnd);
void    FAR PASCAL GetSelection(LPWORD lpwStart, LPWORD lpwEnd, BOOL fDefault);
int     FAR CreateControlBars(void);
void    FAR DeleteControlBarFonts(void);
void    FAR PASCAL TransportCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL TracksCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL MarkCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL MarkPanelPaint(HWND hwnd);

BOOL    FAR PASCAL FrameBoxInit(HINSTANCE hInst, HINSTANCE hPrev);
int     FAR PASCAL FormatFrameTime(WORD wFrame, LPSTR lpsz);
LONG    FAR PASCAL FrameBoxSetText(HWND hwndEdit, LPSTR lpsz);
LRESULT FAR PASCAL FrameBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

BOOL    FAR PASCAL trackbarInit(HINSTANCE hInst, HINSTANCE hPrev);
LRESULT FAR PASCAL trackbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif /* _VEDWND_H_ */
