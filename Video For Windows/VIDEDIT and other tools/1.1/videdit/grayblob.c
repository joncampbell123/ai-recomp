/*
 * grayblob.c - the control panel: code segment 5, s05:30EE-3557
 *
 * "GrayBlob" is the gray panel under the video, behind the trackbar and
 * the mark bar (two step buttons beside the trackbar).  Its WM_CREATE
 * creates those two as children of the main window.  The panel turns
 * WM_HSCROLL into positioning, the mark bar's buttons into line steps,
 * forwards the trackbar messages (WM_USER..WM_USER+7) to the trackbar,
 * and a double click in its upper half opens Go To, in the lower half
 * Set Selection.  The module's data is DS:0B88-0BA1.
 */

#include "videdit.h"
#include "vedwnd.h"

BOOL    gfScrubSeek = FALSE;                    /* DS:0BA0 seek while dragging the trackbar */

/*
 * s05:30EE-3155  GrayBlobInit  (FAR PASCAL)
 *
 * Registers "GrayBlob" and "AnimSTrackBar" (only for the first instance).
 * Returns TRUE even if the trackbar class fails.
 */
BOOL FAR PASCAL GrayBlobInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (hPrev)
        return TRUE;

    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = NULL;
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "GrayBlob";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hInstance     = hInst;
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = GrayBlobWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    if (!RegisterClass(&wc))
        return FALSE;

    trackbarInit(hInst, hPrev);
    return TRUE;
}

/*
 * s05:3158-32B9  GrayBlobCreate  (NEAR)
 *
 * The trackbar and the mark bar with its two 12x12 step buttons
 * (bitmap 101).  The buttons' iActivity and iString are left
 * uninitialised.
 */
static LONG NEAR GrayBlobCreate(void)
{
    TOOLBUTTON  tb;
    RECT        rc;
    POINT       pt;

    gcyPanelBar = 0x1F;
    gcyTrackBar = 0x1B;

    ghwndTrackBar = CreateWindow("AnimSTrackBar", NULL,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                          0, 0, 0, 0, ghwndApp, NULL, ghInst, NULL);
    if (ghwndTrackBar == NULL)
        return -1L;

    gcxMarkBar = 0x19;
    gcyMarkBar = 0x0C;
    ghwndMarkBar = CreateWindow("ToolBarClass", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                0, 0, 0, 0, ghwndApp, NULL, ghInst, NULL);
    if (ghwndMarkBar == NULL)
        return -1L;

    pt.x = 13;
    pt.y = 12;
    toolbarSetBitmap(ghwndMarkBar, ghInst, IDB_101, pt);

    rc.left = 0;
    rc.top = 0;
    rc.right = 12;
    rc.bottom = 12;
    tb.rc = rc;
    tb.iButton = 0;
    tb.iState = BTNST_UP;
    tb.iType = TBTYPE_BUTTON;
    tb.iString = 0;
    toolbarAddTool(ghwndMarkBar, tb);

    rc.left = 12;
    rc.top = 0;
    rc.right = 25;
    rc.bottom = 12;
    tb.rc = rc;
    tb.iButton = 1;
    tb.iState = BTNST_UP;
    tb.iType = TBTYPE_BUTTON;
    tb.iString = 0;
    toolbarAddTool(ghwndMarkBar, tb);

    SendMessage(ghwndTrackBar, TBM_SETRANGEMIN, FALSE, 0L);
    SendMessage(ghwndTrackBar, TBM_SETRANGEMAX, FALSE, 0L);
    SendMessage(ghwndTrackBar, TBM_SETPOS, TRUE, 0L);
    return 0L;
}

/*
 * s05:32BA-3557  GrayBlobWndProc  (window procedure of "GrayBlob")
 */
