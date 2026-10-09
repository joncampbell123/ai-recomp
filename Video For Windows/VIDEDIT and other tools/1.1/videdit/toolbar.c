/*
 * toolbar.c - tool bars: code segment 5, s05:355A-4E52
 *
 * "ToolBarClass": a row of bitmap buttons, the same tool bar code as in
 * Media Player and VidCap.  Each button has a rectangle, an ID (also its
 * column in the bitmap), a state (its row: grayed, up, down, focus up,
 * focus down, full down), a type (push button, check box, push button
 * that works while grayed, radio groups 3 and up), and the last activity.
 * The array is a moveable global block that grows and shrinks in steps of
 * 8 buttons and is kept sorted by position.  Tool bars notify their parent
 * with WM_COMMAND IDC_TOOLBAR, lParam MAKELONG(hwnd, code), see vedwnd.h.
 * The bitmap's gray, white and black are replaced with the system button
 * colours (LoadUIBitmap).  The module's data is DS:0BA2-0BC7.
 */

#include "videdit.h"
#include "vedwnd.h"

#define IDT_TOOLBAR     1

static void NEAR PASCAL toolbarNotify(HWND hwnd, int code);
static BOOL NEAR PASCAL toolbarPaint(HWND hwnd, HDC hdc);

/*
 * s05:355A-35BB  toolbarInit  (FAR PASCAL)
 */
BOOL FAR PASCAL toolbarInit(HINSTANCE hInst, HINSTANCE hPrev)
{
    WNDCLASS wc;

    if (!hPrev) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = "ToolBarClass";
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
 * s05:35BE-3606  toolbarSetBitmap  (FAR PASCAL)
 *
 * Remembers the bitmap; WM_SYSCOLORCHANGE loads it.
 */
BOOL FAR PASCAL toolbarSetBitmap(HWND hwnd, HINSTANCE hInst, int ibmp, POINT ptSize)
{
    SetWindowWord(hwnd, GWW_TB_HINST, (WORD)hInst);
    SetWindowWord(hwnd, GWW_TB_BITMAP, 0);
    SetWindowWord(hwnd, GWW_TB_BITMAPID, ibmp);
    SetWindowLong(hwnd, GWL_TB_BUTTONSIZE, MAKELONG(ptSize.y, ptSize.x));
    return (BOOL)SendMessage(hwnd, WM_SYSCOLORCHANGE, 0, 0L);
}

/*
 * s05:360A-3618  toolbarGetNumButtons  (FAR PASCAL)
 */
int FAR PASCAL toolbarGetNumButtons(HWND hwnd)
{
    return GetWindowWord(hwnd, GWW_TB_COUNT);
}

/*
 * s05:361C-3672  toolbarButtonFromIndex  (FAR PASCAL)
 *
 * -1 if there is no such button.  An index equal to the count is
 * accepted and reads past the last button.
 */
int FAR PASCAL toolbarButtonFromIndex(HWND hwnd, int iBtnPos)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             iButton;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_TB_COUNT) < iBtnPos || iBtnPos < 0)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    iButton = lptb[iBtnPos].iButton;
    GlobalUnlock(h);
    return iButton;
}

/*
 * s05:3676-36F1  toolbarIndexFromButton  (FAR PASCAL)
 */
int FAR PASCAL toolbarIndexFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i, iRet = -1;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_TB_COUNT); i++, lptb++) {
        if (lptb->iButton == iButton) {
            iRet = i;
            break;
        }
    }
    GlobalUnlock(h);
    return iRet;
}

/*
 * s05:36F4-3773  toolbarPrevStateFromButton  (FAR PASCAL)
 */
int FAR PASCAL toolbarPrevStateFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i, iRet = -1;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_TB_COUNT); i++, lptb++) {
        if (lptb->iButton == iButton) {
            iRet = lptb->iPrevState;
            break;
        }
    }
    GlobalUnlock(h);
    return iRet;
}

/*
 * s05:3776-37F6  toolbarActivityFromButton  (FAR PASCAL)
 *
 * Unlike the others it doesn't stop at the first match.
 */
int FAR PASCAL toolbarActivityFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i, iRet = -1;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_TB_COUNT); i++, lptb++) {
        if (lptb->iButton == iButton)
            iRet = lptb->iActivity;
    }
    GlobalUnlock(h);
    return iRet;
}

/*
 * s05:37FA-3879  toolbarIndexFromPoint  (FAR PASCAL)
 */
int FAR PASCAL toolbarIndexFromPoint(HWND hwnd, POINT pt)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i, iRet = -1;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_TB_COUNT); i++, lptb++) {
        if (PtInRect(&lptb->rc, pt)) {
            iRet = i;
            break;
        }
    }
    GlobalUnlock(h);
    return iRet;
}

