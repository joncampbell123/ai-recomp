/*
 * videdit.c - code segment 1 of VIDEDIT.EXE, s01:0010-1311 (the segment
 * starts with 16 zero bytes).
 *
 * WinMain and the message loop, Tab navigation between the controls of
 * the main window, the menus (WM_INITMENUPOPUP) and commands
 * (WM_COMMAND, including the tool bar's notifications), the window
 * title, the "save changes?" prompt and dropped files.  The main
 * window procedure itself is in segment 5 (VidEditWndProc) and calls
 * InitMenuPopup, DoCommand and DropFiles.
 *
 * Functions are in the order of the binary; unless noted they are FAR
 * _cdecl.
 */

#include "videdit.h"
#include "vemain.h"
#include "vedwnd.h"
#include "frames.h"
#include "edit.h"
#include "compress.h"
#include "stats.h"
#include "preview.h"
#include "crop.h"

#define max(a, b)   (((a) > (b)) ? (a) : (b))

/* data */
BOOL    gfInQuerySave;                  /* DS:028A */
BOOL    gfInOpen;                       /* DS:028C */
static char szFrameFmt[] = "%d)";       /* DS:028E */
WORD    gcToolRepeat;                   /* DS:1BE2 */

/*
 * s01:0010-00ED  WinMain
 *
 * Arrow and page keys go to the window ghwndTrackBar whichever control has the
 * focus.  While a preview plays the loop polls PreviewIdle instead of
 * waiting.  Quirk: if AppInit fails, the return value is an
 * uninitialised msg.wParam.
 */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow)
{
    MSG msg;

    if (AppInit(hInstance, hPrevInstance, nCmdShow, lpszCmdLine)) {
        for (;;) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                do {
                    if (msg.message == WM_QUIT)
                        goto done;
                    if (!TranslateAccelerator(ghwndApp, ghAccel, &msg)) {
                        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) &&
                            msg.wParam >= VK_PRIOR && msg.wParam <= VK_DOWN)
                            msg.hwnd = ghwndTrackBar;
                        if (!KbdNavigate(msg)) {
                            TranslateMessage(&msg);
                            DispatchMessage(&msg);
                        }
                    }
                } while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE));
            }
            if (gfPreviewing)
                PreviewIdle();
            else
                WaitMessage();
        }
    }
done:
    AppTerm();
    return msg.wParam;
}

/*
 * The main window's tool bars other than the main one (ghwndToolBar): Tab
 * gives them the focus through toolbarSetFocus, -1 for the first button
 * and -2 for the last.
 */
#define NeedsPrep(hwnd) \
    ((hwnd) == ghwndMarks || (hwnd) == ghwndTransport || (hwnd) == ghwndTracks)

/*
 * s01:00F0-04E3  KbdNavigate
 *
 * Tab and Shift+Tab (and WM_NEXTDLGCTL) move the focus between the
 * controls of the main window as in a dialog box.  Panels with child
 * controls are entered at their first (last, going back) tab stop, and
 * leaving a panel goes on to the main window's next (previous) control.
 * The message is passed by value.
 */
