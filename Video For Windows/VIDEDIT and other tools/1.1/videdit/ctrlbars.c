/*
 * ctrlbars.c - the control bars and the selection: code segment 5,
 * s05:4E56-62A0
 *
 * Under the trackbar: the mark bar (ghwndMarks: Mark In and Mark Out, with
 * the marks as text beside them in four small "GrayBlob" windows), the
 * transport bar (ghwndTransport: play, stop, previous mark or start, rewind,
 * forward, next mark or end) and the tracks bar (ghwndTracks: radio buttons
 * for both tracks, video, audio).  The selection is the range between the
 * marks (DS:11A8-11AA, -1 when unset), shown as ticks on the trackbar.
 * Holding a button shows its help line after the first repeat.  Also the
 * Set Selection dialog.  The module's data is DS:118E-1283.
 */

#include "videdit.h"
#include <string.h>
#include "vedwnd.h"

/* Set Selection dialog (105) */
#define IDC_SEL_ALL             220
#define IDC_SEL_FROM            221
#define IDC_SEL_NONE            222
#define IDC_SEL_START           223
#define IDC_SEL_END             224
#define IDC_SEL_SIZE            225

#define IDS_SETSEL_TIME         480     /* "Set Selection - Time" */
#define IDS_SETSEL_FRAMES       481     /* "Set Selection - Frames" */
#define IDS_APPTITLE            126     /* "VidEdit" */
#define IDS_BADRANGE            200     /* "Illegal range of frames." */
#define IDS_BEGINNING           1052
#define IDS_END                 1055
#define IDS_MARKIN              1060    /* "Mark In:" */
#define IDS_MARKOUT             1061    /* "Mark Out:" */
#define IDS_PREVMARK            1062
#define IDS_NEXTMARK            1063
#define IDS_ZOOMHALF            1046    /* the zoom button's help lines */
#define IDS_ZOOM1               1047
#define IDS_ZOOM2               1048
#define IDS_ZOOM4               1049

#define IDB_MARKBAR             110
#define IDB_TRANSPORT           111
#define IDB_TRACKS              112

/* initialised data */
BOOL    gfSetSelNested  = FALSE;        /* DS:11A6 Set Selection called from another dialog: only "From" */
WORD    gwSelStart      = 0xFFFF;       /* DS:11A8 */
WORD    gwSelEnd        = 0xFFFF;       /* DS:11AA */
BOOL    gfSelDlgBusy    = FALSE;        /* DS:11AC */

/* DS:11AE-121B: the buttons of the three bars: 0-1 mark bar, 2-7 transport, 8-10 tracks */
static int  gaiButton[11] = { 0, 1,  0, 1, 2, 3, 4, 5,  0, 1, 2 };
static int  gaiState[11]  = { 1, 1,  0, 0, 1, 1, 1, 1,  1, 1, 1 };
static int  gaiType[11]   = { 0, 0,  0, 0, 0, 0, 0, 0,  3, 3, 3 };
static int  gaiString[11] = { 1044, 1045,  1050, 1051, 1052, 1053, 1054, 1055,  1056, 1057, 1058 };
static int  gaxButton[11] = { 6, 150,  6, 29, 62, 85, 108, 131,  15, 38, 61 };

/* uninitialised data */
HFONT   ghfontMarks;                    /* DS:2A88 12 point Helv */
HFONT   ghfontMarksBold;                /* DS:2A8A */
BOOL    gfSelEditSize;                  /* DS:2A8C the size field was edited */
int     gnTransportRepeat;              /* DS:2A8E */
int     gnTracksRepeat;                 /* DS:2A90 */
int     gnMarkRepeat;                   /* DS:2A92 */
HWND    ghwndMarkInLabel;               /* DS:2D0E */
int     gcyCtrlBars;                    /* DS:305E height of the bars (50) */
HWND    ghwndMarkInValue;               /* DS:3266 */
HWND    ghwndMarkOutLabel;              /* DS:31D6 */
HWND    ghwndMarkOutValue;              /* DS:2D4E */

