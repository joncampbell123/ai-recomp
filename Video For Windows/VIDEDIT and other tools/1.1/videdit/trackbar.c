/*
 * trackbar.c - the "AnimSTrackBar" trackbar: code segment 5, s05:6974-805A
 *
 * The slider under the video, the trackbar of Media Player that later
 * became the common control: a channel with the selection drawn in a
 * fill pattern, ticks below it (the first two marking the selection),
 * and a thumb bitmap (103, or 104 while dragged).  It doesn't notify its
 * parent: it sends WM_HSCROLL with SB_* codes to the control panel
 * (ghwndBlob).  Clicking beside the thumb pages, repeating every 10 ms
 * while held; dragging the thumb sends SB_THUMBTRACK, and one second
 * without movement sets gfScrubSeek and sends it again so that the panel
 * seeks.  The module's data is DS:1336-135D.
 *
 * Window words (cbWndExtra 4): 0 the TRACKBAR block (moveable global),
 * 2 the ticks (local block: a DWORD count, then the positions).
 */

#include "videdit.h"
#include "vedwnd.h"

#define IDB_THUMB               103
#define IDB_THUMBDOWN           104

#define IDT_REPEAT              1
#define IDT_SCRUB               2

typedef struct tagTRACKBAR {
    LONG    lLogMin;            /* 00 */
    LONG    lLogMax;            /* 04 */
    LONG    lLogPos;            /* 08 */
    WORD    wThumbWidth;        /* 0C width of the thumb bitmap */
    int     iSizePhys;          /* 0E pixels the thumb can travel */
    int     xThumb;             /* 10 left of the thumb */
    RECT    rc;                 /* 12 client rectangle */
    int     xDragMin;           /* 1A mouse x range while dragging */
    int     xDragMax;           /* 1C */
    int     xDrag;              /* 1E last mouse x while dragging */
    RECT    rcFocus;            /* 20 drawn for command 4, which never happens */
    RECT    rcRepeat;           /* 28 where a held click repeats (the thumb while dragging) */
    BOOL    fInRepeat;          /* 30 the mouse is in rcRepeat */
    LONG    lDragPos;           /* 32 */
    WORD    wFlags;             /* 36 TBF_* */
    int     idTimer;            /* 38 the repeat timer */
    int     iCmd;               /* 3A SB_* being repeated, -1 none */
} TRACKBAR, FAR *LPTRACKBAR;    /* 0x3C bytes */

#define TBF_THUMBDRAWN          0x0001
#define TBF_2                   0x0002  /* with TBF_4: white channel brush (never set) */
#define TBF_4                   0x0004
#define TBF_TOOSMALL            0x0008

static void NEAR PASCAL TrackDoTrack(HWND hwnd, int iCmd, LONG lPos);
static int  NEAR PASCAL TrackHitTest(HWND hwnd, POINT pt);
static void NEAR PASCAL TrackStart(HWND hwnd, POINT pt, int iCmd);
static void NEAR PASCAL TrackEnd(HWND hwnd, POINT pt);
static void NEAR PASCAL TrackTimer(HWND hwnd, DWORD dwPos);
static void NEAR PASCAL TrackMouseMove(HWND hwnd, LPTRACKBAR lp, POINT pt);

/* initialised data */
static HBITMAP  ghbmThumb       = NULL;         /* DS:134E */
static HBITMAP  ghbmThumbDown   = NULL;         /* DS:1350 */
static int      gxSelStart      = -1;           /* DS:1352 x of the first tick */
static int      gxSelEnd        = -1;           /* DS:1354 x of the last tick */

/* uninitialised data */
static HBRUSH   ghbrTrackOld;                   /* DS:2CC8 */
static BITMAP   gbmThumbDraw;                   /* DS:2CCA */
static BITMAP   gbmThumb;                       /* DS:2CD8 */

/*
 * s05:6974-69D5  trackbarInit  (FAR PASCAL)
 */
BOOL FAR PASCAL trackbarInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (!hPrev) {
        wc.lpszClassName = "AnimSTrackBar";
        wc.lpfnWndProc   = trackbarWndProc;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hInstance     = hInst;
        wc.style         = CS_DBLCLKS;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 4;
        if (!RegisterClass(&wc))
            return FALSE;
    }
    return TRUE;
}

/*
 * s05:69D8-6AAD  TrackDrawRect  (NEAR PASCAL)
 */
