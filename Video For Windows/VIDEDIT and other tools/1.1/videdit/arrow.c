/*
 * arrow.c - the "comarrow" spin control: code segment 5, s05:2B70-30EB
 *
 * The same control as in CapScrn (see capscrn/arrow.c) and VidCap.  It
 * sends WM_VSCROLL to its parent with the control ID in the position
 * field and its own window handle in the high word of lParam:
 *   left button:          SB_LINEUP / SB_LINEDOWN, repeating
 *   right button:         SB_PAGEUP / SB_PAGEDOWN, repeating
 *   left double-click:    SB_TOP / SB_BOTTOM, then SB_ENDSCROLL
 *   right double-click:   SB_THUMBPOSITION / SB_THUMBTRACK, then SB_ENDSCROLL
 * With Shift, a button press acts like the double-click.  The class is
 * registered by the initialisation code in s02.  The module's data is
 * DS:0668-0691: the two triangles, two "%ld" and "ComArrow".
 */

#include "videdit.h"
#include <stdlib.h>
#include "vedwnd.h"

/* DS:0668, DS:0674: the two triangles in a 15x15 logical space */
static POINT    gaptUp[3]   = { { 7, 1 }, { 3, 5 }, { 11, 5 } };
static POINT    gaptDown[3] = { { 7, 13 }, { 3, 9 }, { 11, 9 } };

static UINT     gwArrowMsg;         /* DS:1CC6 button-down message while tracking */
static RECT     grcUp;              /* DS:1CC8 screen coordinates */
static RECT     grcDown;            /* DS:1CD0 */
static LPRECT   glprcInverted;      /* DS:1CD8 */
static FARPROC  glpfnArrowTimer;    /* DS:1CDC */
static HWND     ghwndArrowParent;   /* DS:1CE0 */

/*
 * s05:2B70-2C00  ArrowEditChange  (FAR PASCAL)
 *
 * SB_LINEUP adds one and SB_LINEDOWN subtracts one, within lMin..lMax
 * (beeping at the ends).  Returns the value.
 */
LONG FAR PASCAL ArrowEditChange(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax)
{
    char    ach[32];
    LONG    l;

    GetWindowText(hwndEdit, ach, sizeof(ach));
    l = atol(ach);

    if (wParam == SB_LINEUP) {
        if (l < lMax) {
            l++;
            wsprintf(ach, "%ld", l);
            SetWindowText(hwndEdit, ach);
        } else {
            MessageBeep(0);
        }
    } else if (wParam == SB_LINEDOWN) {
        if (l > lMin) {
            l--;
            wsprintf(ach, "%ld", l);
            SetWindowText(hwndEdit, ach);
        } else {
            MessageBeep(0);
        }
    }

    return l;
}

/*
 * s05:2C04-2C44  ArrowHitTest  (NEAR)
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
 * s05:2C46-2C90  ArrowTimerProc  (FAR PASCAL)
 *
 * Auto-repeat: the first period is 200 ms, then 50 ms.  Returns 0.
 */
UINT FAR PASCAL ArrowTimerProc(HWND hwnd, UINT msg, UINT idTimer, DWORD dwTime)
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
    return 0;
}

/*
 * s05:2C94-2D12  ArrowInvert
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
 * s05:2D14-30EB  ArrowControlProc  (window procedure of "comarrow")
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