BOOL FAR KbdNavigate(MSG msg)
{
    DWORD   dwCode;
    HWND    hwndFocus;
    HWND    hwndParent;
    HWND    hwndNext;
    HWND    hwndPanel;
    HWND    hwndChild;
    HWND    hwndT;

    if (msg.message == WM_NEXTDLGCTL && msg.lParam != 0) {
        SetFocus((HWND)msg.wParam);
        return TRUE;
    }

    if ((msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_SHIFT) >= 0) ||
        (msg.message == WM_NEXTDLGCTL && msg.wParam == 0)) {
        /* forward */
        dwCode = SendMessage(msg.hwnd, WM_GETDLGCODE, 0, 0L);
        if ((dwCode & DLGC_WANTTAB) || (dwCode & DLGC_WANTALLKEYS))
            goto pass;
        hwndFocus = GetFocus();
        hwndParent = GetParent(hwndFocus);
        if (hwndParent == NULL)
            goto nochild;
        hwndNext = GetNextDlgTabItem(hwndParent, hwndFocus, FALSE);
        if (ghwndApp == hwndParent) {
            if (ghwndToolBar == hwndNext)
                toolbarSetFocus(hwndNext, -1);
            if (NeedsPrep(hwndNext))
                toolbarSetFocus(hwndNext, -1);
            if (GetWindow(hwndNext, GW_CHILD)) {
                hwndPanel = hwndNext;
                hwndNext = GetWindow(hwndPanel, GW_CHILD);
                hwndT = GetNextDlgTabItem(hwndPanel, GetNextDlgTabItem(hwndPanel, hwndNext, FALSE), TRUE);
                if (hwndNext != hwndT)
                    hwndNext = GetNextDlgTabItem(hwndPanel, hwndNext, FALSE);
                if (hwndT == hwndNext && !(GetWindowLong(hwndNext, GWL_STYLE) & WS_TABSTOP))
                    hwndNext = hwndPanel;
            }
            SetFocus(hwndNext);
        } else {
            hwndPanel = hwndParent;
            hwndChild = GetWindow(hwndPanel, GW_CHILD);
            hwndT = GetNextDlgTabItem(hwndPanel, GetNextDlgTabItem(hwndPanel, hwndChild, FALSE), TRUE);
            if (hwndChild != hwndT)
                hwndChild = GetNextDlgTabItem(hwndPanel, hwndChild, FALSE);
            if (hwndChild != hwndNext) {
                SetFocus(hwndNext);
            } else {
                /* wrapped around in the panel: on to the next control */
                hwndPanel = GetNextDlgTabItem(GetParent(hwndPanel), hwndPanel, FALSE);
                if (hwndPanel == ghwndToolBar)
                    toolbarSetFocus(hwndPanel, -1);
                if (NeedsPrep(hwndPanel))
                    toolbarSetFocus(hwndPanel, -1);
                hwndNext = GetWindow(hwndPanel, GW_CHILD);
                if (hwndNext == NULL) {
                    hwndNext = hwndPanel;
                } else {
                    hwndT = GetNextDlgTabItem(hwndPanel, GetNextDlgTabItem(hwndPanel, hwndNext, FALSE), TRUE);
                    if (hwndT != hwndNext)
                        hwndNext = GetNextDlgTabItem(hwndPanel, hwndNext, FALSE);
                }
                SetFocus(hwndNext);
            }
        }
        return TRUE;
    }

    if ((msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_SHIFT) < 0) ||
        (msg.message == WM_NEXTDLGCTL && msg.wParam != 0)) {
        /* backward */
        dwCode = SendMessage(msg.hwnd, WM_GETDLGCODE, 0, 0L);
        if ((dwCode & DLGC_WANTTAB) || (dwCode & DLGC_WANTALLKEYS))
            goto pass;
        hwndFocus = GetFocus();
        hwndParent = GetParent(hwndFocus);
        if (hwndParent == NULL)
            goto nochild;
        hwndNext = GetNextDlgTabItem(hwndParent, hwndFocus, TRUE);
        if (ghwndApp == hwndParent) {
            if (ghwndToolBar == hwndNext)
                toolbarSetFocus(hwndNext, -2);
            if (NeedsPrep(hwndNext))
                toolbarSetFocus(hwndNext, -2);
            if (GetWindow(hwndNext, GW_CHILD)) {
                hwndPanel = hwndNext;
                hwndNext = GetWindow(GetWindow(hwndPanel, GW_CHILD), GW_HWNDLAST);
                hwndT = GetNextDlgTabItem(hwndPanel, GetNextDlgTabItem(hwndPanel, hwndNext, TRUE), FALSE);
                if (hwndNext != hwndT)
                    hwndNext = GetNextDlgTabItem(hwndPanel, hwndNext, TRUE);
                if (hwndT == hwndNext && !(GetWindowLong(hwndNext, GWL_STYLE) & WS_TABSTOP))
                    hwndNext = hwndPanel;
            }
            SetFocus(hwndNext);
        } else {
            hwndPanel = hwndParent;
            hwndChild = GetWindow(GetWindow(hwndPanel, GW_CHILD), GW_HWNDLAST);
            hwndT = GetNextDlgTabItem(hwndPanel, GetNextDlgTabItem(hwndPanel, hwndChild, TRUE), FALSE);
            if (hwndChild != hwndT)
                hwndChild = GetNextDlgTabItem(hwndPanel, hwndChild, TRUE);
            if (hwndChild != hwndNext) {
                SetFocus(hwndNext);
            } else {
                hwndPanel = GetNextDlgTabItem(GetParent(hwndPanel), hwndPanel, TRUE);
                if (hwndPanel == ghwndToolBar)
                    toolbarSetFocus(hwndPanel, -2);
                if (NeedsPrep(hwndPanel))
                    toolbarSetFocus(hwndPanel, -2);
                hwndNext = GetWindow(hwndPanel, GW_CHILD);
                if (hwndNext == NULL) {
                    hwndNext = hwndPanel;
                } else {
                    hwndNext = GetWindow(hwndNext, GW_HWNDLAST);
                    hwndT = GetNextDlgTabItem(hwndPanel, GetNextDlgTabItem(hwndPanel, hwndNext, TRUE), FALSE);
                    if (hwndNext != hwndT)
                        hwndNext = GetNextDlgTabItem(hwndPanel, hwndNext, TRUE);
                }
                SetFocus(hwndNext);
            }
        }
        return TRUE;
    }
    return FALSE;