/*
 * s05:4E56-4E8C  SetSelectionDialog  (FAR PASCAL)
 */
int FAR PASCAL SetSelectionDialog(HWND hwnd)
{
    FARPROC lpfn;
    int     r;

    lpfn = MakeProcInstance((FARPROC)SetSelectionDlgProc, ghInst);
    r = DoDialog(IDD_SETSELECTION, hwnd, lpfn);
    FreeProcInstance(lpfn);
    return r;
}

/*
 * s05:4E90-5318  SetSelectionDlgProc  (dialog procedure of "Set Selection")
 *
 * From, To and Size (aviframebox controls) are kept consistent when one of
 * them loses the focus.  OK refuses an empty or reversed range or one past
 * the end ("Illegal range of frames.").
 */
BOOL FAR PASCAL SetSelectionDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[80];
    BOOL    fOk;
    WORD    wStart, wEnd, wSize;
    int     id;

    wEnd = gwSelEnd;

    switch (msg) {
    case WM_INITDIALOG:
        LoadString(ghInst, gfTimeFormat ? IDS_SETSEL_TIME : IDS_SETSEL_FRAMES, ach, sizeof(ach));
        SetWindowText(hDlg, ach);

        if (gwSelStart != 0xFFFF && gwSelEnd != 0xFFFF) {
            SetDlgItemInt(hDlg, IDC_SEL_START, gwSelStart, FALSE);
            SetDlgItemInt(hDlg, IDC_SEL_END, gwSelEnd, FALSE);
            SetDlgItemInt(hDlg, IDC_SEL_SIZE, gwSelEnd - gwSelStart, FALSE);
        } else {
            SetDlgItemInt(hDlg, IDC_SEL_START, gwCurFrame, FALSE);
            SetDlgItemInt(hDlg, IDC_SEL_END, gwCurFrame + 1, FALSE);
            SetDlgItemInt(hDlg, IDC_SEL_SIZE, 1, FALSE);
        }

        if (gfSetSelNested) {
            EnableWindow(GetDlgItem(hDlg, IDC_SEL_NONE), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_SEL_ALL), FALSE);
            CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_FROM);
        } else if (gwSelStart != 0xFFFF && gwSelEnd != 0xFFFF) {
            if (gwSelStart == 0 && gwLength == gwSelEnd)
                CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_ALL);
            else
                CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_FROM);
        } else {
            CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_NONE);
        }
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDOK), 1L);
            if (IsDlgButtonChecked(hDlg, IDC_SEL_ALL)) {
                wStart = 0;
                wEnd = gwLength;
            } else if (IsDlgButtonChecked(hDlg, IDC_SEL_NONE)) {
                wStart = wEnd = 0xFFFF;
            } else {
                wStart = GetDlgItemInt(hDlg, IDC_SEL_START, &fOk, FALSE);
                if (!fOk) {
                    id = IDC_SEL_START;
                } else {
                    wEnd = GetDlgItemInt(hDlg, IDC_SEL_END, &fOk, FALSE);
                    id = IDC_SEL_END;
                }
                if (!fOk || wEnd <= wStart || (int)wStart < 0 || wEnd > gwLength) {
                    ErrorResBox(hDlg, ghInst, MB_ICONEXCLAMATION, IDS_APPTITLE, IDS_BADRANGE);
                    SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, id), 1L);
                    SendMessage(GetDlgItem(hDlg, id), EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
                    return TRUE;
                }
            }
            SetSelection(wStart, wEnd);
            EndDialog(hDlg, TRUE);
            return FALSE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return FALSE;

        case IDC_SEL_ALL:
            CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_ALL);
            return FALSE;

        case IDC_SEL_FROM:
            CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_FROM);
            SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDC_SEL_START), 1L);
            return FALSE;

        case IDC_SEL_NONE:
            CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_NONE);
            return FALSE;

        case IDC_SEL_START:
        case IDC_SEL_END:
        case IDC_SEL_SIZE:
            if (!IsDlgButtonChecked(hDlg, IDC_SEL_FROM))
                CheckRadioButton(hDlg, IDC_SEL_ALL, IDC_SEL_NONE, IDC_SEL_FROM);
            if (gfSelDlgBusy || HIWORD(lParam) != EN_KILLFOCUS)
                return FALSE;
            gfSelEditSize = (wParam == IDC_SEL_SIZE);
            gfSelDlgBusy = TRUE;

            wStart = GetDlgItemInt(hDlg, IDC_SEL_START, &fOk, FALSE);
            if (!fOk)
                goto beep;
            if ((int)wStart < 0) {
                MessageBeep(0);
                wStart = 0;
            }
            if (gwLength < wStart) {
                MessageBeep(0);
                wStart = gwLength;
            }
            SetDlgItemInt(hDlg, IDC_SEL_START, wStart, FALSE);

            if (!gfSelEditSize) {
                /* From or To changed: compute the size */
                wEnd = GetDlgItemInt(hDlg, IDC_SEL_END, &fOk, FALSE);
                if (!fOk)
                    goto beep;
                if (wStart < wEnd) {
                    if (gwLength < wEnd) {
                        MessageBeep(0);
                        wEnd = gwLength;
                    }
                    SetDlgItemInt(hDlg, IDC_SEL_SIZE, wEnd - wStart, FALSE);
                    SetDlgItemInt(hDlg, IDC_SEL_END, wEnd, FALSE);
                    goto done;
                }
                SetDlgItemInt(hDlg, IDC_SEL_SIZE, 1, FALSE);
            }

            /* Size changed (or To was not after From): compute To */
            wSize = GetDlgItemInt(hDlg, IDC_SEL_SIZE, &fOk, FALSE);
            if (!fOk)
                goto beep;
            if (wSize + wStart > gwLength) {
                MessageBeep(0);
                wSize = gwLength - wStart;
            }
            if (wSize < 1) {
                MessageBeep(0);
                wSize = 1;
            }
            SetDlgItemInt(hDlg, IDC_SEL_SIZE, wSize, FALSE);
            SetDlgItemInt(hDlg, IDC_SEL_END, wSize + wStart, FALSE);
            goto done;

        beep:
            MessageBeep(0);
        done:
            gfSelDlgBusy = FALSE;
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

