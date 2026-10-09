/*
 * toolbar.c - code segment 3 of VIDCAP.EXE, 18AE-32CA: the toolbar control
 * ("ToolBarClass"), the same design as Media Player's toolbar.
 *
 * The buttons are an array of TOOLBUTTON in global memory, sorted by
 * position.  Each button is drawn from one bitmap: column iButton, row
 * iState (LoadUIBitmap recolours it to the button colours).  The control
 * tells its parent about clicks and keys with WM_COMMAND, its ID (189) in
 * wParam and MAKEWORD(button, flags) in the high word of lParam:
 * 0x100 auto-repeat, 0x200 right button or Shift, 0x400 double-click.
 * The button's activity (BTNACT_*) says what happened.
 *
 * Window words (cbWndExtra 0x16):
 *   0 HGLOBAL of the buttons   2 number of buttons   4 a button is pressed
 *   6 Space is down   8 index of the focus button   0A right button / Shift
 *   0C HBITMAP   0E bitmap resource ID   10 button size (MAKELONG(cy, cx))
 *   14 HINSTANCE of the bitmap
 *
 * Index arguments are checked against the count with ">", so the index
 * equal to the count is accepted and reads past the last button.
 */

#include <windows.h>
#include "vidcap.h"

#define GWW_HBUTTONS    0
#define GWW_NBUTTONS    2
#define GWW_PRESSED     4
#define GWW_KEYDOWN     6
#define GWW_FOCUS       8
#define GWW_RIGHT       0x0A
#define GWW_HBM         0x0C
#define GWW_IBMP        0x0E
#define GWL_BMSIZE      0x10
#define GWW_HINSTBMP    0x14

#define IDT_TOOLBAR     1

char        gszToolBarClass[] = "ToolBarClass";     /* DS:054A */

/*
 * seg3:18AE-1920  toolbarInit
 */
BOOL FAR PASCAL toolbarInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (hPrev == NULL) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = gszToolBarClass;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hInstance     = hInst;
        wc.style         = CS_DBLCLKS;
        wc.lpfnWndProc   = toolbarWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0x16;
        if (!RegisterClass(&wc))
            return FALSE;
    }
    return TRUE;
}

/*
 * seg3:1924-1975  toolbarSetBitmap
 */
void FAR PASCAL toolbarSetBitmap(HWND hwnd, HINSTANCE hInst, int ibmp, POINT ptSize)
{
    SetWindowWord(hwnd, GWW_HINSTBMP, (WORD)hInst);
    SetWindowWord(hwnd, GWW_HBM, 0);
    SetWindowWord(hwnd, GWW_IBMP, ibmp);
    SetWindowLong(hwnd, GWL_BMSIZE, MAKELONG(ptSize.y, ptSize.x));
    SendMessage(hwnd, WM_SYSCOLORCHANGE, 0, 0L);
}

/*
 * seg3:1978-1992  toolbarGetNumButtons
 */
int FAR PASCAL toolbarGetNumButtons(HWND hwnd)
{
    return GetWindowWord(hwnd, GWW_NBUTTONS);
}

/*
 * seg3:1996-19F7  toolbarButtonFromIndex
 */
int FAR PASCAL toolbarButtonFromIndex(HWND hwnd, int iBtnPos)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iButton;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_NBUTTONS) < iBtnPos || iBtnPos < 0)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    iButton = lp[iBtnPos].iButton;
    GlobalUnlock(h);
    return iButton;
}

/*
 * seg3:19FA-1A80  toolbarIndexFromButton
 */
int FAR PASCAL toolbarIndexFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iBtnPos;
    int             i;

    iBtnPos = -1;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_NBUTTONS); i++, lp++) {
        if (lp->iButton == iButton) {
            iBtnPos = i;
            break;
        }
    }
    GlobalUnlock(h);
    return iBtnPos;
}

/*
 * seg3:1A84-1B0E  toolbarPrevStateFromButton
 */
int FAR PASCAL toolbarPrevStateFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iPrevState;
    int             i;

    iPrevState = -1;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_NBUTTONS); i++, lp++) {
        if (lp->iButton == iButton) {
            iPrevState = lp->iPrevState;
            break;
        }
    }
    GlobalUnlock(h);
    return iPrevState;
}

/*
 * seg3:1B12-1B9B  toolbarActivityFromButton
 *
 * Keeps searching after a match: the last one counts.
 */
int FAR PASCAL toolbarActivityFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iActivity;
    int             i;

    iActivity = -1;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_NBUTTONS); i++, lp++)
        if (lp->iButton == iButton)
            iActivity = lp->iActivity;
    GlobalUnlock(h);
    return iActivity;
}

