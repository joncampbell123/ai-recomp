/*
 * arrow.c - code segment 2 of CAPSCRN.EXE (0x05A8 bytes, PRELOAD)
 *
 * The "ComArrow" up/down arrow control.  It sends WM_VSCROLL to its
 * parent with the control ID in the position field and its own window
 * handle in the high word of lParam:
 *   left button:          SB_LINEUP / SB_LINEDOWN, repeating
 *   right button:         SB_PAGEUP / SB_PAGEDOWN, repeating
 *   left double-click:    SB_TOP / SB_BOTTOM, then SB_ENDSCROLL
 *   right double-click:   SB_THUMBPOSITION / SB_THUMBTRACK, then SB_ENDSCROLL
 * With Shift, a button press acts like the double-click.  Clicking on
 * neither half gives the code below the pair (-1 + base).
 * ArrowEditChange is the parent's helper for stepping a number in an edit
 * control.  VIDCAP.EXE contains the same control.
 */

#include <windows.h>
#include <stdlib.h>
#include "capscrn.h"

static char     gszFmtUp[]   = "%ld";       /* DS:0134 */
static char     gszFmtDown[] = "%ld";       /* DS:0138 */
static char     gszArrowClass[] = "ComArrow";   /* DS:013C */

/* DS:011C, DS:0128: the two triangles in a 15x15 logical space */
static POINT    gaptUp[3]   = { { 7, 1 }, { 3, 5 }, { 11, 5 } };
static POINT    gaptDown[3] = { { 7, 13 }, { 3, 9 }, { 11, 9 } };

static UINT     gwArrowMsg;         /* DS:07B2 button-down message while tracking */
static RECT     grcUp;              /* DS:07B4 screen coordinates */
static RECT     grcDown;            /* DS:07BC */
static LPRECT   glprcInverted;      /* DS:07C4 */
static FARPROC  glpfnArrowTimer;    /* DS:07C8 */
static HWND     ghwndArrowParent;   /* DS:07CC */
static char     gachArrow[32];      /* DS:090C */

/*
 * seg2:0000-008E  ArrowEditChange  (FAR PASCAL)
 *
 * SB_LINEUP adds one and SB_LINEDOWN subtracts one, within lMin..lMax
 * (beeping at the ends).  Returns the value.
 */
LONG FAR PASCAL ArrowEditChange(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax)
{
    LONG l;

    GetWindowText(hwndEdit, gachArrow, sizeof(gachArrow));
    l = atol(gachArrow);

    if (wParam == SB_LINEUP) {
        if (l < lMax) {
            l++;
            wsprintf(gachArrow, gszFmtUp, l);
            SetWindowText(hwndEdit, gachArrow);
        } else {
            MessageBeep(0);
        }
    } else if (wParam == SB_LINEDOWN) {
        if (l > lMin) {
            l--;
            wsprintf(gachArrow, gszFmtDown, l);
            SetWindowText(hwndEdit, gachArrow);
        } else {
            MessageBeep(0);
        }
    }

    return l;
}

/*
 * seg2:0090-00CE  ArrowHitTest  (NEAR)
 *
 * 0 for the upper half, 1 for the lower half, -1 elsewhere.
 */
static int NEAR ArrowHitTest(void)
{
    DWORD dw;

    dw = GetMessagePos();
    if (PtInRect(&grcUp, MAKEPOINT(dw)))
        return 0;
    if (PtInRect(&grcDown, MAKEPOINT(dw)))
        return 1;
    return -1;
}

/*
 * seg2:00D0-011B  ArrowTimerProc  (FAR PASCAL)
 *
 * Auto-repeat: the first period is 200 ms, then 50 ms.
 */