/*
 * s05:531C-53F9  SetSelection  (FAR PASCAL)
 *
 * Clamps the range to the movie (an empty or invalid range clears both
 * marks), shows it as trackbar ticks and redraws the mark texts and the
 * frame.
 */
void FAR PASCAL SetSelection(WORD wStart, WORD wEnd)
{
    HDC hdc;

    if (wStart != 0xFFFF) {
        if ((int)wStart < 0)
            wStart = 0;
        if (gwLengthShown <= wStart)
            wStart = wEnd = 0xFFFF;
    }
    if (wEnd != 0xFFFF) {
        if (gwLengthShown < wEnd)
            wEnd = gwLengthShown;
        if (wEnd < 1)
            wStart = wEnd = 0xFFFF;
    }

    SendMessage(ghwndTrackBar, TBM_CLEARTICS, 0, 0L);

    gwSelStart = wStart;
    InvalidateRect(ghwndMarkInValue, NULL, FALSE);
    if (gwSelStart != 0xFFFF)
        SendMessage(ghwndTrackBar, TBM_SETTIC, 0, (LONG)gwSelStart);

    gwSelEnd = wEnd;
    InvalidateRect(ghwndMarkOutValue, NULL, FALSE);
    if (gwSelEnd != 0xFFFF)
        SendMessage(ghwndTrackBar, TBM_SETTIC, 0, (LONG)gwSelEnd);

    InvalidateRect(ghwndTrackBar, NULL, TRUE);

    hdc = GetDC(ghwndView);
    ViewDrawBorder(ghwndView, hdc);
    ReleaseDC(ghwndView, hdc);
}

/*
 * s05:53FC-5451  GetSelection  (FAR PASCAL)
 *
 * With fDefault, no selection reads as the current frame.
 */
