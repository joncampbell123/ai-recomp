/*
 * frame.c - code segment 3 of VIDCAP.EXE, 0000-0D42: the frame window
 * ("VidcapEditWindow", EditWndProc) between the toolbar and the status
 * bar.  It holds the video window ("ShowWindow", show.c) with scroll bars
 * when the image is larger than the frame, centred or at the top left,
 * on a background of the chosen colour with a 3D border around it.
 *
 * Functions are FAR PASCAL unless noted.
 */

#include <windows.h>
#include "vidcap.h"

#define IDT_AUTOSCROLL  850

static BOOL gfScrolling = FALSE;        /* DS:01A6 */
static BOOL gfAutoScrollTimer = FALSE;  /* DS:01A8 */
                                        /* DS:01AA, 01B4, 01BE "scrollbar" */
char        gszShowClass[] = "ShowWindow";  /* DS:01C8 */

int         gnHScrollPos;               /* DS:304A */
int         gnVScrollPos;               /* DS:304C */
HWND        ghwndVScroll;               /* DS:2EF0 */
HWND        ghwndHScroll;               /* DS:3060 */
HWND        ghwndSizeBox;               /* DS:1542 */
HWND        ghwndShow;                  /* DS:2EE4 */
HWND        ghwndFrame;                 /* DS:2EE2 */

/*
 * seg3:0000-036D  SizeShowWindow
 *
 * Sizes and places the video window and the scroll bars for the frame's
 * size.  With fKeepPos the scroll positions are scaled to the new ranges,
 * otherwise reset.
 */
