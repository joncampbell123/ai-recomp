/*
 * vidcap.c - code segment 1 of VIDCAP.EXE, 1A0C-2693: WinMain, keyboard
 * navigation between the child windows of the main window, and the
 * menus.
 *
 * VidCap's main window ("VidCap" class, CtrlWndProc in frame.c) holds the
 * toolbar, a scrolling frame with the video window ("ShowWindow") and the
 * status bar.  All menu commands come here through MenuCommand.
 */

#include <windows.h>
#include "vidcap.h"

char        gszIniFile[]    = "mmtools.ini";        /* DS:00F8 */
char        gszAppName[]    = "VidCap";             /* DS:0104 */
char        gszEditClass[]  = "VidcapEditWindow";   /* DS:010C */
FARPROC     ghhkMsgFilter   = NULL;                 /* DS:011E previous WH_MSGFILTER hook */
FARPROC     glpfnMsgFilter  = NULL;                 /* DS:0122 */
HINSTANCE   ghInst          = NULL;                 /* DS:0126 */
HACCEL      ghAccel         = NULL;                 /* DS:0128 */
HDC         ghdcUnused      = NULL;                 /* DS:012A deleted, never created */
char        gszRegisterPenApp[] = "RegisterPenApp"; /* DS:012C */
BOOL        gfInitDone      = FALSE;                /* DS:013C save the settings on exit */