/*
 * seg3:1B9E-1C2A  toolbarIndexFromPoint
 */
int FAR PASCAL toolbarIndexFromPoint(HWND hwnd, POINT pt)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iBtnPos;
    int             i;

    iBtnPos = -1;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_NBUTTONS); i++, lp++) {
        if (PtInRect(&lp->rc, pt)) {
            iBtnPos = i;
            break;
        }
    }
    GlobalUnlock(h);
    return iBtnPos;
}

/*
 * seg3:1C2E-1C96  toolbarRectFromIndex
 *
 * Returns GlobalUnlock's result, or FALSE for a bad index.
 */
BOOL FAR PASCAL toolbarRectFromIndex(HWND hwnd, int iBtnPos, LPRECT lprc)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_NBUTTONS) < iBtnPos || iBtnPos < 0)
        return FALSE;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    *lprc = lp[iBtnPos].rc;
    return GlobalUnlock(h);
}

/*
 * seg3:1C9A-1CF2  toolbarStateFromButton
 */
int FAR PASCAL toolbarStateFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iBtnPos;
    int             iState;

    iBtnPos = toolbarIndexFromButton(hwnd, iButton);
    if (iBtnPos == -1)
        return -1;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    iState = lp[iBtnPos].iState;
    GlobalUnlock(h);
    return iState;
}

/*
 * seg3:1CF6-1D23  toolbarFullStateFromButton
 *
 * The state, or the previous state while the button is held down.
 */
int FAR PASCAL toolbarFullStateFromButton(HWND hwnd, int iButton)
{
    int iState;

    iState = toolbarStateFromButton(hwnd, iButton);
    if (iState == BTNST_FULLDOWN)
        return toolbarPrevStateFromButton(hwnd, iButton);
    return iState;
}

/*
 * seg3:1D26-1D87  toolbarStringFromIndex
 */
int FAR PASCAL toolbarStringFromIndex(HWND hwnd, int iBtnPos)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iString;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_NBUTTONS) < iBtnPos || iBtnPos < 0)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    iString = lp[iBtnPos].iString;
    GlobalUnlock(h);
    return iString;
}

/*
 * seg3:1D8A-1DEB  toolbarTypeFromIndex
 */
int FAR PASCAL toolbarTypeFromIndex(HWND hwnd, int iBtnPos)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iType;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_NBUTTONS) < iBtnPos || iBtnPos < 0)
        return -1;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    iType = lp[iBtnPos].iType;
    GlobalUnlock(h);
    return iType;
}

/*
 * seg3:1DEE-1F86  toolbarAddTool
 *
 * Inserts the button in position order; the array grows by 8 buttons at
 * a time.  A button added with a focus state gets the focus.
 *
 * Quirk of the original: the focus state is stored as it is; only the
 * local copy is turned into the plain state.
 */
BOOL FAR PASCAL toolbarAddTool(HWND hwnd, TOOLBUTTON tb)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    LPTOOLBUTTON    lpT;
    BOOL            fInserted;
    int             n;
    int             i;
    int             j;

    fInserted = FALSE;
    if (toolbarIndexFromButton(hwnd, tb.iButton) != -1)
        return FALSE;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_NBUTTONS);
    if (!(n & 7) && n > 0) {
        h = GlobalReAlloc(h, GlobalSize(h) + 8 * sizeof(TOOLBUTTON), GMEM_MOVEABLE | GMEM_SHARE);
        if (h == NULL)
            return FALSE;
    }

    lp = (LPTOOLBUTTON)GlobalLock(h);
    i = 0;
    if (n > 0) {
        for (i = 0; i < n; i++) {
            if (lp[i].rc.left > tb.rc.left ||
                (lp[i].rc.left == tb.rc.left && lp[i].rc.top > tb.rc.top)) {
                fInserted = TRUE;
                lpT = &lp[n];
                for (j = n - i; j; j--, lpT--)
                    *lpT = lpT[-1];
                lp[i] = tb;
                InvalidateRect(hwnd, &lp[i].rc, FALSE);
                break;
            }
        }
    }
    if (!fInserted)
        lp[i] = tb;

    if (tb.iState == BTNST_FOCUSUP) {
        tb.iState = BTNST_UP;
        SetWindowWord(hwnd, GWW_FOCUS, i);
    } else if (tb.iState == BTNST_FOCUSDOWN || tb.iState == BTNST_FULLDOWN) {
        tb.iState = BTNST_DOWN;
        SetWindowWord(hwnd, GWW_FOCUS, i);
    }

    GlobalUnlock(h);
    SetWindowWord(hwnd, GWW_NBUTTONS, n + 1);
    SetWindowWord(hwnd, GWW_HBUTTONS, (WORD)h);
    InvalidateRect(hwnd, &tb.rc, FALSE);
    return TRUE;
}

