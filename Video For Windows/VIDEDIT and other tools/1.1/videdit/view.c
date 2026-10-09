/*
 * view.c - VIDEDIT.EXE code segment 4, offsets 0000-0C79: the view.
 *
 * The view (ghwndView, class "VidEditWindow") is the client
 * area of the main window.  It holds the frame window (ghwndFrame =
 * ghwndFrame, class "ShowWindow", see framewnd.c), two scroll bars and a size
 * box.  The frame window is as large as the zoomed frame, or as the view
 * if the frame doesn't fit; gxScroll/gyScroll are the scroll position in
 * zoomed pixels.  Around the frame window a sunken border is drawn whose
 * colours show whether the current frame is selected and whether it is
 * past the end of the movie.
 *
 * Dragging outside the frame window scrolls it on a 250 ms timer.
 *
 * The scroll bars are created here; the class is registered at start-up
 * (seg2).  Functions are in the order of the binary.
 */

#include "videdit.h"
#include "vemain.h"
#include "frames.h"
#include "crop.h"
#include "edit.h"

#define ID_HSCROLL          1000
#define ID_VSCROLL          1001
#define ID_SIZEBOX          1000
#define ID_AUTOSCROLL       0x352       /* timer */
#define MK_AUTOSCROLL       0x1000      /* wParam of the synthesised WM_MOUSEMOVE */

/* initialised data (module data starts at DS:0158) */
static BOOL     gfScrolling = FALSE;    /* DS:0170 */
static BOOL     gfAutoScroll = FALSE;   /* DS:0172 */
                                        /* DS:0174, 017E, 0188 "scrollbar" */

/* uninitialised */
HWND            ghwndSizeBox;           /* DS:2D12 */
HWND            ghwndVScroll;           /* DS:319E */
HWND            ghwndHScroll;           /* DS:3260 */

/*
 * s04:0000-0381  ViewLayout  (FAR PASCAL)
 *
 * Shows or hides the scroll bars for the zoomed frame size, places them,
 * the size box and the frame window (centred if gfCenterImage),
 * and repaints.  fKeepPos keeps the scroll positions in proportion to the
 * old scroll range.
 */