/*
 * s05:387C-38D9  toolbarRectFromIndex  (FAR PASCAL)
 */
BOOL FAR PASCAL toolbarRectFromIndex(HWND hwnd, int iBtnPos, LPRECT lprc)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_TB_COUNT) < iBtnPos || iBtnPos < 0)
        return FALSE;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    *lprc = lptb[iBtnPos].rc;
    return GlobalUnlock(h);
}

/*
 * s05:38DC-3929  toolbarStateFromButton  (FAR PASCAL)
 */
int FAR PASCAL toolbarStateFromButton(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i, iState;

    i = toolbarIndexFromButton(hwnd, iButton);
    if (i == -1)
        return -1;
    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    iState = lptb[i].iState;
    GlobalUnlock(h);
    return iState;
}

/*
 * s05:392C-3949  toolbarFullStateFromButton  (FAR PASCAL)
 *
 * The state, except that a full-down button reports its previous state.
 */
int FAR PASCAL toolbarFullStateFromButton(HWND hwnd, int iButton)
{
    int iState;

    iState = toolbarStateFromButton(hwnd, iButton);
    if (iState == BTNST_FULLDOWN)
        iState = toolbarPrevStateFromButton(hwnd, iButton);
    return iState;
}

/*
 * s05:394C-39A2  toolbarStringFromIndex  (FAR PASCAL)
 */
int FAR PASCAL toolbarStringFromIndex(HWND hwnd, int iBtnPos)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             iString;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_TB_COUNT) < iBtnPos || iBtnPos < 0)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    iString = lptb[iBtnPos].iString;
    GlobalUnlock(h);
    return iString;
}

/*
 * s05:39A6-39FC  toolbarTypeFromIndex  (FAR PASCAL)
 */
int FAR PASCAL toolbarTypeFromIndex(HWND hwnd, int iBtnPos)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             iType;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL || GetWindowWord(hwnd, GWW_TB_COUNT) < iBtnPos || iBtnPos < 0)
        return -1;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    iType = lptb[iBtnPos].iType;
    GlobalUnlock(h);
    return iType;
}

/*
 * s05:3A00-3B97  toolbarAddTool  (FAR PASCAL)
 *
 * Inserts a button sorted by left, then top.  Button IDs must be unique.
 * A button added in a focus state gets the focus; it is stored with that
 * state unchanged (the conversion to up or down is only made on the
 * argument copy, after it was stored).
 */
BOOL FAR PASCAL toolbarAddTool(HWND hwnd, TOOLBUTTON tb)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             n, i, j;
    BOOL            fInserted = FALSE;

    if (toolbarIndexFromButton(hwnd, tb.iButton) != -1)
        return FALSE;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_TB_COUNT);
    if (!(n & 7) && n > 0) {
        h = GlobalReAlloc(h, GlobalSize(h) + 8 * sizeof(TOOLBUTTON), GMEM_MOVEABLE | GMEM_DDESHARE);
        if (h == NULL)
            return FALSE;
    }

    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++) {
        if (lptb[i].rc.left > tb.rc.left ||
            (lptb[i].rc.left == tb.rc.left && lptb[i].rc.top > tb.rc.top)) {
            fInserted = TRUE;
            for (j = n; j > i; j--)
                lptb[j] = lptb[j - 1];
            lptb[i] = tb;
            InvalidateRect(hwnd, &lptb[i].rc, FALSE);
            break;
        }
    }
    if (!fInserted)
        lptb[i] = tb;

    if (tb.iState == BTNST_FOCUSUP) {
        tb.iState = BTNST_UP;
        SetWindowWord(hwnd, GWW_TB_FOCUS, i);
    } else if (tb.iState == BTNST_FOCUSDOWN || tb.iState == BTNST_FULLDOWN) {
        tb.iState = BTNST_DOWN;
        SetWindowWord(hwnd, GWW_TB_FOCUS, i);
    }

    GlobalUnlock(h);
    SetWindowWord(hwnd, GWW_TB_COUNT, n + 1);
    SetWindowWord(hwnd, GWW_TB_BUTTONS, (WORD)h);
    InvalidateRect(hwnd, &tb.rc, FALSE);
    return TRUE;
}

/*
 * s05:3B9A-3C1F  toolbarRetrieveTool  (FAR PASCAL)
 */