void FAR PASCAL GetSelection(LPWORD lpwStart, LPWORD lpwEnd, BOOL fDefault)
{
    if (fDefault && (gwSelStart == 0xFFFF || gwSelEnd == 0xFFFF)) {
        if (lpwStart)
            *lpwStart = gwCurFrame;
        if (lpwEnd)
            *lpwEnd = gwCurFrame + 1;
        return;
    }
    if (lpwStart)
        *lpwStart = gwSelStart;
    if (lpwEnd)
        *lpwEnd = gwSelEnd;
}

/*
 * s05:5454-57E1  CreateControlBars
 *
 * Creates the three bars and the mark text windows, the button fonts
 * (12 pixel Helv, normal and bold, or the system font) and checks the
 * tracks button for the edited tracks.  -1 on failure.
 */
int FAR CreateControlBars(void)
{
    TOOLBUTTON  tb;
    POINT       pt;
    int         i;

    ghwndMarks = CreateWindow("ToolBarClass", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                          0, 0, 0, 0, ghwndApp, NULL, ghInst, NULL);
    if (ghwndMarks == NULL)
        return -1;

    ghwndMarkInLabel = CreateWindow("GrayBlob", NULL, WS_CHILD | WS_VISIBLE, 0, 9, 9, 9,
                                    ghwndMarks, NULL, ghInst, NULL);
    if (ghwndMarkInLabel == NULL)
        return -1;
    ghwndMarkInValue = CreateWindow("GrayBlob", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                    ghwndMarks, NULL, ghInst, NULL);
    if (ghwndMarkInValue == NULL)
        return -1;
    ghwndMarkOutLabel = CreateWindow("GrayBlob", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                     ghwndMarks, NULL, ghInst, NULL);
    if (ghwndMarkOutLabel == NULL)
        return -1;
    ghwndMarkOutValue = CreateWindow("GrayBlob", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                     ghwndMarks, NULL, ghInst, NULL);
    if (ghwndMarkOutValue == NULL)
        return -1;

    pt.x = 24;
    pt.y = 18;
    toolbarSetBitmap(ghwndMarks, ghInst, IDB_MARKBAR, pt);
    for (i = 0; i < 2; i++) {
        tb.rc.left = gaxButton[i];
        tb.rc.top = 2;
        tb.rc.right = gaxButton[i] + pt.x;
        tb.rc.bottom = pt.y + 2;
        tb.iButton = gaiButton[i];
        tb.iState = gaiState[i];
        tb.iType = gaiType[i];
        tb.iString = gaiString[i];
        toolbarAddTool(ghwndMarks, tb);
    }

    ghfontMarks = CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0,
                             VARIABLE_PITCH | FF_SWISS, "Helv");
    if (ghfontMarks == NULL)
        ghfontMarks = GetStockObject(SYSTEM_FONT);
    ghfontMarksBold = CreateFont(12, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0,
                                 VARIABLE_PITCH | FF_SWISS, "Helv");
    if (ghfontMarksBold == NULL)
        ghfontMarksBold = GetStockObject(SYSTEM_FONT);

    ghwndTransport = CreateWindow("ToolBarClass", NULL,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER | WS_TABSTOP,
                          0, 0, 0, 0, ghwndApp, NULL, ghInst, NULL);
    if (ghwndTransport == NULL)
        return -1;
    pt.x = 24;
    pt.y = 22;
    toolbarSetBitmap(ghwndTransport, ghInst, IDB_TRANSPORT, pt);
    for (i = 2; i < 8; i++) {
        tb.rc.left = gaxButton[i];
        tb.rc.top = 2;
        tb.rc.right = gaxButton[i] + pt.x;
        tb.rc.bottom = pt.y + 2;
        tb.iButton = gaiButton[i];
        tb.iState = gaiState[i];
        tb.iType = gaiType[i];
        tb.iString = gaiString[i];
        toolbarAddTool(ghwndTransport, tb);
    }

    ghwndTracks = CreateWindow("ToolBarClass", NULL,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER | WS_TABSTOP,
                          0, 0, 0, 0, ghwndApp, NULL, ghInst, NULL);
    if (ghwndTracks == NULL)
        return -1;
    pt.x = 24;
    pt.y = 22;
    toolbarSetBitmap(ghwndTracks, ghInst, IDB_TRACKS, pt);
    for (i = 8; i < 11; i++) {
        tb.rc.left = gaxButton[i];
        tb.rc.top = 2;
        tb.rc.right = gaxButton[i] + pt.x;
        tb.rc.bottom = pt.y + 2;
        tb.iButton = gaiButton[i];
        tb.iState = gaiState[i];
        tb.iType = gaiType[i];
        tb.iString = gaiString[i];
        toolbarAddTool(ghwndTracks, tb);
    }

    if (gfEditVideo && gfEditAudio)
        toolbarModifyState(ghwndTracks, 0, BTNST_DOWN);
    else
        toolbarModifyState(ghwndTracks, gfEditVideo ? 1 : 2, BTNST_DOWN);

    gcyCtrlBars = 50;
    return 1;
}

