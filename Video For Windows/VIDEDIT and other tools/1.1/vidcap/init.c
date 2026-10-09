/*
 * init.c - code segment 2 of VIDCAP.EXE, 0954-1613: start-up and shut-down,
 * opening the capture driver, and creating the toolbar and status bar.
 *
 * Command line: -n (no start-up box), -d<n> or -d=<n> (capture driver n).
 */

#include <windows.h>
#include <mmsystem.h>
#include "vidcap.h"

static char gszHelv[]       = "Helv";               /* DS:0168 */
char        gszRLMeterClass[] = "VCRLMeter";        /* DS:016D */
static char gszFileNameOK[] = "commdlg_FileNameOK"; /* DS:0177 */
static char gszCaptionSep[] = " - ";                /* DS:018A */

/* toolbar buttons, DS:02F2-0347 */
BOOL        gfToolBar   = TRUE;                     /* DS:02F2 */
BOOL        gfStatusBar = TRUE;                     /* DS:02F4 */
HWND        ghwndApp    = NULL;                     /* DS:02F6 */
static int  gaiBtnButton[8] = { BTN_SETFILE, BTN_EDIT, BTN_PREVIEW, BTN_OVERLAY,
                                BTN_SINGLE, BTN_FRAMES, BTN_VIDEO, BTN_PALETTE };   /* DS:02F8 */
static int  gaiBtnState[8]  = { BTNST_FOCUSUP, BTNST_UP, BTNST_UP, BTNST_UP,
                                BTNST_UP, BTNST_UP, BTNST_UP, BTNST_UP };           /* DS:0308 */
static int  gaiBtnType[8]   = { BTNTYPE_PUSH, BTNTYPE_PUSH, BTNTYPE_CHECKBOX, BTNTYPE_CHECKBOX,
                                BTNTYPE_PUSH, BTNTYPE_PUSH, BTNTYPE_PUSH, BTNTYPE_PUSH };  /* DS:0318 */
static int  gaiBtnString[8] = { IDS_TB_SETFILE, IDS_TB_EDIT, IDS_TB_PREVIEW, IDS_TB_OVERLAY,
                                IDS_TB_SINGLE, IDS_TB_FRAMES, IDS_TB_VIDEO, IDS_TB_PALETTE };  /* DS:0328 */
static int  gaiBtnX[8]      = { 10, 35, 75, 100, 150, 175, 200, 225 };            /* DS:0338 */

HBRUSH      ghbrBtnHighlight;           /* DS:1550 */
HBRUSH      ghbrBtnShadow;              /* DS:1556 */
HBRUSH      ghbrBtnText;                /* DS:1558 */
COLORREF    grgbBtnShadow;              /* DS:153E */
int         gcxVScroll;                 /* DS:1532 */
int         gcyHScroll;                 /* DS:1536 */
UINT        gwmFileNameOK;              /* DS:153C */
RECT        grcApp;                     /* DS:15F0 main window, saved */
int         gcyScreen;                  /* DS:25A8 */
BOOL        gfInitializing;             /* DS:25AA */
CHANNEL_CAPS gChannelCaps;              /* DS:25AC */
int         gnDevice;                   /* DS:25D0 */
COLORREF    grgbBtnFace;                /* DS:25FE */
int         gcxClient;                  /* DS:2606 */
HBRUSH      ghbrBtnFace;                /* DS:2ECE */
BOOL        gfWin31;                    /* DS:2ED4 */
HFONT       ghfontApp;                  /* DS:2EEC */
BOOL        gfAudioDevice;              /* DS:2EEE */
RECT        grcCtrl;                    /* DS:2F04 main window position from the profile */
COLORREF    grgbBtnText;                /* DS:2F0C */
int         gcyStatus;                  /* DS:2F14 status bar height */
int         gcyBorder;                  /* DS:2F2E */
int         gcxBorder;                  /* DS:2F32 */
BOOL        gfYield;                    /* DS:2F34 */
HWND        ghwndToolbar;               /* DS:2F38 */
BOOL        gfHasOverlay;               /* DS:2F46 */
BOOL        gfVideoDevice;              /* DS:2FE8 */
HBRUSH      ghbrUnused;                 /* DS:3016 deleted, never created */
HWND        ghwndStatusBar;             /* DS:303A */
COLORREF    grgbBtnHighlight;           /* DS:3040 */
int         gcxScreen;                  /* DS:3048 */
int         gcxStatusField;             /* DS:3052 */
DWORD       gdwWinFlags;                /* DS:3056 */
int         gcyClient;                  /* DS:305E */