BOOL FAR PASCAL toolbarRetrieveTool(HWND hwnd, int iButton, LPTOOLBUTTON lptbOut)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i;
    BOOL            fFound = FALSE;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_TB_COUNT); i++, lptb++) {
        if (lptb->iButton == iButton) {
            *lptbOut = *lptb;
            fFound = TRUE;
            break;
        }
    }
    GlobalUnlock(h);
    return fFound;
}

/*
 * s05:3C22-3D55  toolbarRemoveTool  (FAR PASCAL)
 *
 * Moving the following buttons down copies one entry too many (it reads
 * the entry after the last one).
 */
BOOL FAR PASCAL toolbarRemoveTool(HWND hwnd, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             n, i, j;
    BOOL            fFound = FALSE;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_TB_COUNT);
    lptb = (LPTOOLBUTTON)GlobalLock(h);

    for (i = 0; i < n; i++) {
        if (lptb[i].iButton == iButton) {
            fFound = TRUE;
            InvalidateRect(hwnd, &lptb[i].rc, FALSE);
            if (n - i - 1 != 0) {
                for (j = n - i; j; j--, i++)
                    lptb[i] = lptb[i + 1];
            }
            break;
        }
    }
    GlobalUnlock(h);
    if (!fFound)
        return FALSE;

    n--;
    if (!(n & 7) && n > 0) {
        h = GlobalReAlloc(h, GlobalSize(h) - 8 * sizeof(TOOLBUTTON), GMEM_MOVEABLE | GMEM_DDESHARE);
        if (h == NULL)
            return FALSE;
    }
    SetWindowWord(hwnd, GWW_TB_COUNT, n);
    SetWindowWord(hwnd, GWW_TB_BUTTONS, (WORD)h);
    return TRUE;
}

/*
 * s05:3D58-3DD2  toolbarModifyString  (FAR PASCAL)
 */
BOOL FAR PASCAL toolbarModifyString(HWND hwnd, int iButton, int iString)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             n, i;
    BOOL            fFound = FALSE;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_TB_COUNT);
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lptb++) {
        if (lptb->iButton == iButton) {
            lptb->iString = iString;
            fFound = TRUE;
            break;
        }
    }
    GlobalUnlock(h);
    return fFound;
}

/*
 * s05:3DD6-3E85  toolbarModifyState  (FAR PASCAL)
 *
 * Redraws the button if the state changes.  A radio button going down
 * releases the others of its group.
 */
BOOL FAR PASCAL toolbarModifyState(HWND hwnd, int iButton, int iState)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             n, i;
    BOOL            fFound = FALSE;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_TB_COUNT);
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lptb++) {
        if (lptb->iButton == iButton) {
            if (lptb->iState != iState) {
                lptb->iState = iState;
                InvalidateRect(hwnd, &lptb->rc, FALSE);
            }
            fFound = TRUE;
            if (lptb->iType >= TBTYPE_RADIO && iState == BTNST_DOWN)
                toolbarExclusiveRadio(hwnd, lptb->iType, iButton);
            break;
        }
    }
    GlobalUnlock(h);
    return fFound;
}

/*
 * s05:3E88-3EEB  toolbarModifyPrevState  (FAR PASCAL)
 */
BOOL FAR PASCAL toolbarModifyPrevState(HWND hwnd, int iButton, int iPrevState)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             n, i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_TB_COUNT);
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lptb++) {
        if (lptb->iButton == iButton) {
            lptb->iPrevState = iPrevState;
            break;
        }
    }
    GlobalUnlock(h);
    return TRUE;
}

/*
 * s05:3EEE-3F51  toolbarModifyActivity  (FAR PASCAL)
 */
BOOL FAR PASCAL toolbarModifyActivity(HWND hwnd, int iButton, int iActivity)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             n, i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    n = GetWindowWord(hwnd, GWW_TB_COUNT);
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < n; i++, lptb++) {
        if (lptb->iButton == iButton) {
            lptb->iActivity = iActivity;
            break;
        }
    }
    GlobalUnlock(h);
    return TRUE;
}

/*
 * s05:3F54-3FE2  toolbarFixFocus  (FAR PASCAL)
 *
 * Keeps the focus index in range and off grayed buttons.
 */
BOOL FAR PASCAL toolbarFixFocus(HWND hwnd)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    lptb = (LPTOOLBUTTON)GlobalLock(h);

    i = GetWindowWord(hwnd, GWW_TB_FOCUS);
    if (i < 0 || GetWindowWord(hwnd, GWW_TB_COUNT) <= i)
        SetWindowWord(hwnd, GWW_TB_FOCUS, 0);

    if (lptb[GetWindowWord(hwnd, GWW_TB_FOCUS)].iState == BTNST_GRAYED &&
        !toolbarMoveFocus(hwnd, FALSE)) {
        SetWindowWord(hwnd, GWW_TB_FOCUS, (WORD)-1);
        toolbarMoveFocus(hwnd, FALSE);
    }

    GlobalUnlock(h);
    return TRUE;
}