void FAR PASCAL ViewLayout(BOOL fKeepPos)
{
    RECT    rc;
    BOOL    fHScroll, fVScroll;
    int     cxImage, cyImage;
    int     cxView, cyView;
    int     cxFrame, cyFrame;
    int     nRange, nPos, nMin, nMax;
    int     x, y;

    fHScroll = fVScroll = FALSE;
    if (ghwndView == 0 || IsIconic(ghwndView))
        return;

    GetClientRect(ghwndView, &rc);
    cyView = rc.bottom;
    cxImage = gwZoom * gcxMovie / 2;
    cyImage = gwZoom * gcyMovie / 2;
    cxView = rc.right;

    if (cyImage - cyView > 0) {
        cxView += 1 - gcxVScroll;
        fVScroll = TRUE;
    }
    if (cxImage - cxView > 0) {
        fHScroll = TRUE;
        cyView += 1 - gcyHScroll;
        if (!fVScroll && cyImage - cyView > 0) {
            cxView += 1 - gcxVScroll;
            fVScroll = TRUE;
        }
    }

    if (fVScroll) {
        nRange = cyImage - cyView;
        MoveWindow(ghwndVScroll, rc.right - gcxVScroll + 1, -1, gcxVScroll, cyView + 2, TRUE);
        if (fKeepPos) {
            cyFrame = cyView;
            GetScrollRange(ghwndVScroll, SB_CTL, &nMin, &nMax);
            if (nMax == 0)
                nMax = 1;
            nPos = (WORD)((DWORD)(WORD)nRange * (DWORD)(LONG)(int)gyScroll / (DWORD)(WORD)nMax);
            if ((WORD)nPos > (WORD)nRange)
                nPos = nRange;
        } else {
            cyFrame = cyView;
            nPos = 0;
        }
        SetScrollRange(ghwndVScroll, SB_CTL, 0, nRange, FALSE);
        SetScrollPos(ghwndVScroll, SB_CTL, nPos, TRUE);
        ShowWindow(ghwndVScroll, SW_SHOWNORMAL);
        gyScroll = nPos;
    } else {
        cyFrame = cyImage;
        MoveWindow(ghwndVScroll, 0x1000, 0x1000, gcxVScroll, cyFrame, FALSE);
        ShowWindow(ghwndVScroll, SW_HIDE);
        gyScroll = 0;
    }

    if (fHScroll) {
        nRange = cxImage - cxView;
        MoveWindow(ghwndHScroll, -1, rc.bottom - gcyHScroll + 1, cxView + 2, gcyHScroll, TRUE);
        if (fKeepPos) {
            GetScrollRange(ghwndHScroll, SB_CTL, &nMin, &nMax);
            if (nMax == 0)
                nMax = 1;
            nPos = (WORD)((DWORD)(WORD)nRange * (DWORD)(LONG)(int)gxScroll / (DWORD)(WORD)nMax);
            if ((WORD)nPos > (WORD)nRange)
                nPos = nRange;
        } else {
            nPos = 0;
        }
        SetScrollRange(ghwndHScroll, SB_CTL, 0, nRange, FALSE);
        SetScrollPos(ghwndHScroll, SB_CTL, nPos, TRUE);
        ShowWindow(ghwndHScroll, SW_SHOWNORMAL);
        gxScroll = nPos;
        cxFrame = cxView;
    } else {
        cxFrame = cxImage;
        MoveWindow(ghwndHScroll, 0x1000, 0x1000, cxFrame, gcyHScroll, FALSE);
        ShowWindow(ghwndHScroll, SW_HIDE);
        gxScroll = 0;
    }

    if (fVScroll && fHScroll) {
        MoveWindow(ghwndSizeBox, rc.right - gcxVScroll + 1, rc.bottom - gcyHScroll + 1,
                   gcxVScroll, gcyHScroll, TRUE);
        ShowWindow(ghwndSizeBox, SW_SHOWNORMAL);
        EnableWindow(ghwndSizeBox, FALSE);
    } else {
        ShowWindow(ghwndSizeBox, SW_HIDE);
    }

    if (gfCenterImage) {
        x = rc.right - (fVScroll ? gcxVScroll : 0) - cxImage;
        y = rc.bottom - (fHScroll ? gcyHScroll : 0) - cyImage;
        x = (x > 0) ? x / 2 : 0;
        y = (y > 0) ? y / 2 : 0;
        MoveWindow(ghwndFrame, x, y, cxFrame, cyFrame, TRUE);
    } else {
        MoveWindow(ghwndFrame, 0, 0, cxFrame, cyFrame, TRUE);
    }

    InvalidateRect(ghwndView, NULL, TRUE);
    UpdateWindow(ghwndView);
}

/*
 * s04:0382-061B  ViewDrawBorder  (FAR PASCAL)
 *
 * The 5-pixel border around the frame window, drawn with the button
 * colour brushes: highlight/shadow normally, swapped when the current
 * frame is in the selection, and with the face colour for the inner edge
 * when the current frame is past the last frame.  hwnd is not used.
 */