/*
 * s05:57E2-580E  DeleteControlBarFonts
 */
void FAR DeleteControlBarFonts(void)
{
    if (GetStockObject(SYSTEM_FONT) != ghfontMarks)
        DeleteObject(ghfontMarks);
    if (GetStockObject(SYSTEM_FONT) != ghfontMarksBold)
        DeleteObject(ghfontMarksBold);
}

/*
 * s05:5810-5A32  TransportCommand  (FAR PASCAL)
 *
 * The transport bar's notifications.  Left clicks on the outer buttons
 * go to the previous or next mark (falling back to the start or end),
 * right clicks (or Shift) to the start or end; the help line follows.
 * Play with Alt plays only the selection (command 81).
 */
void FAR PASCAL TransportCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int     iButton, i, iAct, ids;
    BOOL    fRepeat, fRight;
    WORD    wStart, wEnd, wTarget;
    int     n;

    iButton = HIWORD(lParam) & 0xFF;
    fRepeat = (HIWORD(lParam) & 0x100) != 0;
    fRight = (HIWORD(lParam) & 0x200) != 0;

    if (fRepeat)
        gnTransportRepeat++;
    else
        gnTransportRepeat = 0;

    if (!fRepeat) {
        if (iButton == 2 && fRight)
            toolbarModifyString(ghwndTransport, iButton, IDS_BEGINNING);
        else if (iButton == 2)
            toolbarModifyString(ghwndTransport, iButton, IDS_PREVMARK);
        else if (iButton == 5 && fRight)
            toolbarModifyString(ghwndTransport, iButton, IDS_END);
        else if (iButton == 5)
            toolbarModifyString(ghwndTransport, iButton, IDS_NEXTMARK);
    }

    i = toolbarIndexFromButton(ghwndTransport, iButton);
    toolbarFullStateFromButton(ghwndTransport, iButton);
    iAct = toolbarActivityFromButton(ghwndTransport, iButton);
    ids = toolbarStringFromIndex(ghwndTransport, i);

    if ((iAct == TBACT_MOUSEDOWN || iAct == TBACT_KEYDOWN || iAct == TBACT_MOUSEON) &&
        gnTransportRepeat == 1)
        StatusSetMessage(MAKEINTRESOURCE(ids));
    if (iAct == TBACT_MOUSEOFF)
        StatusSetMessage(NULL);
    if (iAct != TBACT_MOUSEUP && iAct != TBACT_KEYUP)
        return;

    StatusSetMessage(NULL);
    switch (iButton) {
    case 0:
        if (GetKeyState(VK_MENU) < 0)
            SendMessage(ghwndApp, WM_COMMAND, IDM_ACCEL81, 0L);
        else
            SendMessage(ghwndApp, WM_COMMAND, IDM_ACCEL80, 0L);
        return;

    case 1:
        SendMessage(ghwndApp, WM_COMMAND, IDM_ACCEL82, 0L);
        return;

    case 2:
        wTarget = 0;
        if (!fRight) {
            GetSelection(&wStart, &wEnd, FALSE);
            if (wStart != 0xFFFF && wEnd != 0xFFFF) {
                if (wEnd < gwCurFrame)
                    wTarget = wEnd;
                else if (wStart < gwCurFrame)
                    wTarget = wStart;
            }
        }
        break;

    case 3:
        n = (int)gwLengthShown / -10;
        if (n == 0)
            n = -1;
        wTarget = ((int)(gwCurFrame + n) < 0) ? 0 : gwCurFrame + n;
        break;

    case 4:
        n = (int)gwLengthShown / 10;
        if (n == 0)
            n = 1;
        wTarget = (gwCurFrame + n > gwLengthShown) ? gwLengthShown : gwCurFrame + n;
        break;

    case 5:
        wTarget = gwLengthShown;
        if (!fRight) {
            GetSelection(&wStart, &wEnd, FALSE);
            if (wStart != 0xFFFF && wEnd != 0xFFFF) {
                if (wStart > gwCurFrame)
                    wTarget = wStart;
                else if (wEnd > gwCurFrame)
                    wTarget = wEnd;
            }
        }
        break;

    default:
        return;
    }
    SeekTo(wTarget);
}