static void NEAR PASCAL TrackDrawRect(HDC hdc, LPRECT lprc, HBRUSH hbr, BOOL fInvert)
{
    HGDIOBJ hbrOld;
    DWORD   rop;
    int     x, y, cx, cy;

    x = lprc->left;
    y = lprc->top;
    cx = lprc->right - lprc->left - 1;
    cy = lprc->bottom - lprc->top - 1;

    hbrOld = SelectObject(hdc, hbr);
    SetBrushOrg(hdc, 0, 0);
    rop = fInvert ? PATINVERT : PATCOPY;
    PatBlt(hdc, x, y, 1, cy, rop);
    PatBlt(hdc, x, y, cx, 1, rop);
    PatBlt(hdc, x, y + cy, cx, 1, rop);
    PatBlt(hdc, x + cx, y, 1, cy + 1, rop);
    if (hbrOld)
        SelectObject(hdc, hbrOld);
}

/*
 * s05:6AB0-6B0B  TrackSelectBrush  (NEAR PASCAL)
 *
 * Selects the channel brush (what DefWindowProc answers to WM_CTLCOLOR
 * for a scroll bar) or puts the old one back.  Deselecting returns an
 * uninitialised value.
 */
static HBRUSH NEAR PASCAL TrackSelectBrush(HWND hwnd, HDC hdc, LPTRACKBAR lp, BOOL fSelect)
{
    HBRUSH hbr;

    if (fSelect) {
        if ((lp->wFlags & TBF_2) && (lp->wFlags & TBF_4))
            hbr = GetStockObject(WHITE_BRUSH);
        else
            hbr = (HBRUSH)DefWindowProc(hwnd, WM_CTLCOLOR, (WPARAM)hdc,
                                        MAKELONG(hwnd, CTLCOLOR_SCROLLBAR));
        ghbrTrackOld = SelectObject(hdc, hbr);
        return hbr;
    }
    SelectObject(hdc, ghbrTrackOld);
    return hbr;
}

/*
 * s05:6B0E-6D51  TrackDrawTics  (NEAR PASCAL)
 *
 * The end ticks, the tick marks (the first as a triangle pointing left,
 * the others pointing right; their x positions are remembered for the
 * selection) and the base line under them.
 */
static void NEAR PASCAL TrackDrawTics(HWND hwnd, HDC hdc, LPTRACKBAR lp)
{
    HGDIOBJ hbrOld;
    HLOCAL  hTics;
    LPDWORD lpdw;
    DWORD   n, i;
    int     x, k;

    hbrOld = SelectObject(hdc, (HGDIOBJ)ghbrBtnText);
    PatBlt(hdc, (lp->wThumbWidth >> 1) + 1, lp->rc.bottom - 6, 1, 4, PATCOPY);
    PatBlt(hdc, lp->rc.right - (lp->wThumbWidth >> 1) - 2, lp->rc.bottom - 6, 1, 4, PATCOPY);
    SelectObject(hdc, (HGDIOBJ)ghbrBtnHighlight);
    PatBlt(hdc, (lp->wThumbWidth >> 1) + 2, lp->rc.bottom - 6, 1, 4, PATCOPY);
    PatBlt(hdc, lp->rc.right - (lp->wThumbWidth >> 1) - 1, lp->rc.bottom - 6, 1, 5, PATCOPY);
    SelectObject(hdc, (HGDIOBJ)ghbrBtnText);

    hTics = (HLOCAL)GetWindowWord(hwnd, 2);
    if (hTics) {
        lpdw = (LPDWORD)LocalLock(hTics);
        n = lpdw[0];
        for (i = 1; i <= n; i++) {
            x = (int)muldiv32((LONG)lp->iSizePhys, lpdw[i] - lp->lLogMin,
                              lp->lLogMax - lp->lLogMin) + (lp->wThumbWidth >> 1) + 1;
            if (i == 1) {
                gxSelStart = x;
                for (k = 0; k < 4; k++)
                    PatBlt(hdc, x - k, lp->rc.bottom + k - 6, 1, 4 - k, PATCOPY);
            } else {
                gxSelEnd = x;
                for (k = 0; k < 4; k++)
                    PatBlt(hdc, x + k, lp->rc.bottom + k - 6, 1, 4 - k, PATCOPY);
            }
        }
        LocalUnlock(hTics);
    }

    PatBlt(hdc, (lp->wThumbWidth >> 1) + 1, lp->rc.bottom - 2,
           lp->rc.right - lp->wThumbWidth - 1, 1, PATCOPY);
    SelectObject(hdc, (HGDIOBJ)ghbrBtnHighlight);
    PatBlt(hdc, (lp->wThumbWidth >> 1) + 1, lp->rc.bottom - 1,
           lp->rc.right - lp->wThumbWidth - 1, 1, PATCOPY);
    if (hbrOld)
        SelectObject(hdc, hbrOld);
}

