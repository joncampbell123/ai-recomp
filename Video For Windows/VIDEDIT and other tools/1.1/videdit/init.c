/*
 * init.c - code segment 2 of VIDEDIT.EXE (0x1356 bytes, PRELOAD
 * DISCARDABLE), the initialisation segment.
 *
 * AppInit (called by WinMain), the settings in [VidEdit] of
 * MMTOOLS.INI, the F1 help hook and the radio-style menu check marks.
 * The segment also holds start-up code of other modules, apparently
 * placed here with alloc_text: their strings and data are in those
 * modules' parts of DGROUP (InitStatusClasses and CreateChildWindows:
 * the status/tool bar module; InitDibClipboard: the video module;
 * InitWave: the audio module; InitUndo: undo.c).
 *
 * The INI key names are in the code segment (_based(_segname("_CODE"))).
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"
#include "edit.h"
#include "croprect.h"

/* ------------------------------------------------------------------ */
/* data                                                                */
/* ------------------------------------------------------------------ */

char        gszIniFile[]    = "mmtools.ini";    /* DS:0028 */
char        gszAppName[]    = "VidEdit";        /* DS:004C */
char        gszViewClass[]  = "VidEditWindow";  /* DS:0054 */
HOOKPROC    glpfnOldMsgFilter;                  /* DS:0062 */
FARPROC     glpfnHelpMsgFilter;                 /* DS:0066 */
                                                /* DS:006A ghInst, shared.c */
HACCEL      ghAccel;                            /* DS:006C */
HDC         ghdcDib;                            /* DS:006E */
char        gszRegisterPenApp[] = "RegisterPenApp";  /* DS:0070 */
HCURSOR     ghcurCross;                         /* DS:0080 */
HCURSOR     ghcurArrow;                         /* DS:0082 */
WORD        gwEditor;                           /* DS:0084 */
WORD        gwWrkInstance;                      /* DS:0086 */
BOOL        gfWrkClient;                        /* DS:0088 */
static char szFmtD[]        = "%d";             /* DS:008A */
static char szBoolUnset[]   = "X";              /* DS:008D */
static char szYes[]         = "Yes";            /* DS:008F */
static char szNo[]          = "No";             /* DS:0093 */
static char szDefBkSave[]   = "L";              /* DS:0096 */
static char szDefRedrawSave[] = "F";            /* DS:0098 */
static char szDefBkLoad[]   = "L";              /* DS:009A */
static char szDefRedrawLoad[] = "F";            /* DS:009C */
                                                /* DS:009E, DS:00A0 menu bitmaps, shared.c */
static char szHelpFileName[] = "videdit.hlp";   /* DS:00A2 */
static char szHelpFileTooLong[] = "?";          /* DS:00AE */
static char szHelv[]        = "Helv";           /* DS:00B0 */
static char szMedBits[]     = "medbits";        /* DS:00B5 */
static char szDibDriver[]   = "dib";            /* DS:00BD */
static char szMedWave[]     = "medwave";        /* DS:00C1 */
static char szMedDibs[]     = "meddibs";        /* DS:00C9 */

/* tool bar buttons (the status/tool bar module's data, DS:01BE-0217) */
static int  gaiTBButton[9]  = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };                  /* DS:01BE */
static int  gaiTBState[9]   = { BTNST_UP, BTNST_UP, BTNST_UP, BTNST_UP, BTNST_UP,
                                BTNST_UP, BTNST_UP, BTNST_UP, BTNST_UP };     /* DS:01D0 */
static int  gaiTBType[9]    = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };                  /* DS:01E2 push buttons */
static int  gaiTBString[9]  = { IDS_TBOPEN, IDS_TBSAVE, IDS_TBCUT, IDS_TBCOPY,
                                IDS_TBPASTE, IDS_TBNOUNDO, 1040, 1041, IDS_TBZOOM2 };  /* DS:01F4 */
static int  gaxTB[9]        = { 6, 29, 62, 85, 108, 131, 174, 217, 263 };     /* DS:0206 */

static char szStatusClass[] = "StatusClass";    /* DS:0102 */
static char szSText[]       = "SText";          /* DS:010E */
static char szToolBarClass[] = "ToolBarClass";  /* DS:0BBA */
static char szDibRange[]    = "VidfrmDibRange"; /* DS:02DC */
static char szWaveClip[]    = "WaveEditIntSound";       /* DS:0404 */
static char szMedWaveLib[]  = "medwave.mmh";            /* DS:0415 */
static char szWaveFree[]    = "MediaWaveFreeHWAVE";     /* DS:0421 */
static char szWaveFormat[]  = "MediaWaveFormatHWAVE";   /* DS:0434 */
static char szWaveCopy[]    = "MediaWaveCopyHWAVE";     /* DS:0449 */
static char szWaveSize[]    = "MediaWaveSizeHWAVE";     /* DS:045C */
static char szWaveRead[]    = "MediaWaveReadHWAVE";     /* DS:046F */