nochild:
    SetFocus(GetWindow(hwndFocus, GW_CHILD));
    return TRUE;

pass:
    SendMessage(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    return TRUE;
}

/*
 * s01:04E4-0835  InitMenuPopup  (FAR PASCAL; WM_INITMENUPOPUP)
 *
 * Note: max() evaluates the frame counts twice, as the original does.
 */
LRESULT FAR PASCAL InitMenuPopup(HWND hwnd, HMENU hMenu, BOOL fSystemMenu, int iPopup)
{
    UINT    wFrames;
    WORD    wSelStart;
    WORD    wSelEnd;
    UINT    fNoSel;
    UINT    fGray;
    BOOL    fSel;

    if (fSystemMenu)
        return DefWindowProc(hwnd, WM_INITMENUPOPUP, (WPARAM)hMenu, MAKELONG(iPopup, fSystemMenu));

    switch (iPopup) {
    case 0:     /* File */
        EnableMenuItem(hMenu, IDM_REVERT, (gszFileName[0] && gfDirty) ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hMenu, IDM_SAVE, gwLength == 0);
        EnableMenuItem(hMenu, IDM_SAVEAS, gwLength == 0);
        EnableMenuItem(hMenu, IDM_EXTRACT, gwLength == 0);
        EnableMenuItem(hMenu, IDM_PREVIEW, gwLength == 0);
        break;

    case 1:     /* Edit */
        SetUndoMenu(hMenu, IDM_UNDO);
        CheckMenuItem(hMenu, IDM_TRACKVIDEO, (gfEditVideo && !gfEditAudio) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hMenu, IDM_TRACKAUDIO, (gfEditAudio && !gfEditVideo) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hMenu, IDM_TRACKBOTH, (gfEditVideo && gfEditAudio) ? MF_CHECKED : MF_UNCHECKED);
        gfEditMidi = 0;
        wFrames = max(gfEditVideo ? GetFrameCount() : 0, gfEditAudio ? WaveNumFrames() : 0);
        GetSelection(&wSelStart, &wSelEnd, 0);
        fSel = (wFrames && (gwLengthShown > gwCurFrame || wSelStart != 0xFFFF)) ? TRUE : FALSE;
        fNoSel = !fSel;
        EnableMenuItem(hMenu, IDM_CUT, fNoSel);
        EnableMenuItem(hMenu, IDM_COPY, fNoSel);
        EnableMenuItem(hMenu, IDM_DELETE, fNoSel);
        EnableMenuItem(hMenu, IDM_PASTE,
                       ((gfEditVideo && CanPasteFrames()) || (gfEditAudio && WaveCanPaste())) ? MF_ENABLED : MF_GRAYED);
        fGray = GetFrameCount() ? !IsClipboardFormatAvailable(CF_PALETTE) : TRUE;
        EnableMenuItem(hMenu, IDM_PASTEPALETTE, fGray);
        EnableMenuItem(hMenu, IDM_SETSELECTION, gwLength == 0);
        EnableMenuItem(hMenu, IDM_GOTO, gwLength == 0);
        break;

    case 2:     /* View */
        CheckMenuItem(hMenu, IDM_ZOOMHALF, (gwZoom == 1) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hMenu, IDM_ZOOM1, (gwZoom == 2) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hMenu, IDM_ZOOM2, (gwZoom == 4) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hMenu, IDM_ZOOM4, (gwZoom == 8) ? MF_CHECKED : MF_UNCHECKED);
        EnableMenuItem(hMenu, IDM_DRAWFULLFRAME,
                       (gwLengthShown > gwCurFrame && gwFrameUpdate == IDM_FASTFRAMEUPDATE) ? MF_ENABLED : MF_GRAYED);
        CheckMenuItem(hMenu, IDM_FULLFRAMEUPDATE, (gwFrameUpdate == IDM_FULLFRAMEUPDATE) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(hMenu, IDM_FASTFRAMEUPDATE, (gwFrameUpdate == IDM_FASTFRAMEUPDATE) ? MF_CHECKED : MF_UNCHECKED);
        break;

    case 3:     /* Video */
        fGray = GetFrameCount() == 0;
        EnableMenuItem(hMenu, IDM_CROP, fGray);
        EnableMenuItem(hMenu, IDM_CREATEPALETTE, fGray);
        EnableMenuItem(hMenu, IDM_CONVERTFRAMERATE, fGray);
        EnableMenuItem(hMenu, IDM_VIDEOFORMAT, fGray);
        EnableMenuItem(hMenu, IDM_LOADINTOMEMORY, fGray);
        EnableMenuItem(hMenu, IDM_AUDIOFORMAT, WaveNumFrames() == 0);
        EnableMenuItem(hMenu, IDM_STATISTICS, gwLength == 0);
        EnableMenuItem(hMenu, IDM_SYNCHRONIZE, gwLength == 0);
        break;
    }
    return 0L;
}

/*
 * s01:0838-0FE1  DoCommand  (FAR PASCAL; WM_COMMAND)
 *
 * Menu and accelerator commands, and the notifications (id 189) of the
 * tool bar and other child controls.  A tool bar button is reported in
 * HIWORD(lParam): the low byte is the button, 0x100 means an
 * auto-repeat, 0x200 a "shifted" press (zoom out instead of in).  The
 * tool bar actions duplicate the menu commands; the original shares
 * their code.
 */
LRESULT FAR PASCAL DoCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[40];
    BOOL    fUndo;
    WORD    wSelEnd;
    WORD    wSelStart;
    BOOL    fCanPaste;
    WORD    w394c;
    WORD    w3676;
    BOOL    fSel;
    WORD    iButton;
    BOOL    fRepeat;
    BOOL    fShift;
    WORD    wState;
    UINT    wFrames;
    UINT    wZoom;

    switch (wParam) {
    case IDM_NEW:
        if (QuerySave()) {
            FileClear(FALSE);
            NewFile(TRUE);
        }
        break;

    case IDM_OPEN:
    open:
        gfInOpen = TRUE;
        if (QuerySave()) {
            FileOpen();
            gfPalettePasted = 0;
            ViewLayout(TRUE);
        }
        gfInOpen = FALSE;
        break;

    case IDM_SAVE:
        if (gszFileName[0]) {
            if (!gfDirty)
                break;
        } else {
            wParam = IDM_SAVEAS;
        }
        /* fall through */
    case IDM_SAVEAS:
        FileSave(wParam == IDM_SAVEAS);
        break;

    case IDM_REVERT:
        FileRevert();
        break;

    case IDM_INSERT:
        FileInsert();
        break;

    case IDM_EXTRACT:
        FileExtract();
        break;

    case IDM_CAPTURE:
        RunVidCap();
        break;

    case IDM_EXIT:
        PostMessage(ghwndApp, WM_CLOSE, 0, 0L);
        break;

    case IDM_PASTEPALETTE:
        EditPastePalette(hwnd);
        break;

    case IDM_DELETE:
        EditDelete(hwnd);
        break;

    case IDM_TRACKVIDEO:
        EditSetTracks(TRUE, FALSE, FALSE);
        toolbarModifyState(ghwndTracks, 1, BTNST_DOWN);
        LoadString(ghInst, IDS_TRACKVIDEO, ach, sizeof(ach));
        SetWindowText(ghwndStatTrack, ach);
        break;

    case IDM_TRACKAUDIO:
        EditSetTracks(FALSE, TRUE, FALSE);
        toolbarModifyState(ghwndTracks, 2, BTNST_DOWN);
        LoadString(ghInst, IDS_TRACKAUDIO, ach, sizeof(ach));
        SetWindowText(ghwndStatTrack, ach);
        break;

    case IDM_TRACKBOTH:
        EditSetTracks(TRUE, TRUE, FALSE);
        toolbarModifyState(ghwndTracks, 0, BTNST_DOWN);
        LoadString(ghInst, IDS_TRACKBOTH, ach, sizeof(ach));
        SetWindowText(ghwndStatTrack, ach);
        break;

    case IDM_PREFERENCES:
        Preferences(hwnd);
        break;

    case IDM_GOTO:
        GoTo();
        break;

    case IDM_SETSELECTION:
        SetSelectionDialog(ghwndApp);
        break;

    case IDM_INSERTMODE:
        gfInsertMode = !gfInsertMode;
        LoadString(ghInst, gfInsertMode ? IDS_INS : IDS_OVR, ach, sizeof(ach));
        SetWindowText(ghwndStatMode, ach);
        break;

    case IDM_ZOOM1:
        SetZoom(2);
        break;
    case IDM_ZOOM2:
        SetZoom(4);
        break;
    case IDM_ZOOM4:
        SetZoom(8);
        break;
    case IDM_ZOOMHALF:
        SetZoom(1);
        break;

    case IDM_FULLFRAMEUPDATE:
        gwFrameUpdate = IDM_FULLFRAMEUPDATE;
        toolbarModifyState(ghwndToolBar, 7, BTNST_GRAYED);
        InvalidateRect(ghwndFrame, NULL, FALSE);
        break;

    case IDM_FASTFRAMEUPDATE:
        gwFrameUpdate = IDM_FASTFRAMEUPDATE;
        toolbarModifyState(ghwndToolBar, 7, BTNST_UP);
        break;

    case IDM_DRAWFULLFRAME:
    drawfull:
        if (gwFrameUpdate == IDM_FASTFRAMEUPDATE) {
            gwFrameUpdate = IDM_FULLFRAMEUPDATE;
            InvalidateRect(ghwndFrame, NULL, FALSE);
            UpdateWindow(ghwndFrame);
            gwFrameUpdate = IDM_FASTFRAMEUPDATE;
        }
        break;

    case IDM_COMPRESSIONOPTIONS:
        DoCompressionOptions(hwnd);
        break;

    case IDM_CROP:
        DoCrop();
        break;

    case IDM_RESIZE:
        ResizeVideo();
        break;

    case IDM_SYNCHRONIZE:
        EditSynchronize(hwnd);
        break;

    case IDM_CREATEPALETTE:
        EditCreatePalette(hwnd);
        break;

    case IDM_LOADINTOMEMORY:
        SetUndo(0);
        if (LoadAllFrames(hwnd) && WaveLoadIntoMemory((WORD)hwnd)) {
            gfInMemory = TRUE;
            ClosePreview();
        }
        break;

    case IDM_CONVERTFRAMERATE:
        EditConvertFrameRate(hwnd);
        break;

    case IDM_STATISTICS:
        DoStatistics(hwnd);
        break;

    case IDM_AUDIOFORMAT:
        WaveChooseFormat();
        break;

    case IDM_VIDEOFORMAT:
        VideoFormat();
        break;

    case IDM_ABOUT:
        WrkShowAboutDialog(ghInst, hwnd, IDS_ABOUT, IDS_APPNAME, IDS_VERSION, IDS_COPYRIGHT, IDB_INTRO);
        break;

    case IDM_HELPCONTENTS:
        WinHelp(hwnd, gszHelpFile, HELP_INDEX, 0L);
        break;

    case IDM_ACCEL80:
        if (gfPreviewing)
            break;
        /* fall through */
    case IDM_PREVIEW:
        StartPreview(FALSE);
        break;

    case IDM_ACCEL81:
        if (gfPreviewing)
            StopPreview();
        StartPreview(TRUE);
        break;

    case IDM_ACCEL82:
        StopPreview();
        toolbarSetFocus(ghwndTransport, 0);
        break;

    case IDM_CUT:
        EditCut(hwnd);
        break;

    case IDM_COPY:
        EditCopy(hwnd);
        break;

    case IDM_PASTE:
        EditPaste(hwnd);
        break;

    case IDM_UNDO:
        Undo();
        break;

    case 189:   /* notification from a child control */
        fUndo = FALSE;
        if ((HWND)LOWORD(lParam) == ghwndMarkBar) {
            SendMessage(ghwndBlob, msg, wParam, lParam);
            break;
        }
        if (gfCropping) {
            MessageBeep(0);
            break;
        }
        if ((HWND)LOWORD(lParam) == ghwndTransport) {
            TransportCommand(hwnd, msg, wParam, lParam);
            break;
        }
        if ((HWND)LOWORD(lParam) == ghwndTracks) {
            TracksCommand(hwnd, msg, wParam, lParam);
            break;
        }
        if ((HWND)LOWORD(lParam) == ghwndMarks) {
            MarkCommand(hwnd, msg, wParam, lParam);
            break;
        }
        if ((HWND)LOWORD(lParam) != ghwndToolBar)
            break;

        /* the tool bar */
        iButton = HIWORD(lParam) & 0x00FF;
        fRepeat = (HIWORD(lParam) & 0x0100) ? TRUE : FALSE;
        fShift  = (HIWORD(lParam) & 0x0200) ? TRUE : FALSE;
        if (fRepeat)
            gcToolRepeat++;
        else
            gcToolRepeat = 0;

        if (!fRepeat) {
            if (iButton == 8) {
                if ((!fShift && gwZoom == 8) || (fShift && gwZoom == 2))
                    toolbarModifyString(ghwndToolBar, 8, IDS_TBZOOMHALF);
                else if ((!fShift && gwZoom == 1) || (fShift && gwZoom == 4))
                    toolbarModifyString(ghwndToolBar, 8, IDS_TBZOOM1);
                else if ((!fShift && gwZoom == 2) || (fShift && gwZoom == 8))
                    toolbarModifyString(ghwndToolBar, 8, IDS_TBZOOM2);
                else
                    toolbarModifyString(ghwndToolBar, 8, IDS_TBZOOM4);
            }
            if (iButton == 5)
                fUndo = SetUndoMenu(GetMenu(ghwndApp), IDM_UNDO);
            GetSelection(&wSelStart, &wSelEnd, 0);
            wFrames = max(gfEditVideo ? GetFrameCount() : 0, gfEditAudio ? WaveNumFrames() : 0);
            fSel = (wFrames && (gwLengthShown > gwCurFrame || wSelStart != 0xFFFF)) ? TRUE : FALSE;
            fCanPaste = ((gfEditVideo && CanPasteFrames()) || (gfEditAudio && WaveCanPaste())) ? TRUE : FALSE;
            switch (iButton) {
            case 2:
                toolbarModifyString(ghwndToolBar, 2, fSel ? IDS_TBCUT : IDS_TBNOCUT);
                break;
            case 3:
                toolbarModifyString(ghwndToolBar, 3, fSel ? IDS_TBCOPY : IDS_TBNOCOPY);
                break;
            case 4:
                toolbarModifyString(ghwndToolBar, 4, fCanPaste ? IDS_TBPASTE : IDS_TBNOPASTE);
                break;
            }
        }

        w3676 = toolbarIndexFromButton(ghwndToolBar, iButton);
        toolbarFullStateFromButton(ghwndToolBar, iButton);
        wState = toolbarActivityFromButton(ghwndToolBar, iButton);
        w394c = toolbarStringFromIndex(ghwndToolBar, w3676);
        if ((wState == 0 || wState == 5 || wState == 3) && gcToolRepeat == 1)
            StatusSetMessage((LPCSTR)MAKELONG(w394c, 0));
        if (wState == 2)
            StatusSetMessage(NULL);
        if (wState != 1 && wState != 6)
            break;
        StatusSetMessage(NULL);

        switch (iButton) {
        case 0:
            goto open;
        case 1:
            if (!gwLength)
                break;
            if (!gszFileName[0])
                FileSave(TRUE);
            else if (gfDirty)
                FileSave(FALSE);
            break;
        case 2:
            if (fSel)
                EditCut(ghwndApp);
            break;
        case 3:
            if (fSel)
                EditCopy(ghwndApp);
            break;
        case 4:
            if (fCanPaste)
                EditPaste(ghwndApp);
            break;
        case 5:
            if (fUndo)
                Undo();
            break;
        case 6:
            RunVidCap();
            break;
        case 7:
            goto drawfull;
        case 8:
            UpdateWindow(ghwndToolBar);
            if (fShift)
                wZoom = (gwZoom == 1) ? 8 : gwZoom >> 1;
            else
                wZoom = (gwZoom == 8) ? 1 : gwZoom + gwZoom;
            SetZoom(wZoom);
            break;
        }
        break;
    }
    return 0L;
}