/*
 * s05:6D54-6F2D  TrackDrawChannel  (NEAR PASCAL)
 *
 * The sunken channel and, between the first two ticks, the selection in
 * the fill pattern.  With a selection the brush put back at the end is
 * the black one, not the original.
 */
static void NEAR PASCAL TrackDrawChannel(HWND hwnd, HDC hdc, LPTRACKBAR lp)
{
    HGDIOBJ hbrOld;

    hbrOld = SelectObject(hdc, (HGDIOBJ)ghbrBtnText);
    PatBlt(hdc, 6, lp->rc.top + 3, lp->rc.right - 11, 1, PATCOPY);
    PatBlt(hdc, 6, lp->rc.top + 14, lp->rc.right - 11, 1, PATCOPY);
    PatBlt(hdc, 5, lp->rc.top + 3, 1, 12, PATCOPY);
    PatBlt(hdc, lp->rc.right - 6, lp->rc.top + 3, 1, 12, PATCOPY);
    SelectObject(hdc, (HGDIOBJ)ghbrBtnShadow);
    PatBlt(hdc, 6, lp->rc.top + 4, lp->rc.right - 12, 1, PATCOPY);
    PatBlt(hdc, 6, lp->rc.top + 5, lp->rc.right - 12, 1, PATCOPY);
    SelectObject(hdc, (HGDIOBJ)ghbrBtnHighlight);
    PatBlt(hdc, 5, lp->rc.top + 15, lp->rc.right - 10, 1, PATCOPY);

    if (gxSelStart != -1 && gxSelEnd != -1) {
        hbrOld = SelectObject(hdc, (HGDIOBJ)ghbrBtnText);
        PatBlt(hdc, gxSelStart, lp->rc.top + 4, 1, 10, PATCOPY);
        PatBlt(hdc, gxSelEnd, lp->rc.top + 4, 1, 10, PATCOPY);
        SelectObject(hdc, (HGDIOBJ)ghbrFillPat);
        PatBlt(hdc, gxSelStart + 1, lp->rc.top + 6, gxSelEnd - gxSelStart - 1, 8, PATCOPY);
    }
    SelectObject(hdc, hbrOld);
}

/*
 * s05:6F30-6FD7  TrackMoveThumb  (NEAR PASCAL)
 *
 * Invalidates the uncovered part of the old thumb position (erased) and
 * the new position.
 */
static void NEAR PASCAL TrackMoveThumb(HWND hwnd, HDC hdc, LPTRACKBAR lp, int xOld)
{
    RECT    rc;
    int     n, x;

    n = lp->xThumb - xOld;
    if (n < 0)
        n = -n;
    if (n > (int)lp->wThumbWidth)
        n = lp->wThumbWidth;
    if (n == 0)
        return;

    if ((int)lp->wThumbWidth > n && (WORD)lp->xThumb < (WORD)xOld)
        x = lp->wThumbWidth - n + xOld;
    else
        x = xOld;
    SetRect(&rc, x, 0, x + n, lp->rc.bottom - 1);
    InvalidateRect(hwnd, &rc, TRUE);

    SetRect(&rc, lp->xThumb, 0, lp->xThumb + lp->wThumbWidth, lp->rc.bottom - 1);
    InvalidateRect(hwnd, &rc, FALSE);
    UpdateWindow(hwnd);
}

/*
 * s05:6FDA-704D  TrackDrawThumbBitmap  (NEAR PASCAL)
 *
 * The x argument is not used.  The memory DC is deleted with the bitmap
 * still selected.
 */
static void NEAR PASCAL TrackDrawThumbBitmap(HWND hwnd, HDC hdc, LPTRACKBAR lp, int x)
{
    HDC     hdcMem;
    HBITMAP hbm;

    hdcMem = CreateCompatibleDC(hdc);
    hbm = (lp->iCmd == SB_THUMBTRACK) ? ghbmThumbDown : ghbmThumb;
    GetObject(hbm, sizeof(BITMAP), &gbmThumbDraw);
    SelectObject(hdcMem, hbm);
    BitBlt(hdc, lp->xThumb, 0, lp->wThumbWidth, lp->rc.bottom - 1, hdcMem, 0, 0, SRCCOPY);
    DeleteDC(hdcMem);
    lp->wFlags |= TBF_THUMBDRAWN;
}