void FAR PASCAL ViewDrawBorder(HWND hwnd, HDC hdc)
{
    RECT    rcView, rcClient, rcFrame;
    int     x, y, cx, cy;
    WORD    wStart, wEnd;
    BOOL    fSel;
    HBRUSH  hbrOld;

    GetWindowRect(ghwndView, &rcView);
    GetClientRect(ghwndView, &rcClient);
    GetWindowRect(ghwndFrame, &rcFrame);

    /* frame window position in the view's client coordinates */
    x = (rcClient.right - rcView.right + rcView.left) / 2 - rcView.left + rcFrame.left;
    y = rcFrame.top - rcView.top - 1;
    cx = rcFrame.right - rcFrame.left;
    cy = rcFrame.bottom - rcFrame.top;

    GetSelection(&wStart, &wEnd, FALSE);
    if (wEnd > gwCurFrame && wStart <= gwCurFrame)
        fSel = TRUE;
    else
        fSel = FALSE;

    hbrOld = SelectObject(hdc, fSel ? ghbrBtnShadow : ghbrBtnHighlight);
    PatBlt(hdc, x - 5, y - 5, 1, cy + 9, PATCOPY);
    PatBlt(hdc, x - 4, y - 5, cx + 8, 1, PATCOPY);

    if (GetFrameCount() <= gwCurFrame)
        SelectObject(hdc, ghbrBtnFace);
    PatBlt(hdc, x - 1, cy + y, cx + 2, 1, PATCOPY);
    PatBlt(hdc, cx + x, y - 1, 1, cy + 1, PATCOPY);

    SelectObject(hdc, fSel ? ghbrBtnHighlight : ghbrBtnFace);
    PatBlt(hdc, x - 4, y - 4, 3, cy + 8, PATCOPY);
    PatBlt(hdc, x - 1, y - 4, cx + 5, 3, PATCOPY);
    PatBlt(hdc, x - 1, cy + y + 1, cx + 5, 3, PATCOPY);
    PatBlt(hdc, cx + x + 1, y - 1, 3, cy + 2, PATCOPY);

    SelectObject(hdc, ghbrBtnShadow);
    if (GetFrameCount() <= gwCurFrame)
        SelectObject(hdc, ghbrBtnFace);
    PatBlt(hdc, x - 1, y - 1, 1, cy + 1, PATCOPY);
    PatBlt(hdc, x, y - 1, cx, 1, PATCOPY);

    SelectObject(hdc, ghbrBtnShadow);
    PatBlt(hdc, x - 5, cy + y + 4, cx + 10, 1, PATCOPY);
    PatBlt(hdc, cx + x + 4, y - 5, 1, cy + 9, PATCOPY);

    if (hbrOld)
        SelectObject(hdc, hbrOld);
}

/*
 * s04:061C-0699  ViewPaintBackground
 *
 * Fills the view with the background colour chosen in Preferences
 * (gidBackground, the radio button id; the default leaves it to the class
 * brush) and draws the border.  The brush is left selected; with an
 * unknown id the brush is an uninitialised value.
 */
void FAR ViewPaintBackground(HWND hwnd, HDC hdc)
{
    HBRUSH  hbr;
    RECT    rc;

    if (gidBackground != IDC_PREF_BKDEFAULT) {
        if (gidBackground == IDC_PREF_BKLTGRAY)
            hbr = GetStockObject(LTGRAY_BRUSH);
        if (gidBackground == IDC_PREF_BKDKGRAY)
            hbr = GetStockObject(DKGRAY_BRUSH);
        if (gidBackground == IDC_PREF_BKBLACK)
            hbr = GetStockObject(BLACK_BRUSH);
        SelectObject(hdc, hbr);
        GetClientRect(hwnd, &rc);
        PatBlt(hdc, 0, 0, rc.right, rc.bottom, PATCOPY);
    }
    ViewDrawBorder(hwnd, hdc);
}

/*
 * s04:069A-07A5  ViewScroll  (FAR PASCAL)
 *
 * Scrolls the frame window by (dx, dy) zoomed pixels, limited to the
 * scroll range.  Returns whether it moved.  (The old position is copied
 * to a local that is never used.)
 */
