/*
 * ctrl.c - code segment 3 of VIDCAP.EXE, 1188-18AC: the main window
 * ("VidCap" class, CtrlWndProc): the layout of the toolbar, the frame
 * window and the status bar, showing and hiding the bars, and the menu
 * help in the status bar.
 */

#include <windows.h>
#include "vidcap.h"

#define TOOLBAR_HEIGHT  28

int         gcyFrame;                   /* DS:1530 */
int         gcyExtra;                   /* DS:25F6 never set */
int         gcxFrame;                   /* DS:2F26 */
BOOL        gfAppActive;                /* DS:2F36 */
int         gxFrame;                    /* DS:3044 */
int         gyFrame;                    /* DS:3046 */
int         gcyMinApp;                  /* DS:3054 */

/*
 * seg3:1188-12CB  LayoutWindows  (FAR CDECL)
 */
void FAR LayoutWindows(void)
{
    RECT    rc;
    HDWP    hdwp;
    int     y;
    int     cy;
    int     cyStatus;

    y = -gcyBorder;
    hdwp = BeginDeferWindowPos(1);
    if (hdwp == NULL)
        return;
    GetClientRect(ghwndApp, &rc);

    if (gfToolBar) {
        hdwp = DeferWindowPos(hdwp, ghwndToolbar, NULL, -gcxBorder, y,
                              gcxBorder * 2 + gcxClient, TOOLBAR_HEIGHT, SWP_NOZORDER);
        if (hdwp == NULL)
            return;
        y += TOOLBAR_HEIGHT - gcyBorder;
        ShowWindow(ghwndToolbar, SW_SHOW);
    } else {
        ShowWindow(ghwndToolbar, SW_HIDE);
    }

    cyStatus = gfStatusBar ? gcyStatus - gcyBorder : 0;
    cy = rc.bottom - cyStatus - gcyExtra - gcyBorder - y;
    if (cy < 0)
        cy = 0;

    gyFrame = y;
    gxFrame = 0;
    gcxFrame = gcxBorder * 2 + rc.right;
    gcyFrame = gcyBorder * 2 + cy;
    hdwp = DeferWindowPos(hdwp, ghwndFrame, NULL, -1, y, gcxFrame, gcyFrame, SWP_NOZORDER);
    if (hdwp == NULL)
        return;

    if (gfStatusBar) {
        hdwp = DeferWindowPos(hdwp, ghwndStatusBar, NULL, -1, rc.bottom - gcyStatus + 1,
                              gcxClient + 2, gcyStatus, SWP_NOZORDER);
        if (hdwp == NULL)
            return;
        ShowWindow(ghwndStatusBar, SW_SHOW);
    } else {
        ShowWindow(ghwndStatusBar, SW_HIDE);
    }
    EndDeferWindowPos(hdwp);
}

/*
 * seg3:12CC-145B  ShowBars
 *
 * Shows or hides the toolbar and status bar.  The main window is resized
 * so that the frame keeps its height, except that the image going from
 * fitting to not fitting (or back) gives the frame the image's height.
 */
void FAR PASCAL ShowBars(BOOL fToolBar, BOOL fStatusBar)
{
    RECT    rcWindow;
    RECT    rcClient;
    int     cyStatus;
    int     cyNonClient;
    int     cyStatusOld;
    int     cyStatusNew;
    int     cyAvail;
    int     cyImage;
    int     cyFrame;
    int     cy;
    BOOL    fFitOld;
    BOOL    fFitNew;

    if (gfToolBar == fToolBar && gfStatusBar == fStatusBar)
        return;

    GetWindowRect(ghwndApp, &rcWindow);
    GetClientRect(ghwndApp, &rcClient);
    if (rcClient.bottom == 0)
        rcWindow.bottom += 12;

    cyStatus = gcyStatus;
    cyNonClient = rcWindow.bottom - rcClient.bottom - rcWindow.top;
    gcyMinApp = (UINT)((cyNonClient + gcyExtra + cyStatus + TOOLBAR_HEIGHT) * 3) >> 1;
    cyStatusOld = gfStatusBar ? cyStatus - 1 : 0;

    cyAvail = rcClient.bottom - (gfToolBar ? TOOLBAR_HEIGHT - 1 : 0) - gcyExtra - cyStatusOld;
    cyImage = gcyCapture;
    fFitOld = (cyAvail >= cyImage);

    if (gfToolBar != fToolBar)
        cyAvail += gfToolBar ? TOOLBAR_HEIGHT - 1 : -(TOOLBAR_HEIGHT - 1);
    if (gfStatusBar != fStatusBar)
        cyAvail += gfStatusBar ? cyStatus - 1 : 1 - cyStatus;
    fFitNew = (cyImage <= cyAvail);
    cyFrame = (fFitNew == fFitOld) ? cyAvail : cyImage;

    gfStatusBar = fStatusBar;
    gfToolBar = fToolBar;
    cyStatusNew = fStatusBar ? cyStatus - 1 : 0;
    cy = (fToolBar ? TOOLBAR_HEIGHT - 1 : 0) + cyNonClient + cyFrame + gcyExtra + cyStatusNew;

    if (cy + rcWindow.top == rcWindow.bottom) {
        InvalidateRect(ghwndApp, NULL, FALSE);
        LayoutWindows();
        return;
    }

    if ((UINT)(grcCtrl.top + cy) > (UINT)gcyScreen)
        grcCtrl.top = gcyScreen - cy;
    SetWindowPos(ghwndApp, NULL, grcCtrl.left, grcCtrl.top, grcCtrl.right - grcCtrl.left, cy,
                 SWP_NOZORDER);
    InvalidateRect(ghwndApp, NULL, FALSE);
    UpdateWindow(ghwndApp);
}

