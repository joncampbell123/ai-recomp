/*
 * capscrn.h - shared declarations for the reconstructed CAPSCRN.EXE
 * (CapScrn, "AVI Screen Capture", Video for Windows 1.1, Win16 NE
 * application, MD5 51abd4aaf2881136e3906ff99c1c70fa).
 *
 * Reconstructed from the binary.  Offsets in comments are:
 *   segN:xxxx  code segment N, offset xxxx  (seg1..seg4)
 *   DS:xxxx    automatic data segment (seg5) offset
 *
 * Built with a Microsoft C 7 / Visual C++ 1.x class compiler (LINK 5.50,
 * 386 code: ENTER/LEAVE, o32 pushes, inline 8087 code with WIN87EM),
 * medium model: far code, near data.  Unless a prototype says otherwise,
 * functions are FAR _cdecl.  Exported callbacks have no DS-loading
 * prologue; they rely on DS being DGROUP when they are entered.
 */

#ifndef _CAPSCRN_H_
#define _CAPSCRN_H_

#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <mmreg.h>
#include <msacm.h>
#include <avicap.h>

/* string table */
#define IDS_ERR_REGCLASS        1       /* "Error registering window class." */
#define IDS_ERR_CREATEWINDOW    2       /* "Window creation failed!" */
#define IDS_ERR_NODRIVER        3       /* "Unable to load the capture driver..." */
#define IDS_ERR_CANTINCREMENT   4       /* "Cannot increment capture filename." */
#define IDS_ERR_CAPSETUP        5       /* "Error setting up for capture." */
#define IDS_ERR_CAPDISKFULL     6       /* "Error during capture, disk may be full." */
#define IDS_ERR_MAXFRAMES       7       /* "Maximum frame count exceeded." */
#define IDS_ERR_NOMEM           8       /* "Out of memory." */
#define IDS_ERR_NOEDITOR        9       /* "Unable to start video editor." */
#define IDS_ERR_RUNNING         10      /* "Capscrn is already running." */
#define IDS_OVERWRITE           11      /* "Overwrite file %s?" */
#define IDS_SETFILE_TITLE       100     /* "Set Capture File" */
#define IDS_DEFFILE             101     /* "c:\cap0000.avi" */
#define IDS_FILTER              102     /* "Microsoft AVI!*.avi!All Files!*.*!!" */
#define IDS_DEFEXT              103     /* "AVI" */
#define IDS_DEFAUDIOFORMAT      104     /* "11 kHz. 8 bit, Mono" */
#define IDS_INTRO_NAME          150     /* "CapScrn" */
#define IDS_INTRO_COPYRIGHT     151
#define IDS_INTRO_VERSION       152     /* "1.1" */
#define IDS_INTRO_STOPKEY       153     /* "To stop capture, press:" */
#define IDS_INTRO_CTRL          154     /* "Ctrl + " */
#define IDS_INTRO_SHIFT         155     /* "Shift + " */
#define IDS_MENU_SETFILE        200
#define IDS_MENU_SETWINDOW      201
#define IDS_MENU_PREFERENCES    202
#define IDS_MENU_CAPTURE        203
#define IDS_MENU_COPYFRAME      204
#define IDS_MENU_EDIT           205
#define IDS_MENU_HELP           206
#define IDS_CAPSTOPPED          210     /* "Capture Stopped" */
#define IDS_PALCHANGE           211     /* "A palette change occurred so..." */
#define IDS_CLOSEFIRST          212     /* "Close the application before exiting Windows." */

/* commands, all on the system menu */
#define IDM_SETFILE             1050
#define IDM_EDIT                1350
#define IDM_SETWINDOW           1400
#define IDM_PREFERENCES         1450
#define IDM_COPYFRAME           1480
#define IDM_CAPTURE             1500
#define IDM_HELP                1600

/* "Preferences" dialog (the original's CONFIG.H) */
#define IDD_PREFERENCES         600
#define IDC_FRAMERATE           601
#define IDC_FRAMERATEARROW      605
#define IDC_AUDIO               610
#define IDC_AUDIOCHANGE         611
#define IDC_AUDIOFORMAT         612
#define IDC_AUDIOGROUP          613
#define IDC_AUDIOFORMATLABEL    614
#define IDC_STOPKEYGROUP        101
#define IDC_HOTCTRL             640
#define IDC_HOTSHIFT            641
#define IDC_HOTKEY              642
#define IDC_INCFILE             690
#define IDC_ONTOP               691