void FAR PASCAL SizeShowWindow(BOOL fKeepPos)
{
    RECT    rc;
    BOOL    fVScroll;
    BOOL    fHScroll;
    int     cxClient;
    int     cyClient;
    int     cxImage;
    int     cyImage;
    int     cxAvail;
    int     cyAvail;
    int     cyShow;
    int     nRange;
    int     nMin;
    int     nMax;
    int     nPos;
    int     x;
    int     y;
    int     dx;
    int     dy;

    fVScroll = FALSE;
    fHScroll = FALSE;
    if (ghwndFrame == NULL || IsIconic(ghwndFrame))
        return;

    GetClientRect(ghwndFrame, &rc);
    cyClient = rc.bottom;
    cxImage = gcxCapture;
    cyImage = gcyCapture;
    cxClient = rc.right;
    cxAvail = rc.right;
    cyAvail = rc.bottom;

    if (cyImage - cyAvail > 0) {
        cxAvail += 1 - gcxVScroll;
        fVScroll = TRUE;
    }
    if (cxImage - cxAvail > 0) {
        fHScroll = TRUE;
        cyAvail += 1 - gcyHScroll;
        if (!fVScroll && cyImage - cyAvail > 0) {
            cxAvail += 1 - gcxVScroll;
            fVScroll = TRUE;
        }
    }

    if (fVScroll) {
        nRange = cyImage - cyAvail;
        MoveWindow(ghwndVScroll, cxClient - gcxVScroll + 1, -1, gcxVScroll, cyAvail + 2, TRUE);
        if (fKeepPos) {
            cyShow = cyAvail;
            GetScrollRange(ghwndVScroll, SB_CTL, &nMin, &nMax);
            if (nMax == 0)
                nMax = 1;
            nPos = (int)((DWORD)((long)gnVScrollPos * (long)(UINT)nRange) / (DWORD)(UINT)nMax);
            if ((UINT)nPos > (UINT)nRange)
                nPos = nRange;
        } else {
            cyShow = cyAvail;
            nPos = 0;
        }
        SetScrollRange(ghwndVScroll, SB_CTL, 0, nRange, FALSE);
        SetScrollPos(ghwndVScroll, SB_CTL, nPos, TRUE);
        ShowWindow(ghwndVScroll, SW_SHOWNORMAL);
        gnVScrollPos = nPos;
    } else {
        cyShow = cyImage;
        MoveWindow(ghwndVScroll, 0x1000, 0x1000, gcxVScroll, cyImage, FALSE);
        ShowWindow(ghwndVScroll, SW_HIDE);
        gnVScrollPos = 0;
    }

    if (fHScroll) {
        nRange = cxImage - cxAvail;
        MoveWindow(ghwndHScroll, -1, cyClient - gcyHScroll + 1, cxAvail + 2, gcyHScroll, TRUE);
        if (fKeepPos) {
            GetScrollRange(ghwndHScroll, SB_CTL, &nMin, &nMax);
            if (nMax == 0)
                nMax = 1;
            nPos = (int)((DWORD)((long)gnHScrollPos * (long)(UINT)nRange) / (DWORD)(UINT)nMax);
            if ((UINT)nPos > (UINT)nRange)
                nPos = nRange;
        } else {
            nPos = 0;
        }
        SetScrollRange(ghwndHScroll, SB_CTL, 0, nRange, FALSE);
        SetScrollPos(ghwndHScroll, SB_CTL, nPos, TRUE);
        ShowWindow(ghwndHScroll, SW_SHOWNORMAL);
        gnHScrollPos = nPos;
    } else {
        cxAvail = cxImage;
        MoveWindow(ghwndHScroll, 0x1000, 0x1000, cxImage, gcyHScroll, FALSE);
        ShowWindow(ghwndHScroll, SW_HIDE);
        gnHScrollPos = 0;
    }

    if (fVScroll && fHScroll) {
        MoveWindow(ghwndSizeBox, cxClient - gcxVScroll + 1, cyClient - gcyHScroll + 1,
                   gcxVScroll, gcyHScroll, TRUE);
        ShowWindow(ghwndSizeBox, SW_SHOWNORMAL);
        EnableWindow(ghwndSizeBox, FALSE);
    } else {
        ShowWindow(ghwndSizeBox, SW_HIDE);
    }

    if (gfCenterImage) {
        dx = cxClient - (fVScroll ? gcxVScroll : 0) - cxImage;
        dy = cyClient - (fHScroll ? gcyHScroll : 0) - cyImage;
        x = (dx > 0) ? dx / 2 : 0;
        y = (dy > 0) ? dy / 2 : 0;
        MoveWindow(ghwndShow, x, y, cxAvail, cyShow, TRUE);
    } else {
        MoveWindow(ghwndShow, 0, 0, cxAvail, cyShow, TRUE);
    }

    InvalidateRect(ghwndFrame, NULL, TRUE);
    UpdateWindow(ghwndFrame);
}

/*
 * seg3:0370-0597  DrawFrameBorder
 *
 * A 5-pixel raised 3D border around the video window (in the frame's
 * coordinates).
 */
void FAR PASCAL DrawFrameBorder(HWND hwnd, HDC hdc)
{
    RECT    rcFrame;
    RECT    rcClient;
    RECT    rcShow;
    HGDIOBJ hbrOld;
    int     x;
    int     y;
    int     cx;
    int     cy;

    GetWindowRect(ghwndFrame, &rcFrame);
    GetClientRect(ghwndFrame, &rcClient);
    GetWindowRect(ghwndShow, &rcShow);

    x = (rcClient.right - rcFrame.right + rcFrame.left) / 2 - rcFrame.left + rcShow.left;
    y = rcShow.top - rcFrame.top - 1;
    cx = rcShow.right - rcShow.left;
    cy = rcShow.bottom - rcShow.top;

    hbrOld = NULL;
    hbrOld = SelectObject(hdc, ghbrBtnHighlight);
    PatBlt(hdc, x - 5, y - 5, 1, cy + 9, PATCOPY);
    PatBlt(hdc, x - 4, y - 5, cx + 8, 1, PATCOPY);
    PatBlt(hdc, x - 1, cy + y, cx + 2, 1, PATCOPY);
    PatBlt(hdc, cx + x, y - 1, 1, cy + 1, PATCOPY);

    SelectObject(hdc, ghbrBtnFace);
    PatBlt(hdc, x - 4, y - 4, 3, cy + 8, PATCOPY);
    PatBlt(hdc, x - 1, y - 4, cx + 5, 3, PATCOPY);
    PatBlt(hdc, x - 1, cy + y + 1, cx + 5, 3, PATCOPY);
    PatBlt(hdc, cx + x + 1, y - 1, 3, cy + 2, PATCOPY);

    SelectObject(hdc, ghbrBtnShadow);
    PatBlt(hdc, x - 1, y - 1, 1, cy + 1, PATCOPY);
    PatBlt(hdc, x, y - 1, cx, 1, PATCOPY);

    SelectObject(hdc, ghbrBtnShadow);
    PatBlt(hdc, x - 5, cy + y + 4, cx + 10, 1, PATCOPY);
    PatBlt(hdc, cx + x + 4, y - 5, 1, cy + 9, PATCOPY);

    if (hbrOld)
        SelectObject(hdc, hbrOld);
}