/* uninitialised */
HWND        ghwndBlob;                          /* DS:01B8 */
char        gsz155[20];                         /* DS:1BE4 */
COLORREF    gclrBtnShadow;                      /* DS:2D0A */
RECT        grcAppInit;                         /* DS:2D22 */
BOOL        gfInitializing;                     /* DS:2D52 */
int         gcyPanel;                           /* DS:2D56 */
COLORREF    gclrBtnFace;                        /* DS:3072 */
int         gcxClient;                          /* DS:307E */
char        gszP[5];                            /* DS:3080 */
char        gszHelpFile[80];                    /* DS:3126 */
FARPROC     glpfnMediaWaveCopyHWAVE;            /* DS:3184 */
BOOL        gfWin31;                            /* DS:3188 */
char        gszK[5];                            /* DS:31A6 */
COLORREF    gclrBtnText;                        /* DS:31B6 */
HWND        ghwndStatus;                        /* DS:321E */
COLORREF    gclrBtnHighlight;                   /* DS:3220 */
DWORD       gdwWinFlags;                        /* DS:325A */
int         gcyClient;                          /* DS:325E */

/*
 * seg2:0000-004C  WriteIniInt  (FAR PASCAL)
 *
 * Writes an integer to [lpszSection] of MMTOOLS.INI, but only if it
 * differs from what is there (or from nDefault if the key is missing).
 */
void FAR PASCAL WriteIniInt(LPSTR lpszSection, LPSTR lpszKey, int nDefault, int nValue)
{
    char ach[30];

    wsprintf(ach, szFmtD, nValue);
    if (GetPrivateProfileInt(lpszSection, lpszKey, nDefault, gszIniFile) != nValue)
        WritePrivateProfileString(lpszSection, lpszKey, ach, gszIniFile);
}

/* seg2:004D-0181  the INI key names, in the code segment */
static char _based(_segname("_CODE")) szCtrlYPos[]        = "CtrlYPos";
static char _based(_segname("_CODE")) szCtrlXPos[]        = "CtrlXPos";
static char _based(_segname("_CODE")) szStatusBar[]       = "StatusBar";
static char _based(_segname("_CODE")) szToolBar[]         = "ToolBar";
/* also here, unreferenced: "None", "", "Differences" */
static char _based(_segname("_CODE")) szOverlay[]         = "Overlay";
static char _based(_segname("_CODE")) szFullFrame[]       = "FullFrame";
static char _based(_segname("_CODE")) szRedrawOption[]    = "RedrawOption";
static char _based(_segname("_CODE")) szZoomFactor[]      = "ZoomFactor";
static char _based(_segname("_CODE")) szEditWAVE[]        = "EditWAVE";
static char _based(_segname("_CODE")) szEditVideo[]       = "EditVideo";
static char _based(_segname("_CODE")) szDefFrameHeight[]  = "DefaultFrameHeight";
static char _based(_segname("_CODE")) szDefFrameWidth[]   = "DefaultFrameWidth";
static char _based(_segname("_CODE")) szFrameNumInTitle[] = "FrameNumInTitle";
static char _based(_segname("_CODE")) szCacheMemory[]     = "CacheMemory";
static char _based(_segname("_CODE")) szResizeOnOpen[]    = "ResizeOnOpen";
/* also here, unreferenced: "PromptSummary" */
static char _based(_segname("_CODE")) szBlack[]           = "Black";
static char _based(_segname("_CODE")) szDkGray[]          = "DkGray";
static char _based(_segname("_CODE")) szLtGray[]          = "LtGray";
static char _based(_segname("_CODE")) szDefault[]         = "Default";
static char _based(_segname("_CODE")) szBackgroundColor[] = "BackgroundColor";
static char _based(_segname("_CODE")) szCtrlHeight[]      = "CtrlHeight";
static char _based(_segname("_CODE")) szCtrlWidth[]       = "CtrlWidth";
static char _based(_segname("_CODE")) szInsertMode[]      = "InsertMode";
static char _based(_segname("_CODE")) szTimeFormat[]      = "TimeFormat";
static char _based(_segname("_CODE")) szCenterImage[]     = "CenterImage";