/*
 * seg3:1F8A-2018  toolbarRetrieveTool
 */
BOOL FAR PASCAL toolbarRetrieveTool(HWND hwnd, int iButton, LPTOOLBUTTON lptb)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    BOOL            f;
    int             i;

    f = FALSE;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_NBUTTONS); i++, lp++) {
        if (lp->iButton == iButton) {
            *lptb = *lp;
            f = TRUE;
            break;
        }
    }
    GlobalUnlock(h);
    return f;
}

/*
 * seg3:201C-2159  toolbarRemoveTool
 *
 * Quirk of the original: closing the gap copies one button more than
 * there is, from past the end of the array.
 */
BOOL FAR PASCAL toolbarRemoveTool(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    LPTOOLBUTTON    lpT;
    BOOL            f;
    int             n;
    int             i;
    int             j;

    f = FALSE;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_NBUTTONS);
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++) {
        if (lp[i].iButton == iButton) {
            f = TRUE;
            InvalidateRect(hwnd, &lp[i].rc, FALSE);
            if (n - i - 1 != 0 && i < n) {
                lpT = &lp[i];
                for (j = n - i; j; j--, lpT++)
                    *lpT = lpT[1];
            }
            break;
        }
    }
    GlobalUnlock(h);
    if (!f)
        return FALSE;

    n--;
    if (!(n & 7) && n > 0) {
        h = GlobalReAlloc(h, GlobalSize(h) - 8 * sizeof(TOOLBUTTON), GMEM_MOVEABLE | GMEM_SHARE);
        if (h == NULL)
            return FALSE;
    }
    SetWindowWord(hwnd, GWW_NBUTTONS, n);
    SetWindowWord(hwnd, GWW_HBUTTONS, (WORD)h);
    return TRUE;
}

/*
 * seg3:215C-21DD  toolbarModifyString
 */
BOOL FAR PASCAL toolbarModifyString(HWND hwnd, int iButton, int iString)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    BOOL            f;
    int             n;
    int             i;

    f = FALSE;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_NBUTTONS);
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lp++) {
        if (lp->iButton == iButton) {
            lp->iString = iString;
            f = TRUE;
            break;
        }
    }
    GlobalUnlock(h);
    return f;
}

/*
 * seg3:21E0-2298  toolbarModifyState
 *
 * Pressing a radio button (type 3 and up) releases the others of its
 * type.
 */
BOOL FAR PASCAL toolbarModifyState(HWND hwnd, int iButton, int iState)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    BOOL            f;
    int             n;
    int             i;

    f = FALSE;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_NBUTTONS);
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lp++) {
        if (lp->iButton == iButton) {
            if (lp->iState != iState) {
                lp->iState = iState;
                InvalidateRect(hwnd, &lp->rc, FALSE);
            }
            f = TRUE;
            if (lp->iType >= BTNTYPE_RADIO + 1 && iState == BTNST_DOWN)
                toolbarExclusiveRadio(hwnd, lp->iType, iButton);
            break;
        }
    }
    GlobalUnlock(h);
    return f;
}

/*
 * seg3:229C-230B  toolbarModifyPrevState
 *
 * TRUE unless there are no buttons, found or not.
 */
BOOL FAR PASCAL toolbarModifyPrevState(HWND hwnd, int iButton, int iPrevState)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             n;
    int             i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_NBUTTONS);
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lp++) {
        if (lp->iButton == iButton) {
            lp->iPrevState = iPrevState;
            break;
        }
    }
    GlobalUnlock(h);
    return TRUE;
}

/*
 * seg3:230E-237D  toolbarModifyActivity
 */
BOOL FAR PASCAL toolbarModifyActivity(HWND hwnd, int iButton, int iActivity)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             n;
    int             i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_NBUTTONS);
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lp++) {
        if (lp->iButton == iButton) {
            lp->iActivity = iActivity;
            break;
        }
    }
    GlobalUnlock(h);
    return TRUE;
}

/*
 * seg3:2380-2418  toolbarFixFocus
 *
 * Moves the focus off a grayed button.
 */