/*
 * seg3:059A-0624  PaintFrameBackground  (FAR CDECL)
 */
void FAR PaintFrameBackground(HWND hwnd, HDC hdc)
{
    RECT    rc;
    HBRUSH  hbr;

    if (gidBkColor != IDC_BKDEFAULT) {
        if (gidBkColor == IDC_BKLTGRAY)
            hbr = GetStockObject(LTGRAY_BRUSH);
        if (gidBkColor == IDC_BKDKGRAY)
            hbr = GetStockObject(DKGRAY_BRUSH);
        if (gidBkColor == IDC_BKBLACK)
            hbr = GetStockObject(BLACK_BRUSH);
        SelectObject(hdc, hbr);
        GetClientRect(hwnd, &rc);
        PatBlt(hdc, 0, 0, rc.right, rc.bottom, PATCOPY);
    }
    DrawFrameBorder(hwnd, hdc);
}

/*
 * seg3:0626-0733  ScrollShow
 *
 * Scrolls the video window by dx, dy (clipped to the ranges).  Returns
 * TRUE if it moved.
 */
BOOL FAR PASCAL ScrollShow(int dx, int dy)
{
    RECT    rc;
    int     cxImage;
    int     cyImage;
    int     nMax;

    cxImage = gcxCapture;
    cyImage = gcyCapture;
    if (dx == 0 && dy == 0)
        return FALSE;

    GetClientRect(ghwndShow, &rc);
    if (dx) {
        nMax = cxImage - rc.right;
        if (nMax < 0)
            nMax = 0;
        if (gnHScrollPos + dx < 0)
            dx = -gnHScrollPos;
        if (gnHScrollPos + dx > nMax)
            dx = nMax - gnHScrollPos;
        gnHScrollPos += dx;
        SetScrollPos(ghwndHScroll, SB_CTL, gnHScrollPos, TRUE);
    }
    if (dy) {
        nMax = cyImage - rc.bottom;
        if (nMax < 0)
            nMax = 0;
        if (gnVScrollPos + dy < 0)
            dy = -gnVScrollPos;
        if (gnVScrollPos + dy > nMax)
            dy = nMax - gnVScrollPos;
        gnVScrollPos += dy;
        SetScrollPos(ghwndVScroll, SB_CTL, gnVScrollPos, TRUE);
    }
    if (dx == 0 && dy == 0)
        return FALSE;

    gfScrolling = TRUE;
    ScrollWindow(ghwndShow, -dx, -dy, NULL, NULL);
    UpdateWindow(ghwndShow);
    gfScrolling = FALSE;
    return TRUE;
}

/*
 * seg3:0736-0813  StartAutoScroll  (unreferenced)
 *
 * Starts scrolling by timer when pt (in image coordinates) is just
 * outside the video window along a visible scroll bar.
 */