/*
 * seg2:0180-01D5  GetIniBool  (NEAR PASCAL)
 *
 * Reads a yes/no setting: "y", "Y" and "1" are TRUE, "n", "N" and "0"
 * FALSE, anything else (or a missing key) gives fDefault.
 */
static BOOL NEAR PASCAL GetIniBool(LPSTR lpszSection, LPSTR lpszKey, BOOL fDefault)
{
    char ach[10];

    GetPrivateProfileString(lpszSection, lpszKey, szBoolUnset, ach, sizeof(ach), gszIniFile);
    switch (ach[0]) {
    case 'y':
    case 'Y':
    case '1':
        return TRUE;
    case 'n':
    case 'N':
    case '0':
        return FALSE;
    }
    return fDefault;
}

/*
 * seg2:01D6-0228  WriteIniBool  (NEAR PASCAL)
 *
 * Writes "Yes" or "No" if the setting differs from what is there.
 */
static void NEAR PASCAL WriteIniBool(LPSTR lpszSection, LPSTR lpszKey, BOOL fDefault, BOOL fValue)
{
    char ach[10];

    if (GetIniBool(lpszSection, lpszKey, fDefault) != fValue) {
        lstrcpy(ach, fValue ? szYes : szNo);
        WritePrivateProfileString(lpszSection, lpszKey, ach, gszIniFile);
    }
}

/*
 * seg2:022A-0464  SaveSettings
 *
 * Quirk: if gidBackground is none of the four colours, an uninitialised
 * buffer is compared with (and possibly written as) BackgroundColor.
 */
void FAR SaveSettings(void)
{
    char ach[30];
    char achOld[30];

    WriteIniBool(gszAppName, szResizeOnOpen, TRUE, gfResizeOnOpen);
    WriteIniBool(gszAppName, szFrameNumInTitle, FALSE, gfFrameNumInTitle);
    WriteIniInt(gszAppName, szCacheMemory, 1024, gwCacheKB);
    WriteIniInt(gszAppName, szDefFrameWidth, 160, gcxDefault);
    WriteIniInt(gszAppName, szDefFrameHeight, 120, gcyDefault);
    WriteIniBool(gszAppName, szTimeFormat, FALSE, gfTimeFormat);
    WriteIniInt(gszAppName, szCtrlXPos, 300, grcAppInit.left);
    WriteIniInt(gszAppName, szCtrlYPos, 200, grcAppInit.top);
    WriteIniInt(gszAppName, szCtrlWidth, 404, grcAppInit.right - grcAppInit.left);
    WriteIniInt(gszAppName, szCtrlHeight, 244, grcAppInit.bottom - grcAppInit.top);
    WriteIniBool(gszAppName, szToolBar, TRUE, gfToolBar);
    WriteIniBool(gszAppName, szStatusBar, TRUE, gfStatusBar);
    WriteIniBool(gszAppName, szCenterImage, TRUE, gfCenterImage);

    if (gidBackground == IDC_PREF_BKDEFAULT)
        lstrcpy(ach, szDefault);
    if (gidBackground == IDC_PREF_BKLTGRAY)
        lstrcpy(ach, szLtGray);
    if (gidBackground == IDC_PREF_BKDKGRAY)
        lstrcpy(ach, szDkGray);
    if (gidBackground == IDC_PREF_BKBLACK)
        lstrcpy(ach, szBlack);
    GetPrivateProfileString(gszAppName, szBackgroundColor, szDefBkSave, achOld, sizeof(achOld), gszIniFile);
    if (ach[0] != achOld[0] || ach[1] != achOld[1])
        WritePrivateProfileString(gszAppName, szBackgroundColor, ach, gszIniFile);

    WriteIniBool(gszAppName, szEditVideo, TRUE, gfEditVideo);
    WriteIniBool(gszAppName, szEditWAVE, TRUE, gfEditAudio);
    WriteIniInt(gszAppName, szZoomFactor, 2, gwZoom);
    WriteIniBool(gszAppName, szInsertMode, TRUE, gfInsertMode);

    if (gwFrameUpdate == IDM_FULLFRAMEUPDATE)
        lstrcpy(ach, szFullFrame);
    if (gwFrameUpdate == IDM_FASTFRAMEUPDATE)
        lstrcpy(ach, szOverlay);
    GetPrivateProfileString(gszAppName, szRedrawOption, szDefRedrawSave, achOld, sizeof(achOld), gszIniFile);
    if (ach[0] != achOld[0])
        WritePrivateProfileString(gszAppName, szRedrawOption, ach, gszIniFile);
}

/*
 * seg2:0466-0671  LoadSettings  (NEAR)
 *
 * The window position and size come from CtrlXPos, CtrlYPos, CtrlWidth
 * and CtrlHeight; BackgroundColor is recognised by its first letter, or
 * by the second for "Default" and "DkGray".
 */
