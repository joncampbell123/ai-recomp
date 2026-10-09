/*
 * vedwnd.c - the VidEdit main window: code segment 5, s05:0000-1903
 *
 * The main window procedure (class "VidEditWindow"), the status bar at the
 * bottom (class "StatusClass" with "SText" panes: position, insert or
 * overwrite mode, key frame and palette flags, edited tracks and the menu
 * help line), the layout of the child windows, sizing the window to the
 * video, zoom, the "Frame Information" child dialog and the "Resize Video"
 * dialog.  The classes are registered by the initialisation code in s02.
 *
 * The module's initialised data is DS:00EA-0157 (its strings and the last
 * shown position).  Functions are in the order of the binary.  Window and
 * dialog procedures have MSC's "smart callback" entry (mov ax,ss ... mov
 * ds,ax) two bytes before their ENTER.
 */

#include "videdit.h"
#include "vedwnd.h"

static void NEAR PASCAL STextPaint(HWND hwnd, HDC hdc);

/* initialised data (module data DS:00EA-0157) */
WORD    gwLastPos       = 0xFFFF;       /* DS:0114 position last shown */
WORD    gwLastLen       = 0xFFFF;       /* DS:0116 length last shown */
BOOL    gfMsgHidden     = TRUE;         /* DS:012A the menu help line is hidden */
BOOL    gfResizePending = FALSE;        /* DS:01AE size to the video once restored */
HWND    ghwndFocusSave  = NULL;         /* DS:01BC focus while deactivated */
BOOL    gfDlgInit       = FALSE;        /* DS:0572 setting edit fields, ignore EN_ notifications */

/* uninitialised data */
BOOL    gfLastTimeFmt;                  /* DS:1BF8 time format last shown */
WORD    gwResizeWidth;                  /* DS:1CAE Resize Video dialog */
WORD    gwResizeHeight;                 /* DS:1CB0 */
BOOL    gfResizeStretch;                /* DS:1CB2 always 1 */
WORD    gwResizeEditWidth;              /* DS:1CB4 */
WORD    gwResizeEditHeight;             /* DS:1CB6 */
WORD    gidResizeEdit;                  /* DS:1CB8 edit field with a pending check (also its timer id) */
int     gcyFrameWnd;                    /* DS:2D00 */
int     gcxFrameInfo;                   /* DS:2D08 size of the Frame Information dialog */
COLORREF gcrBtnShadow;                  /* DS:2D0A */
RECT    grcNormal;                      /* DS:2D22 restored window rectangle */
int     gcxMarkBar;                     /* DS:306A width of the mark tool bar */
COLORREF gcrBtnFace;                    /* DS:3072 */
HWND    ghwndStatKey;                   /* DS:3182 status pane: key frame */
HWND    ghwndStatMode;                  /* DS:318A status pane: INS / OVR */
COLORREF gcrBtnText;                    /* DS:31B6 */
HWND    ghwndStatPos;                   /* DS:31BA status pane: position */
int     gcyMarkBar;                     /* DS:31BC */
HWND    ghwndStatPal;                   /* DS:31C2 status pane: palette change */
int     gcyFrameInfo;                   /* DS:31C4 */
HWND    ghwndStatMsg;                   /* DS:31CC status bar: menu help line */
int     gcxFrameWnd;                    /* DS:31D0 */
HWND    ghwndStatTrack;                 /* DS:31D4 status pane: edited tracks */
HWND    ghwndFrameInfo;                 /* DS:31DE Frame Information dialog */
int     gcyPanelBar;                    /* DS:31E4 */
int     gcxTrackBar;                    /* DS:31EE */
int     gcyTrackBar;                    /* DS:31FC */
COLORREF gcrBtnHighlight;               /* DS:3220 */
int     gxFrameWnd;                     /* DS:3224 */
int     gyFrameWnd;                     /* DS:3226 */
int     gcyMinTrack;                    /* DS:3258 minimum tracking height */
HWND    ghwndMarkBar;                   /* DS:2D1C the "ToolBarClass" mark bar */

/*
 * s05:0000-0096  StatusUpdatePosition
 *
 * Shows "position / length" in the first status pane, as frame numbers or,
 * with the time format preference, as times.  Only redraws on a change.
 */
void FAR StatusUpdatePosition(void)
{
    char    ach[80];
    char    achLen[36];
    char    achPos[36];

    if (gwLastPos == gwCurFrame && gwLastLen == gwLengthShown && gfLastTimeFmt == gfTimeFormat)
        return;

    gwLastPos = gwCurFrame;
    gwLastLen = gwLengthShown;
    gfLastTimeFmt = gfTimeFormat;

    if (gfTimeFormat) {
        FormatFrameTime(gwCurFrame, achPos);
        FormatFrameTime(gwLengthShown, achLen);
        wsprintf(ach, "%ls / %ls", (LPSTR)achPos, (LPSTR)achLen);
    } else {
        wsprintf(ach, "%u / %u", gwCurFrame, gwLengthShown);
    }
    SetWindowText(ghwndStatPos, ach);
}

/*
 * s05:0098-0133  StatusSetMessage  (FAR PASCAL)
 *
 * Shows a help line over the status bar.  lpsz may be a string resource
 * ID.  NULL or an empty string hides the line again.
 */