/*
 * seg2:0954-0A39  AppTerm
 */
BOOL FAR AppTerm(void)
{
    FARPROC lpfn;

    if (gfInitDone)
        SaveSettings();
    TermHelp();
    if (ghdcUnused)
        DeleteDC(ghdcUnused);
    if (ghfontApp && GetStockObject(SYSTEM_FONT) != ghfontApp)
        DeleteObject(ghfontApp);
    if (ghbrBtnText)
        DeleteObject(ghbrBtnText);
    if (ghbrBtnFace)
        DeleteObject(ghbrBtnFace);
    if (ghbrBtnShadow)
        DeleteObject(ghbrBtnShadow);
    if (ghbrBtnHighlight)
        DeleteObject(ghbrBtnHighlight);
    if (ghbrUnused)
        DeleteObject(ghbrUnused);
    ICCompressorFree(&gCompVars);
    if (gfVideoDevice)
        CloseVideo();
    TermVideoFormat();
    TermCapturePalette();

    lpfn = GetProcAddress((HINSTANCE)GetSystemMetrics(SM_PENWINDOWS), gszRegisterPenApp);
    if (lpfn)
        (*(void (FAR PASCAL *)(WORD, BOOL))lpfn)(1, FALSE);
    return TRUE;
}

/*
 * seg2:0A3A-0AA3  ParseNumber  (NEAR PASCAL)
 *
 * A decimal number after an optional '=', up to a space.
 *
 * Quirk of the original: the digit test is "c >= '0' || c <= '9'", so
 * every character is taken as a digit.
 */
static LONG NEAR PASCAL ParseNumber(LPSTR lpsz)
{
    LONG l;

    if (*lpsz == '=')
        lpsz++;
    l = 0;
    while (*lpsz && *lpsz != ' ') {
        if (*lpsz >= '0' || *lpsz <= '9')
            l = l * 10 + (*lpsz - '0');
        lpsz = AnsiNext(lpsz);
    }
    return l;
}

/*
 * seg2:0AA6-0B0D  CloseVideo
 */
BOOL FAR CloseVideo(void)
{
    if (ghVideoExtIn) {
        videoStreamFini(ghVideoExtIn);
        videoClose(ghVideoExtIn);
        ghVideoExtIn = NULL;
    }
    if (ghVideoExtOut) {
        videoStreamFini(ghVideoExtOut);
        videoClose(ghVideoExtOut);
        ghVideoExtOut = NULL;
    }
    if (ghVideoIn) {
        videoClose(ghVideoIn);
        ghVideoIn = NULL;
    }
    return TRUE;
}

/*
 * seg2:0B0E-0D61  OpenVideo
 *
 * Opens the capture driver's channels.  Without a driver the capture
 * commands are disabled and the Initialization Error dialog is shown
 * (Exit closes VidCap).  Always returns TRUE.
 *
 * Quirks of the original: the audio menu items depend on wave *output*
 * devices, and capture is turned off on a 286 (unless WF_WINNT).
 */