/*
 * s01:0FE4-0FEF  SetDirty
 */
void FAR SetDirty(void)
{
    gfDirty = TRUE;
    ClosePreview();
}

/*
 * s01:0FF0-10E4  UpdateTitle
 *
 * "VidEdit - NAME.AVI", plus " (Frame n)" with FrameNumInTitle.
 */
void FAR UpdateTitle(void)
{
    char achTitle[150];
    char achPrefix[50];
    char achName[50];
    char achFrame[20];
    char achNum[20];
    char achFName[10];
    char achExt[6];

    if (!gszFileName[0]) {
        LoadString(ghInst, IDS_UNTITLED, achName, sizeof(achName));
    } else {
        SplitPath(gszFileName, NULL, NULL, achFName, achExt);
        lstrcpy(achName, achFName);
        lstrcat(achName, achExt);
        AnsiUpper(achName);
    }
    LoadString(ghInst, IDS_TITLEPREFIX, achPrefix, sizeof(achPrefix));
    lstrcpy(achTitle, achPrefix);
    lstrcat(achTitle, achName);
    if (gfFrameNumInTitle) {
        LoadString(ghInst, IDS_TITLEFRAME, achFrame, sizeof(achFrame));
        wsprintf(achNum, szFrameFmt, gwCurFrame);
        lstrcat(achTitle, achFrame);
        lstrcat(achTitle, achNum);
    }
    SetWindowText(ghwndApp, achTitle);
}