BOOL FAR PASCAL ViewScroll(int dx, int dy)
{
    int     cxImage, cyImage;
    int     nMax;
    RECT    rc;

    cxImage = gcxMovie * gwZoom / 2;
    cyImage = gcyMovie * gwZoom / 2;

    if (dx == 0 && dy == 0)
        return FALSE;

    GetClientRect(ghwndFrame, &rc);

    if (dx) {
        nMax = cxImage - rc.right;
        if (nMax < 0)
            nMax = 0;
        if ((int)gxScroll + dx < 0)
            dx = -(int)gxScroll;
        if ((int)gxScroll + dx > nMax)
            dx = nMax - gxScroll;
        gxScroll += dx;
        SetScrollPos(ghwndHScroll, SB_CTL, gxScroll, TRUE);
    }

    if (dy) {
        nMax = cyImage - rc.bottom;
        if (nMax < 0)
            nMax = 0;
        if ((int)gyScroll + dy < 0)
            dy = -(int)gyScroll;
        if ((int)gyScroll + dy > nMax)
            dy = nMax - gyScroll;
        gyScroll += dy;
        SetScrollPos(ghwndVScroll, SB_CTL, gyScroll, TRUE);
    }

    if (dx == 0 && dy == 0)
        return FALSE;

    gfScrolling = TRUE;
    ScrollWindow(ghwndFrame, -dx, -dy, NULL, NULL);
    UpdateWindow(ghwndFrame);
    gfScrolling = FALSE;
    return TRUE;
}

/*
 * s04:07A6-0875  ViewAutoScroll  (FAR PASCAL)
 *
 * While dragging: if pt (image coordinates) is just outside the frame
 * window, 3 to 60 pixels beyond an edge that can scroll, start the
 * auto-scroll timer (and scroll once at once); otherwise stop it.
 */
BOOL FAR PASCAL ViewAutoScroll(POINT pt)
{
    BOOL    f = FALSE;
    int     x, y;
    RECT    rc;

    x = pt.x - gxScroll;
    y = pt.y - gyScroll;
    GetClientRect(ghwndFrame, &rc);

    if (IsWindowVisible(ghwndHScroll)) {
        if ((x < -3 && x > -60) || (x > rc.right + 3 && x < rc.right + 60))
            f = TRUE;
    }
    if (IsWindowVisible(ghwndVScroll)) {
        if ((y < -3 && y > -60) || (y > rc.bottom + 3 && y < rc.bottom + 60))
            f = TRUE;
    }

    if (f) {
        if (!gfAutoScroll) {
            gfAutoScroll = TRUE;
            SetTimer(ghwndView, ID_AUTOSCROLL, 250, NULL);
            PostMessage(ghwndView, WM_TIMER, ID_AUTOSCROLL, 0L);
        }
    } else {
        ViewStopAutoScroll();
    }
    return f;
}

/*
 * s04:0876-088F  ViewStopAutoScroll
 */
void FAR ViewStopAutoScroll(void)
{
    if (gfAutoScroll) {
        KillTimer(ghwndView, ID_AUTOSCROLL);
        gfAutoScroll = FALSE;
    }
}

/*
 * s04:0890-0961  ViewAutoScrollTimer  (NEAR)
 *
 * The auto-scroll timer: scrolls 30 pixels towards the mouse and sends
 * the frame window a mouse move at the edge (wParam MK_AUTOSCROLL) so
 * the drag follows.
 */
static void NEAR ViewAutoScrollTimer(void)
{
    RECT    rc;
    POINT   pt, ptEdge;
    int     dx, dy;

    GetClientRect(ghwndFrame, &rc);
    GetCursorPos(&pt);
    ScreenToClient(ghwndFrame, &pt);

    dx = dy = 0;
    ptEdge = pt;

    if (IsWindowVisible(ghwndHScroll)) {
        if (pt.x < 0) {
            ptEdge.x = 1;
            dx = -1;
        } else if (pt.x > rc.right) {
            ptEdge.x = rc.right - 1;
            dx = 1;
        }
    }
    if (IsWindowVisible(ghwndVScroll)) {
        if (pt.y < 0) {
            ptEdge.y = 1;
            dy = -1;
        } else if (pt.y > rc.bottom) {
            ptEdge.y = rc.bottom - 1;
            dy = 1;
        }
    }

    if (dx || dy) {
        if (ViewScroll(dx * 30, dy * 30))
            PostMessage(ghwndFrame, WM_MOUSEMOVE, MK_AUTOSCROLL, MAKELONG(ptEdge.x, ptEdge.y));
    }
}