BOOL FAR OpenVideo(int nDevice)
{
    char    ach[128];
    HMENU   hmenu;
    DWORD   dwErr;
    BOOL    f;

    ghVideoExtIn = NULL;
    ghVideoExtOut = NULL;
    ghVideoIn = NULL;
    gfVideoDevice = FALSE;

    dwErr = videoOpen(&ghVideoIn, (DWORD)nDevice, VIDEO_IN);
    if (dwErr == 0) {
        dwErr = videoOpen(&ghVideoExtIn, (DWORD)nDevice, VIDEO_EXTERNALIN);
        if (dwErr == 0) {
            videoOpen(&ghVideoExtOut, (DWORD)nDevice, VIDEO_EXTERNALOUT);
            if (dwErr == 0 && ghVideoExtIn && ghVideoIn)
                gfVideoDevice = TRUE;
        }
    }

    if (dwErr) {
        videoGetErrorText(ghVideoIn, (UINT)dwErr, ach, sizeof(ach));
        StatusText(ach);
    } else {
        ach[0] = '\0';
    }

    if (!gfVideoDevice) {
        CloseVideo();
    } else {
        if (ghVideoExtOut &&
            videoGetChannelCaps(ghVideoExtOut, &gChannelCaps, sizeof(CHANNEL_CAPS)) == DV_ERR_OK)
            gfHasOverlay = (int)(gChannelCaps.dwFlags & VCAPS_OVERLAY);
        else
            gfHasOverlay = FALSE;
        if (!gfHasOverlay)
            gfOverlay = FALSE;
        videoStreamInit(ghVideoExtIn, 0L, 0L, 0L, 0L);
    }

    gfAudioDevice = (waveOutGetNumDevs() != 0);

    if (!(gdwWinFlags & 0x4000) && (gdwWinFlags & WF_CPU286))
        gfVideoDevice = FALSE;

    if (!gfVideoDevice) {
        toolbarModifyState(ghwndToolbar, BTN_SINGLE, BTNST_GRAYED);
        toolbarModifyState(ghwndToolbar, BTN_FRAMES, BTNST_GRAYED);
        toolbarModifyState(ghwndToolbar, BTN_VIDEO, BTNST_GRAYED);
        toolbarModifyState(ghwndToolbar, BTN_PALETTE, BTNST_GRAYED);
        toolbarModifyState(ghwndToolbar, BTN_PREVIEW, BTNST_GRAYED);
        gfPreview = FALSE;
        gfOverlay = FALSE;
        InvalidateRect(ghwndApp, NULL, FALSE);
        UpdateWindow(ghwndApp);
        hmenu = GetMenu(ghwndApp);
        EnableMenuItem(hmenu, 3, MF_BYPOSITION | MF_GRAYED);
        if (!gfAudioDevice)
            EnableMenuItem(hmenu, 2, MF_BYPOSITION | MF_GRAYED);
        DrawMenuBar(ghwndApp);
        f = DoDialogParam(IDD_INITERROR, ghwndApp, (FARPROC)InitErrDlgProc, (LPARAM)(LPSTR)ach);
        StatusText(NULL);
        InvalidateRect(ghwndApp, NULL, FALSE);
        if (!f)
            PostMessage(ghwndApp, WM_COMMAND, IDM_EXIT, 0L);
    }

    if (!gfVideoDevice || !gfHasOverlay)
        toolbarModifyState(ghwndToolbar, BTN_OVERLAY, BTNST_GRAYED);

    ghcurWait = LoadCursor(NULL, IDC_WAIT);
    return TRUE;
}

/*
 * seg2:0D62-143D  AppInit
 *
 * Shows the start-up box (unless -n or another instance is running),
 * reads the settings, registers the window classes, makes the main
 * window (which makes the toolbar and status bar) and the frame window,
 * checks the capture file, opens the driver and turns preview or overlay
 * back on as they were.
 *
 * Quirk of the original: a command line ending in "-" or "/" makes it
 * loop for ever.
 */