/*
 * s05:7050-7078  TrackDrawFocus  (NEAR PASCAL)
 *
 * Only for command 4 (SB_THUMBPOSITION), which is never the current
 * command, so it never draws.
 */
static void NEAR PASCAL TrackDrawFocus(HWND hwnd, LPTRACKBAR lp, HDC hdc)
{
    if (lp->iCmd == SB_THUMBPOSITION)
        TrackDrawRect(hdc, &lp->rcFocus, GetStockObject(GRAY_BRUSH), TRUE);
}

/*
 * s05:707C-70D9  TrackDrawThumb  (NEAR PASCAL)
 *
 * Re-sets the position (to recompute the thumb x) and draws the thumb.
 */
static void NEAR PASCAL TrackDrawThumb(HWND hwnd, HDC hdc, LPTRACKBAR lp)
{
    TrackSelectBrush(hwnd, hdc, lp, TRUE);
    SendMessage(hwnd, TBM_SETPOS, FALSE, SendMessage(hwnd, TBM_GETPOS, 0, 0L));
    TrackDrawThumbBitmap(hwnd, hdc, lp, lp->xThumb);
    TrackSelectBrush(hwnd, hdc, lp, FALSE);
}

/*
 * s05:70DC-70FC  TrackSetCaret  (NEAR PASCAL)
 */
static void NEAR PASCAL TrackSetCaret(HWND hwnd, LPTRACKBAR lp)
{
    if (GetFocus() == hwnd)
        SetCaretPos(lp->xThumb + 3, 3);
}

/*
 * s05:7100-7BB6  trackbarWndProc  (window procedure of "AnimSTrackBar")
 *
 * Quirks: WM_KEYDOWN passes the SB_ code instead of the key code on to
 * DefWindowProc; WM_MOUSEMOVE falls through into the WM_KEYUP code (which
 * then does nothing, as mouse key flags never reach 0x21); WM_DESTROY
 * deletes the thumb bitmaps that all trackbars share.
 */