/*
 * s01:10E6-118E  UpdateTitles
 *
 * Sets "VidEdit Control - NAME.AVI" and then overwrites it with
 * UpdateTitle's title.
 */
void FAR UpdateTitles(void)
{
    char achTitle[100];
    char achPrefix[50];
    char achName[50];
    char achFName[10];
    char achExt[6];

    if (!gszFileName[0]) {
        LoadString(ghInst, IDS_UNTITLED, achName, sizeof(achName));
    } else {
        SplitPath(gszFileName, NULL, NULL, achFName, achExt);
        lstrcpy(achName, achFName);
        lstrcat(achName, achExt);
        AnsiUpper(achName);
    }
    LoadString(ghInst, IDS_CONTROLTITLE, achPrefix, sizeof(achPrefix));
    lstrcpy(achTitle, achPrefix);
    lstrcat(achTitle, achName);
    SetWindowText(ghwndApp, achTitle);
    UpdateTitle();
}

/*
 * s01:1190-1266  QuerySave
 *
 * Asks whether to save a changed movie.  Returns FALSE for Cancel, or if
 * the save fails.
 */
BOOL FAR QuerySave(void)
{
    char    achName[14];
    char    achExt[6];
    int     id;

    gfInQuerySave = TRUE;
    if (gfDirty && gwLength) {
        EnableWindow(ghwndView, FALSE);
        if (gszFileName[0]) {
            SplitPath(gszFileName, NULL, NULL, achName, achExt);
            lstrcat(achName, achExt);
            AnsiUpper(achName);
            id = ErrorResBox(ghwndApp, ghInst, MB_YESNOCANCEL | MB_ICONQUESTION | MB_TASKMODAL,
                             IDS_APPTITLE, IDS_SAVECHANGES, (LPSTR)achName);
        } else {
            id = ErrorResBox(ghwndApp, ghInst, MB_YESNOCANCEL | MB_ICONQUESTION | MB_TASKMODAL,
                             IDS_APPTITLE, IDS_SAVECHANGESUNTITLED);
        }
        EnableWindow(ghwndView, TRUE);
        if (id == IDYES && !FileSave(FALSE))
            id = IDCANCEL;
    } else {
        id = IDYES;
    }
    gfInQuerySave = FALSE;
    return (id != IDCANCEL) ? TRUE : FALSE;
}