BOOL FAR PASCAL toolbarFixFocus(HWND hwnd)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             iFocus;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    iFocus = GetWindowWord(hwnd, GWW_FOCUS);
    if (iFocus < 0 || GetWindowWord(hwnd, GWW_NBUTTONS) <= iFocus)
        SetWindowWord(hwnd, GWW_FOCUS, 0);
    if (lp[GetWindowWord(hwnd, GWW_FOCUS)].iState == BTNST_GRAYED &&
        !toolbarMoveFocus(hwnd, FALSE)) {
        SetWindowWord(hwnd, GWW_FOCUS, (WORD)-1);
        toolbarMoveFocus(hwnd, FALSE);
    }
    GlobalUnlock(h);
    return TRUE;
}

/*
 * seg3:241C-24B1  toolbarExclusiveRadio
 */
BOOL FAR PASCAL toolbarExclusiveRadio(HWND hwnd, int iType, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lp;
    int             i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS);
    if (h == NULL)
        return FALSE;
    lp = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_NBUTTONS); i++, lp++)
        if (lp->iType == iType && lp->iButton != iButton && lp->iState != BTNST_GRAYED)
            toolbarModifyState(hwnd, lp->iButton, BTNST_UP);
    GlobalUnlock(h);
    return TRUE;
}

/*
 * seg3:24B4-24D9  toolbarNotify  (NEAR PASCAL)
 */
static void NEAR PASCAL toolbarNotify(HWND hwnd, WORD w)
{
    PostMessage(GetParent(hwnd), WM_COMMAND, GetWindowWord(hwnd, GWW_ID), MAKELONG(hwnd, w));
}

/*
 * seg3:24DC-263C  toolbarPaint  (NEAR PASCAL)
 *
 * Quirk of the original: the test that turns a "focus down" button back
 * to "down" when it does not have the focus checks for the focus button
 * instead, so the focused pressed look never shows.
 */
static BOOL NEAR PASCAL toolbarPaint(HWND hwnd, HDC hdc)
{
    HDC     hdcMem;
    RECT    rc;
    DWORD   dwSize;
    int     iButton;
    int     iState;
    int     i;

    hdcMem = CreateCompatibleDC(hdc);
    if (hdcMem == NULL)
        return FALSE;
    if (GetWindowWord(hwnd, GWW_HBM) &&
        !SelectObject(hdcMem, (HGDIOBJ)GetWindowWord(hwnd, GWW_HBM))) {
        DeleteDC(hdcMem);
        return FALSE;
    }

    toolbarFixFocus(hwnd);
    for (i = 0; i < toolbarGetNumButtons(hwnd); i++) {
        iButton = toolbarButtonFromIndex(hwnd, i);
        iState = toolbarStateFromButton(hwnd, iButton);
        toolbarRectFromIndex(hwnd, i, &rc);

        if (GetFocus() == hwnd && GetWindowWord(hwnd, GWW_FOCUS) == i && iState == BTNST_UP)
            iState = BTNST_FOCUSUP;
        if (GetFocus() == hwnd && GetWindowWord(hwnd, GWW_FOCUS) == i && iState == BTNST_DOWN)
            iState = BTNST_FOCUSDOWN;
        if ((GetFocus() != hwnd || GetWindowWord(hwnd, GWW_FOCUS) != i) && iState == BTNST_FOCUSUP)
            iState = BTNST_UP;
        if ((GetFocus() != hwnd || GetWindowWord(hwnd, GWW_FOCUS) == i) && iState == BTNST_FOCUSDOWN)
            iState = BTNST_DOWN;

        dwSize = GetWindowLong(hwnd, GWL_BMSIZE);
        BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hdcMem,
               HIWORD(dwSize) * iButton, LOWORD(dwSize) * iState, SRCCOPY);
    }

    DeleteDC(hdcMem);
    return TRUE;
}

/*
 * seg3:2640-2730  toolbarMoveFocus
 *
 * Moves the focus to the next (or previous) button that is not grayed.
 * Returns TRUE if it moved.
 */