void FAR PASCAL ArrowTimerProc(HWND hwnd, UINT msg, UINT idTimer, DWORD dwTime)
{
    int i;

    i = ArrowHitTest();
    if (i != -1) {
        if (gwArrowMsg == WM_RBUTTONDOWN)
            i += 2;
        SendMessage(ghwndArrowParent, WM_VSCROLL, i,
                    MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
    }
    SetTimer(hwnd, idTimer, 50, (TIMERPROC)glpfnArrowTimer);
}

/*
 * seg2:011E-019C  ArrowInvert
 *
 * Inverts one half of the control.  The rectangles are kept in screen
 * coordinates; ValidateRect is called after converting back, so it gets
 * screen coordinates too.
 */
void FAR ArrowInvert(HWND hwnd, int iPart)
{
    HDC hdc;

    glprcInverted = iPart ? &grcDown : &grcUp;

    hdc = GetDC(hwnd);
    ScreenToClient(hwnd, (LPPOINT)glprcInverted);
    ScreenToClient(hwnd, (LPPOINT)glprcInverted + 1);
    InvertRect(hdc, glprcInverted);
    ClientToScreen(hwnd, (LPPOINT)glprcInverted);
    ClientToScreen(hwnd, (LPPOINT)glprcInverted + 1);
    ReleaseDC(hwnd, hdc);
    ValidateRect(hwnd, glprcInverted);
}

/*
 * seg2:019E-054B  ArrowControlProc  (exported ordinal 2)
 */
LRESULT FAR PASCAL ArrowControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    HBRUSH      hbr;
    HGDIOBJ     hbrOld;
    int         iPressed;
    int         i;

    switch (msg) {
    case WM_LBUTTONDBLCLK:
    lbuttondblclk:
        SendMessage(ghwndArrowParent, WM_VSCROLL, ArrowHitTest() + SB_TOP,
                    MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
        SendMessage(ghwndArrowParent, WM_VSCROLL, SB_ENDSCROLL,
                    MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
        break;

    case WM_RBUTTONDBLCLK:
    rbuttondblclk:
        SendMessage(ghwndArrowParent, WM_VSCROLL, ArrowHitTest() + SB_THUMBPOSITION,
                    MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
        SendMessage(ghwndArrowParent, WM_VSCROLL, SB_ENDSCROLL,
                    MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
        break;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        hbr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        FillRect(ps.hdc, &rc, hbr);
        DeleteObject(hbr);
        hbrOld = SelectObject(ps.hdc, GetStockObject(BLACK_BRUSH));
        SetTextColor(ps.hdc, GetSysColor(COLOR_WINDOWFRAME));
        SetMapMode(ps.hdc, MM_ANISOTROPIC);
        SetViewportOrg(ps.hdc, rc.left, rc.top);
        SetViewportExt(ps.hdc, rc.right - rc.left, rc.bottom - rc.top);
        SetWindowOrg(ps.hdc, 0, 0);
        SetWindowExt(ps.hdc, 15, 15);
        MoveTo(ps.hdc, 0, 7);
        LineTo(ps.hdc, 15, 7);
        Polygon(ps.hdc, gaptUp, 3);
        Polygon(ps.hdc, gaptDown, 3);
        SelectObject(ps.hdc, hbrOld);
        EndPaint(hwnd, &ps);
        break;

    case WM_MOUSEMOVE:
        if (!gwArrowMsg)
            break;
        if (glprcInverted == &grcUp)
            iPressed = 0;
        else if (glprcInverted == &grcDown)
            iPressed = 1;
        else
            iPressed = -1;

        switch (ArrowHitTest()) {
        case 0:
            if (iPressed == 1)
                ArrowInvert(hwnd, 1);
            if (iPressed != 0)
                ArrowInvert(hwnd, 0);
            break;
        case 1:
            if (iPressed == 0)
                ArrowInvert(hwnd, 0);
            if (iPressed != 1)
                ArrowInvert(hwnd, 1);
            break;
        default:
            if (glprcInverted) {
                ArrowInvert(hwnd, iPressed);
                glprcInverted = NULL;
            }
            break;
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (gwArrowMsg)
            break;
        gwArrowMsg = msg;
        SetCapture(hwnd);
        ghwndArrowParent = GetParent(hwnd);

        GetWindowRect(hwnd, &grcUp);
        CopyRect(&grcDown, &grcUp);
        grcUp.bottom = (grcUp.top + grcUp.bottom) / 2;
        grcDown.top = grcUp.bottom + 1;

        i = ArrowHitTest();
        ArrowInvert(hwnd, i);

        if (wParam & MK_SHIFT) {
            if (msg == WM_RBUTTONDOWN)
                goto rbuttondblclk;
            goto lbuttondblclk;
        }

        if (msg == WM_RBUTTONDOWN)
            i += 2;
        SendMessage(ghwndArrowParent, WM_VSCROLL, i,
                    MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
        glpfnArrowTimer = MakeProcInstance((FARPROC)ArrowTimerProc, ghInst);
        SetTimer(hwnd, GetWindowWord(hwnd, GWW_ID), 200, (TIMERPROC)glpfnArrowTimer);
        break;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (gwArrowMsg != msg - 1)
            break;
        gwArrowMsg = 0;
        ReleaseCapture();
        if (glprcInverted)
            ArrowInvert(hwnd, (glprcInverted == &grcUp) ? 0 : 1);
        if (glpfnArrowTimer) {
            SendMessage(ghwndArrowParent, WM_VSCROLL, SB_ENDSCROLL,
                        MAKELONG(GetWindowWord(hwnd, GWW_ID), hwnd));
            KillTimer(hwnd, GetWindowWord(hwnd, GWW_ID));
            FreeProcInstance(glpfnArrowTimer);
            ReleaseCapture();
            glpfnArrowTimer = NULL;
        }
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0L;
}

/*
 * seg2:054E-05A6  ArrowInit  (FAR PASCAL)
 */
BOOL FAR PASCAL ArrowInit(HINSTANCE hInst)
{
    WNDCLASS wc;

    wc.lpszClassName = gszArrowClass;
    wc.hInstance     = hInst;
    wc.lpfnWndProc   = ArrowControlProc;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = NULL;
    wc.lpszMenuName  = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;

    return RegisterClass(&wc) != 0;
}