/*
 * seg1:1A0C-1AC2  WinMain
 */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow)
{
    MSG msg;

    if (AppInit(hInstance, hPrevInstance, nCmdShow, lpszCmdLine)) {
        for (;;) {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT)
                    goto done;
                if (!TranslateAccelerator(ghwndApp, ghAccel, &msg) && !TabMessage(msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            WaitMessage();
        }
    }
done:
    AppTerm();
    return msg.wParam;
}

/*
 * Returns the first (fLast FALSE) or last tab stop among the children of
 * hwndParent.  If there is none, the first or last child is returned.
 */
#define FIRSTTAB(hwndParent, hwnd)                                                      \
    if (GetNextDlgTabItem(hwndParent, GetNextDlgTabItem(hwndParent, hwnd, FALSE), TRUE) != hwnd) \
        hwnd = GetNextDlgTabItem(hwndParent, hwnd, FALSE)
#define LASTTAB(hwndParent, hwnd)                                                       \
    if (GetNextDlgTabItem(hwndParent, GetNextDlgTabItem(hwndParent, hwnd, TRUE), FALSE) != hwnd) \
        hwnd = GetNextDlgTabItem(hwndParent, hwnd, TRUE)

/*
 * seg1:1AC6-1E5A  TabMessage  (FAR CDECL)
 *
 * Tab and Shift+Tab (or WM_NEXTDLGCTL sent to a control) move the focus
 * through the main window's children and through the controls inside
 * them, like a dialog box.  Returns TRUE if the message was used.  A
 * control that wants Tab gets the message sent directly.
 */
BOOL FAR TabMessage(MSG msg)
{
    LONG    lCode;
    HWND    hwndFocus;
    HWND    hwndParent;
    HWND    hwndNext;
    HWND    hwndChild;
    HWND    hwndFirst;
    HWND    hwnd;

    if (msg.message == WM_NEXTDLGCTL && msg.lParam != 0L) {
        SetFocus((HWND)msg.wParam);
        return TRUE;
    }

    if ((msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_SHIFT) >= 0) ||
        (msg.message == WM_NEXTDLGCTL && msg.wParam == 0)) {
        /* forward */
        lCode = SendMessage(msg.hwnd, WM_GETDLGCODE, 0, 0L);
        if ((lCode & DLGC_WANTTAB) || (lCode & DLGC_WANTALLKEYS))
            goto pass;

        hwndFocus = GetFocus();
        hwndParent = GetParent(hwndFocus);
        if (hwndParent == NULL)
            goto nochild;
        hwnd = GetNextDlgTabItem(hwndParent, hwndFocus, FALSE);

        if (hwndParent == ghwndApp) {
            /* from one child of the main window to the next */
            if (hwnd == ghwndToolbar)
                toolbarSetFocus(hwnd, -1);
            if (GetWindow(hwnd, GW_CHILD)) {
                hwndChild = hwnd;
                hwnd = GetWindow(hwnd, GW_CHILD);
                hwndFirst = GetNextDlgTabItem(hwndChild, GetNextDlgTabItem(hwndChild, hwnd, FALSE), TRUE);
                if (hwnd != hwndFirst)
                    hwnd = GetNextDlgTabItem(hwndChild, hwnd, FALSE);
                if (hwndFirst == hwnd && !(GetWindowLong(hwnd, GWL_STYLE) & WS_TABSTOP))
                    hwnd = hwndChild;
            }
        } else {
            /* inside a child: past its last control, on to the next child */
            hwndNext = hwnd;
            hwnd = GetWindow(hwndParent, GW_CHILD);
            FIRSTTAB(hwndParent, hwnd);
            if (hwnd == hwndNext) {
                hwndParent = GetNextDlgTabItem(GetParent(hwndParent), hwndParent, FALSE);
                if (hwndParent == ghwndToolbar)
                    toolbarSetFocus(hwndParent, -1);
                hwnd = GetWindow(hwndParent, GW_CHILD);
                if (hwnd == NULL)
                    hwnd = hwndParent;
                else
                    FIRSTTAB(hwndParent, hwnd);
            } else {
                hwnd = hwndNext;
            }
        }
        SetFocus(hwnd);
        return TRUE;
    }

    if ((msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_SHIFT) < 0) ||
        (msg.message == WM_NEXTDLGCTL && msg.wParam != 0)) {
        /* backward */
        lCode = SendMessage(msg.hwnd, WM_GETDLGCODE, 0, 0L);
        if ((lCode & DLGC_WANTTAB) || (lCode & DLGC_WANTALLKEYS))
            goto pass;

        hwndFocus = GetFocus();
        hwndParent = GetParent(hwndFocus);
        if (hwndParent == NULL)
            goto nochild;
        hwnd = GetNextDlgTabItem(hwndParent, hwndFocus, TRUE);

        if (hwndParent == ghwndApp) {
            if (hwnd == ghwndToolbar)
                toolbarSetFocus(hwnd, -2);
            if (GetWindow(hwnd, GW_CHILD)) {
                hwndChild = hwnd;
                hwnd = GetWindow(GetWindow(hwnd, GW_CHILD), GW_HWNDLAST);
                hwndFirst = GetNextDlgTabItem(hwndChild, GetNextDlgTabItem(hwndChild, hwnd, TRUE), FALSE);
                if (hwnd != hwndFirst)
                    hwnd = GetNextDlgTabItem(hwndChild, hwnd, TRUE);
                if (hwndFirst == hwnd && !(GetWindowLong(hwnd, GWL_STYLE) & WS_TABSTOP))
                    hwnd = hwndChild;
            }
        } else {
            hwndNext = hwnd;
            hwnd = GetWindow(GetWindow(hwndParent, GW_CHILD), GW_HWNDLAST);
            LASTTAB(hwndParent, hwnd);
            if (hwnd == hwndNext) {
                hwndParent = GetNextDlgTabItem(GetParent(hwndParent), hwndParent, TRUE);
                if (hwndParent == ghwndToolbar)
                    toolbarSetFocus(hwndParent, -2);
                hwnd = GetWindow(hwndParent, GW_CHILD);
                if (hwnd == NULL) {
                    hwnd = hwndParent;
                } else {
                    hwnd = GetWindow(hwnd, GW_HWNDLAST);
                    LASTTAB(hwndParent, hwnd);
                }
            } else {
                hwnd = hwndNext;
            }
        }
        SetFocus(hwnd);
        return TRUE;
    }

    return FALSE;

nochild:
    hwndFocus = GetWindow(hwndFocus, GW_CHILD);
    SetFocus(hwndFocus);
    return TRUE;