LRESULT FAR PASCAL trackbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    LPTRACKBAR  lp;
    HLOCAL      hTics;
    LPDWORD     lpdw;
    HDC         hdc;
    HBITMAP     hbmFill;
    COLORREF    crHighlight;
    POINT       pt;
    LONG        lResult;
    DWORD       n, i;
    BOOL        fDrawn;
    int         xOld, x, iCmd;

    switch (msg) {
    case WM_CREATE:
        SetWindowWord(hwnd, 2, 0);
        SetWindowWord(hwnd, 0, (WORD)GlobalAlloc(GMEM_MOVEABLE, sizeof(TRACKBAR)));
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        GetClientRect(hwnd, &lp->rc);
        lp->iCmd = -1;
        lp->idTimer = 0;
        lp->lLogMax = lp->lLogMin = lp->lLogPos = 0;
        lp->xThumb = 1;
        lp->wFlags = 0;
        SendMessage(hwnd, WM_SYSCOLORCHANGE, 0, 0L);
        GetObject(ghbmThumb, sizeof(BITMAP), &gbmThumb);
        lp->wThumbWidth = gbmThumb.bmWidth;
        goto unlock;

    case WM_DESTROY:
        GlobalFree((HGLOBAL)GetWindowWord(hwnd, 0));
        if (ghbmThumb)
            DeleteObject(ghbmThumb);
        if (ghbmThumbDown)
            DeleteObject(ghbmThumbDown);
        break;

    case WM_SIZE:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        GetClientRect(hwnd, &lp->rc);
        if (lp->rc.right - lp->rc.left <= (int)lp->wThumbWidth) {
            lp->wFlags |= TBF_TOOSMALL;
            lp->iSizePhys = 1;
        } else {
            lp->wFlags &= ~TBF_TOOSMALL;
            lp->iSizePhys = lp->rc.right - lp->wThumbWidth - lp->rc.left - 2;
        }
        goto unlock;

    case WM_SETFOCUS:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        CreateCaret(hwnd, (HBITMAP)1, 3, 13);
        TrackSetCaret(hwnd, lp);
        ShowCaret(hwnd);
        goto unlock;

    case WM_KILLFOCUS:
        DestroyCaret();
        break;

    case WM_PAINT:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        hdc = (HDC)wParam;
        if (hdc == NULL)
            hdc = BeginPaint(hwnd, &ps);
        fDrawn = lp->wFlags & TBF_THUMBDRAWN;
        TrackDrawTics(hwnd, hdc, lp);
        TrackDrawChannel(hwnd, hdc, lp);
        TrackDrawThumb(hwnd, hdc, lp);
        if (!fDrawn)
            lp->wFlags &= ~TBF_THUMBDRAWN;
        if (wParam == 0)
            EndPaint(hwnd, &ps);
        goto unlock;

    case WM_SYSCOLORCHANGE:
        if (ghbmThumb)
            DeleteObject(ghbmThumb);
        if (ghbmThumbDown)
            DeleteObject(ghbmThumbDown);

        crHighlight = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
        ghbmThumb = LoadUIBitmap(ghInst, MAKEINTRESOURCE(IDB_THUMB), GetSysColor(COLOR_BTNTEXT),
                                 GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNSHADOW),
                                 crHighlight, GetSysColor(COLOR_BTNFACE),
                                 GetSysColor(COLOR_WINDOWFRAME));
        crHighlight = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
        ghbmThumbDown = LoadUIBitmap(ghInst, MAKEINTRESOURCE(IDB_THUMBDOWN),
                                     GetSysColor(COLOR_BTNTEXT), GetSysColor(COLOR_BTNFACE),
                                     GetSysColor(COLOR_BTNSHADOW), crHighlight,
                                     GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWFRAME));
        hbmFill = LoadUIBitmap(ghInst, "FillPat", GetSysColor(COLOR_BTNTEXT),
                               GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNSHADOW),
                               GetSysColor(COLOR_BTNHIGHLIGHT), GetSysColor(COLOR_BTNFACE),
                               GetSysColor(COLOR_WINDOWFRAME));
        if (ghbrFillPat) {
            DeleteObject((HGDIOBJ)ghbrFillPat);
            ghbrFillPat = 0;
        }
        ghbrFillPat = (WORD)CreatePatternBrush(hbmFill);
        DeleteObject(hbmFill);
        break;

    case WM_NCHITTEST:
        if (gfCropping)
            return 0L;
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_PRIOR:  iCmd = SB_PAGEUP;   break;
        case VK_NEXT:   iCmd = SB_PAGEDOWN; break;
        case VK_END:    iCmd = SB_BOTTOM;   break;
        case VK_HOME:   iCmd = SB_TOP;      break;
        case VK_LEFT:
        case VK_UP:     iCmd = SB_LINEUP;   break;
        case VK_RIGHT:
        case VK_DOWN:   iCmd = SB_LINEDOWN; break;
        default:
            goto def;
        }
        TrackDoTrack(hwnd, iCmd, 0L);
        wParam = iCmd;
        break;

    case WM_TIMER:
        if (wParam == IDT_SCRUB) {
            KillTimer(hwnd, IDT_SCRUB);
            gfScrubSeek = TRUE;
            lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
            TrackDoTrack(hwnd, SB_THUMBTRACK, lp->lDragPos);
            goto unlock;
        }
        TrackTimer(hwnd, GetMessagePos());
        break;

    case WM_MOUSEMOVE:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        if (lp->iCmd != -1)
            TrackMouseMove(hwnd, lp, MAKEPOINT(lParam));
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
        /* fall through */
    case WM_KEYUP:
        if (wParam >= VK_PRIOR && wParam <= VK_DOWN)
            TrackDoTrack(hwnd, SB_ENDSCROLL, 0L);
        break;

    case WM_LBUTTONDOWN:
        if (GetFocus() != hwnd)
            SetFocus(hwnd);
        iCmd = TrackHitTest(hwnd, MAKEPOINT(lParam));
        if (iCmd) {
            HideCaret(hwnd);
            TrackStart(hwnd, MAKEPOINT(lParam), iCmd);
        }
        break;

    case WM_LBUTTONUP:
        KillTimer(hwnd, IDT_SCRUB);
        TrackEnd(hwnd, MAKEPOINT(lParam));
        break;

    case WM_LBUTTONDBLCLK:
        /* above or below the channel: upper half Go To, lower half Set Selection */
        if (ghwndTrackBar != hwnd || !gwLength)
            break;
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        pt = MAKEPOINT(lParam);
        GetClientRect(ghwndTrackBar, &rc);
        if (lp->rc.top + 3 <= pt.y && lp->rc.top + 14 >= pt.y)
            goto unlock;
        if (rc.bottom / 2 < pt.y)
            SetSelectionDialog(ghwndApp);
        else
            GoTo();
        goto unlock;

    case TBM_GETPOS:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lResult = lp->lLogPos;
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
        return lResult;

    case TBM_GETRANGEMIN:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lResult = lp->lLogMin;
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
        return lResult;

    case TBM_GETRANGEMAX:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lResult = lp->lLogMax;
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
        return lResult;

    case TBM_GETTICS:
        return (LONG)(WORD)GetWindowWord(hwnd, 2);

    case TBM_SETTIC:
        if ((int)HIWORD(lParam) < 0)
            break;
        hTics = (HLOCAL)GetWindowWord(hwnd, 2);
        if (hTics) {
            lpdw = (LPDWORD)LocalLock(hTics);
            n = lpdw[0];
            LocalUnlock(hTics);
            hTics = LocalReAlloc(hTics, ((WORD)n + 2) * sizeof(DWORD), LMEM_MOVEABLE | LMEM_ZEROINIT);
        } else {
            hTics = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, 2 * sizeof(DWORD));
        }
        if (hTics == NULL)
            return 0L;
        SetWindowWord(hwnd, 2, (WORD)hTics);
        lpdw = (LPDWORD)LocalLock(hTics);
        lpdw[0]++;
        lpdw[(WORD)lpdw[0]] = lParam;

        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        x = (int)muldiv32((LONG)lp->iSizePhys, lParam - lp->lLogMin, lp->lLogMax - lp->lLogMin) +
            (lp->wThumbWidth >> 1) + 1;
        SetRect(&rc, x - 3, lp->rc.bottom - 6, x + 4, lp->rc.bottom - 2);
        InvalidateRect(hwnd, &rc, TRUE);
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
        LocalUnlock(hTics);
        return 1L;

    case TBM_SETPOS:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lp->lLogPos = lParam;
        goto setpos;

    case TBM_SETRANGEMIN:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lp->lLogMin = lParam;
        goto unlock;

    case TBM_SETRANGEMAX:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lp->lLogMax = lParam;
    setpos:
        /* clamp (unsigned) and place the thumb; redraw if wParam */
        lp->wFlags &= ~TBF_THUMBDRAWN;
        if ((DWORD)lp->lLogPos > (DWORD)lp->lLogMax)
            lp->lLogPos = lp->lLogMax;
        if ((DWORD)lp->lLogPos < (DWORD)lp->lLogMin)
            lp->lLogPos = lp->lLogMin;
        xOld = lp->xThumb;
        if (lp->lLogMax == lp->lLogMin)
            lp->xThumb = 1;
        else
            lp->xThumb = (int)muldiv32((LONG)lp->iSizePhys, lp->lLogPos - lp->lLogMin,
                                       lp->lLogMax - lp->lLogMin) + 1;

        if (!wParam || lp->xThumb == xOld)
            goto unlock;
        HideCaret(hwnd);
        TrackSetCaret(hwnd, lp);
        if (IsWindowVisible(hwnd)) {
            hdc = GetDC(hwnd);
            TrackSelectBrush(hwnd, hdc, lp, TRUE);
            if (msg != TBM_SETPOS)
                TrackDrawFocus(hwnd, lp, hdc);
            TrackMoveThumb(hwnd, hdc, lp, xOld);
            if (msg != TBM_SETPOS)
                TrackDrawFocus(hwnd, lp, hdc);
            TrackSelectBrush(hwnd, hdc, lp, FALSE);
            ReleaseDC(hwnd, hdc);
        }
        ShowCaret(hwnd);
        goto unlock;

    case TBM_ERASETHUMB:
        /* redraws the thumb area by pretending the thumb moved its width */
        if (!IsWindowVisible(hwnd))
            goto unlock;
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        lp->wFlags &= ~TBF_THUMBDRAWN;
        xOld = lp->xThumb;
        lp->xThumb += lp->wThumbWidth;
        HideCaret(hwnd);
        hdc = GetDC(hwnd);
        TrackSelectBrush(hwnd, hdc, lp, TRUE);
        TrackMoveThumb(hwnd, hdc, lp, xOld);
        TrackSelectBrush(hwnd, hdc, lp, FALSE);
        ReleaseDC(hwnd, hdc);
        lp->xThumb -= lp->wThumbWidth;
        goto unlock;

    case TBM_CLEARTICS:
        lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
        if (gxSelStart != -1 && gxSelEnd != -1) {
            rc.left = gxSelStart;
            rc.top = lp->rc.top + 4;
            rc.right = ((WORD)gxSelEnd >= (WORD)(gxSelStart + 1)) ? gxSelEnd : gxSelStart + 1;
            rc.bottom = rc.top + 10;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        gxSelStart = gxSelEnd = -1;

        hTics = (HLOCAL)GetWindowWord(hwnd, 2);
        if (hTics == NULL)
            goto unlock;
        lpdw = (LPDWORD)LocalLock(hTics);
        n = lpdw[0];
        for (i = 1; i <= n; i++) {
            x = (int)muldiv32((LONG)lp->iSizePhys, lpdw[i] - lp->lLogMin,
                              lp->lLogMax - lp->lLogMin) + (lp->wThumbWidth >> 1) + 1;
            SetRect(&rc, x - 3, lp->rc.bottom - 6, x + 4, lp->rc.bottom - 2);
            InvalidateRect(hwnd, &rc, TRUE);
        }
        LocalUnlock(hTics);
        LocalFree(hTics);
        SetWindowWord(hwnd, 2, 0);
        goto unlock;
    }