BOOL FAR PASCAL StartAutoScroll(POINT pt)
{
    RECT    rc;
    BOOL    f;
    int     x;
    int     y;

    f = FALSE;
    x = pt.x - gnHScrollPos;
    y = pt.y - gnVScrollPos;
    GetClientRect(ghwndShow, &rc);

    if (IsWindowVisible(ghwndHScroll) &&
        ((x < -3 && x > -60) || (rc.right + 3 < x && rc.right + 60 > x)))
        f = TRUE;
    if (IsWindowVisible(ghwndVScroll) &&
        ((y < -3 && y > -60) || (rc.bottom + 3 < y && rc.bottom + 60 > y)))
        f = TRUE;

    if (f) {
        if (!gfAutoScrollTimer) {
            gfAutoScrollTimer = TRUE;
            SetTimer(ghwndFrame, IDT_AUTOSCROLL, 250, NULL);
            PostMessage(ghwndFrame, WM_TIMER, IDT_AUTOSCROLL, 0L);
        }
    } else {
        StopAutoScroll();
    }
    return f;
}

/*
 * seg3:0816-083F  StopAutoScroll  (FAR CDECL)
 */
void FAR StopAutoScroll(void)
{
    if (gfAutoScrollTimer) {
        KillTimer(ghwndFrame, IDT_AUTOSCROLL);
        gfAutoScrollTimer = FALSE;
    }
}

/*
 * seg3:0840-0913  AutoScroll  (NEAR)
 *
 * The auto-scroll timer: scrolls by 30 pixels towards the cursor and
 * sends the video window a mouse move (wParam 0x1000) at the edge.
 */
static void NEAR AutoScroll(void)
{
    RECT    rc;
    POINT   pt;
    POINT   ptNew;
    int     dx;
    int     dy;

    GetClientRect(ghwndShow, &rc);
    GetCursorPos(&pt);
    ScreenToClient(ghwndShow, &pt);
    dx = 0;
    dy = 0;
    ptNew = pt;

    if (IsWindowVisible(ghwndHScroll)) {
        if (pt.x < 0) {
            ptNew.x = 1;
            dx = -1;
        } else if (pt.x > rc.right) {
            ptNew.x = rc.right - 1;
            dx = 1;
        }
    }
    if (IsWindowVisible(ghwndVScroll)) {
        if (pt.y < 0) {
            ptNew.y = 1;
            dy = -1;
        } else if (pt.y > rc.bottom) {
            ptNew.y = rc.bottom - 1;
            dy = 1;
        }
    }

    if ((dx || dy) && ScrollShow(dx * 30, dy * 30))
        PostMessage(ghwndShow, WM_MOUSEMOVE, 0x1000, MAKELONG(ptNew.x, ptNew.y));
}

/*
 * seg3:0914-0C2C  EditWndProc  (exported ordinal 4)
 *
 * While a dialog is up (there is a help context) WM_QUERYENDSESSION beeps,
 * posts message 2 (WM_DESTROY) to the dialog and returns 0x00701008,
 * which CtrlWndProc takes as "no".
 */