LRESULT FAR PASCAL GrayBlobWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    HWND        hwndParent;
    HDC         hdc;
    HGDIOBJ     hbrOld;
    POINT       pt;
    WORD        wPos, wNew;
    int         iButton, iState, n;

    hwndParent = (HWND)GetWindowWord(hwnd, GWW_HWNDPARENT);

    switch (msg) {
    case WM_CREATE:
        if (ghwndBlob == NULL && GrayBlobCreate() != 0L)
            return -1L;
        break;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        hbrOld = SelectObject(hdc, (HGDIOBJ)ghbrBtnHighlight);
        if (hbrOld)
            SelectObject(hdc, hbrOld);
        MarkPanelPaint(hwnd);
        EndPaint(hwnd, &ps);
        break;

    case WM_NCHITTEST:
        if (gfCropping)
            return 0L;
        break;

    case WM_COMMAND:
        if (wParam != IDC_TOOLBAR) {
            SendMessage(hwndParent, msg, wParam, lParam);
            break;
        }
        if ((HWND)LOWORD(lParam) != ghwndMarkBar)
            break;

        /* the mark bar's step buttons: step while held */
        if (GetFocus() != ghwndTrackBar)
            SetFocus(ghwndTrackBar);
        iButton = LOBYTE(HIWORD(lParam));
        iState = toolbarFullStateFromButton(ghwndMarkBar, iButton);
        switch (iButton) {
        case 0:
            if (iState == BTNST_UP)
                SendMessage(ghwndBlob, WM_HSCROLL, SB_ENDSCROLL, 0L);
            else
                SendMessage(ghwndBlob, WM_HSCROLL, SB_LINEUP, 0L);
            break;
        case 1:
            if (iState == BTNST_UP)
                SendMessage(ghwndBlob, WM_HSCROLL, SB_ENDSCROLL, 0L);
            else
                SendMessage(ghwndBlob, WM_HSCROLL, SB_LINEDOWN, 0L);
            break;
        }
        break;

    case WM_HSCROLL:
        wPos = gwCurFrame;
        switch (wParam) {
        case SB_LINEUP:
            n = -1;
            break;
        case SB_LINEDOWN:
            n = 1;
            break;
        case SB_PAGEUP:
            n = (int)gwLengthShown / -10;
            if (n == 0)
                n = -1;
            break;
        case SB_PAGEDOWN:
            n = gwLengthShown / 10;
            if (n == 0)
                n = 1;
            break;
        case SB_THUMBPOSITION:
            n = LOWORD(lParam) - wPos;
            break;
        case SB_THUMBTRACK:
            /* show the position; seek only with gfScrubSeek */
            gwCurFrame = LOWORD(lParam);
            StatusUpdatePosition();
            SendMessage(ghwndTrackBar, TBM_SETPOS, TRUE, (LONG)gwCurFrame);
            gwCurFrame = wPos;
            if (gfScrubSeek && !gfPreviewing)
                n = LOWORD(lParam) - wPos;
            else
                n = 0;
            break;
        case SB_TOP:
            n = -(int)wPos;
            break;
        case SB_BOTTOM:
            n = gwLengthShown - wPos;
            break;
        default:
            n = 0;
            break;
        }

        if ((int)(n + wPos) < 0)
            wNew = 0;
        else if ((int)(n + wPos) > (int)gwLengthShown)
            wNew = gwLengthShown;
        else
            wNew = wPos + n;
        if (wNew != gwCurFrame)
            SeekTo(wNew);
        break;

    case WM_LBUTTONDOWN:
        if (GetParent(hwnd) == ghwndMarks)
            SetFocus(ghwndMarks);
        break;

    case WM_LBUTTONDBLCLK:
        if (hwnd != ghwndBlob || !gwLength)
            break;
        pt = MAKEPOINT(lParam);
        GetClientRect(ghwndBlob, &rc);
        if (rc.bottom / 2 < pt.y)
            SetSelectionDialog(ghwndApp);
        else
            GoTo();
        break;

    case TBM_GETPOS:
    case TBM_GETRANGEMIN:
    case TBM_GETRANGEMAX:
    case TBM_GETTICS:
    case TBM_SETTIC:
    case TBM_SETPOS:
    case TBM_SETRANGEMIN:
    case TBM_SETRANGEMAX:
        return SendMessage(ghwndTrackBar, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