static void NEAR LoadSettings(void)
{
    char ach[30];

    gfResizeOnOpen = GetIniBool(gszAppName, szResizeOnOpen, TRUE);
    gfFrameNumInTitle = GetIniBool(gszAppName, szFrameNumInTitle, FALSE);
    gwCacheKB = GetPrivateProfileInt(gszAppName, szCacheMemory, 1024, gszIniFile);
    gcxMovie = gcxDefault = GetPrivateProfileInt(gszAppName, szDefFrameWidth, 160, gszIniFile);
    gcyMovie = gcyDefault = GetPrivateProfileInt(gszAppName, szDefFrameHeight, 120, gszIniFile);
    gfTimeFormat = GetIniBool(gszAppName, szTimeFormat, FALSE);
    grcApp.top = GetPrivateProfileInt(gszAppName, szCtrlYPos, 200, gszIniFile);
    grcApp.left = GetPrivateProfileInt(gszAppName, szCtrlXPos, 300, gszIniFile);
    grcApp.bottom = GetPrivateProfileInt(gszAppName, szCtrlHeight, 244, gszIniFile) + grcApp.top;
    grcApp.right = GetPrivateProfileInt(gszAppName, szCtrlWidth, 404, gszIniFile) + grcApp.left;
    gfToolBar = GetIniBool(gszAppName, szToolBar, TRUE);
    gfStatusBar = GetIniBool(gszAppName, szStatusBar, TRUE);
    gfCenterImage = GetIniBool(gszAppName, szCenterImage, TRUE);

    GetPrivateProfileString(gszAppName, szBackgroundColor, szDefBkLoad, ach, sizeof(ach), gszIniFile);
    switch (ach[0]) {
    case 'l':
    case 'L':
        gidBackground = IDC_PREF_BKLTGRAY;
        break;
    case 'b':
    case 'B':
        gidBackground = IDC_PREF_BKBLACK;
        break;
    default:
        if (ach[1] == 'e' || ach[1] == 'E')
            gidBackground = IDC_PREF_BKDEFAULT;
        if (ach[1] == 'k' || ach[1] == 'K')
            gidBackground = IDC_PREF_BKDKGRAY;
        break;
    }

    gfEditVideo = GetIniBool(gszAppName, szEditVideo, TRUE);
    gfEditAudio = GetIniBool(gszAppName, szEditWAVE, TRUE);
    if (!gfEditVideo && !gfEditAudio)
        gfEditVideo = TRUE;
    gwZoom = GetPrivateProfileInt(gszAppName, szZoomFactor, 2, gszIniFile);
    gfInsertMode = GetIniBool(gszAppName, szInsertMode, TRUE);

    GetPrivateProfileString(gszAppName, szRedrawOption, szDefRedrawLoad, ach, sizeof(ach), gszIniFile);
    if (ach[0] == 'F' || ach[0] == 'f')
        gwFrameUpdate = IDM_FULLFRAMEUPDATE;
    else
        gwFrameUpdate = IDM_FASTFRAMEUPDATE;
}

/*
 * seg2:0672-08CD  CreateMenuBitmaps  (NEAR PASCAL)
 *
 * Makes the round check marks of the radio-style menu items (tracks,
 * zoom, redraw mode): a black dot for "checked", a blank bitmap for
 * "unchecked".
 */
