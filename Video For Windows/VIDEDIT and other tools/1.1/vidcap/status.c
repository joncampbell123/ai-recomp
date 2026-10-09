/*
 * status.c - code segment 7 of VIDCAP.EXE, 286E-2B84: the status bar
 * ("StatusClass") and the sunken text field in it ("SText"), which shows
 * menu help and capture progress.  The classes are registered by
 * StatusInit in init.c.
 */

#include <windows.h>
#include "vidcap.h"

char        gszStatusClass[] = "StatusClass";   /* DS:0558 */
char        gszSTextClass[]  = "SText";         /* DS:0564 */
BOOL        gfStatusDefault  = TRUE;            /* DS:056A the text is empty */
static char gszEmpty[]       = "";              /* DS:056C */
static char gszNoTitle[]     = "";              /* DS:056D */
static char gszSText[]       = "SText";         /* DS:056E */
char        gachStatusUnused[128] = " - ";      /* DS:0574 unreferenced */

HWND        ghwndStatusText;                    /* DS:2F24 */

static void NEAR PASCAL PaintStatusText(HWND hwnd, HDC hdc);

/*
 * seg7:286E-28E9  StatusText  (FAR PASCAL)
 *
 * Shows lpsz (or a string resource for MAKEINTRESOURCE) in the status
 * bar; NULL or "" clears it.
 */
void FAR PASCAL StatusText(LPCSTR lpsz)
{
    char    ach[80];

    if (lpsz != NULL && HIWORD((DWORD)lpsz) == 0) {
        LoadString(ghInst, LOWORD((DWORD)lpsz), ach, sizeof(ach));
        lpsz = ach;
    }

    if (lpsz != NULL && *lpsz) {
        SetWindowText(ghwndStatusText, lpsz);
        gfStatusDefault = FALSE;
    } else if (!gfStatusDefault) {
        SetWindowText(ghwndStatusText, gszEmpty);
        gfStatusDefault = TRUE;
    }
}

/*
 * seg7:28EA-29A2  StatusWndProc  (exported ordinal 3)
 *
 * The bar itself; WM_CREATE makes the text field, WM_SIZE fits it in
 * with a 2 by 1 pixel margin.  Everything goes on to DefWindowProc.
 */
LRESULT FAR PASCAL StatusWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;

    switch (msg) {
    case WM_CREATE:
        ghwndStatusText = CreateWindow(gszSText, gszNoTitle, WS_CHILD | WS_CLIPSIBLINGS,
                                       0, 0, 0, 0, hwnd, NULL, ghInst, NULL);
        if (ghwndStatusText == NULL)
            return -1L;
        break;

    case WM_SIZE:
        SetWindowPos(ghwndStatusText, NULL, 2, 1, gcxClient - 4, gcyStatus - 4, SWP_NOZORDER);
        ShowWindow(ghwndStatusText, SW_SHOW);
        break;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * seg7:29A6-2A30  fnText  (exported ordinal 12)
 *
 * The text field: repaints at once when the text changes.
 */
LRESULT FAR PASCAL fnText(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;

    switch (msg) {
    case WM_SETTEXT:
        DefWindowProc(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        break;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        PaintStatusText(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        break;

    case WM_ERASEBKGND:
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0L;
}

/*
 * seg7:2A34-2B82  PaintStatusText  (NEAR PASCAL)
 *
 * Text on the button face, then a one pixel sunken border.  The parent
 * window is fetched and not used.
 */
static void NEAR PASCAL PaintStatusText(HWND hwnd, HDC hdc)
{
    char    ach[128];
    RECT    rc;
    RECT    rcText;
    HFONT   hfontOld;
    HBRUSH  hbrOld;
    int     cch;
    int     cx;
    int     cy;

    GetClientRect(hwnd, &rc);
    cch = GetWindowText(hwnd, ach, sizeof(ach));
    SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    GetParent(hwnd);
    hfontOld = SelectObject(hdc, ghfontApp);

    rcText.left   = rc.left + 1;
    rcText.right  = rc.right - 1;
    rcText.top    = rc.top + 1;
    rcText.bottom = rc.bottom - 1;
    ExtTextOut(hdc, 4, 1, ETO_OPAQUE, &rcText, ach, cch, NULL);

    cx = rc.right - rc.left;
    cy = rc.bottom - rc.top;

    hbrOld = SelectObject(hdc, ghbrBtnShadow);
    PatBlt(hdc, rc.left, rc.top, 1, cy, PATCOPY);
    PatBlt(hdc, rc.left, rc.top, cx, 1, PATCOPY);

    SelectObject(hdc, ghbrBtnHighlight);
    PatBlt(hdc, rc.right - 1, rc.top + 1, 1, cy - 1, PATCOPY);
    PatBlt(hdc, rc.left + 1, rc.bottom - 1, cx - 1, 1, PATCOPY);

    if (hfontOld)
        SelectObject(hdc, hfontOld);
    if (hbrOld)
        SelectObject(hdc, hbrOld);
}