def:
    return DefWindowProc(hwnd, msg, wParam, lParam);

unlock:
    GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * s05:7BBA-7BD1  TrackDoTrack  (NEAR PASCAL)
 *
 * The "notification": WM_HSCROLL to the control panel.
 */
static void NEAR PASCAL TrackDoTrack(HWND hwnd, int iCmd, LONG lPos)
{
    SendMessage(ghwndBlob, WM_HSCROLL, iCmd, lPos);
}

/*
 * s05:7BD4-7CDF  TrackHitTest  (NEAR PASCAL)
 *
 * SB_PAGEUP or SB_PAGEDOWN beside the thumb (in the channel rows), 5
 * (SB_THUMBTRACK) on the thumb, which is a rectangle 16 rows high with a
 * point below (rows 16 to 19) and without its top corners; 0 elsewhere.
 */
static int NEAR PASCAL TrackHitTest(HWND hwnd, POINT pt)
{
    LPTRACKBAR  lp;
    int         iCmd = 0, xRight, x1, x2, y;

    lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
    if (lp->wFlags & TBF_TOOSMALL) {
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
        return 0;
    }

    if (lp->xThumb > pt.x) {
        if (pt.y > 3 && pt.y < 14 && pt.x > 0)
            iCmd = SB_PAGEUP;
    } else {
        xRight = lp->wThumbWidth + lp->xThumb;
        if ((WORD)(xRight - 1) < (WORD)pt.x) {
            if (pt.y > 3 && pt.y < 14 && lp->rc.right - 1 > pt.x)
                iCmd = SB_PAGEDOWN;
        } else if (pt.y > 0 && pt.y < 16) {
            iCmd = SB_THUMBTRACK;
        } else if (pt.y == 0 && lp->xThumb != pt.x && xRight - pt.x - 1 != 0) {
            iCmd = SB_THUMBTRACK;
        } else {
            x1 = lp->xThumb + 1;
            x2 = xRight - 1;
            for (y = 16; y <= 19; y++, x1++, x2--)
                if (pt.y == y && x1 <= pt.x && x2 > pt.x)
                    iCmd = SB_THUMBTRACK;
        }
    }

    GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
    return iCmd;
}