BOOL FAR PASCAL AppInit(HINSTANCE hInstance, HINSTANCE hPrevInstance, int nCmdShow, LPSTR lpszCmdLine)
{
    char        achCaption[256];
    TEXTMETRIC  tm;
    WNDCLASS    wc;
    MMCKINFO    ck;
    RECT        rc;
    FARPROC     lpfn;
    HDC         hdc;
    HGDIOBJ     hfOld;
    HMMIO       hmmio;
    HWND        hwndIntro;
    LPSTR       p;
    BOOL        fMinimized;
    BOOL        fIntro;
    BOOL        f;
    BYTE        bMajor;
    int         x;
    int         y;

    fMinimized = (nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_SHOWMINNOACTIVE ||
                  nCmdShow == SW_MINIMIZE);
    hwndIntro = NULL;
    fIntro = TRUE;
    gfInitializing = TRUE;
    ghInst = hInstance;
    gdwWinFlags = GetWinFlags();

    lpfn = GetProcAddress((HINSTANCE)GetSystemMetrics(SM_PENWINDOWS), gszRegisterPenApp);
    if (lpfn)
        (*(void (FAR PASCAL *)(WORD, BOOL))lpfn)(1, TRUE);

    bMajor = LOBYTE(LOWORD(GetVersion()));
    gfWin31 = (MAKEWORD(HIBYTE(LOWORD(GetVersion())), bMajor) >= 0x030A) ? TRUE : FALSE;

    grgbBtnText = GetSysColor(COLOR_BTNTEXT);
    ghbrBtnText = CreateSolidBrush(grgbBtnText);
    grgbBtnShadow = GetSysColor(COLOR_BTNSHADOW);
    ghbrBtnShadow = CreateSolidBrush(grgbBtnShadow);
    grgbBtnFace = GetSysColor(COLOR_BTNFACE);
    ghbrBtnFace = CreateSolidBrush(grgbBtnFace);
    grgbBtnHighlight = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
    ghbrBtnHighlight = CreateSolidBrush(grgbBtnHighlight);
    if (!ghbrBtnFace || !ghbrBtnShadow || !ghbrBtnHighlight || !ghbrBtnText)
        goto fail;

    ghfontApp = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0,
                           VARIABLE_PITCH | FF_MODERN, gszHelv);
    if (ghfontApp == NULL)
        ghfontApp = GetStockObject(SYSTEM_FONT);
    hdc = GetDC(NULL);
    hfOld = SelectObject(hdc, ghfontApp);
    GetTextMetrics(hdc, &tm);
    gcxStatusField = tm.tmAveCharWidth * 58;
    gcyStatus = (tm.tmHeight * 3) / 2;
    if (hfOld)
        SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);

    gcxScreen  = GetSystemMetrics(SM_CXSCREEN);
    gcyScreen  = GetSystemMetrics(SM_CYSCREEN);
    gcyHScroll = GetSystemMetrics(SM_CYHSCROLL);
    gcxVScroll = GetSystemMetrics(SM_CXVSCROLL);
    gcxBorder  = GetSystemMetrics(SM_CXBORDER);
    gcyBorder  = GetSystemMetrics(SM_CYBORDER);

    /* the command line */
    p = lpszCmdLine;
    while (*p && *p == ' ')
        p = AnsiNext(p);
    for (;;) {
        if (*p != '-' && *p != '/')
            break;
        if (p[1] == '\0')
            continue;
        p++;
        switch (*p) {
        case 'n':
        case 'N':
            fIntro = FALSE;
            p++;
            break;
        case 'd':
        case 'D':
            p++;
            gnDevice = (int)ParseNumber(p);
            while (*p && *p != ' ')
                p = AnsiNext(p);
            break;
        }
        while (*p == ' ') {
            p++;
            if (*p == '\0')
                break;
        }
    }

    ArrowInit(ghInst);
    gfInitDone = TRUE;

    if (fIntro && hPrevInstance == NULL)
        hwndIntro = IntroBox(hInstance, IDS_TITLE, IDS_VERSION, IDS_COPYRIGHT, IDB_INTRO);
    ReadSettings();

    if (hPrevInstance == NULL) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = LoadIcon(hInstance, gszAppName);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpszMenuName  = gszAppName;
        wc.lpszClassName = gszAppName;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hInstance     = hInstance;
        wc.lpfnWndProc   = CtrlWndProc;
        wc.cbWndExtra    = 0;
        wc.cbClsExtra    = 0;
        if (!RegisterClass(&wc))
            goto fail;

        wc.lpszClassName = gszRLMeterClass;
        wc.hInstance     = hInstance;
        wc.lpfnWndProc   = RLMeterProc;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_GLOBALCLASS;
        wc.cbClsExtra    = 0x14;
        wc.cbWndExtra    = 0x14;
        if (!RegisterClass(&wc))
            goto fail;

        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = gszEditClass;
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.hInstance     = hInstance;
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = EditWndProc;
        wc.cbWndExtra    = 0;
        wc.cbClsExtra    = 0;
        if (!RegisterClass(&wc))
            goto fail;
    }

    if (!toolbarInit(hInstance, hPrevInstance))
        goto fail;
    if (!StatusInit(hInstance, hPrevInstance))
        goto fail;
    InitHelp();
    ShowWndInit(hInstance, hPrevInstance);
    ghAccel = LoadAccelerators(hInstance, gszAppName);

    x = hPrevInstance ? CW_USEDEFAULT : grcCtrl.left;
    y = hPrevInstance ? 0 : grcCtrl.top;
    ghwndApp = CreateWindow(gszAppName, gszAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, grcCtrl.right - grcCtrl.left, grcCtrl.bottom - grcCtrl.top,
                            NULL, NULL, hInstance, NULL);
    if (ghwndApp == NULL)
        goto fail;

    ghwndFrame = CreateWindow(gszEditClass, gszAppName,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
                              0, 0, 0, 0, ghwndApp, NULL, hInstance, NULL);
    if (ghwndFrame == NULL)
        goto fail;

    GetWindowRect(ghwndApp, &grcCtrl);
    GetClientRect(ghwndApp, &rc);
    gcxClient = rc.right;
    gcyClient = rc.bottom;

    if (InitCapturePalette())
        goto fail;
    if (InitVideoFormat())
        goto fail;

    f = gfToolBar;
    gfToolBar = !f;
    ShowBars(f, gfStatusBar);
    ShowWindow(ghwndApp, fMinimized ? SW_SHOWMINIMIZED : nCmdShow);
    LayoutWindows();
    if (hwndIntro)
        IntroBoxDone(hwndIntro);
    StatusText(NULL);
    UpdateWindow(ghwndFrame);
    UpdateWindow(ghwndApp);

    gwmFileNameOK = RegisterWindowMessage(gszFileNameOK);

    lstrcpy(achCaption, gszAppName);
    if (gachCaptureFile[0]) {
        hmmio = mmioOpen(gachCaptureFile, NULL, MMIO_READ);
        if (hmmio == NULL) {
            gfCaptureFileHasData = FALSE;
            gachCaptureFile[0] = '\0';
        } else {
            ck.fccType = formtypeAVI;
            if (mmioDescend(hmmio, &ck, NULL, 0) == 0 &&
                ck.ckid == FOURCC_RIFF && ck.fccType == formtypeAVI)
                gfCaptureFileHasData = TRUE;
            else
                gfCaptureFileHasData = FALSE;
            mmioClose(hmmio, 0);
            lstrcat(achCaption, gszCaptionSep);
            lstrcat(achCaption, gachCaptureFile);
        }
        toolbarModifyState(ghwndToolbar, BTN_EDIT, gfCaptureFileHasData ? BTNST_UP : BTNST_GRAYED);
    }
    SetWindowText(ghwndApp, achCaption);

    if (!OpenVideo(gnDevice))
        goto fail;
    if (GetCaptureFormat())
        goto fail;
    if (GetCapturePalette())
        goto fail;
    if (!gfPalettized)
        toolbarModifyState(ghwndToolbar, BTN_PALETTE, BTNST_GRAYED);
    SizeShowWindow(TRUE);
    InvalidateRect(ghwndShow, NULL, TRUE);

    if (gfPreview) {
        gfPreview = FALSE;
        gfOverlay = gfHasOverlay;
        PostMessage(ghwndApp, WM_COMMAND, IDM_PREVIEW, 0L);
    } else if (gfOverlay) {
        gfOverlay = FALSE;
        gfPreview = TRUE;
        PostMessage(ghwndApp, WM_COMMAND, IDM_OVERLAY, 0L);
    }

    GetWindowRect(ghwndApp, &grcApp);
    gfInitializing = FALSE;
    return TRUE;