/*
 * s05:3FE6-4072  toolbarExclusiveRadio  (FAR PASCAL)
 *
 * Releases the other (not grayed) buttons of a radio group.
 */
BOOL FAR PASCAL toolbarExclusiveRadio(HWND hwnd, int iType, int iButton)
{
    HGLOBAL         h;
    LPTOOLBUTTON    lptb;
    int             i;

    h = (HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS);
    if (h == NULL)
        return FALSE;
    lptb = (LPTOOLBUTTON)GlobalLock(h);
    for (i = 0; i < GetWindowWord(hwnd, GWW_TB_COUNT); i++, lptb++) {
        if (lptb->iType == iType && lptb->iButton != iButton && lptb->iState != BTNST_GRAYED)
            toolbarModifyState(hwnd, lptb->iButton, BTNST_UP);
    }
    GlobalUnlock(h);
    return TRUE;
}

/*
 * s05:4076-409B  toolbarNotify  (NEAR PASCAL)
 */
static void NEAR PASCAL toolbarNotify(HWND hwnd, int code)
{
    PostMessage(GetParent(hwnd), WM_COMMAND, GetWindowWord(hwnd, GWW_ID), MAKELONG(hwnd, code));
}

/*
 * s05:409E-41FD  toolbarPaint  (NEAR PASCAL)
 *
 * Each button is copied from the bitmap: the column is the button ID,
 * the row its state (a focus state when the tool bar has the focus and
 * this is the focus button).  The memory DC is deleted with the bitmap
 * still selected.
 */
static BOOL NEAR PASCAL toolbarPaint(HWND hwnd, HDC hdc)
{
    HDC     hdcMem;
    RECT    rc;
    DWORD   dwSize;
    int     i, iButton, iState;

    hdcMem = CreateCompatibleDC(hdc);
    if (hdcMem == NULL)
        return FALSE;
    if (GetWindowWord(hwnd, GWW_TB_BITMAP) &&
        !SelectObject(hdcMem, (HGDIOBJ)GetWindowWord(hwnd, GWW_TB_BITMAP))) {
        DeleteDC(hdcMem);
        return FALSE;
    }

    toolbarFixFocus(hwnd);

    for (i = 0; i < toolbarGetNumButtons(hwnd); i++) {
        iButton = toolbarButtonFromIndex(hwnd, i);
        iState = toolbarStateFromButton(hwnd, iButton);
        toolbarRectFromIndex(hwnd, i, &rc);

        if (GetFocus() == hwnd && GetWindowWord(hwnd, GWW_TB_FOCUS) == i && iState == BTNST_UP)
            iState = BTNST_FOCUSUP;
        if (GetFocus() == hwnd && GetWindowWord(hwnd, GWW_TB_FOCUS) == i && iState == BTNST_DOWN)
            iState = BTNST_FOCUSDOWN;
        if ((GetFocus() != hwnd || GetWindowWord(hwnd, GWW_TB_FOCUS) != i) && iState == BTNST_FOCUSUP)
            iState = BTNST_UP;
        if ((GetFocus() != hwnd || GetWindowWord(hwnd, GWW_TB_FOCUS) != i) && iState == BTNST_FOCUSDOWN)
            iState = BTNST_DOWN;

        dwSize = GetWindowLong(hwnd, GWL_TB_BUTTONSIZE);
        BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hdcMem,
               HIWORD(dwSize) * iButton, LOWORD(dwSize) * iState, SRCCOPY);
    }

    DeleteDC(hdcMem);
    return TRUE;
}

/*
 * s05:4200-42E7  toolbarMoveFocus  (FAR PASCAL)
 *
 * Moves the focus to the next (or previous) button that isn't grayed.
 * Returns TRUE if it moved.
 */
BOOL FAR PASCAL toolbarMoveFocus(HWND hwnd, BOOL fBackward)
{
    RECT    rc;
    int     iOld, iStep, iLimit, i;

    iOld = GetWindowWord(hwnd, GWW_TB_FOCUS);
    if (iOld < -1 || GetWindowWord(hwnd, GWW_TB_COUNT) < iOld)
        SetWindowWord(hwnd, GWW_TB_FOCUS, 0);

    if (fBackward) {
        iStep = -1;
        iLimit = -1;
    } else {
        iStep = 1;
        iLimit = GetWindowWord(hwnd, GWW_TB_COUNT);
    }

    for (i = GetWindowWord(hwnd, GWW_TB_FOCUS) + iStep; i != iLimit; i += iStep) {
        if (toolbarFullStateFromButton(hwnd, toolbarButtonFromIndex(hwnd, i)) != BTNST_GRAYED) {
            SetWindowWord(hwnd, GWW_TB_FOCUS, i);
            toolbarRectFromIndex(hwnd, iOld, &rc);
            InvalidateRect(hwnd, &rc, FALSE);
            toolbarRectFromIndex(hwnd, i, &rc);
            InvalidateRect(hwnd, &rc, FALSE);
            break;
        }
    }

    return GetWindowWord(hwnd, GWW_TB_FOCUS) != iOld;
}