void FAR PASCAL StatusSetMessage(LPCSTR lpsz)
{
    char    ach[80];

    if (lpsz != NULL && SELECTOROF(lpsz) == 0) {
        LoadString(ghInst, OFFSETOF(lpsz), ach, sizeof(ach));
        lpsz = ach;
    }

    if (lpsz != NULL && *lpsz != '\0') {
        SetWindowText(ghwndStatMsg, lpsz);
        ShowWindow(ghwndStatMsg, SW_SHOWNA);
        UpdateWindow(ghwndStatMsg);
        gfMsgHidden = FALSE;
        return;
    }

    if (!gfMsgHidden) {
        SetWindowText(ghwndStatMsg, "");
        ShowWindow(ghwndStatMsg, SW_HIDE);
        InvalidateRect(ghwndStatMsg, NULL, TRUE);
        gfMsgHidden = TRUE;
    }
}

/*
 * s05:0136-0434  StatusWndProc  (window procedure of "StatusClass")
 *
 * Creates the panes and places them on WM_SIZE.  The widths are fractions
 * of the minimum window width (gcxMinWindow), the edited-tracks pane starts at
 * three quarters of the client width.
 */
LRESULT FAR PASCAL StatusWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[40];
    PAINTSTRUCT ps;
    int     cxMin, cxPos, cxMode, cxFlag, x;

    switch (msg) {
    case WM_CREATE:
        ghwndStatPos = CreateWindow("SText", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatPos == NULL)
            return -1L;

        ghwndStatMode = CreateWindow("SText", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                     0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatMode == NULL)
            return -1L;
        LoadString(ghInst, gfInsertMode ? IDS_INS : IDS_OVR, ach, sizeof(ach));
        SetWindowText(ghwndStatMode, ach);

        ghwndStatPal = CreateWindow("SText", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatPal == NULL)
            return -1L;

        ghwndStatKey = CreateWindow("SText", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatKey == NULL)
            return -1L;

        ghwndStatTrack = CreateWindow("SText", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                      0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatTrack == NULL)
            return -1L;
        if (gfEditVideo && gfEditAudio)
            LoadString(ghInst, IDS_AUDIOVIDEO, ach, sizeof(ach));
        else if (gfEditVideo)
            LoadString(ghInst, IDS_VIDEO, ach, sizeof(ach));
        else
            LoadString(ghInst, IDS_AUDIO, ach, sizeof(ach));
        SetWindowText(ghwndStatTrack, ach);

        ghwndStatMsg = CreateWindow("SText", "", WS_CHILD | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatMsg == NULL)
            return -1L;
        break;

    case WM_SIZE:
        cxMin = gcxMinWindow;
        SetWindowPos(ghwndStatMsg, NULL, 2, 1, gcxClient - 4, gcyStatus - 4, 0);

        if (gfTimeFormat)
            cxPos = (cxMin * 9) / 20;
        else
            cxPos = (cxMin * 3) / 10;
        SetWindowPos(ghwndStatPos, NULL, 2, 1, cxPos, gcyStatus - 4, SWP_NOZORDER);

        x = cxPos + 13;
        cxMode = cxMin / 10;
        SetWindowPos(ghwndStatMode, NULL, x, 1, cxMode, gcyStatus - 4, SWP_NOZORDER);

        x += cxMode + 5;
        cxFlag = cxMin / 20;
        SetWindowPos(ghwndStatPal, NULL, x, 1, cxFlag, gcyStatus - 4, SWP_NOZORDER);
        SetWindowPos(ghwndStatKey, NULL, x + cxFlag + 5, 1, cxFlag, gcyStatus - 4, SWP_NOZORDER);

        SetWindowPos(ghwndStatTrack, NULL, (gcxClient * 3) >> 2, 1, (cxMin * 2) / 9,
                     gcyStatus - 4, SWP_NOZORDER);
        break;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        break;

    case WM_NCHITTEST:
        if (gfCropping)
            return 0L;
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * s05:0438-0526  STextWndProc  (window procedure of "SText")
 *
 * A sunken text pane.  A click on the position pane opens Go To, a click
 * on the mode pane switches between insert and overwrite.
 */
LRESULT FAR PASCAL STextWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    char        ach[20];

    switch (msg) {
    case WM_SETTEXT:
        DefWindowProc(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        return 0L;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        STextPaint(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0L;

    case WM_ERASEBKGND:
        return 0L;

    case WM_LBUTTONDOWN:
        if (hwnd == ghwndStatPos && gwLength && !gfCropping)
            GoTo();
        if (hwnd == ghwndStatMode && !gfCropping) {
            gfInsertMode = !gfInsertMode;
            LoadString(ghInst, gfInsertMode ? IDS_INS : IDS_OVR, ach, sizeof(ach));
            SetWindowText(ghwndStatMode, ach);
        }
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * s05:052A-067D  STextPaint  (NEAR PASCAL)
 *
 * Text on button face colour with a one pixel 3D border.  The parent
 * window is fetched and not used.
 */
static void NEAR PASCAL STextPaint(HWND hwnd, HDC hdc)
{
    char    ach[128];
    RECT    rc;
    RECT    rcText;
    int     cch, cx, cy;
    HGDIOBJ hfOld, hbrOld;

    GetClientRect(hwnd, &rc);
    cch = GetWindowText(hwnd, ach, sizeof(ach));
    SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    GetParent(hwnd);
    hfOld = SelectObject(hdc, (HGDIOBJ)ghfontApp);

    rcText.left   = rc.left + 1;
    rcText.right  = rc.right - 1;
    rcText.top    = rc.top + 1;
    rcText.bottom = rc.bottom - 1;
    ExtTextOut(hdc, 4, 1, ETO_OPAQUE, &rcText, ach, cch, NULL);

    cx = rc.right - rc.left;
    cy = rc.bottom - rc.top;
    hbrOld = SelectObject(hdc, (HGDIOBJ)ghbrBtnShadow);
    PatBlt(hdc, rc.left, rc.top, 1, cy, PATCOPY);
    PatBlt(hdc, rc.left, rc.top, cx, 1, PATCOPY);
    SelectObject(hdc, (HGDIOBJ)ghbrBtnHighlight);
    PatBlt(hdc, rc.right - 1, rc.top + 1, 1, cy - 1, PATCOPY);
    PatBlt(hdc, rc.left + 1, rc.bottom - 1, cx - 1, 1, PATCOPY);

    if (hfOld)
        SelectObject(hdc, hfOld);
    if (hbrOld)
        SelectObject(hdc, hbrOld);
}

/*
 * s05:0680-07E7  AppSizeToFit
 *
 * Sizes the main window so that the zoomed video, the tool bar, the
 * control panel and the status bar fit, within the minimum tracking size
 * and the screen.  If the minimum width makes the window wider, a quarter
 * of the extra width is added to the height.  Remembers the request when
 * the window is maximised or minimised.
 */
void FAR AppSizeToFit(void)
{
    RECT    rcW, rcC;
    int     cyNonClient, cyTool, cyStatus;
    int     cxWant, cyWant, cx, cy, dcx, dcy;

    if (IsZoomed(ghwndApp) || IsIconic(ghwndApp)) {
        gfResizePending = TRUE;
        return;
    }

    GetWindowRect(ghwndApp, &rcW);
    GetClientRect(ghwndApp, &rcC);
    cyNonClient = rcW.bottom - rcC.bottom - rcW.top;
    cxWant = ((WORD)(gwZoom * gcxMovie) >> 1) - rcC.right - rcW.left + rcW.right;

    cyTool = gfToolBar ? 0x1C - gcyBorder : 0;
    cyStatus = gfStatusBar ? gcyStatus - gcyBorder : 0;
    cyWant = cyStatus + ((WORD)(gwZoom * gcyMovie) >> 1) + cyNonClient + gcyPanel + cyTool;

    cx = ((WORD)cxWant < (WORD)gcxMinWindow) ? gcxMinWindow : cxWant;
    cy = ((WORD)gcyMinTrack > (WORD)cyWant) ? gcyMinTrack : cyWant;
    if ((WORD)cx > (WORD)gcxScreen)
        cx = gcxScreen;
    if ((WORD)cy > (WORD)gcyScreen)
        cy = gcyScreen;

    dcx = cx - cxWant;
    dcy = cy - cyWant;
    if (dcx > 0 && dcy >= 0 && dcx / 4 > dcy)
        cy = dcx / 4 + cyWant;

    if ((WORD)(cy + rcW.top) > (WORD)gcyScreen)
        rcW.top = gcyScreen - cy;
    if ((WORD)(cx + rcW.left) > (WORD)gcxScreen)
        rcW.left = gcxScreen - cx;

    SetWindowPos(ghwndApp, NULL, rcW.left, rcW.top, cx, cy, SWP_NOZORDER);
}

/*
 * s05:07E8-0B02  AppLayout
 *
 * Places the child windows from top to bottom: tool bar, video window,
 * trackbar with the mark bar beside it, control panel, the play bar (its
 * button 1 is kept at half the client width) with the two halves below
 * it, and the status bar.
 */
void FAR AppLayout(void)
{
    HDWP        hdwp;
    RECT        rc;
    int         y, cyAvail, cyStatus, cx;
    TOOLBUTTON  tb;
    int         xHalf;

    y = -gcyBorder;
    hdwp = BeginDeferWindowPos(1);
    if (hdwp == NULL)
        return;

    GetClientRect(ghwndApp, &rc);

    if (gfToolBar) {
        hdwp = DeferWindowPos(hdwp, ghwndToolBar, NULL, -gcxBorder, y, gcxBorder * 2 + gcxClient, 0x1C,
                              SWP_NOZORDER);
        if (hdwp == NULL)
            return;
        y += 0x1C - gcyBorder;
        ShowWindow(ghwndToolBar, SW_SHOW);
    } else {
        ShowWindow(ghwndToolBar, SW_HIDE);
    }

    cyStatus = gfStatusBar ? gcyStatus - gcyBorder : 0;
    cyAvail = rc.bottom - cyStatus - y - gcyBorder - gcyPanel;
    if (cyAvail < 0)
        cyAvail = 0;

    gyFrameWnd = y;
    gxFrameWnd = 0;
    gcxFrameWnd = gcxBorder * 2 + rc.right;
    gcyFrameWnd = gcyBorder * 2 + cyAvail;
    hdwp = DeferWindowPos(hdwp, ghwndView, NULL, -1, y, gcxFrameWnd, gcyFrameWnd, SWP_NOZORDER);
    if (hdwp == NULL)
        return;
    y += cyAvail + gcyBorder;

    SendMessage(ghwndTrackBar, TBM_ERASETHUMB, 0, 0L);
    hdwp = DeferWindowPos(hdwp, ghwndTrackBar, NULL, 4, y + 3, rc.right - gcxMarkBar - 7,
                          gcyTrackBar, SWP_NOZORDER);
    gcxTrackBar = rc.right - gcxMarkBar - 11;
    hdwp = DeferWindowPos(hdwp, ghwndMarkBar, ghwndTrackBar, rc.right - gcxMarkBar - 3, y + 6,
                          gcxMarkBar, gcyMarkBar, 0);
    hdwp = DeferWindowPos(hdwp, ghwndBlob, ghwndMarkBar, 0, y + 2, rc.right, gcyPanelBar, 0);
    y += gcyBorder + gcyPanelBar;

    toolbarGetButton(ghwndMarks, 1, &tb);
    cx = (WORD)(gcxClient * 5) / 10;
    if (cx != tb.rc.left) {
        tb.rc.left = cx;
        tb.rc.right = cx + 0x18;
        toolbarRemoveButton(ghwndMarks, 1);
        toolbarAddButton(ghwndMarks, tb);
        InvalidateRect(ghwndMarkOutLabel, NULL, FALSE);
        InvalidateRect(ghwndMarkOutValue, NULL, FALSE);
    }

    xHalf = (gcxClient * 7) / 10;
    hdwp = DeferWindowPos(hdwp, ghwndMarks, NULL, 0, y, gcxClient, 0x17, SWP_NOZORDER);
    if (hdwp == NULL)
        return;
    y += 0x17;

    hdwp = DeferWindowPos(hdwp, ghwndTransport, NULL, -1, y, xHalf + 1, 0x1C, SWP_NOZORDER);
    if (hdwp == NULL)
        return;
    hdwp = DeferWindowPos(hdwp, ghwndTracks, NULL, xHalf - 1, y, gcxClient - xHalf + 2, 0x1C,
                          SWP_NOZORDER);
    if (hdwp == NULL)
        return;

    if (gfStatusBar) {
        hdwp = DeferWindowPos(hdwp, ghwndStatus, NULL, -1, rc.bottom - gcyStatus + 1,
                              gcxClient + 2, gcyStatus, SWP_NOZORDER);
        if (hdwp == NULL)
            return;
        ShowWindow(ghwndStatus, SW_SHOW);
    } else {
        ShowWindow(ghwndStatus, SW_HIDE);
    }

    EndDeferWindowPos(hdwp);
    InvalidateRect(ghwndBlob, NULL, TRUE);
    InvalidateRect(ghwndTrackBar, NULL, TRUE);
    InvalidateRect(ghwndMarkBar, NULL, FALSE);
}

/*
 * s05:0B04-0C87  AppShowBars  (FAR PASCAL)
 *
 * Shows or hides the tool bar and the status bar, changing the window
 * height so that the video area stays the same, unless the video area is
 * smaller than the video (or becomes so), in which case the video area
 * gets exactly the zoomed video height.
 */
void FAR PASCAL AppShowBars(BOOL fToolBar, BOOL fStatusBar)
{
    RECT    rcW, rcC;
    int     cyStatus, cyNonClient, cyStat, cyVideo, cyArea, cy, cyStatNew;
    BOOL    fFits;

    if (fToolBar == gfToolBar && fStatusBar == gfStatusBar)
        return;

    GetWindowRect(ghwndApp, &rcW);
    GetClientRect(ghwndApp, &rcC);
    if (rcC.bottom == 0)
        rcW.bottom += 12;

    cyStatus = gcyStatus;
    cyNonClient = rcW.bottom - rcC.bottom - rcW.top;
    gcyMinTrack = (WORD)((cyNonClient + gcyPanel + cyStatus + 0x1C) * 3) >> 1;

    cyStat = gfStatusBar ? cyStatus - 1 : 0;
    cyArea = rcC.bottom - (gfToolBar ? 0x1B : 0) - gcyPanel - cyStat;
    cyVideo = (WORD)(gwZoom * gcyMovie) >> 1;
    fFits = (cyArea >= cyVideo);

    if (fToolBar != gfToolBar) {
        if (gfToolBar)
            cyArea += 0x1B;
        else
            cyArea -= 0x1B;
    }
    if (fStatusBar != gfStatusBar)
        cyArea += gfStatusBar ? cyStatus - 1 : 1 - cyStatus;

    if ((cyVideo <= cyArea) != fFits)
        cyArea = cyVideo;

    gfStatusBar = fStatusBar;
    gfToolBar = fToolBar;

    cyStatNew = fStatusBar ? cyStatus - 1 : 0;
    cy = (fToolBar ? 0x1B : 0) + cyNonClient + gcyPanel + cyArea + cyStatNew;

    if (cy + rcW.top == rcW.bottom) {
        InvalidateRect(ghwndApp, NULL, FALSE);
        AppLayout();
        return;
    }

    if ((WORD)(grcApp.top + cy) > (WORD)gcyScreen)
        grcApp.top = gcyScreen - cy;
    SetWindowPos(ghwndApp, NULL, grcApp.left, grcApp.top, grcApp.right - grcApp.left, cy, SWP_NOZORDER);
    InvalidateRect(ghwndApp, NULL, FALSE);
    UpdateWindow(ghwndApp);
}

/*
 * s05:0C8A-0CC0  UpdatePlayButtons
 *
 * Play (button 0) is enabled when there is something to play and no
 * preview is running; Stop (button 1) while one is.
 */
void FAR UpdatePlayButtons(void)
{
    toolbarModifyState(ghwndTransport, 0, (gwLength != 0 && gfPreviewing == 0) ? 1 : 0);
    toolbarModifyState(ghwndTransport, 1, gfPreviewing != 0);
}

/*
 * s05:0CC2-0D33  SetLength  (FAR PASCAL)
 *
 * Sets the length shown by the status bar and the trackbar.  The second
 * argument is not used.
 */
void FAR PASCAL SetLength(WORD wFrames, BOOL fUnused)
{
    if (gwLengthShown == wFrames)
        return;

    gwLengthShown = wFrames;
    if (gwLength > wFrames)
        gwLength = wFrames;
    if ((int)gwCurFrame < 0 && (int)wFrames >= 0)
        gwCurFrame = 0;

    StatusUpdatePosition();
    SendMessage(ghwndTrackBar, TBM_SETRANGEMIN, TRUE, 0L);
    SendMessage(ghwndTrackBar, TBM_SETRANGEMAX, TRUE, (LONG)gwLengthShown);
    SendMessage(ghwndTrackBar, TBM_SETPOS, TRUE, (LONG)gwCurFrame);
    UpdatePlayButtons();
}

/*
 * s05:0D36-0E06  UpdateLength
 *
 * The length is the longest of the tracks.  Also updates the key frame
 * and palette change panes for the current frame (not while previewing).
 */
void FAR UpdateLength(void)
{
    char    ach[40];
    WORD    w, wLen;
    DWORD   dwFlags;

    wLen = 0;
    w = GetFrameCount();
    if (w)
        wLen = w;
    w = WaveNumFrames();
    if (w > wLen)
        wLen = w;
    w = MidiNumFrames();
    if (w > wLen)
        wLen = w;

    gwLength = wLen;
    if (wLen < gwCurFrame)
        gwCurFrame = wLen;
    if (gwLengthShown != wLen)
        SetLength(wLen, TRUE);

    if (!gfPreviewDrawing && !gfPreviewing && gfStatusBar) {
        dwFlags = GetFrameInfo(gwCurFrame, 0x11L);

        if (LOBYTE(dwFlags) & 0x10)
            lstrcpy(ach, gszK);
        else
            ach[0] = '\0';
        SetWindowText(ghwndStatKey, ach);

        if (LOBYTE(dwFlags) & 0x01)
            lstrcpy(ach, gszP);
        else
            ach[0] = '\0';
        SetWindowText(ghwndStatPal, ach);
    }

    StatusUpdatePosition();
}

/*
 * s05:0E08-0E2B  SeekTo  (FAR PASCAL)
 *
 * Moves to a frame; a running preview is restarted from there.
 */
void FAR PASCAL SeekTo(WORD wFrame)
{
    BOOL fPlaying = (gfPreviewing != 0);

    SetPosition(wFrame);
    if (fPlaying)
        StartPreview(FALSE);
}

/*
 * s05:0E2E-0EAD  SetPosition  (FAR PASCAL)
 */
void FAR PASCAL SetPosition(WORD wFrame)
{
    WORD    wOld;
    HDC     hdc;

    if (gwCurFrame == wFrame) {
        StatusUpdatePosition();
        return;
    }

    wOld = gwCurFrame;
    gwCurFrame = wFrame;
    UpdateLength();
    SendMessage(ghwndTrackBar, TBM_SETPOS, TRUE, (LONG)wFrame);

    if (gfFrameNumInTitle)
        UpdateTitle();
    if (!gfInFileDialog)
        RedrawFrame(gwCurFrame, wOld);

    if (!gfPreviewing) {
        hdc = GetDC(ghwndView);
        ViewDrawBorder(ghwndView, hdc);
        ReleaseDC(ghwndView, hdc);
    }
}

/*
 * s05:0EB0-1394  VidEditWndProc  (window procedure of "VidEditWindow")
 *
 * WM_CLOSE, WM_QUERYENDSESSION and WM_ENDSESSION are passed to the video
 * window first: it answers 0x00701008 to swallow them.  While gfCropping is
 * set the window ignores the mouse and refuses minimise, task switching
 * and close.
 */
LRESULT FAR PASCAL VidEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RECT        rc;
    PAINTSTRUCT ps;
    COLORREF    cr;
    HMENU       hmenu;
    int         i, n;
    BOOL        fFound;

    switch (msg) {
    case WM_CREATE:
        if (CreateChildWindows(hwnd) == -1L)
            return -1L;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0L;

    case WM_MOVE:
        /* DS:31AE-31B5 is RECT grcApp: all four
         * fields are copied; the bottom is read by SaveSettings */
        GetWindowRect(hwnd, &rc);
        grcApp = rc;
        if (!IsZoomed(hwnd) && !IsIconic(hwnd))
            grcNormal = rc;
        break;

    case WM_SIZE:
        GetWindowRect(hwnd, &rc);
        grcApp = rc;
        if (!IsIconic(hwnd) && !IsZoomed(hwnd)) {
            grcNormal = rc;
            if (gfResizePending && gfResizeOnOpen) {
                gfResizePending = FALSE;
                AppSizeToFit();
            }
        }
        GetClientRect(hwnd, &rc);
        gcxClient = rc.right;
        gcyClient = rc.bottom;
        AppLayout();
        break;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE) {
            ghwndFocusSave = GetFocus();
            return 0L;
        }
        SetFocus(ghwndFocusSave ? ghwndFocusSave : ghwndBlob);
        return 0L;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        break;

    case WM_CLOSE:
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        if (SendMessage(ghwndView, msg, wParam, lParam) == 0x00701008L)
            return 0L;
        break;

    case WM_SYSCOLORCHANGE:
        cr = GetSysColor(COLOR_BTNTEXT);
        if (cr != gcrBtnText) {
            if (ghbrBtnText)
                DeleteObject((HGDIOBJ)ghbrBtnText);
            gcrBtnText = GetSysColor(COLOR_BTNTEXT);
            ghbrBtnText = (WORD)CreateSolidBrush(gcrBtnText);
        }
        cr = GetSysColor(COLOR_BTNFACE);
        if (cr != gcrBtnFace) {
            if (ghbrBtnFace)
                DeleteObject((HGDIOBJ)ghbrBtnFace);
            gcrBtnFace = GetSysColor(COLOR_BTNFACE);
            ghbrBtnFace = (WORD)CreateSolidBrush(gcrBtnFace);
        }
        cr = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
        if (cr != gcrBtnHighlight) {
            if (ghbrBtnHighlight)
                DeleteObject((HGDIOBJ)ghbrBtnHighlight);
            gcrBtnHighlight = cr;
            ghbrBtnHighlight = (WORD)CreateSolidBrush(cr);
        }
        cr = GetSysColor(COLOR_BTNSHADOW);
        if (cr != gcrBtnShadow) {
            if (ghbrBtnShadow)
                DeleteObject((HGDIOBJ)ghbrBtnShadow);
            gcrBtnShadow = GetSysColor(COLOR_BTNSHADOW);
            ghbrBtnShadow = (WORD)CreateSolidBrush(gcrBtnShadow);
        }
        SendMessage(ghwndToolBar, msg, wParam, lParam);
        SendMessage(ghwndMarks, msg, wParam, lParam);
        SendMessage(ghwndTransport, msg, wParam, lParam);
        SendMessage(ghwndTracks, msg, wParam, lParam);
        SendMessage(ghwndTrackBar, msg, wParam, lParam);
        SendMessage(ghwndMarkBar, msg, wParam, lParam);
        break;

    case WM_ACTIVATEAPP:
        return 0L;

    case WM_MOUSEACTIVATE:
        if (gfCropping)
            return MA_NOACTIVATE;
        break;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.x = gcxMinWindow;
        ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.y = gcyMinTrack;
        break;

    case WM_NCHITTEST:
        if (gfCropping)
            return 0L;
        break;

    case WM_COMMAND:
        return DoCommand(hwnd, msg, wParam, lParam);

    case WM_SYSCOMMAND:
        switch (wParam & 0xFFF0) {
        case SC_MINIMIZE:
        case SC_NEXTWINDOW:
        case SC_PREVWINDOW:
        case SC_CLOSE:
            if (gfCropping)
                return 0L;
            break;
        }
        break;

    case WM_INITMENUPOPUP:
        return InitMenuPopup(hwnd, (HMENU)wParam, HIWORD(lParam), LOWORD(lParam));

    case WM_MENUSELECT:
        if (LOWORD(lParam) == 0xFFFF && HIWORD(lParam) == 0) {
            StatusSetMessage(NULL);
            break;
        }
        if ((LOWORD(lParam) & (MF_SYSMENU | MF_POPUP)) == (MF_SYSMENU | MF_POPUP)) {
            StatusSetMessage(MAKEINTRESOURCE(IDS_SYSMENU_HELP));
            break;
        }
        if (!(LOWORD(lParam) & MF_POPUP)) {
            StatusSetMessage(MAKEINTRESOURCE(wParam));
            break;
        }

        /* a popup: the top level menus have help lines 890.., and two submenus */
        fFound = FALSE;
        hmenu = GetMenu(hwnd);
        n = GetMenuItemCount(hmenu);
        for (i = 0; i < n; i++) {
            if (GetSubMenu(hmenu, i) == (HMENU)wParam) {
                StatusSetMessage(MAKEINTRESOURCE(IDS_MENU_FILE_HELP + i));
                fFound = TRUE;
                break;
            }
        }
        if (GetSubMenu(GetSubMenu(hmenu, 1), 10) == (HMENU)wParam) {
            StatusSetMessage(MAKEINTRESOURCE(IDS_MENU_TRACK_HELP));
            fFound = TRUE;
        } else if (GetSubMenu(GetSubMenu(hmenu, 2), 4) == (HMENU)wParam) {
            StatusSetMessage(MAKEINTRESOURCE(IDS_MENU_ZOOM_HELP));
            fFound = TRUE;
        }
        if (!fFound)
            StatusSetMessage(NULL);
        break;

    case WM_DROPFILES:
        DropFiles(hwnd, (HANDLE)wParam);
        break;

    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
        if (!IsIconic(hwnd) && ghwndFrame)
            return SendMessage(ghwndFrame, msg, wParam, lParam);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * s05:1398-13C6  GetFrameSizes  (FAR PASCAL)
 *
 * The sizes of a frame's bitmap, palette and waveform data.
 */
BOOL FAR PASCAL GetFrameSizes(WORD wFrame, LPDWORD lpdwBitmap, LPDWORD lpdwWave,
                              LPDWORD lpdwOther, LPDWORD lpdwPalette)
{
    GetFrameDataSize(wFrame, lpdwBitmap, lpdwPalette);
    WaveFrameBytes(wFrame, lpdwWave);
    MidiFrameBytes(wFrame, lpdwOther);
    return TRUE;
}

/*
 * s05:13CA-14B1  FrameInfoUpdate
 *
 * Fills the Frame Information dialog.  "Optimal" is the data rate times
 * the frame time (gCompOptions.dwDataRate * gCompOptions.dwUSecPerFrame / 1000000); frame 0 shows "(No limit)".
 */
void FAR FrameInfoUpdate(void)
{
    DWORD   dwBitmap = 0, dwWave = 0, dwOther = 0, dwPalette = 0;
    DWORD   dwOptimal;
    char    ach[20];

    if (!GetFrameSizes(gwCurFrame, &dwBitmap, &dwWave, &dwOther, &dwPalette))
        return;

    dwOptimal = muldiv32(gCompOptions.dwDataRate, gCompOptions.dwUSecPerFrame, 1000000L);
    SetDlgItemLong(ghwndFrameInfo, IDC_FI_BITMAP, dwBitmap, FALSE);
    SetDlgItemLong(ghwndFrameInfo, IDC_FI_PALETTE, dwPalette, FALSE);
    SetDlgItemLong(ghwndFrameInfo, IDC_FI_WAVE, dwWave, FALSE);
    SetDlgItemLong(ghwndFrameInfo, IDC_FI_TOTAL, dwPalette + dwWave + dwBitmap, FALSE);

    if (gwCurFrame == 0) {
        LoadString(ghInst, IDS_NOLIMIT, ach, sizeof(ach));
        SetDlgItemText(ghwndFrameInfo, IDC_FI_OPTIMAL, ach);
        return;
    }
    SetDlgItemLong(ghwndFrameInfo, IDC_FI_OPTIMAL, dwOptimal, FALSE);
}

/*
 * s05:14B2-14FA  FrameInfoDlgProc  (dialog procedure of child dialog 901)
 */
BOOL FAR PASCAL FrameInfoDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RECT rc;

    switch (msg) {
    case WM_SHOWWINDOW:
        if (wParam)
            FrameInfoUpdate();
        break;

    case WM_INITDIALOG:
        GetWindowRect(hDlg, &rc);
        gcyFrameInfo = rc.bottom - rc.top;
        gcxFrameInfo = rc.right - rc.left;
        break;
    }
    return FALSE;
}

/*
 * s05:14FE-1562  SetFrameSize  (FAR PASCAL)
 *
 * New video dimensions: resizes the video window (and the main window if
 * that preference is on) and redraws.
 */
void FAR PASCAL SetFrameSize(WORD cx, WORD cy)
{
    HCURSOR hcurOld;

    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    gcxMovie = cx;
    gcyMovie = cy;
    SetWindowPos(ghwndFrame, NULL, 0, 0, (WORD)(cx * gwZoom) >> 1, (WORD)(cy * gwZoom) >> 1,
                 SWP_NOMOVE | SWP_NOZORDER);
    if (gfResizeOnOpen)
        AppSizeToFit();
    ViewLayout(TRUE);
    SetCursor(hcurOld);
}

/*
 * s05:1566-1588  SetZoom  (FAR PASCAL)
 *
 * The zoom factor counts half sizes: 1 = 0.5x, 2 = 1x, 4 = 2x, 8 = 4x.
 */
void FAR PASCAL SetZoom(WORD wZoom)
{
    gwZoom = wZoom;
    SetFrameSize(gcxMovie, gcyMovie);
    if (gfPreviewing)
        PreviewSetRect();
}

/*
 * s05:158C-161A  ResizeVideo
 *
 * Video / Resize.  Stretching the frames is undoable ("Undo Resize").
 * gfResizeStretch is always set, so the other branch (only resizing the
 * window, undo type 62) is never taken.
 */
void FAR ResizeVideo(void)
{
    FARPROC lpfn;
    int     r;

    gwResizeWidth = gcxMovie;
    gwResizeHeight = gcyMovie;
    gfResizeStretch = TRUE;

    lpfn = MakeProcInstance((FARPROC)ResizeDlgProc, ghInst);
    r = DoDialog(IDD_RESIZE, ghwndApp, lpfn);
    FreeProcInstance(lpfn);

    if (!r)
        return;
    if (gwResizeWidth == gcxMovie && gwResizeHeight == gcyMovie)
        return;

    if (gfResizeStretch) {
        SetUndo(IDS_UNDO_RESIZE);
        ResizeFrames(gwResizeWidth, gwResizeHeight);
    } else {
        SetUndo(IDM_RESIZE);
    }
    SetFrameSize(gwResizeWidth, gwResizeHeight);
}

/*
 * s05:161C-1900  ResizeDlgProc  (dialog procedure of "Resize Video")
 *
 * An edited size is checked one second after the last change (or when the
 * field loses the focus, or on OK): not a number beeps and selects the
 * field, values are clamped to 1..1024.  OK refuses sizes above 1024.
 */
BOOL FAR PASCAL ResizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BOOL    fOk;
    int     n;
    WORD    idEdit;

    switch (msg) {
    case WM_INITDIALOG:
        gfDlgInit = TRUE;
        SetDlgItemInt(hDlg, IDC_RS_WIDTH, gwResizeWidth, FALSE);
        SetDlgItemInt(hDlg, IDC_RS_HEIGHT, gwResizeHeight, FALSE);
        gfDlgInit = FALSE;
        gwResizeEditWidth = gwResizeWidth;
        gwResizeEditHeight = gwResizeHeight;
        gidResizeEdit = 0;
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gidResizeEdit)
                SendMessage(hDlg, WM_TIMER, gidResizeEdit, 0L);
            if (gwResizeEditWidth > 1024 || gwResizeEditHeight > 1024)
                break;
            gwResizeWidth = gwResizeEditWidth;
            gwResizeHeight = gwResizeEditHeight;
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            if (gidResizeEdit)
                KillTimer(hDlg, gidResizeEdit);
            EndDialog(hDlg, FALSE);
            break;

        case IDC_RS_WIDTH:
        case IDC_RS_HEIGHT:
            if (gfDlgInit)
                break;
            if (HIWORD(lParam) == EN_KILLFOCUS) {
                if (gidResizeEdit)
                    SendMessage(hDlg, WM_TIMER, gidResizeEdit, 0L);
            } else if (HIWORD(lParam) == EN_CHANGE) {
                if (gidResizeEdit)
                    KillTimer(hDlg, gidResizeEdit);
                gidResizeEdit = wParam;
                if (wParam && !SetTimer(hDlg, wParam, 1000, NULL))
                    SendMessage(hDlg, WM_TIMER, gidResizeEdit, 0L);
            }
            break;

        case IDC_RS_DEFAULT:
            if (gwResizeEditWidth < 1024 && gwResizeEditHeight < 1024) {
                gcxDefault = gwResizeEditWidth;
                gcyDefault = gwResizeEditHeight;
            } else {
                MessageBeep(0);
            }
            break;
        }
        break;

    case WM_TIMER:
        if (gidResizeEdit == 0) {
            KillTimer(hDlg, wParam);
            break;
        }
        KillTimer(hDlg, gidResizeEdit);
        n = GetDlgItemInt(hDlg, gidResizeEdit, &fOk, FALSE);
        if (!fOk) {
            MessageBeep(0);
            SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, gidResizeEdit), 1L);
            SendDlgItemMessage(hDlg, gidResizeEdit, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
            gidResizeEdit = 0;
            break;
        }

        if (gidResizeEdit == IDC_RS_WIDTH)
            gwResizeEditWidth = n;
        else
            gwResizeEditHeight = n;

        if (n < 1) {
            MessageBeep(0);
            SetDlgItemInt(hDlg, gidResizeEdit, 1, FALSE);
            if (gidResizeEdit == IDC_RS_WIDTH)
                gwResizeEditWidth = 1;
            else
                gwResizeEditHeight = 1;
        } else if (n > 1024) {
            MessageBeep(0);
            SetDlgItemInt(hDlg, gidResizeEdit, 1024, FALSE);
            if (gidResizeEdit == IDC_RS_WIDTH)
                gwResizeEditWidth = 1024;
            else
                gwResizeEditHeight = 1024;
        } else {
            gidResizeEdit = 0;
            break;
        }
        SendDlgItemMessage(hDlg, gidResizeEdit, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
        SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, gidResizeEdit), 1L);
        gidResizeEdit = 0;
        break;

    case WM_VSCROLL:
        /* the spin arrows */
        idEdit = (GetWindowWord((HWND)HIWORD(lParam), GWW_ID) == IDC_RS_HEIGHTARROW)
                 ? IDC_RS_HEIGHT : IDC_RS_WIDTH;
        ArrowEditStep(GetDlgItem(hDlg, idEdit), wParam, 1L, 1024L, 1);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}