static BOOL NEAR PASCAL CreateMenuBitmaps(HWND hwnd)
{
    BOOL    fOK = FALSE;
    DWORD   dw;
    int     cx, cy, d, x, y;
    HDC     hdc, hdcMem;
    HMENU   hMenu, hSub;

    dw = GetMenuCheckMarkDimensions();
    hdc = GetDC(NULL);
    if (hdc == NULL)
        return fOK;
    hdcMem = CreateCompatibleDC(hdc);
    if (hdcMem != NULL) {
        cx = LOWORD(dw);
        cy = HIWORD(dw);
        ghbmMenuCheck = CreateBitmap(cx, cy, 1, 1, NULL);
        ghbmMenuUncheck = CreateBitmap(cx, cy, 1, 1, NULL);
        if (ghbmMenuCheck && ghbmMenuUncheck) {
            SelectObject(hdcMem, ghbmMenuCheck);
            SelectObject(hdcMem, GetStockObject(WHITE_BRUSH));
            SelectObject(hdcMem, GetStockObject(WHITE_PEN));
            Rectangle(hdcMem, 0, 0, cx, cy);
            SelectObject(hdcMem, GetStockObject(BLACK_BRUSH));
            SelectObject(hdcMem, GetStockObject(BLACK_PEN));
            d = min(cx, cy) / 2;
            x = (cx - d) / 2;
            y = (cy - d) / 2;
            Ellipse(hdcMem, x, y, x + d, y + d);

            SelectObject(hdcMem, ghbmMenuUncheck);
            SelectObject(hdcMem, GetStockObject(WHITE_BRUSH));
            SelectObject(hdcMem, GetStockObject(WHITE_PEN));
            Rectangle(hdcMem, 0, 0, LOWORD(dw), HIWORD(dw));

            if ((hMenu = GetMenu(hwnd)) != NULL &&
                (hSub = GetSubMenu(hMenu, 1)) != NULL) {
                SetMenuItemBitmaps(hSub, IDM_TRACKVIDEO, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                SetMenuItemBitmaps(hSub, IDM_TRACKAUDIO, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                SetMenuItemBitmaps(hSub, IDM_TRACKBOTH, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                if ((hSub = GetSubMenu(hMenu, 2)) != NULL &&
                    (hSub = GetSubMenu(hSub, 4)) != NULL) {
                    SetMenuItemBitmaps(hSub, IDM_ZOOMHALF, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                    SetMenuItemBitmaps(hSub, IDM_ZOOM1, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                    SetMenuItemBitmaps(hSub, IDM_ZOOM2, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                    SetMenuItemBitmaps(hSub, IDM_ZOOM4, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                    if ((hSub = GetSubMenu(hMenu, 2)) != NULL) {
                        SetMenuItemBitmaps(hSub, IDM_FULLFRAMEUPDATE, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                        SetMenuItemBitmaps(hSub, IDM_FASTFRAMEUPDATE, MF_BYCOMMAND, ghbmMenuUncheck, ghbmMenuCheck);
                        fOK = TRUE;
                    }
                }
            }
        }
        DeleteDC(hdcMem);
    }
    ReleaseDC(NULL, hdc);
    return fOK;
}

/*
 * seg2:08D0-0937  HelpMsgFilter  (FAR PASCAL, WH_MSGFILTER hook, loads DS
 * from SS)
 *
 * F1 in a dialog or menu opens help at gidHelpContext, or sends
 * IDM_HELPCONTENTS when there is none.
 */
DWORD FAR PASCAL HelpMsgFilter(int nCode, WORD wParam, LPMSG lpMsg)
{
    if (nCode >= 0 && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_F1) {
        if (gidHelpContext)
            WinHelp(ghwndApp, gszHelpFile, HELP_CONTEXT, (DWORD)gidHelpContext);
        else
            SendMessage(ghwndApp, WM_COMMAND, IDM_HELPCONTENTS, 0L);
    }
    return DefHookProc(nCode, wParam, (LONG)lpMsg, (HOOKPROC FAR *)&glpfnOldMsgFilter);
}

/*
 * seg2:0938-09DB  InitHelp
 *
 * The help file is VIDEDIT.HLP in the directory of the executable ("?"
 * if that path would not fit in 80 characters).
 */
BOOL FAR InitHelp(void)
{
    int     n;
    LPSTR   p;

    n = GetModuleFileName(ghInst, gszHelpFile, sizeof(gszHelpFile));
    p = gszHelpFile + n;
    while (p > gszHelpFile) {
        if (*p == '\\' || *p == ':') {
            p[1] = '\0';
            break;
        }
        n--;
        p = AnsiPrev(gszHelpFile, p);
    }
    lstrcat(gszHelpFile, (n + 13 < sizeof(gszHelpFile)) ? szHelpFileName : szHelpFileTooLong);

    glpfnHelpMsgFilter = MakeProcInstance((FARPROC)HelpMsgFilter, ghInst);
    glpfnOldMsgFilter = SetWindowsHook(WH_MSGFILTER, (HOOKPROC)glpfnHelpMsgFilter);
    gidHelpContext = 0;
    return TRUE;
}

/*
 * seg2:09DC-09FB  TermHelp
 */
void FAR TermHelp(void)
{
    if (glpfnOldMsgFilter) {
        UnhookWindowsHook(WH_MSGFILTER, (HOOKPROC)glpfnHelpMsgFilter);
        FreeProcInstance(glpfnHelpMsgFilter);
    }
}

/*
 * seg2:09FC-0A76  ParseNumber  (NEAR PASCAL, unreferenced)
 *
 * Reads a decimal number after an optional '=' up to a space.  The digit
 * test can never fail (c >= '0' || c <= '9'), so every character counts
 * as a digit.
 */
DWORD NEAR PASCAL ParseNumber(LPSTR lpsz)
{
    DWORD dw;

    if (*lpsz == '=')
        lpsz++;
    dw = 0;
    while (*lpsz && *lpsz != ' ') {
        if (*lpsz >= '0' || *lpsz <= '9')
            dw = dw * 10 + (*lpsz - '0');
        lpsz = AnsiNext(lpsz);
    }
    return dw;
}

/*
 * seg2:0A78-104C  AppInit  (FAR PASCAL)
 *
 * Called by WinMain.  The command line is "[-n | /n] [file]": -n skips
 * the intro box.  Quirks: a lone "-" or "/" at the end of the command
 * line hangs (the option loop never advances), unknown options are taken
 * as the start of the file name, and the intro box is shown only by the
 * first instance.
 */
BOOL FAR PASCAL AppInit(HINSTANCE hInst, HINSTANCE hPrev, int nCmdShow, LPSTR lpszCmdLine)
{
    TEXTMETRIC  tm;
    WNDCLASS    wc;
    RECT        rc;
    LPSTR       lpszFile;
    LPSTR       p;
    BOOL        fMinimized;
    HWND        hwndIntro;
    BOOL        fIntro;
    HDC         hdc;
    HGDIOBJ     hfontOld;
    FARPROC     lpfnRegisterPenApp;
    WORD        wVersion;
    int         x, y;

    fMinimized = (nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_SHOWMINNOACTIVE ||
                  nCmdShow == SW_MINIMIZE);
    hwndIntro = NULL;
    gfInitializing = fIntro = TRUE;
    ghInst = hInst;
    gdwWinFlags = GetWinFlags();

    lpfnRegisterPenApp = GetProcAddress((HINSTANCE)GetSystemMetrics(SM_PENWINDOWS), gszRegisterPenApp);
    if (lpfnRegisterPenApp)
        (*(void (FAR PASCAL *)(WORD, BOOL))lpfnRegisterPenApp)(1, TRUE);

    wVersion = (LOBYTE(GetVersion()) << 8) | HIBYTE(GetVersion());
    gfWin31 = (wVersion >= 0x030A) ? TRUE : FALSE;

    ghbrBtnText = CreateSolidBrush(gclrBtnText = GetSysColor(COLOR_BTNTEXT));
    ghbrBtnShadow = CreateSolidBrush(gclrBtnShadow = GetSysColor(COLOR_BTNSHADOW));
    ghbrBtnFace = CreateSolidBrush(gclrBtnFace = GetSysColor(COLOR_BTNFACE));
    gclrBtnHighlight = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
    ghbrBtnHighlight = CreateSolidBrush(gclrBtnHighlight);
    if (!ghbrBtnFace || !ghbrBtnShadow || !ghbrBtnHighlight || !ghbrBtnText)
        goto fail;

    ghfontApp = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0,
                        VARIABLE_PITCH | FF_SWISS, szHelv);
    if (ghfontApp == NULL)
        ghfontApp = GetStockObject(SYSTEM_FONT);
    hdc = GetDC(NULL);
    hfontOld = SelectObject(hdc, ghfontApp);
    GetTextMetrics(hdc, &tm);
    gcxMinWindow = tm.tmAveCharWidth << 6;
    gcyStatus = tm.tmHeight * 3 / 2;
    if (hfontOld)
        SelectObject(hdc, hfontOld);
    ReleaseDC(NULL, hdc);

    gcxScreen = GetSystemMetrics(SM_CXSCREEN);
    gcyScreen = GetSystemMetrics(SM_CYSCREEN);
    gcyHScroll = GetSystemMetrics(SM_CYHSCROLL);
    gcxVScroll = GetSystemMetrics(SM_CXVSCROLL);
    gcxBorder = GetSystemMetrics(SM_CXBORDER);
    gcyBorder = GetSystemMetrics(SM_CYBORDER);
    ghcurCross = LoadCursor(hInst, MAKEINTRESOURCE(IDC_CURSOR400));
    ghcurArrow = LoadCursor(NULL, IDC_ARROW);
    LoadString(ghInst, IDS_STATKEY, gszK, sizeof(gszK));
    LoadString(ghInst, IDS_STATPAL, gszP, sizeof(gszP));

    /* command line */
    p = lpszCmdLine;
    while (*p && *p == ' ')
        p = AnsiNext(p);
    for (;;) {
        if (*p != '-' && *p != '/')
            break;
        if (p[1] == '\0')
            continue;
        p++;
        if (*p == 'N' || *p == 'n') {
            fIntro = FALSE;
            p++;
        }
        if (*p == '\0')
            continue;
        while (*p == ' ') {
            p++;
            if (*p == '\0')
                break;
        }
    }
    lpszFile = p;

    WrkClientInit();
    gfWrkClient = TRUE;

    if (fIntro && !hPrev)
        hwndIntro = IntroBox(hInst, IDS_APPNAME, IDS_VERSION, IDS_COPYRIGHT, IDB_INTRO);

    LoadSettings();

    if (!hPrev) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = LoadIcon(hInst, gszAppName);
        wc.style         = 0;
        wc.cbWndExtra    = 30;
        wc.lpszMenuName  = gszAppName;
        wc.lpszClassName = gszAppName;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hInstance     = hInst;
        wc.lpfnWndProc   = VidEditWndProc;
        wc.cbClsExtra    = 0;
        if (!RegisterClass(&wc))
            goto fail;

        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = gszViewClass;
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.hInstance     = hInst;
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ViewWndProc;
        wc.cbWndExtra    = 0;
        wc.cbClsExtra    = 0;
        if (!RegisterClass(&wc))
            goto fail;
    }

    if (!toolbarInit(hInst, hPrev))
        goto fail;
    if (!InitStatusClasses(hInst, hPrev))
        goto fail;
    InitHelp();
    FrameRegisterClass(hInst, hPrev);
    if (!WrkVerifyHandler(medFOURCC('M', 'D', 'I', 'B'), szMedBits))
        goto fail;
    if (!GrayBlobInit(hInst, hPrev))
        goto fail;
    ghdcDib = CreateDC(szDibDriver, NULL, NULL, NULL);
    CropInitBrush();
    InitUndo();
    FrameBoxInit(hInst, hPrev);
    if (!WrkVerifyHandler(medFOURCC('W', 'A', 'V', 'E'), szMedWave))
        goto fail;
    if (!WrkVerifyHandler(medFOURCC('D', 'I', 'B', 'S'), szMedDibs))
        goto fail;
    if (!InitWave())
        goto fail;
    if (!InitDibClipboard())
        goto fail;

    ghAccel = LoadAccelerators(hInst, gszAppName);

    x = hPrev ? CW_USEDEFAULT : grcApp.left;
    y = hPrev ? 0 : grcApp.top;
    ghwndApp = CreateWindow(gszAppName, gszAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          x, y, grcApp.right - grcApp.left, grcApp.bottom - grcApp.top,
                          NULL, NULL, hInst, NULL);
    if (ghwndApp == NULL)
        goto fail;

    ghwndView = CreateWindow(gszViewClass, gszAppName,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
                          0, 0, 0, 0, ghwndApp, NULL, hInst, NULL);
    if (ghwndView == NULL)
        goto fail;

    GetWindowRect(ghwndApp, (LPRECT)&grcApp);
    GetClientRect(ghwndApp, &rc);
    gcxClient = rc.right;
    gcyClient = rc.bottom;

    if (!CreateMenuBitmaps(ghwndApp))
        goto fail;

    /* inverted first, so that AppShowBars sees a change and lays out */
    {
        BOOL fToolBar = gfToolBar;

        gfToolBar = !fToolBar;
        AppShowBars(fToolBar, gfStatusBar);
    }
    UpdateTitles();
    StatusUpdatePosition();

    if (*lpszFile)
        OpenFileByName(lpszFile);
    else
        NewFile(TRUE);

    ShowWindow(ghwndApp, fMinimized ? SW_SHOWMINIMIZED : nCmdShow);
    AppLayout();

    gwEditor = WrkRegisterEditor(gszAppName, medFOURCC('D', 'I', 'B', 'S'), gszAppName, 0);
    if (!gwEditor)
        goto fail;
    gwWrkInstance = WrkAddInstance(gwEditor, ghwndFrame, gszAppName, 0);
    if (!gwWrkInstance)
        goto fail;
    ghdd = DrawDibOpen();
    if (!ghdd)
        goto fail;

    if (hwndIntro)
        IntroBoxDone(hwndIntro);
    StatusSetMessage(NULL);
    UpdateWindow(ghwndView);
    UpdateWindow(ghwndApp);
    GetWindowRect(ghwndApp, &grcAppInit);
    gfInitializing = FALSE;
    return TRUE;

fail:
    gfInitializing = FALSE;
    return FALSE;
}

/*
 * seg2:104E-1118  InitStatusClasses  (FAR PASCAL; the status bar
 * module's)
 *
 * Registers "StatusClass" and "SText" (first instance only) and loads
 * string 155, which is not in the string table.
 */
BOOL FAR PASCAL InitStatusClasses(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (!hPrev) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = szStatusClass;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hInstance     = hInst;
        wc.style         = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc   = StatusWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        if (!RegisterClass(&wc))
            return FALSE;

        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = szSText;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hInstance     = hInst;
        wc.style         = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc   = STextWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        if (!RegisterClass(&wc))
            return FALSE;
    }
    LoadString(ghInst, IDS_155, gsz155, sizeof(gsz155));
    return TRUE;
}

/*
 * seg2:111A-128B  CreateChildWindows  (FAR PASCAL; the tool bar
 * module's; called from the main window's WM_CREATE)
 *
 * Sets ghwndApp, then creates the tool bar with its 9 buttons, the gray
 * filler window and the status bar, and accepts dropped files.  Returns
 * -1 on failure; on success the original returns whatever is left in
 * DX:AX (any value but -1 lets the window be created).  The buttons'
 * iPrevState and iActivity are left uninitialised, as in the original.
 */
LONG FAR PASCAL CreateChildWindows(HWND hwnd)
{
    TOOLBUTTON  tb;
    RECT        rc;
    POINT       ptButton;
    int         i;

    ghwndToolBar = CreateWindow(szToolBarClass, NULL,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER | WS_TABSTOP,
                          0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
    if (ghwndToolBar == NULL)
        return -1L;

    ptButton.x = 24;
    ptButton.y = 22;
    toolbarSetBitmap(ghwndToolBar, ghInst, IDB_100, ptButton);
    for (i = 0; i < 9; i++) {
        rc.top    = 2;
        rc.left   = gaxTB[i];
        rc.right  = rc.left + ptButton.x;
        rc.bottom = ptButton.y + 2;
        tb.rc      = rc;
        tb.iButton = gaiTBButton[i];
        tb.iState  = gaiTBState[i];
        tb.iType   = gaiTBType[i];
        tb.iString = gaiTBString[i];
        toolbarAddTool(ghwndToolBar, tb);
    }
    if (gwFrameUpdate == IDM_FASTFRAMEUPDATE)
        toolbarModifyState(ghwndToolBar, 7, BTNST_UP);      /* Draw Full Frame usable */
    else
        toolbarModifyState(ghwndToolBar, 7, BTNST_GRAYED);

    ghwndApp = hwnd;
    ghwndBlob = CreateWindow("GrayBlob", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
    if (ghwndBlob == NULL)
        return -1L;

    gcyPanel = gcyBorder + gcyPanelBar;
    if (!CreateControlBars())
        return -1L;
    gcyPanel += gcyCtrlBars;

    ghwndStatus = CreateWindow(szStatusClass, NULL,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_BORDER,
                               0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
    if (ghwndStatus == NULL)
        return -1L;

    DragAcceptFiles(hwnd, TRUE);
    return 0L;
}

/*
 * seg2:128C-12BD  InitDibClipboard  (the video module's)
 */
BOOL FAR InitDibClipboard(void)
{
    gcfDibRange = RegisterClipboardFormat(szDibRange);
    if (!gcfDibRange)
        return FALSE;
    ghDibRangeList = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 0x20L);
    if (!ghDibRangeList)
        return FALSE;
    gcDibRangeList = 0;
    return TRUE;
}

/*
 * seg2:12BE-1347  InitWave  (the audio module's)
 *
 * Loads MEDWAVE.MMH, the MediaMan wave handler, for its HWAVE helpers.
 * A LoadLibrary error (a value below 32) is not caught.
 */
BOOL FAR InitWave(void)
{
    gcfWave = RegisterClipboardFormat(szWaveClip);
    if (!gcfWave)
        return FALSE;
    ghMedWave = LoadLibrary(szMedWaveLib);
    if (!ghMedWave)
        return FALSE;
    glpfnMediaWaveFreeHWAVE = (LPVOID)GetProcAddress(ghMedWave, szWaveFree);
    glpfnMediaWaveFormatHWAVE = (LPVOID)GetProcAddress(ghMedWave, szWaveFormat);
    glpfnMediaWaveCopyHWAVE = GetProcAddress(ghMedWave, szWaveCopy);
    glpfnMediaWaveSizeHWAVE = (LPVOID)GetProcAddress(ghMedWave, szWaveSize);
    glpfnMediaWaveReadHWAVE = (LPVOID)GetProcAddress(ghMedWave, szWaveRead);
    return TRUE;
}

/*
 * seg2:1348-134B  InitStub1348  (unreferenced)
 */
BOOL FAR InitStub1348(void)
{
    return TRUE;
}

/*
 * seg2:134C-1354  InitUndo  (undo.c's)
 */
void FAR InitUndo(void)
{
    gwUndoType = gwRedoType = 0;
}