/*
 * s05:5A36-5B80  TracksCommand  (FAR PASCAL)
 *
 * The tracks bar's notifications (which tracks Cut, Copy etc. work on).
 * A notification for button 8 updates the help line of the main tool
 * bar's zoom button for the current zoom.
 */
void FAR PASCAL TracksCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[40];
    int     iButton, i, iAct, ids;
    BOOL    fRepeat;

    iButton = HIWORD(lParam) & 0xFF;
    fRepeat = (HIWORD(lParam) & 0x100) != 0;
    if (fRepeat)
        gnTracksRepeat++;
    else
        gnTracksRepeat = 0;

    if (!fRepeat && iButton == 8) {
        if (gwZoom == 8)
            toolbarModifyString(ghwndToolBar, iButton, IDS_ZOOMHALF);
        else if (gwZoom == 1)
            toolbarModifyString(ghwndToolBar, iButton, IDS_ZOOM1);
        else if (gwZoom == 2)
            toolbarModifyString(ghwndToolBar, iButton, IDS_ZOOM2);
        else
            toolbarModifyString(ghwndToolBar, iButton, IDS_ZOOM4);
    }

    i = toolbarIndexFromButton(ghwndTracks, iButton);
    toolbarFullStateFromButton(ghwndTracks, iButton);
    iAct = toolbarActivityFromButton(ghwndTracks, iButton);
    ids = toolbarStringFromIndex(ghwndTracks, i);

    if ((iAct == TBACT_MOUSEDOWN || iAct == TBACT_KEYDOWN || iAct == TBACT_MOUSEON) &&
        gnTracksRepeat == 1)
        StatusSetMessage(MAKEINTRESOURCE(ids));
    if (iAct == TBACT_MOUSEOFF)
        StatusSetMessage(NULL);
    if (iAct != TBACT_MOUSEUP && iAct != TBACT_KEYUP)
        return;

    StatusSetMessage(NULL);
    switch (iButton) {
    case 0:
        EditSetTracks(TRUE, TRUE, FALSE);
        LoadString(ghInst, IDS_AUDIOVIDEO, ach, sizeof(ach));
        break;
    case 1:
        EditSetTracks(TRUE, FALSE, FALSE);
        LoadString(ghInst, IDS_VIDEO, ach, sizeof(ach));
        break;
    case 2:
        EditSetTracks(FALSE, TRUE, FALSE);
        LoadString(ghInst, IDS_AUDIO, ach, sizeof(ach));
        break;
    default:
        return;
    }
    SetWindowText(ghwndStatTrack, ach);
}

/*
 * s05:5B84-5CA1  MarkCommand  (FAR PASCAL)
 *
 * The mark bar's notifications: Mark In sets the start at the current
 * frame (the end moves after it if needed), Mark Out the end.
 */