LRESULT FAR PASCAL EditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC         hdc;
    int         n;

    switch (msg) {
    case WM_CREATE:
        ghwndHScroll = CreateWindow("scrollbar", NULL, WS_CHILD | WS_CLIPSIBLINGS | SBS_HORZ,
                                    0, 0, 0, 0, hwnd, (HMENU)1000, ghInst, NULL);
        if (ghwndHScroll == NULL)
            return -1L;
        ghwndVScroll = CreateWindow("scrollbar", NULL, WS_CHILD | WS_CLIPSIBLINGS | SBS_VERT,
                                    0, 0, 0, 0, hwnd, (HMENU)1001, ghInst, NULL);
        if (ghwndVScroll == NULL)
            return -1L;
        ghwndSizeBox = CreateWindow("scrollbar", NULL,
                                    WS_CHILD | WS_CLIPSIBLINGS | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN,
                                    0, 0, 0, 0, hwnd, (HMENU)1000, ghInst, NULL);
        if (ghwndSizeBox == NULL)
            return -1L;
        ghwndShow = CreateWindow(gszShowClass, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                 0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndShow == NULL)
            return -1L;
        break;

    case WM_SIZE:
        SizeShowWindow(TRUE);
        break;

    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
        if (wParam)
            SetWindowPos(ghwndFrame, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
        return SendMessage(ghwndApp, msg, wParam, lParam);

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        PaintFrameBackground(hwnd, hdc);
        EndPaint(hwnd, &ps);
        break;

    case WM_QUERYENDSESSION:
        if (gidHelpContext) {
            MessageBeep(0);
            PostMessage(GetLastActivePopup(hwnd), WM_DESTROY, 0, 0L);
            return 0x00701008L;
        }
        return 1L;

    case WM_ERASEBKGND:
        if (gidBkColor == IDC_BKDEFAULT)
            break;
        return 1L;

    case WM_ENDSESSION:
        if (wParam)
            SaveSettings();
        break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        SetActiveWindow(ghwndApp);
        PostMessage(ghwndApp, msg, wParam, lParam);
        return 0L;

    case WM_SYSCOMMAND:
        break;

    case WM_TIMER:
        if (wParam == IDT_AUTOSCROLL)
            AutoScroll();
        break;

    case WM_HSCROLL:
        switch (wParam) {
        case SB_LINEUP:         n = -10;    break;
        case SB_LINEDOWN:       n = 10;     break;
        case SB_PAGEUP:         n = -50;    break;
        case SB_PAGEDOWN:       n = 50;     break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:     n = LOWORD(lParam) - gnHScrollPos;  break;
        default:                n = 0;      break;
        }
        ScrollShow(n, 0);
        break;

    case WM_VSCROLL:
        switch (wParam) {
        case SB_LINEUP:         n = -10;    break;
        case SB_LINEDOWN:       n = 10;     break;
        case SB_PAGEUP:         n = -50;    break;
        case SB_PAGEDOWN:       n = 50;     break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:     n = LOWORD(lParam) - gnVScrollPos;  break;
        default:                n = 0;      break;
        }
        ScrollShow(0, n);
        break;

    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
        if (!IsIconic(hwnd) && ghwndShow)
            return SendMessage(ghwndShow, msg, wParam, lParam);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * seg3:0C30-0C50  ScrollPtToClient  (unreferenced)
 */
void FAR PASCAL ScrollPtToClient(LPPOINT lppt)
{
    lppt->x -= gnHScrollPos;
    lppt->y -= gnVScrollPos;
}

/*
 * seg3:0C54-0C74  ScrollPtToImage  (unreferenced)
 */
void FAR PASCAL ScrollPtToImage(LPPOINT lppt)
{
    lppt->x += gnHScrollPos;
    lppt->y += gnVScrollPos;
}

/*
 * seg3:0C78-0CA0  ScrollRectToClient  (unreferenced)
 */
void FAR PASCAL ScrollRectToClient(LPRECT lprc)
{
    lprc->left -= gnHScrollPos;
    lprc->right -= gnHScrollPos;
    lprc->top -= gnVScrollPos;
    lprc->bottom -= gnVScrollPos;
}

/*
 * seg3:0CA4-0CCC  ScrollRectToImage  (unreferenced)
 */
void FAR PASCAL ScrollRectToImage(LPRECT lprc)
{
    lprc->left += gnHScrollPos;
    lprc->right += gnHScrollPos;
    lprc->top += gnVScrollPos;
    lprc->bottom += gnVScrollPos;
}

/*
 * seg3:0CD0-0D40  ShowWndInit
 */
BOOL FAR PASCAL ShowWndInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (hPrev == NULL) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = gszShowClass;
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.hInstance     = hInst;
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_BYTEALIGNCLIENT;
        wc.lpfnWndProc   = ShowWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        if (!RegisterClass(&wc))
            return FALSE;
    }
    return TRUE;
}