BOOL FAR PASCAL toolbarMoveFocus(HWND hwnd, BOOL fBackward)
{
    RECT    rc;
    int     iOld;
    int     iStep;
    int     iEnd;
    int     i;

    iOld = GetWindowWord(hwnd, GWW_FOCUS);
    if (iOld < -1 || GetWindowWord(hwnd, GWW_NBUTTONS) < iOld)
        SetWindowWord(hwnd, GWW_FOCUS, 0);

    if (fBackward) {
        iStep = -1;
        iEnd = -1;
    } else {
        iStep = 1;
        iEnd = GetWindowWord(hwnd, GWW_NBUTTONS);
    }

    i = GetWindowWord(hwnd, GWW_FOCUS) + iStep;
    if (i != iEnd) {
        for (;;) {
            if (toolbarFullStateFromButton(hwnd, toolbarButtonFromIndex(hwnd, i)) != BTNST_GRAYED) {
                SetWindowWord(hwnd, GWW_FOCUS, i);
                toolbarRectFromIndex(hwnd, iOld, &rc);
                InvalidateRect(hwnd, &rc, FALSE);
                toolbarRectFromIndex(hwnd, i, &rc);
                InvalidateRect(hwnd, &rc, FALSE);
                break;
            }
            i += iStep;
            if (i == iEnd)
                break;
        }
    }
    return GetWindowWord(hwnd, GWW_FOCUS) != iOld;
}

/*
 * seg3:2734-27D2  toolbarSetFocus
 *
 * -1 is the first button that can have the focus, -2 the last.
 */
BOOL FAR PASCAL toolbarSetFocus(HWND hwnd, int iButton)
{
    RECT    rc;
    int     iBtnPos;

    if (GetCapture() == hwnd || GetWindowWord(hwnd, GWW_KEYDOWN))
        return FALSE;

    toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_FOCUS), &rc);
    InvalidateRect(hwnd, &rc, FALSE);

    if (iButton == -1) {
        SetWindowWord(hwnd, GWW_FOCUS, (WORD)iButton);
        return toolbarMoveFocus(hwnd, FALSE);
    }
    if (iButton == -2) {
        SetWindowWord(hwnd, GWW_FOCUS, GetWindowWord(hwnd, GWW_NBUTTONS));
        return toolbarMoveFocus(hwnd, TRUE);
    }
    iBtnPos = toolbarIndexFromButton(hwnd, iButton);
    if (iBtnPos == -1)
        return FALSE;
    SetWindowWord(hwnd, GWW_FOCUS, iBtnPos - 1);
    return toolbarMoveFocus(hwnd, FALSE);
}

/*
 * seg3:27D6-2931  LoadUIBitmap
 *
 * Loads a 4-bit DIB resource with its black, grays, white, bright green
 * and yellow replaced by the given colours.
 */
HBITMAP FAR PASCAL LoadUIBitmap(HINSTANCE hInst, LPCSTR lpszName, COLORREF rgbText, COLORREF rgbFace,
                                COLORREF rgbShadow, COLORREF rgbHighlight, COLORREF rgbWindow,
                                COLORREF rgbFrame)
{
    LPBITMAPINFOHEADER  lpbi;
    RGBQUAD FAR        *prgb;
    HGLOBAL             h;
    HDC                 hdc;
    HBITMAP             hbm;
    int                 nColors;

    h = LoadResource(hInst, FindResource(hInst, lpszName, RT_BITMAP));
    lpbi = (LPBITMAPINFOHEADER)LockResource(h);
    if (lpbi == NULL)
        return NULL;
    if (lpbi->biSize != sizeof(BITMAPINFOHEADER) || lpbi->biBitCount != 4)
        return NULL;

    prgb = (RGBQUAD FAR *)((LPBYTE)lpbi + (WORD)lpbi->biSize);
    nColors = LOWORD(lpbi->biClrUsed) ? LOWORD(lpbi->biClrUsed) : 1 << lpbi->biBitCount;

#define SETRGB(i, rgb) \
    (((LPWORD)&prgb[i])[0] = MAKEWORD(GetBValue(rgb), GetGValue(rgb)), \
     ((LPWORD)&prgb[i])[1] = GetRValue(rgb))
    SETRGB(0, rgbText);
    SETRGB(7, rgbFace);
    SETRGB(8, rgbShadow);
    SETRGB(15, rgbHighlight);
    SETRGB(11, rgbWindow);
    SETRGB(10, rgbFrame);
#undef SETRGB

    hdc = GetDC(NULL);
    hbm = CreateDIBitmap(hdc, lpbi, CBM_INIT, (LPBYTE)prgb + nColors * sizeof(RGBQUAD),
                         (LPBITMAPINFO)lpbi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);
    GlobalUnlock(h);
    FreeResource(h);
    return hbm;
}

#define RIGHTFLAG(hwnd)     (GetWindowWord(hwnd, GWW_RIGHT) ? 0x0200 : 0)

/*
 * seg3:2934-32C8  toolbarWndProc  (exported ordinal 6)
 *
 * Quirk of the original: on Space up the right-button flag is added to
 * the focus index instead of to the button.
 */