/*
 * seg3:145E-18AA  CtrlWndProc  (exported ordinal 10)
 */
LRESULT FAR PASCAL CtrlWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    COLORREF    rgb;
    HMENU       hmenu;
    LONG        l;
    BOOL        f;
    int         n;
    int         i;

    switch (msg) {
    case WM_CREATE:
        if (CreateBars(hwnd) == -1L)
            return -1L;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0L;

    case WM_MOVE:
        GetWindowRect(hwnd, &rc);
        grcCtrl = rc;
        if (!IsZoomed(hwnd) && !IsIconic(hwnd))
            grcApp = rc;
        break;

    case WM_SIZE:
        GetWindowRect(hwnd, &rc);
        grcCtrl = rc;
        if (!IsIconic(hwnd) && !IsZoomed(hwnd))
            grcApp = rc;
        GetClientRect(hwnd, &rc);
        gcxClient = rc.right;
        gcyClient = rc.bottom;
        LayoutWindows();
        break;

    case WM_ACTIVATE:
    case WM_WINDOWPOSCHANGED:
        if (IsWindow(ghwndShow))
            UpdateOverlay(ghwndShow, NULL, TRUE);
        break;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        break;

    case WM_CLOSE:
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        if (SendMessage(ghwndFrame, msg, wParam, lParam) == 0x00701008L)
            return 0L;
        break;

    case WM_SYSCOLORCHANGE:
        if (GetSysColor(COLOR_BTNTEXT) != grgbBtnText) {
            if (ghbrBtnText)
                DeleteObject(ghbrBtnText);
            grgbBtnText = GetSysColor(COLOR_BTNTEXT);
            ghbrBtnText = CreateSolidBrush(grgbBtnText);
        }
        if (GetSysColor(COLOR_BTNFACE) != grgbBtnFace) {
            if (ghbrBtnFace)
                DeleteObject(ghbrBtnFace);
            grgbBtnFace = GetSysColor(COLOR_BTNFACE);
            ghbrBtnFace = CreateSolidBrush(grgbBtnFace);
        }
        rgb = gfWin31 ? GetSysColor(COLOR_BTNHIGHLIGHT) : RGB(255, 255, 255);
        if (rgb != grgbBtnHighlight) {
            if (ghbrBtnHighlight)
                DeleteObject(ghbrBtnHighlight);
            grgbBtnHighlight = rgb;
            ghbrBtnHighlight = CreateSolidBrush(grgbBtnHighlight);
        }
        if (GetSysColor(COLOR_BTNSHADOW) != grgbBtnShadow) {
            if (ghbrBtnShadow)
                DeleteObject(ghbrBtnShadow);
            grgbBtnShadow = GetSysColor(COLOR_BTNSHADOW);
            ghbrBtnShadow = CreateSolidBrush(grgbBtnShadow);
        }
        SendMessage(ghwndToolbar, msg, wParam, lParam);
        break;

    case WM_ACTIVATEAPP:
        gfAppActive = wParam;
        return 0L;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.x = gcxStatusField;
        ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.y = gcyMinApp;
        break;

    case WM_COMMAND:
        return MenuCommand(hwnd, msg, wParam, lParam);

    case WM_INITMENUPOPUP:
        return InitMenuPopup(hwnd, (HMENU)wParam, LOWORD(lParam), HIWORD(lParam));

    case WM_MENUSELECT:
        if (LOWORD(lParam) == 0xFFFF && HIWORD(lParam) == 0) {
            StatusText(NULL);
        } else if ((LOWORD(lParam) & (MF_SYSMENU | MF_POPUP)) == (MF_SYSMENU | MF_POPUP)) {
            StatusText(MAKEINTRESOURCE(IDS_HELP_SYSMENU));
        } else if (!(LOWORD(lParam) & MF_POPUP)) {
            StatusText(MAKEINTRESOURCE(wParam));
        } else {
            f = FALSE;
            hmenu = GetMenu(hwnd);
            n = GetMenuItemCount(hmenu);
            for (i = 0; i < n; i++) {
                if (GetSubMenu(hmenu, i) == (HMENU)wParam) {
                    StatusText(MAKEINTRESOURCE(IDS_HELP_FILEMENU + i));
                    f = TRUE;
                    break;
                }
            }
            if (!f)
                StatusText(NULL);
        }
        break;

    case WM_ENTERIDLE:
        if (gfOverlay)
            UpdateOverlay(ghwndShow, NULL, FALSE);
        break;

    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
        if (!IsIconic(hwnd) && ghwndShow)
            return SendMessage(ghwndShow, msg, wParam, lParam);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