/* help contexts set while a dialog is up */
#define HELP_SETFILE            500
#define HELP_PREFERENCES        600
#define HELP_SETWINDOW          700
#define HELP_AUDIOFORMAT        800

#define WM_USER_PALCHANGE       (WM_USER + 1)

#define IDB_INTRO               151     /* "DIB" resource */

#ifndef RC_INVOKED

/* ------------------------------------------------------------------ */
/* DGROUP (seg5, 0x128E bytes; heap 0x1000, stack 0x1800)              */
/* ------------------------------------------------------------------ */

/* initialised, capscrn.c */
extern int          gnFPS;              /* DS:0010 = 2, 1..20 */
extern BOOL         gfAudio;            /* DS:0012 = 1 */
extern UINT         gwHotKey;           /* DS:0014 = VK_ESCAPE */
extern BOOL         gfHotCtrl;          /* DS:0016 */
extern BOOL         gfHotShift;         /* DS:0018 */
extern BOOL         gfIncFile;          /* DS:001A = 1 */
extern BOOL         gfOnTop;            /* DS:001C = 1 */
extern BOOL         gfDriverOK;         /* DS:001E */
extern BOOL         gfCapturing;        /* DS:0020 */
                                        /* DS:0022-0161 strings and tables, see capscrn.c, arrow.c, intro.c */
/* uninitialised (in the original the linker placed these after the C runtime data) */
extern HACCEL       ghAccel;            /* DS:07F0 */
extern char         gszFNameTmp[256];   /* DS:07F2 */
extern char         gszAppName[20];     /* DS:08F4 "CAPSCRN" */
extern char         gszDrive[4];        /* DS:0908 */
extern HMENU        ghmenuSys;          /* DS:092C */
extern char         gszFileTitle[260];  /* DS:092E name.ext of the capture file */
extern char         gszCaptureFile[260];/* DS:0A32 */
extern char         gszHelpFile[260];   /* DS:0B36 */
extern DWORD        gdwACMVersion;      /* DS:0C3C */
extern BOOL         gfACM;              /* DS:0C40 ACM 2.0 or later */
extern char         gachFPS[256];       /* DS:0C42 frame rate as typed */
extern HWND         ghwndApp;           /* DS:0D42 */
extern char         gszDefFile[260];    /* DS:0D44 */
extern int          gidHelp;            /* DS:0E48 */
extern char         gszLastCapture[260];/* DS:0E4A */
extern HINSTANCE    ghInst;             /* DS:0F4E */
extern char         gszExt[256];        /* DS:0F50 */
extern char         gszDir[256];        /* DS:1050 */
extern CAPSTATUS    gCapStatus;         /* DS:1150 */
extern char         gszFName[256];      /* DS:1188 */
extern DWORD        gdwUSecPerFrame;    /* DS:1288 */
extern HWND         ghwndCap;           /* DS:128C */

/* ------------------------------------------------------------------ */
/* capscrn.c (seg1)                                                    */
/* ------------------------------------------------------------------ */

int     PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow);
LRESULT FAR PASCAL WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
DWORD   FAR PASCAL HelpMsgFilter(int nCode, WORD wParam, LPMSG lpMsg);
BOOL    FAR PASCAL PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* arrow.c (seg2), the "ComArrow" spin control                         */
/* ------------------------------------------------------------------ */

BOOL    FAR PASCAL ArrowInit(HINSTANCE hInst);
LONG    FAR PASCAL ArrowEditChange(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax);
LRESULT FAR PASCAL ArrowControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* intro.c (seg3), the start-up box                                    */
/* ------------------------------------------------------------------ */

HWND    FAR PASCAL IntroBox(HINSTANCE hInst, UINT idsTitle, UINT idsVersion, UINT idsCopyright, UINT idDIB);
BOOL    FAR PASCAL IntroBoxDone(HWND hwnd);
LRESULT FAR PASCAL IntroWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif /* RC_INVOKED */

#endif /* _CAPSCRN_H_ */