fail:
    gfInitializing = FALSE;
    return FALSE;
}

/*
 * seg2:1440-1546  CreateBars
 *
 * Makes the toolbar with its eight buttons (24x22) and the status bar.
 * Returns -1 if either cannot be made.
 *
 * Quirk of the original: the buttons' previous state and activity are
 * left uninitialised.
 */
LONG FAR PASCAL CreateBars(HWND hwndParent)
{
    TOOLBUTTON  tb;
    RECT        rc;
    POINT       pt;
    int         i;

    ghwndToolbar = CreateWindow(gszToolBarClass, NULL,
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER | WS_TABSTOP,
                                0, 0, 0, 0, hwndParent, NULL, ghInst, NULL);
    if (ghwndToolbar == NULL)
        return -1L;

    pt.x = 24;
    pt.y = 22;
    toolbarSetBitmap(ghwndToolbar, ghInst, IDB_TOOLBAR, pt);

    for (i = 0; i < 8; i++) {
        rc.top = 2;
        rc.left = gaiBtnX[i];
        rc.right = rc.left + pt.x;
        rc.bottom = pt.y + 2;
        tb.rc = rc;
        tb.iButton = gaiBtnButton[i];
        tb.iState = gaiBtnState[i];
        tb.iType = gaiBtnType[i];
        tb.iString = gaiBtnString[i];
        toolbarAddTool(ghwndToolbar, tb);
    }

    ghwndStatusBar = CreateWindow(gszStatusClass, NULL,
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
                                  0, 0, 0, 0, hwndParent, NULL, ghInst, NULL);
    if (ghwndStatusBar == NULL)
        return -1L;
    return (LONG)(WORD)ghwndStatusBar;
}

/*
 * seg2:154A-1611  StatusInit
 *
 * Registers the status bar ("StatusClass") and its text ("SText")
 * classes.
 */
BOOL FAR PASCAL StatusInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (hPrev)
        return TRUE;

    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = NULL;
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = gszStatusClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hInstance     = hInst;
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = StatusWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    if (!RegisterClass(&wc))
        return FALSE;

    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = NULL;
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = gszSTextClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hInstance     = hInst;
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = fnText;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    if (!RegisterClass(&wc))
        return FALSE;
    return TRUE;
}