pass:
    SendMessage(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    return TRUE;
}

/*
 * seg1:1E5C-20B3  InitMenuPopup  (FAR CDECL)
 */
LRESULT FAR InitMenuPopup(HWND hwnd, HMENU hmenu, int iMenu, BOOL fSystemMenu)
{
    UINT wGray;

    if (fSystemMenu)
        return DefWindowProc(hwnd, WM_INITMENUPOPUP, (WPARAM)hmenu, MAKELONG(iMenu, fSystemMenu));

    switch (iMenu) {
    case 0:     /* File */
        EnableMenuItem(hmenu, IDM_EDITCAPTURE,
                       (gachCaptureFile[0] && gfCaptureFileHasData) ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_SAVEAS, gachCaptureFile[0] ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_SAVEPALETTE, (gfPalettized && ghpalCurrent) ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_LOADPALETTE, gfPalettized ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_SAVEFRAME, HaveFrame() ? MF_ENABLED : MF_GRAYED);
        break;

    case 1:     /* Edit */
        EnableMenuItem(hmenu, IDM_PASTEPALETTE,
                       (gfPalettized && IsClipboardFormatAvailable(CF_PALETTE)) ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_COPY, HaveFrame() ? MF_ENABLED : MF_GRAYED);
        break;

    case 2:     /* Options */
        EnableMenuItem(hmenu, IDM_AUDIOFORMAT, gfAudioDevice ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_PREVIEW, gfVideoDevice ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_OVERLAY, gfHasOverlay ? MF_ENABLED : MF_GRAYED);
        CheckMenuItem(hmenu, IDM_PREVIEW, gfPreview ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hmenu, IDM_OVERLAY, gfOverlay ? MF_CHECKED : MF_UNCHECKED);
        EnableMenuItem(hmenu, IDM_VIDEOSOURCE,
                       (gfVideoDevice && videoDialog(ghVideoExtIn, ghwndApp, VIDEO_DLG_QUERY) == DV_ERR_OK)
                       ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_VIDEOFORMAT,
                       (gfVideoDevice && videoDialog(ghVideoIn, ghwndApp, VIDEO_DLG_QUERY) == DV_ERR_OK)
                       ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hmenu, IDM_VIDEODISPLAY,
                       (gfVideoDevice && ghVideoExtOut &&
                        videoDialog(ghVideoExtOut, ghwndApp, VIDEO_DLG_QUERY) == DV_ERR_OK)
                       ? MF_ENABLED : MF_GRAYED);
        break;

    case 3:     /* Capture */
        wGray = gfVideoDevice ? MF_ENABLED : MF_GRAYED;
        EnableMenuItem(hmenu, IDM_CAPSINGLE, wGray);
        EnableMenuItem(hmenu, IDM_CAPFRAMES, wGray);
        EnableMenuItem(hmenu, IDM_CAPVIDEO, wGray);
        EnableMenuItem(hmenu, IDM_CAPPALETTE, (gfVideoDevice & gfPalettized) ? MF_ENABLED : MF_GRAYED);
        break;
    }

    return 0L;
}

/*
 * seg1:20B4-2693  MenuCommand  (FAR CDECL)
 *
 * WM_COMMAND from the menu, the accelerators and the toolbar.  The
 * toolbar's notification has the button in the low byte of HIWORD(lParam).
 */