/*
 * s01:1268-12E6  DropFiles  (FAR PASCAL; WM_DROPFILES)
 *
 * Only one file dropped on the main window is opened (after asking to
 * save); files dropped on other windows are ignored.
 */
void FAR PASCAL DropFiles(HWND hwnd, HANDLE hDrop)
{
    char    achFile[144];
    POINT   pt;
    UINT    n;
    UINT    i;

    DragQueryPoint(hDrop, &pt);
    n = DragQueryFile(hDrop, 0xFFFF, NULL, 0);
    if (hwnd == ghwndApp && n > 1)
        n = 1;
    for (i = 0; i < n; i++) {
        DragQueryFile(hDrop, i, achFile, sizeof(achFile));
        if (hwnd == ghwndApp) {
            if (!QuerySave())
                break;
            OpenFileByName(achFile);
        }
    }
    DragFinish(hDrop);
}

/*
 * s01:12E8-12FE  DosRename
 *
 * INT 21h function 56h.  Returns 0 or the DOS error code.
 */
int FAR _cdecl DosRename(LPSTR lpszOld, LPSTR lpszNew)
{
    int err = 0;                    /* set by the assembly */

    _asm {
        push    ds
        lds     dx, lpszOld
        les     di, lpszNew
        mov     ah, 56h
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     ds
        mov     err, ax
    }
    return err;
}

/*
 * s01:1300-1311  DosDelete
 *
 * INT 21h function 41h.  Returns 0 or the DOS error code.
 */
int FAR _cdecl DosDelete(LPSTR lpszFile)
{
    int err = 0;                    /* set by the assembly */

    _asm {
        push    ds
        lds     dx, lpszFile
        mov     ah, 41h
        int     21h
        jc      fail
        xor     ax, ax
    fail:
        pop     ds
        mov     err, ax
    }
    return err;
}