/*
 * s05:7CE2-7E40  TrackStart  (NEAR PASCAL)
 *
 * A click: paging repeats while the mouse stays in the area beside the
 * thumb; on the thumb dragging starts with the pressed thumb bitmap.
 */
static void NEAR PASCAL TrackStart(HWND hwnd, POINT pt, int iCmd)
{
    LPTRACKBAR  lp;
    RECT        rc;
    HDC         hdc;

    lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
    if (lp->wFlags & TBF_TOOSMALL)
        goto done;

    switch (iCmd) {
    case SB_PAGEUP:
        SetRect(&rc, 1, lp->rc.top + 1, lp->xThumb, lp->rc.bottom - 1);
        break;

    case SB_PAGEDOWN:
        SetRect(&rc, lp->wThumbWidth + lp->xThumb + 1, lp->rc.top + 1,
                lp->rc.right - 1, lp->rc.bottom - 1);
        break;

    case SB_THUMBTRACK:
        lp->xDragMin = pt.x - lp->xThumb;
        lp->xDragMax = lp->rc.right - lp->wThumbWidth - lp->xThumb + pt.x - 2;
        SetRect(&rc, lp->xThumb, lp->rc.top + 1, lp->wThumbWidth + lp->xThumb, lp->rc.bottom - 1);
        lp->xDrag = pt.x;
        lp->lDragPos = lp->lLogPos;
        gfScrubSeek = FALSE;
        SetTimer(hwnd, IDT_SCRUB, 1000, NULL);
        break;

    default:
        goto done;
    }

    SetCapture(hwnd);
    CopyRect(&lp->rcRepeat, &rc);
    lp->iCmd = iCmd;
    lp->fInRepeat = TRUE;
    if (iCmd != SB_THUMBTRACK) {
        TrackDoTrack(hwnd, iCmd, 0L);
        lp->idTimer = SetTimer(hwnd, IDT_REPEAT, 10, NULL);
    } else {
        hdc = GetDC(hwnd);
        TrackDrawThumbBitmap(hwnd, hdc, lp, lp->xThumb);
        ReleaseDC(hwnd, hdc);
    }

done:
    GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
}