/*
 * s05:42EA-437D  toolbarSetFocus  (FAR PASCAL)
 *
 * Gives the focus to a button, or to the first (TB_FIRST) or last
 * (TB_LAST) one that isn't grayed.  Not while a button is held.
 */
BOOL FAR PASCAL toolbarSetFocus(HWND hwnd, int iButton)
{
    RECT    rc;
    int     i;

    if (GetCapture() == hwnd || GetWindowWord(hwnd, GWW_TB_KEYDOWN))
        return FALSE;

    toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_TB_FOCUS), &rc);
    InvalidateRect(hwnd, &rc, FALSE);

    if (iButton == TB_FIRST) {
        SetWindowWord(hwnd, GWW_TB_FOCUS, (WORD)-1);
        return toolbarMoveFocus(hwnd, FALSE);
    }
    if (iButton == TB_LAST) {
        SetWindowWord(hwnd, GWW_TB_FOCUS, GetWindowWord(hwnd, GWW_TB_COUNT));
        return toolbarMoveFocus(hwnd, TRUE);
    }

    i = toolbarIndexFromButton(hwnd, iButton);
    if (i == -1)
        return FALSE;
    SetWindowWord(hwnd, GWW_TB_FOCUS, i - 1);
    return toolbarMoveFocus(hwnd, FALSE);
}

/*
 * s05:4380-44C1  LoadUIBitmap  (FAR PASCAL)
 *
 * Loads a 16-colour bitmap resource with its black, light gray, dark
 * gray, white, yellow and green replaced by the given colours.
 */
HBITMAP FAR PASCAL LoadUIBitmap(HINSTANCE hInst, LPCSTR szName, COLORREF rgbText,
                                COLORREF rgbFace, COLORREF rgbShadow, COLORREF rgbHighlight,
                                COLORREF rgbWindow, COLORREF rgbFrame)
{
    HGLOBAL             h;
    LPBITMAPINFOHEADER  lpbi;
    LPDWORD             lprgb;
    LPBYTE              lpBits;
    HBITMAP             hbm;
    HDC                 hdc;
    int                 nColors;

#define RGB2BGR(rgb)    ((DWORD)(((rgb) & 0x0000FF00L) | (((rgb) >> 16) & 0xFF) | (((rgb) & 0xFF) << 16)))

    h = LoadResource(hInst, FindResource(hInst, szName, RT_BITMAP));
    lpbi = (LPBITMAPINFOHEADER)LockResource(h);
    if (lpbi == NULL)
        return NULL;
    if (lpbi->biSize != sizeof(BITMAPINFOHEADER) || lpbi->biBitCount != 4)
        return NULL;

    lprgb = (LPDWORD)((LPBYTE)lpbi + (WORD)lpbi->biSize);
    nColors = lpbi->biClrUsed ? (int)lpbi->biClrUsed : 1 << lpbi->biBitCount;
    lpBits = (LPBYTE)(lprgb + nColors);

    lprgb[0]  = RGB2BGR(rgbText);           /* black */
    lprgb[7]  = RGB2BGR(rgbFace);           /* light gray */
    lprgb[8]  = RGB2BGR(rgbShadow);         /* dark gray */
    lprgb[15] = RGB2BGR(rgbHighlight);      /* white */
    lprgb[11] = RGB2BGR(rgbWindow);         /* yellow */
    lprgb[10] = RGB2BGR(rgbFrame);          /* green */

    hdc = GetDC(NULL);
    hbm = CreateDIBitmap(hdc, lpbi, CBM_INIT, lpBits, (LPBITMAPINFO)lpbi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);

    GlobalUnlock(h);
    FreeResource(h);
    return hbm;
}

/*
 * s05:44C4-4E52  toolbarWndProc  (window procedure of "ToolBarClass")
 *
 * The mouse: a press captures, presses the button and sets a 200 ms
 * timer that sends repeat notifications while the button is held (but
 * the timer is never set faster); moving off and on releases and presses
 * it again.  The keyboard: Tab and Shift+Tab move the focus between the
 * buttons and then on to the next dialog control, the space bar presses
 * the focus button.  The key-up notification computes the button from
 * "focus index + right-button flag", where the other paths add the flag
 * to the button.
 */