/*
 * s04:0962-0C79  ViewWndProc  (FAR PASCAL, loads DS from SS)
 *
 * Activation and system keys are passed on to the main window
 * (ghwndApp).  While the Crop dialog is up (gfCropping) the
 * window can't be closed or minimised.  When closing is refused the
 * window procedure returns 0x701008.
 */
LRESULT FAR PASCAL ViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC         hdc;
    int         d;

    switch (msg) {
    case WM_CREATE:
        ghwndHScroll = CreateWindow("scrollbar", NULL, WS_CHILD | WS_CLIPSIBLINGS | SBS_HORZ,
                                    0, 0, 0, 0, hwnd, (HMENU)ID_HSCROLL, ghInst, NULL);
        if (ghwndHScroll == 0)
            return -1L;
        ghwndVScroll = CreateWindow("scrollbar", NULL, WS_CHILD | WS_CLIPSIBLINGS | SBS_VERT,
                                    0, 0, 0, 0, hwnd, (HMENU)ID_VSCROLL, ghInst, NULL);
        if (ghwndVScroll == 0)
            return -1L;
        ghwndSizeBox = CreateWindow("scrollbar", NULL,
                                    WS_CHILD | WS_CLIPSIBLINGS | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN,
                                    0, 0, 0, 0, hwnd, (HMENU)ID_SIZEBOX, ghInst, NULL);
        if (ghwndSizeBox == 0)
            return -1L;
        ghwndFrame = CreateWindow(gszFrameClass, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, 0, 0, hwnd, 0, ghInst, NULL);
        if (ghwndFrame == 0)
            return -1L;
        break;

    case WM_SIZE:
        ViewLayout(TRUE);
        break;

    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
        if (wParam)
            SetWindowPos(ghwndView, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
        return SendMessage(ghwndApp, msg, wParam, lParam);

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        ViewPaintBackground(hwnd, hdc);
        EndPaint(hwnd, &ps);
        break;

    case WM_CLOSE:
        if (gfCropping || !QuerySave())
            return 0x701008L;
        FileClear(FALSE);
        break;

    case WM_QUERYENDSESSION:
        if (QuerySave())
            return 1L;
        return 0x701008L;

    case WM_ERASEBKGND:
        if (gidBackground != IDC_PREF_BKDEFAULT)
            return 1L;
        break;

    case WM_ENDSESSION:
        if (wParam) {
            ClipboardMakeStatic();
            FileClear(TRUE);
            SaveSettings();
        }
        break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        SetActiveWindow(ghwndApp);
        PostMessage(ghwndApp, msg, wParam, lParam);
        return 0L;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) != SC_MINIMIZE && (wParam & 0xFFF0) != SC_CLOSE)
            break;
        /* fall through */
    case WM_NCHITTEST:
        if (gfCropping)
            return 0L;
        break;

    case WM_TIMER:
        if (wParam == ID_AUTOSCROLL)
            ViewAutoScrollTimer();
        break;

    case WM_HSCROLL:
        switch (wParam) {
        case SB_LINEUP:         d = -10;                        break;
        case SB_LINEDOWN:       d = 10;                         break;
        case SB_PAGEUP:         d = -50;                        break;
        case SB_PAGEDOWN:       d = 50;                         break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:     d = LOWORD(lParam) - gxScroll;    break;
        default:                d = 0;                          break;
        }
        ViewScroll(d, 0);
        break;

    case WM_VSCROLL:
        switch (wParam) {
        case SB_LINEUP:         d = -10;                        break;
        case SB_LINEDOWN:       d = 10;                         break;
        case SB_PAGEUP:         d = -50;                        break;
        case SB_PAGEDOWN:       d = 50;                         break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:     d = LOWORD(lParam) - gyScroll;    break;
        default:                d = 0;                          break;
        }
        ViewScroll(0, d);
        break;

    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
        if (!IsIconic(hwnd) && ghwndFrame)
            return SendMessage(ghwndFrame, msg, wParam, lParam);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