LRESULT FAR PASCAL toolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    HBITMAP     hbm;
    HINSTANCE   hInst;
    COLORREF    rgbHighlight;
    BOOL        fIn;
    int         ibmp;
    int         iBtnPos;
    int         iOld;
    int         iType;
    int         iButton;
    int         iState;
    int         iPrevState;
    WORD        wFlag;

    switch (msg) {
    case WM_CREATE:
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowLong(hwnd, GWL_STYLE, ((LPCREATESTRUCT)lParam)->style & 0xFFFF00FFL);
        SetWindowWord(hwnd, GWW_HBUTTONS,
                      (WORD)GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 8 * sizeof(TOOLBUTTON)));
        SetWindowWord(hwnd, GWW_NBUTTONS, 0);
        SetWindowWord(hwnd, GWW_PRESSED, 0);
        SetWindowWord(hwnd, GWW_KEYDOWN, 0);
        SetWindowWord(hwnd, GWW_FOCUS, (WORD)-1);
        SetWindowWord(hwnd, GWW_RIGHT, 0);
        SetWindowWord(hwnd, GWW_ID, IDC_TOOLBAR);
        SetWindowWord(hwnd, GWW_HBM, 0);
        break;

    case WM_DESTROY:
        if (GetWindowWord(hwnd, GWW_HBM))
            DeleteObject((HGDIOBJ)GetWindowWord(hwnd, GWW_HBM));
        SetWindowWord(hwnd, GWW_HBM, 0);
        if (GetWindowWord(hwnd, GWW_HBUTTONS))
            GlobalFree((HGLOBAL)GetWindowWord(hwnd, GWW_HBUTTONS));
        SetWindowWord(hwnd, GWW_HBUTTONS, 0);
        break;

    case WM_SETFOCUS:
        iBtnPos = GetWindowWord(hwnd, GWW_FOCUS);
        if (iBtnPos < 0 || toolbarGetNumButtons(hwnd) <= iBtnPos) {
            iBtnPos = 0;
            SetWindowWord(hwnd, GWW_FOCUS, iBtnPos);
        }
        while (toolbarStateFromButton(hwnd, toolbarButtonFromIndex(hwnd, iBtnPos)) == BTNST_GRAYED) {
            iBtnPos++;
            if (toolbarGetNumButtons(hwnd) <= iBtnPos)
                iBtnPos = 0;
            if (GetWindowWord(hwnd, GWW_FOCUS) == iBtnPos)
                return 0L;
        }
        SetWindowWord(hwnd, GWW_FOCUS, iBtnPos);
        toolbarRectFromIndex(hwnd, iBtnPos, &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
        return 0L;

    case WM_KILLFOCUS:
        if (GetWindowWord(hwnd, GWW_KEYDOWN))
            SendMessage(hwnd, WM_KEYUP, VK_SPACE, 0L);
        toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_FOCUS), &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
        return 0L;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        toolbarPaint(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0L;

    case WM_SYSCOLORCHANGE:
        hInst = (HINSTANCE)GetWindowWord(hwnd, GWW_HINSTBMP);
        ibmp = GetWindowWord(hwnd, GWW_IBMP);
        hbm = (HBITMAP)GetWindowWord(hwnd, GWW_HBM);
        if (hbm)
            DeleteObject(hbm);
        rgbHighlight = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
        hbm = LoadUIBitmap(hInst, MAKEINTRESOURCE(ibmp), GetSysColor(COLOR_BTNTEXT),
                           GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNSHADOW), rgbHighlight,
                           GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWFRAME));
        SetWindowWord(hwnd, GWW_HBM, (WORD)hbm);
        return (LONG)(WORD)hbm;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB;

    case WM_KEYDOWN:
        if (!IsWindowEnabled(hwnd) || GetWindowWord(hwnd, GWW_PRESSED))
            break;
        if (wParam == VK_TAB && GetKeyState(VK_SHIFT) >= 0) {
            if (toolbarMoveFocus(hwnd, FALSE))
                return 0L;
            PostMessage(GetParent(hwnd), WM_NEXTDLGCTL, 0, 0L);
        } else if (wParam == VK_TAB && GetKeyState(VK_SHIFT) < 0) {
            if (toolbarMoveFocus(hwnd, TRUE))
                return 0L;
            PostMessage(GetParent(hwnd), WM_NEXTDLGCTL, 1, 0L);
        } else {
            if (wParam != VK_SPACE || GetCapture() == hwnd)
                break;
            iBtnPos = GetWindowWord(hwnd, GWW_FOCUS);
            iType = toolbarTypeFromIndex(hwnd, iBtnPos);
            iButton = toolbarButtonFromIndex(hwnd, iBtnPos);
            iState = toolbarStateFromButton(hwnd, iButton);
            if (GetWindowWord(hwnd, GWW_KEYDOWN) == 0) {
                SetWindowWord(hwnd, GWW_KEYDOWN, 1);
                SetWindowWord(hwnd, GWW_RIGHT, 0);
                SetWindowWord(hwnd, GWW_PRESSED, 1);
                if (iType == BTNTYPE_PUSH)
                    toolbarModifyState(hwnd, iButton, BTNST_DOWN);
                if (iType >= BTNTYPE_RADIO + 1 || iType == BTNTYPE_CHECKBOX) {
                    toolbarModifyPrevState(hwnd, iButton, iState);
                    toolbarModifyState(hwnd, iButton, BTNST_FULLDOWN);
                }
                toolbarModifyActivity(hwnd, iButton, BTNACT_KEYDOWN);
                toolbarNotify(hwnd, RIGHTFLAG(hwnd) + iButton);
                return 0L;
            }
            wFlag = RIGHTFLAG(hwnd);
            toolbarNotify(hwnd, wFlag + toolbarButtonFromIndex(hwnd, GetWindowWord(hwnd, GWW_FOCUS)) + 0x100);
            break;
        }
        toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_FOCUS), &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        return 0L;

    case WM_KEYUP:
        if (wParam != VK_SPACE || !GetWindowWord(hwnd, GWW_KEYDOWN))
            break;
        iBtnPos = GetWindowWord(hwnd, GWW_FOCUS);
        SetWindowWord(hwnd, GWW_KEYDOWN, 0);
        SetWindowWord(hwnd, GWW_PRESSED, 0);
        toolbarRectFromIndex(hwnd, iBtnPos, &rc);
        iType = toolbarTypeFromIndex(hwnd, iBtnPos);
        iButton = toolbarButtonFromIndex(hwnd, iBtnPos);
        toolbarStateFromButton(hwnd, iButton);
        if (iType == BTNTYPE_PUSH)
            toolbarModifyState(hwnd, iButton, BTNST_UP);
        if (iType == BTNTYPE_CHECKBOX) {
            iPrevState = toolbarPrevStateFromButton(hwnd, iButton);
            if (iPrevState == BTNST_DOWN)
                toolbarModifyState(hwnd, iButton, BTNST_UP);
            if (iPrevState == BTNST_UP)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
        }
        if (iType >= BTNTYPE_RADIO + 1) {
            toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            toolbarExclusiveRadio(hwnd, iType, iButton);
        }
        toolbarModifyActivity(hwnd, iButton, BTNACT_KEYUP);
        wFlag = RIGHTFLAG(hwnd);
        toolbarNotify(hwnd, toolbarButtonFromIndex(hwnd, wFlag + GetWindowWord(hwnd, GWW_FOCUS)));
        break;

    case WM_SYSKEYDOWN:
        if (GetWindowWord(hwnd, GWW_KEYDOWN))
            SendMessage(hwnd, WM_KEYUP, VK_SPACE, 0L);
        break;

    case WM_TIMER:
        if (!GetWindowWord(hwnd, GWW_PRESSED))
            break;
        iBtnPos = GetWindowWord(hwnd, GWW_FOCUS);
        toolbarButtonFromIndex(hwnd, iBtnPos);
        toolbarTypeFromIndex(hwnd, iBtnPos);
        wFlag = RIGHTFLAG(hwnd);
        toolbarNotify(hwnd, toolbarButtonFromIndex(hwnd, iBtnPos) + wFlag + 0x100);
        break;

    case WM_MOUSEMOVE:
        if (GetCapture() != hwnd)
            return 0L;
        iBtnPos = GetWindowWord(hwnd, GWW_FOCUS);
        toolbarRectFromIndex(hwnd, iBtnPos, &rc);
        fIn = PtInRect(&rc, MAKEPOINT(lParam));
        if (GetWindowWord(hwnd, GWW_PRESSED) == fIn)
            return 0L;
        SetWindowWord(hwnd, GWW_PRESSED, fIn);
        iType = toolbarTypeFromIndex(hwnd, iBtnPos);
        iButton = toolbarButtonFromIndex(hwnd, iBtnPos);
        toolbarStateFromButton(hwnd, iButton);
        if (fIn) {
            if (iType == BTNTYPE_PUSH)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            if (iType >= BTNTYPE_RADIO + 1 || iType == BTNTYPE_CHECKBOX)
                toolbarModifyState(hwnd, iButton, BTNST_FULLDOWN);
            toolbarModifyActivity(hwnd, iButton, BTNACT_MOUSEMOVEON);
            toolbarNotify(hwnd, RIGHTFLAG(hwnd) + iButton);
        } else {
            if (iType == BTNTYPE_PUSH)
                toolbarModifyState(hwnd, iButton, BTNST_UP);
            if (iType >= BTNTYPE_RADIO + 1 || iType == BTNTYPE_CHECKBOX)
                toolbarModifyState(hwnd, iButton, toolbarPrevStateFromButton(hwnd, iButton));
            toolbarModifyActivity(hwnd, iButton, BTNACT_MOUSEMOVEOFF);
            wFlag = RIGHTFLAG(hwnd);
            toolbarNotify(hwnd, wFlag + toolbarButtonFromIndex(hwnd, iBtnPos));
        }
        return 0L;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        if ((GetWindowLong(hwnd, GWL_STYLE) & WS_TABSTOP) && GetFocus() != hwnd)
            SetFocus(hwnd);
        if (!IsWindowEnabled(hwnd) || GetCapture() == hwnd || GetWindowWord(hwnd, GWW_PRESSED))
            return 0L;
        iBtnPos = toolbarIndexFromPoint(hwnd, MAKEPOINT(lParam));
        if (iBtnPos < 0)
            return 0L;
        iType = toolbarTypeFromIndex(hwnd, iBtnPos);
        iButton = toolbarButtonFromIndex(hwnd, iBtnPos);
        iState = toolbarStateFromButton(hwnd, iButton);
        if (iType != BTNTYPE_RADIO && iState == BTNST_GRAYED)
            return 0L;

        SetCapture(hwnd);
        SetWindowWord(hwnd, GWW_RIGHT, (msg == WM_RBUTTONDOWN || (wParam & MK_SHIFT)) ? 1 : 0);
        SetWindowWord(hwnd, GWW_PRESSED, 1);
        iOld = GetWindowWord(hwnd, GWW_FOCUS);
        SetWindowWord(hwnd, GWW_FOCUS, iBtnPos);
        if (iType == BTNTYPE_PUSH)
            toolbarModifyState(hwnd, iButton, BTNST_DOWN);
        if (iType == BTNTYPE_CHECKBOX || iType >= BTNTYPE_RADIO + 1) {
            toolbarModifyPrevState(hwnd, iButton, iState);
            toolbarModifyState(hwnd, iButton, BTNST_FULLDOWN);
        }
        toolbarModifyActivity(hwnd, iButton, BTNACT_MOUSEDOWN);
        if (msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK)
            toolbarNotify(hwnd, RIGHTFLAG(hwnd) + iButton + 0x400);
        else
            toolbarNotify(hwnd, RIGHTFLAG(hwnd) + iButton);

        toolbarRectFromIndex(hwnd, iBtnPos, &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        toolbarRectFromIndex(hwnd, iOld, &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
        SetTimer(hwnd, IDT_TOOLBAR, 200, NULL);
        return 0L;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (GetCapture() != hwnd)
            return 0L;
        iBtnPos = GetWindowWord(hwnd, GWW_FOCUS);
        ReleaseCapture();
        KillTimer(hwnd, IDT_TOOLBAR);
        toolbarRectFromIndex(hwnd, iBtnPos, &rc);
        iType = toolbarTypeFromIndex(hwnd, iBtnPos);
        iButton = toolbarButtonFromIndex(hwnd, iBtnPos);
        toolbarStateFromButton(hwnd, iButton);
        if (!GetWindowWord(hwnd, GWW_PRESSED))
            return 0L;
        SetWindowWord(hwnd, GWW_PRESSED, 0);
        if (iType == BTNTYPE_PUSH)
            toolbarModifyState(hwnd, iButton, BTNST_UP);
        if (iType == BTNTYPE_CHECKBOX) {
            iPrevState = toolbarPrevStateFromButton(hwnd, iButton);
            if (iPrevState == BTNST_DOWN)
                toolbarModifyState(hwnd, iButton, BTNST_UP);
            if (iPrevState == BTNST_UP)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
        }
        if (iType >= BTNTYPE_RADIO + 1) {
            toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            toolbarExclusiveRadio(hwnd, iType, iButton);
        }
        toolbarModifyActivity(hwnd, iButton, BTNACT_MOUSEUP);
        toolbarNotify(hwnd, RIGHTFLAG(hwnd) + iButton);
        return 0L;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