void FAR PASCAL MarkCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int     iButton, i, iAct, ids;
    WORD    wStart, wEnd;

    wStart = gwSelStart;
    wEnd = gwSelEnd;
    iButton = HIWORD(lParam) & 0xFF;
    if (HIWORD(lParam) & 0x100)
        gnMarkRepeat++;
    else
        gnMarkRepeat = 0;

    i = toolbarIndexFromButton(ghwndMarks, iButton);
    toolbarFullStateFromButton(ghwndMarks, iButton);
    iAct = toolbarActivityFromButton(ghwndMarks, iButton);
    ids = toolbarStringFromIndex(ghwndMarks, i);

    if ((iAct == TBACT_MOUSEDOWN || iAct == TBACT_KEYDOWN || iAct == TBACT_MOUSEON) &&
        gnMarkRepeat == 1)
        StatusSetMessage(MAKEINTRESOURCE(ids));
    if (iAct == TBACT_MOUSEOFF)
        StatusSetMessage(NULL);
    if (iAct != TBACT_MOUSEUP && iAct != TBACT_KEYUP)
        return;

    StatusSetMessage(NULL);
    switch (iButton) {
    case 0:
        PreviewUpdatePosition();
        wStart = (gwLengthShown - 1 < gwCurFrame) ? gwLengthShown - 1 : gwCurFrame;
        if (gwLengthShown == 0) {
            wStart = wEnd = 0xFFFF;
        } else if (wEnd == 0xFFFF || wEnd <= wStart) {
            wEnd = wStart + 1;
        }
        break;

    case 1:
        PreviewUpdatePosition();
        wEnd = (gwLengthShown < gwCurFrame) ? gwLengthShown : gwCurFrame;
        if (gwLengthShown == 0) {
            wStart = wEnd = 0xFFFF;
        } else {
            if (wEnd == 0)
                wEnd = 1;
            if (wStart == 0xFFFF || wEnd <= wStart)
                wStart = wEnd - 1;
        }
        break;
    }
    SetSelection(wStart, wEnd);
}

/*
 * s05:5CA4-62A0  MarkPanelPaint  (FAR PASCAL)
 *
 * For any of the four mark text windows: places them beside the Mark In
 * and Mark Out buttons (11/10 of the text width, centred vertically) and
 * draws all four: the labels in bold, the marks as frame numbers or
 * times, blank when unset.
 */