LRESULT FAR PASCAL toolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    COLORREF    crHighlight;
    HBITMAP     hbm;
    POINT       pt;
    int         i, iType, iButton, iState, iPrev, iOldFocus, fRight, fIn;

    switch (msg) {
    case WM_CREATE:
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowLong(hwnd, GWL_STYLE, ((LPCREATESTRUCT)lParam)->style & 0xFFFF00FFL);
        SetWindowWord(hwnd, GWW_TB_BUTTONS,
                      (WORD)GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 8 * sizeof(TOOLBUTTON)));
        SetWindowWord(hwnd, GWW_TB_COUNT, 0);
        SetWindowWord(hwnd, GWW_TB_TRACKING, 0);
        SetWindowWord(hwnd, GWW_TB_KEYDOWN, 0);
        SetWindowWord(hwnd, GWW_TB_FOCUS, (WORD)-1);
        SetWindowWord(hwnd, GWW_TB_RIGHT, 0);
        SetWindowWord(hwnd, GWW_ID, IDC_TOOLBAR);
        SetWindowWord(hwnd, GWW_TB_BITMAP, 0);
        break;

    case WM_DESTROY:
        if (ghwndTransport == hwnd)
            ghwndTransport = NULL;
        if (GetWindowWord(hwnd, GWW_TB_BITMAP))
            DeleteObject((HGDIOBJ)GetWindowWord(hwnd, GWW_TB_BITMAP));
        SetWindowWord(hwnd, GWW_TB_BITMAP, 0);
        if (GetWindowWord(hwnd, GWW_TB_BUTTONS))
            GlobalFree((HGLOBAL)GetWindowWord(hwnd, GWW_TB_BUTTONS));
        SetWindowWord(hwnd, GWW_TB_BUTTONS, 0);
        break;

    case WM_SETFOCUS:
        i = GetWindowWord(hwnd, GWW_TB_FOCUS);
        if (i < 0 || toolbarGetNumButtons(hwnd) <= i) {
            i = 0;
            SetWindowWord(hwnd, GWW_TB_FOCUS, i);
        }
        /* the focus button, or the next that isn't grayed */
        while (toolbarStateFromButton(hwnd, toolbarButtonFromIndex(hwnd, i)) == BTNST_GRAYED) {
            if (toolbarGetNumButtons(hwnd) <= ++i)
                i = 0;
            if (GetWindowWord(hwnd, GWW_TB_FOCUS) == i)
                return 0L;
        }
        SetWindowWord(hwnd, GWW_TB_FOCUS, i);
        toolbarRectFromIndex(hwnd, i, &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
        return 0L;

    case WM_KILLFOCUS:
        if (GetWindowWord(hwnd, GWW_TB_KEYDOWN))
            SendMessage(hwnd, WM_KEYUP, VK_SPACE, 0L);
        toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_TB_FOCUS), &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
        return 0L;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        toolbarPaint(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0L;

    case WM_SYSCOLORCHANGE:
        if (GetWindowWord(hwnd, GWW_TB_BITMAP))
            DeleteObject((HGDIOBJ)GetWindowWord(hwnd, GWW_TB_BITMAP));
        crHighlight = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
        hbm = LoadUIBitmap((HINSTANCE)GetWindowWord(hwnd, GWW_TB_HINST),
                           MAKEINTRESOURCE(GetWindowWord(hwnd, GWW_TB_BITMAPID)),
                           GetSysColor(COLOR_BTNTEXT), GetSysColor(COLOR_BTNFACE),
                           GetSysColor(COLOR_BTNSHADOW), crHighlight,
                           GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWFRAME));
        SetWindowWord(hwnd, GWW_TB_BITMAP, (WORD)hbm);
        return (LRESULT)(WORD)hbm;

    case WM_NCHITTEST:
        if (gfCropping)
            return 0L;
        break;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB;

    case WM_KEYDOWN:
        if (!IsWindowEnabled(hwnd) || GetWindowWord(hwnd, GWW_TB_TRACKING))
            break;

        if (wParam == VK_TAB && GetKeyState(VK_SHIFT) >= 0) {
            if (toolbarMoveFocus(hwnd, FALSE))
                return 0L;
            PostMessage(GetParent(hwnd), WM_NEXTDLGCTL, 0, 0L);
            toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_TB_FOCUS), &rc);
            InvalidateRect(hwnd, &rc, FALSE);
            return 0L;
        }
        if (wParam == VK_TAB && GetKeyState(VK_SHIFT) < 0) {
            if (toolbarMoveFocus(hwnd, TRUE))
                return 0L;
            PostMessage(GetParent(hwnd), WM_NEXTDLGCTL, 1, 0L);
            toolbarRectFromIndex(hwnd, GetWindowWord(hwnd, GWW_TB_FOCUS), &rc);
            InvalidateRect(hwnd, &rc, FALSE);
            return 0L;
        }
        if (wParam != VK_SPACE || GetCapture() == hwnd)
            break;

        i = GetWindowWord(hwnd, GWW_TB_FOCUS);
        iType = toolbarTypeFromIndex(hwnd, i);
        iButton = toolbarButtonFromIndex(hwnd, i);
        iState = toolbarStateFromButton(hwnd, iButton);

        if (GetWindowWord(hwnd, GWW_TB_KEYDOWN) == 0) {
            SetWindowWord(hwnd, GWW_TB_KEYDOWN, 1);
            SetWindowWord(hwnd, GWW_TB_RIGHT, 0);
            SetWindowWord(hwnd, GWW_TB_TRACKING, 1);
            if (iType == TBTYPE_BUTTON)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            if (iType >= TBTYPE_RADIO || iType == TBTYPE_CHECKBOX) {
                toolbarModifyPrevState(hwnd, iButton, iState);
                toolbarModifyState(hwnd, iButton, BTNST_FULLDOWN);
            }
            toolbarModifyActivity(hwnd, iButton, TBACT_KEYDOWN);
            toolbarNotify(hwnd, iButton + (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0));
            return 0L;
        }

        /* auto-repeat */
        toolbarNotify(hwnd, (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) +
                            toolbarButtonFromIndex(hwnd, GetWindowWord(hwnd, GWW_TB_FOCUS)) + 0x100);
        break;

    case WM_KEYUP:
        if (wParam != VK_SPACE || !GetWindowWord(hwnd, GWW_TB_KEYDOWN))
            break;

        i = GetWindowWord(hwnd, GWW_TB_FOCUS);
        SetWindowWord(hwnd, GWW_TB_KEYDOWN, 0);
        SetWindowWord(hwnd, GWW_TB_TRACKING, 0);
        toolbarRectFromIndex(hwnd, i, &rc);
        iType = toolbarTypeFromIndex(hwnd, i);
        iButton = toolbarButtonFromIndex(hwnd, i);
        toolbarStateFromButton(hwnd, iButton);

        if (iType == TBTYPE_BUTTON)
            toolbarModifyState(hwnd, iButton, BTNST_UP);
        if (iType == TBTYPE_CHECKBOX) {
            iPrev = toolbarPrevStateFromButton(hwnd, iButton);
            if (iPrev == BTNST_DOWN)
                toolbarModifyState(hwnd, iButton, BTNST_UP);
            if (iPrev == BTNST_UP)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
        }
        if (iType >= TBTYPE_RADIO) {
            toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            toolbarExclusiveRadio(hwnd, iType, iButton);
        }
        toolbarModifyActivity(hwnd, iButton, TBACT_KEYUP);
        toolbarNotify(hwnd, toolbarButtonFromIndex(hwnd,
                          (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) +
                          GetWindowWord(hwnd, GWW_TB_FOCUS)));
        break;

    case WM_TIMER:
        if (!GetWindowWord(hwnd, GWW_TB_TRACKING))
            break;
        i = GetWindowWord(hwnd, GWW_TB_FOCUS);
        toolbarButtonFromIndex(hwnd, i);
        toolbarTypeFromIndex(hwnd, i);
        toolbarNotify(hwnd, toolbarButtonFromIndex(hwnd, i) +
                            (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) + 0x100);
        break;

    case WM_MOUSEMOVE:
        if (GetCapture() != hwnd)
            return 0L;
        i = GetWindowWord(hwnd, GWW_TB_FOCUS);
        pt = MAKEPOINT(lParam);
        toolbarRectFromIndex(hwnd, i, &rc);
        fIn = PtInRect(&rc, pt);
        if (GetWindowWord(hwnd, GWW_TB_TRACKING) == fIn)
            return 0L;
        SetWindowWord(hwnd, GWW_TB_TRACKING, fIn);

        iType = toolbarTypeFromIndex(hwnd, i);
        iButton = toolbarButtonFromIndex(hwnd, i);
        toolbarStateFromButton(hwnd, iButton);
        if (fIn) {
            if (iType == TBTYPE_BUTTON)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            if (iType >= TBTYPE_RADIO || iType == TBTYPE_CHECKBOX)
                toolbarModifyState(hwnd, iButton, BTNST_FULLDOWN);
            toolbarModifyActivity(hwnd, iButton, TBACT_MOUSEON);
            toolbarNotify(hwnd, (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) + iButton);
        } else {
            if (iType == TBTYPE_BUTTON)
                toolbarModifyState(hwnd, iButton, BTNST_UP);
            if (iType >= TBTYPE_RADIO || iType == TBTYPE_CHECKBOX)
                toolbarModifyState(hwnd, iButton, toolbarPrevStateFromButton(hwnd, iButton));
            toolbarModifyActivity(hwnd, iButton, TBACT_MOUSEOFF);
            toolbarNotify(hwnd, (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) +
                                toolbarButtonFromIndex(hwnd, i));
        }
        return 0L;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        if ((GetWindowLong(hwnd, GWL_STYLE) & WS_TABSTOP) && GetFocus() != hwnd)
            SetFocus(hwnd);
        if (!IsWindowEnabled(hwnd) || GetCapture() == hwnd ||
            GetWindowWord(hwnd, GWW_TB_TRACKING))
            return 0L;

        pt = MAKEPOINT(lParam);
        i = toolbarIndexFromPoint(hwnd, pt);
        if (i < 0)
            return 0L;
        iType = toolbarTypeFromIndex(hwnd, i);
        iButton = toolbarButtonFromIndex(hwnd, i);
        iState = toolbarStateFromButton(hwnd, iButton);
        if (iType != TBTYPE_GRAYPUSH && iState == BTNST_GRAYED)
            return 0L;

        SetCapture(hwnd);
        fRight = (msg == WM_RBUTTONDOWN || (wParam & MK_SHIFT)) ? 1 : 0;
        SetWindowWord(hwnd, GWW_TB_RIGHT, fRight);
        SetWindowWord(hwnd, GWW_TB_TRACKING, 1);
        iOldFocus = GetWindowWord(hwnd, GWW_TB_FOCUS);
        SetWindowWord(hwnd, GWW_TB_FOCUS, i);

        if (iType == TBTYPE_BUTTON)
            toolbarModifyState(hwnd, iButton, BTNST_DOWN);
        if (iType == TBTYPE_CHECKBOX || iType >= TBTYPE_RADIO) {
            toolbarModifyPrevState(hwnd, iButton, iState);
            toolbarModifyState(hwnd, iButton, BTNST_FULLDOWN);
        }
        toolbarModifyActivity(hwnd, iButton, TBACT_MOUSEDOWN);

        if (msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK)
            toolbarNotify(hwnd, (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) + iButton + 0x400);
        else
            toolbarNotify(hwnd, (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) + iButton);

        toolbarRectFromIndex(hwnd, i, &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        toolbarRectFromIndex(hwnd, iOldFocus, &rc);
        InvalidateRect(hwnd, &rc, FALSE);
        UpdateWindow(hwnd);
        SetTimer(hwnd, IDT_TOOLBAR, 200, NULL);
        return 0L;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (GetCapture() != hwnd)
            return 0L;
        i = GetWindowWord(hwnd, GWW_TB_FOCUS);
        ReleaseCapture();
        KillTimer(hwnd, IDT_TOOLBAR);
        toolbarRectFromIndex(hwnd, i, &rc);
        iType = toolbarTypeFromIndex(hwnd, i);
        iButton = toolbarButtonFromIndex(hwnd, i);
        toolbarStateFromButton(hwnd, iButton);
        if (!GetWindowWord(hwnd, GWW_TB_TRACKING))
            return 0L;
        SetWindowWord(hwnd, GWW_TB_TRACKING, 0);

        if (iType == TBTYPE_BUTTON)
            toolbarModifyState(hwnd, iButton, BTNST_UP);
        if (iType == TBTYPE_CHECKBOX) {
            iPrev = toolbarPrevStateFromButton(hwnd, iButton);
            if (iPrev == BTNST_DOWN)
                toolbarModifyState(hwnd, iButton, BTNST_UP);
            if (iPrev == BTNST_UP)
                toolbarModifyState(hwnd, iButton, BTNST_DOWN);
        }
        if (iType >= TBTYPE_RADIO) {
            toolbarModifyState(hwnd, iButton, BTNST_DOWN);
            toolbarExclusiveRadio(hwnd, iType, iButton);
        }
        toolbarModifyActivity(hwnd, iButton, TBACT_MOUSEUP);
        toolbarNotify(hwnd, (GetWindowWord(hwnd, GWW_TB_RIGHT) ? 0x200 : 0) + iButton);
        return 0L;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