LRESULT FAR MenuCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[64];
    int     iButton;
    int     iIndex;
    int     iActivity;
    int     iString;
    BOOL    f;

    switch (wParam) {
    case IDC_TOOLBAR:
        if (HIWORD(lParam) & 0x0100)
            break;
        iButton = HIWORD(lParam) & 0x00FF;
        iIndex = toolbarIndexFromButton(ghwndToolbar, iButton);
        toolbarFullStateFromButton(ghwndToolbar, iButton);
        iActivity = toolbarActivityFromButton(ghwndToolbar, iButton);
        iString = toolbarStringFromIndex(ghwndToolbar, iIndex);

        if (iActivity == BTNACT_MOUSEDOWN || iActivity == BTNACT_KEYDOWN ||
            iActivity == BTNACT_MOUSEMOVEON)
            StatusText(MAKEINTRESOURCE(iString));
        if (iActivity == BTNACT_MOUSEMOVEOFF)
            StatusText(NULL);
        if (iActivity != BTNACT_MOUSEUP && iActivity != BTNACT_KEYUP)
            break;
        StatusText(NULL);

        switch (iButton) {
        case BTN_SETFILE:   SetCaptureFile();                                   break;
        case BTN_EDIT:      EditCapture();                                      break;
        case BTN_PREVIEW:   SendMessage(hwnd, WM_COMMAND, IDM_PREVIEW, 0L);    break;
        case BTN_SINGLE:    SendMessage(hwnd, WM_COMMAND, IDM_CAPSINGLE, 0L);  break;
        case BTN_FRAMES:    SendMessage(hwnd, WM_COMMAND, IDM_CAPFRAMES, 0L);  break;
        case BTN_VIDEO:     SendMessage(hwnd, WM_COMMAND, IDM_CAPVIDEO, 0L);   break;
        case BTN_PALETTE:   CapturePalette();                                   break;
        case BTN_OVERLAY:   SendMessage(hwnd, WM_COMMAND, IDM_OVERLAY, 0L);    break;
        }
        break;

    case IDM_EXIT:
        PostMessage(hwnd, WM_CLOSE, 0, 0L);
        break;

    case IDM_LOADPALETTE:
        LoadPalette();
        break;

    case IDM_SETCAPFILE:
        SetCaptureFile();
        break;

    case IDM_EDITCAPTURE:
        EditCapture();
        break;

    case IDM_SAVEAS:
        SaveCaptureAs();
        break;

    case IDM_SAVEPALETTE:
        SavePalette();
        break;

    case IDM_SAVEFRAME:
        SaveFrame();
        break;

    case IDM_COPY:
        if (!OpenClipboard(ghwndApp))
            break;
        EmptyClipboard();
        if (glpbiCapture->biBitCount <= 8)
            SetClipboardData(CF_PALETTE, CopyPalette(ghpalCurrent));
        SetClipboardData(CF_DIB, FrameToDIB());
        CloseClipboard();
        break;

    case IDM_PASTEPALETTE:
        {
            HPALETTE hpal;

            if (!OpenClipboard(ghwndApp))
                break;
            hpal = GetClipboardData(CF_PALETTE);
            CloseClipboard();
            if (hpal == NULL)
                break;
            SetCapturePalette(CopyPalette(hpal), NULL);
            InvalidateRect(ghwndShow, NULL, TRUE);
        }
        break;

    case IDM_PREFERENCES:
        DoDialogParam(IDD_PREFERENCES, ghwndApp, (FARPROC)PrefsDlgProc, 0L);
        break;

    case IDM_AUDIOFORMAT:
        DoDialogParam(IDD_AUDIOFORMAT, ghwndApp, (FARPROC)AudioFormatDlgProc, 0L);
        break;

    case IDM_VIDEOSOURCE:
        gidHelpContext = IDH_VIDEOSOURCE;
        videoDialog(ghVideoExtIn, ghwndApp, 0L);
        goto formatchanged;

    case IDM_VIDEOFORMAT:
        gidHelpContext = IDH_VIDEOFORMAT;
        videoDialog(ghVideoIn, ghwndApp, 0L);
    formatchanged:
        gidHelpContext = 0;
        GetCaptureFormat();
        GetCapturePalette();
        SizeShowWindow(TRUE);
    redraw:
        InvalidateRect(ghwndShow, NULL, TRUE);
        UpdateWindow(ghwndShow);
        break;

    case IDM_VIDEODISPLAY:
        gidHelpContext = IDH_VIDEODISPLAY;
        videoDialog(ghVideoExtOut, ghwndApp, 0L);
        gidHelpContext = 0;
        break;

    case IDM_PREVIEW:
        if (!gfPreview) {
            if (gfHasOverlay && gfOverlay)
                SendMessage(hwnd, WM_COMMAND, IDM_OVERLAY, 0L);
            gidTimer = SetTimer(ghwndShow, 1, 1, NULL);
            gfPreview = TRUE;
            StatusText(MAKEINTRESOURCE(IDS_LIVEWINDOW));
            InvalidateRect(ghwndShow, NULL, TRUE);
        } else {
            if (gidTimer)
                KillTimer(ghwndShow, gidTimer);
            gidTimer = 0;
            gfPreview = FALSE;
            StatusText(NULL);
        }
        toolbarModifyState(ghwndToolbar, BTN_PREVIEW, gfPreview ? BTNST_DOWN : BTNST_UP);
        break;

    case IDM_OVERLAY:
        if (!gfHasOverlay)
            break;
        if (!gfOverlay) {
            if (gfPreview)
                SendMessage(hwnd, WM_COMMAND, IDM_PREVIEW, 0L);
            gfOverlay = TRUE;
            gidTimer = SetTimer(ghwndShow, 1, 250, NULL);
            InvalidateRect(ghwndShow, NULL, TRUE);
            UpdateWindow(ghwndShow);
            StatusText(MAKEINTRESOURCE(IDS_OVERLAYWINDOW));
        } else {
            if (gidTimer)
                KillTimer(ghwndShow, gidTimer);
            gidTimer = 0;
            gfOverlay = FALSE;
            InvalidateRect(ghwndShow, NULL, TRUE);
            UpdateWindow(ghwndShow);
            videoStreamFini(ghVideoExtOut);
            StatusText(NULL);
        }
        toolbarModifyState(ghwndToolbar, BTN_OVERLAY, gfOverlay ? BTNST_DOWN : BTNST_UP);
        break;

    case IDM_COMPRESSION:
        gidHelpContext = IDH_COMPRESSION;
        ICCompressorChoose(ghwndApp, ICMF_CHOOSE_KEYFRAME, glpbiCapture, NULL, &gCompVars, NULL);
        gidHelpContext = 0;
        break;

    case IDM_CAPSINGLE:
        videoFrame(ghVideoIn, &gvhFrame);
        if (gfPreview)
            SendMessage(hwnd, WM_COMMAND, IDM_PREVIEW, 0L);
        else if (gfOverlay)
            SendMessage(hwnd, WM_COMMAND, IDM_OVERLAY, 0L);
        goto redraw;

    case IDM_CAPFRAMES:
        CaptureFrames();
        break;

    case IDM_CAPVIDEO:
        if (!gfWarnedDefaultPal && gfDefaultPal && glpbiCapture->biBitCount <= 8 &&
            glpbiCapture->biClrUsed == 0) {
            gfWarnedDefaultPal = TRUE;
            LoadString(ghInst, IDS_WARN_DEFAULTPAL, ach, sizeof(ach));
            if (MessageBox(ghwndApp, ach, gszAppName, MB_OKCANCEL | MB_ICONEXCLAMATION) == IDCANCEL)
                break;
        }
        f = DoDialogParam(IDD_CAPSEQUENCE, ghwndApp, (FARPROC)CapSequenceDlgProc, 0L);
        UpdateWindow(ghwndShow);
        UpdateWindow(ghwndFrame);
        if (f) {
            if (gfMCIControl && !gfMCIPlay)
                MCIStepCapture();
            else
                CaptureVideo();
        }
        break;

    case IDM_CAPPALETTE:
        CapturePalette();
        break;

    case IDM_HELPCONTENTS:
        WinHelp(hwnd, gachHelpFile, HELP_INDEX, 0L);
        break;

    case IDM_ABOUT:
        AboutBox(ghInst, hwnd, IDS_ABOUT, IDS_TITLE, IDS_VERSION, IDS_COPYRIGHT, IDB_INTRO);
        break;
    }

    return 0L;
}