void FAR PASCAL MarkPanelPaint(HWND hwnd)
{
    char    achIn[80];
    char    achInVal[80];
    char    achOut[80];
    char    achOutVal[80];
    RECT    rc;
    DWORD   dwIn, dwInVal, dwOut, dwOutVal;
    HDC     hdc;
    HGDIOBJ hfOld;
    COLORREF crBk, crText;
    int     x, y, cxIn, cxInVal, cxOut, cxOutVal;

    if (hwnd != ghwndMarkInLabel && hwnd != ghwndMarkOutLabel &&
        hwnd != ghwndMarkInValue && hwnd != ghwndMarkOutValue)
        return;

    hdc = GetDC(ghwndMarks);
    hfOld = SelectObject(hdc, ghfontMarksBold);

    /* Mark In */
    LoadString(ghInst, IDS_MARKIN, achIn, sizeof(achIn));
    dwIn = GetTextExtent(hdc, achIn, strlen(achIn));
    toolbarRectFromIndex(ghwndMarks, 0, &rc);
    x = rc.left + 26;
    GetClientRect(ghwndMarks, &rc);
    y = (WORD)(rc.bottom - HIWORD(dwIn)) >> 1;
    cxIn = (WORD)(LOWORD(dwIn) * 11) / 10;
    MoveWindow(ghwndMarkInLabel, x, y, cxIn, HIWORD(dwIn), TRUE);
    x += cxIn;

    SelectObject(hdc, ghfontMarks);
    if (gwSelStart != 0xFFFF && gfTimeFormat)
        FormatFrameTime(gwSelStart, achInVal);
    else if (gwSelStart != 0xFFFF)
        wsprintf(achInVal, "%d", gwSelStart);
    else
        lstrcpy(achInVal, "           ");
    dwInVal = GetTextExtent(hdc, achInVal, strlen(achInVal));
    cxInVal = (WORD)(LOWORD(dwInVal) * 11) / 10;
    MoveWindow(ghwndMarkInValue, x, y, cxInVal, HIWORD(dwInVal), TRUE);

    /* Mark Out */
    SelectObject(hdc, ghfontMarksBold);
    LoadString(ghInst, IDS_MARKOUT, achOut, sizeof(achOut));
    dwOut = GetTextExtent(hdc, achOut, strlen(achOut));
    toolbarRectFromIndex(ghwndMarks, 1, &rc);
    x = rc.left + 26;
    GetClientRect(ghwndMarks, &rc);
    y = (WORD)(rc.bottom - HIWORD(dwOut)) >> 1;
    cxOut = (WORD)(LOWORD(dwOut) * 11) / 10;
    MoveWindow(ghwndMarkOutLabel, x, y, cxOut, HIWORD(dwOut), TRUE);
    x += cxOut;

    SelectObject(hdc, ghfontMarks);
    if (gwSelEnd != 0xFFFF && gfTimeFormat)
        FormatFrameTime(gwSelEnd, achOutVal);
    else if (gwSelEnd != 0xFFFF)
        wsprintf(achOutVal, "%d", gwSelEnd);
    else
        lstrcpy(achOutVal, "           ");
    dwOutVal = GetTextExtent(hdc, achOutVal, strlen(achOutVal));
    cxOutVal = (WORD)(LOWORD(dwOutVal) * 11) / 10;
    MoveWindow(ghwndMarkOutValue, x, y, cxOutVal, HIWORD(dwOutVal), TRUE);

    SelectObject(hdc, hfOld);
    ReleaseDC(ghwndMarks, hdc);

    /* draw them */
    hdc = GetDC(ghwndMarkInLabel);
    hfOld = SelectObject(hdc, ghfontMarksBold);
    crBk = SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    crText = SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    SetRect(&rc, 0, 0, cxIn, HIWORD(dwIn));
    ExtTextOut(hdc, LOWORD(dwIn) / 20, 0, ETO_OPAQUE, &rc, achIn, strlen(achIn), NULL);
    SelectObject(hdc, hfOld);
    SetBkColor(hdc, crBk);
    SetTextColor(hdc, crText);
    ReleaseDC(ghwndMarkInLabel, hdc);

    hdc = GetDC(ghwndMarkOutLabel);
    hfOld = SelectObject(hdc, ghfontMarksBold);
    crBk = SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    crText = SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    SetRect(&rc, 0, 0, cxOut, HIWORD(dwOut));
    ExtTextOut(hdc, LOWORD(dwOut) / 20, 0, ETO_OPAQUE, &rc, achOut, strlen(achOut), NULL);
    SelectObject(hdc, hfOld);
    SetBkColor(hdc, crBk);
    SetTextColor(hdc, crText);
    ReleaseDC(ghwndMarkOutLabel, hdc);

    hdc = GetDC(ghwndMarkInValue);
    hfOld = SelectObject(hdc, ghfontMarks);
    crBk = SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    crText = SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    SetRect(&rc, 0, 0, cxInVal, HIWORD(dwInVal));
    ExtTextOut(hdc, LOWORD(dwInVal) / 20, 0, ETO_OPAQUE, &rc, achInVal, strlen(achInVal), NULL);
    SelectObject(hdc, hfOld);
    SetBkColor(hdc, crBk);
    SetTextColor(hdc, crText);
    ReleaseDC(ghwndMarkInValue, hdc);

    hdc = GetDC(ghwndMarkOutValue);
    hfOld = SelectObject(hdc, ghfontMarks);
    crBk = SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    crText = SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    SetRect(&rc, 0, 0, cxOutVal, HIWORD(dwOutVal));
    ExtTextOut(hdc, LOWORD(dwOutVal) / 20, 0, ETO_OPAQUE, &rc, achOutVal, strlen(achOutVal), NULL);
    SelectObject(hdc, hfOld);
    SetBkColor(hdc, crBk);
    SetTextColor(hdc, crText);
    ReleaseDC(ghwndMarkOutValue, hdc);
}