/*
 * s05:7E44-7EDF  TrackEnd  (NEAR PASCAL)
 */
static void NEAR PASCAL TrackEnd(HWND hwnd, POINT pt)
{
    LPTRACKBAR  lp;
    HDC         hdc;

    lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
    ReleaseCapture();
    lp->fInRepeat = FALSE;
    if (lp->iCmd == SB_THUMBTRACK)
        TrackDoTrack(hwnd, SB_THUMBPOSITION, lp->lDragPos);
    else if (lp->idTimer)
        KillTimer(hwnd, IDT_REPEAT);
    TrackDoTrack(hwnd, SB_ENDSCROLL, 0L);
    ShowCaret(hwnd);
    lp->iCmd = -1;

    hdc = GetDC(hwnd);
    TrackDrawThumbBitmap(hwnd, hdc, lp, lp->xThumb);
    ReleaseDC(hwnd, hdc);
    GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
}

/*
 * s05:7EE2-7F71  TrackTimer  (NEAR PASCAL)
 *
 * Paging repeat: the area shrinks to stop at the thumb.
 */
static void NEAR PASCAL TrackTimer(HWND hwnd, DWORD dwPos)
{
    LPTRACKBAR  lp;
    POINT       pt;

    pt = MAKEPOINT(dwPos);
    lp = (LPTRACKBAR)GlobalLock((HGLOBAL)GetWindowWord(hwnd, 0));
    ScreenToClient(hwnd, &pt);
    if (lp->iCmd == SB_PAGEUP)
        lp->rcRepeat.right = lp->xThumb + 1;
    else if (lp->iCmd == SB_PAGEDOWN)
        lp->rcRepeat.left = lp->wThumbWidth + lp->xThumb + 1;
    lp->fInRepeat = PtInRect(&lp->rcRepeat, pt);
    if (lp->fInRepeat)
        TrackDoTrack(hwnd, lp->iCmd, 0L);
    GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 0));
}

/*
 * s05:7F74-805A  TrackMouseMove  (NEAR PASCAL)
 *
 * Dragging the thumb: SB_THUMBTRACK with the new position, restarting the
 * one-second seek timer.
 */
static void NEAR PASCAL TrackMouseMove(HWND hwnd, LPTRACKBAR lp, POINT pt)
{
    LONG    lPos;
    int     x, dx;

    if (lp->iCmd != SB_THUMBTRACK) {
        lp->fInRepeat = PtInRect(&lp->rcRepeat, pt);
        return;
    }

    x = pt.x;
    if (lp->xDragMin > x)
        x = lp->xDragMin;
    else if (lp->xDragMax < x)
        x = lp->xDragMax;
    dx = x - lp->xDrag;
    if (dx == 0)
        return;

    lp->xDrag = x;
    OffsetRect(&lp->rcRepeat, dx, 0);
    lPos = muldiv32((LONG)lp->rcRepeat.left, lp->lLogMax - lp->lLogMin, (LONG)lp->iSizePhys) +
           lp->lLogMin;
    if (lPos == lp->lDragPos)
        return;

    KillTimer(hwnd, IDT_SCRUB);
    gfScrubSeek = FALSE;
    SetTimer(hwnd, IDT_SCRUB, 1000, NULL);
    TrackDoTrack(hwnd, SB_THUMBTRACK, lPos);
    lp->lDragPos = lPos;
}
