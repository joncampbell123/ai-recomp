/*
 * util.c - code segment 1 of VIDCAP.EXE, 527A-554B: flushing the
 * keyboard, dialogs with help, placing a dialog beside a window, and
 * stepping a frame rate in an edit control.
 *
 * Functions are FAR CDECL unless noted.
 */

#include <windows.h>
#include <stdlib.h>
#include "vidcap.h"

/*
 * seg1:527A-52C5  FlushKeys
 *
 * Removes all keyboard messages (waiting for one first if fWait).
 * Returns the wParam of the last one.
 */
WPARAM FAR FlushKeys(BOOL fWait)
{
    MSG msg;

    msg.wParam = 0;
    if (fWait)
        GetMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST);
    while (PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE | PM_NOYIELD))
        ;
    return msg.wParam;
}

/*
 * seg1:52C6-5332  DoDialogParam
 *
 * DialogBoxParam with the dialog ID as the help context, making the
 * procedure instance itself.
 */
int FAR DoDialogParam(int id, HWND hwndParent, FARPROC lpfn, LPARAM lParam)
{
    FARPROC lpfnInst;
    int     idOld;
    int     n;

    lpfnInst = MakeProcInstance(lpfn, ghInst);
    idOld = gidHelpContext;
    gidHelpContext = id;
    n = DialogBoxParam(ghInst, MAKEINTRESOURCE(id), hwndParent, (DLGPROC)lpfnInst, lParam);
    gidHelpContext = idOld;
    FreeProcInstance(lpfnInst);
    return n;
}

/*
 * seg1:5334-5487  PositionDialog
 *
 * Moves a dialog that overlaps hwnd (with a 5 pixel margin) next to it:
 * below it, else to the right, else above, else to the left, else to the
 * bottom right corner of the screen; then into the screen.  Returns
 * FALSE if it did not overlap.
 *
 * Quirk of the original: "above" and "left" are only rejected when the
 * position would be exactly 0, so they are used even when they are off
 * the screen (and then moved to 0).
 */
BOOL FAR PositionDialog(HWND hDlg, HWND hwnd)
{
    RECT    rcDlg;
    RECT    rc;
    RECT    rcT;
    int     cx;
    int     cy;
    int     x;
    int     y;

    GetWindowRect(hDlg, &rcDlg);
    GetWindowRect(hwnd, &rc);
    InflateRect(&rc, 5, 5);
    if (!IntersectRect(&rcT, &rcDlg, &rc))
        return FALSE;

    cy = rcDlg.bottom - rcDlg.top;
    cx = rcDlg.right - rcDlg.left;

    if ((UINT)(cy + rc.bottom + 1) < (UINT)gcyScreen) {
        y = rc.bottom + 1;
        x = (rc.right - rc.left) / 2 - cx / 2 + rc.left;
    } else if ((UINT)(rc.right + cx + 1) < (UINT)gcxScreen) {
        x = rc.right + 1;
        y = (rc.bottom - rc.top) / 2 - cy / 2 + rc.top;
    } else if ((y = rc.top - cy - 1) != 0) {
        x = (rc.right - rc.left) / 2 - cx / 2 + rc.left;
    } else if ((x = rc.left - cx - 1) != 0) {
        y = (rc.bottom - rc.top) / 2 - cy / 2 + rc.top;
    } else {
        y = gcyScreen - cy;
        x = gcxScreen - cx;
    }

    if (x < 0)
        x = 0;
    else if ((UINT)(x + cx) > (UINT)gcxScreen)
        x = gcxScreen - cx;
    if (y < 0)
        y = 0;
    else if ((UINT)(y + cy) > (UINT)gcyScreen)
        y = gcyScreen - cy;

    SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

/*
 * seg1:5488-554B  ArrowEditChangeRate  (FAR PASCAL)
 *
 * Steps a frame rate shown as "%ld.000" (atol reads the integer part).
 */
LONG FAR PASCAL ArrowEditChangeRate(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax, UINT wStep)
{
    char    ach[32];
    LONG    l;
    LONG    lNew;

    GetWindowText(hwndEdit, ach, sizeof(ach));
    l = atol(ach);

    if (wParam == SB_LINEUP) {
        lNew = l + wStep;
        if (lNew > lMax)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld.000", l);
    } else if (wParam == SB_LINEDOWN) {
        lNew = l - wStep;
        if (lNew < lMin)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld.000", l);
    } else {
        return l;
    }
    SetWindowText(hwndEdit, ach);
    return l;

beep:
    MessageBeep(0);
    return l;
}
