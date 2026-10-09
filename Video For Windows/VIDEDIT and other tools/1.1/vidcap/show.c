/*
 * show.c - code segment 3 of VIDCAP.EXE, 0D44-1186: the video window
 * ("ShowWindow").  It draws the last frame (DrawDib) or, in overlay mode,
 * points the driver's overlay at itself.  In preview mode a 1 ms timer
 * grabs frames: every tick while VidCap's task is active, every 50th tick
 * otherwise.
 */

#include <windows.h>
#include "vidcap.h"

static int      gnLastClip;             /* DS:0FC6 */
static RECT     grcLastClip;            /* DS:0FC8 */
static DWORD    gdwLastOrg;             /* DS:0FD0 */
static UINT     gnPreviewTicks;         /* DS:0FD4 */

/*
 * seg3:0D44-0D71  GetShowDC  (FAR CDECL, unreferenced)
 */
HDC FAR GetShowDC(void)
{
    HDC hdc;

    hdc = GetDC(ghwndShow);
    SetWindowOrg(hdc, gnHScrollPos, gnVScrollPos);
    return hdc;
}

/*
 * seg3:0D72-0E5D  SetOverlayRect  (FAR CDECL)
 *
 * Gives the overlay channel the window's screen rectangle and the
 * scrolled source rectangle, or stops it if the window is hidden.
 */
void FAR SetOverlayRect(HWND hwnd)
{
    RECT    rc;
    HDC     hdc;
    BOOL    fVisible;

    if (ghVideoExtOut == NULL)
        return;

    hdc = GetDC(hwnd);
    fVisible = (GetClipBox(hdc, &rc) != NULLREGION);
    ReleaseDC(hwnd, hdc);
    if (!fVisible) {
        videoStreamFini(ghVideoExtOut);
        return;
    }

    GetClientRect(hwnd, &rc);
    ClientToScreen(hwnd, (LPPOINT)&rc.left);
    ClientToScreen(hwnd, (LPPOINT)&rc.right);
    videoMessage(ghVideoExtOut, DVM_DST_RECT, (DWORD)(LPVOID)&rc, VIDEO_CONFIGURE_SET);
    SetRect(&rc, gnHScrollPos, gnVScrollPos,
            rc.right - rc.left + gnHScrollPos, rc.bottom - rc.top + gnVScrollPos);
    videoMessage(ghVideoExtOut, DVM_SRC_RECT, (DWORD)(LPVOID)&rc, VIDEO_CONFIGURE_SET);
    videoStreamInit(ghVideoExtOut, 0L, 0L, 0L, 0L);
}

/*
 * seg3:0E5E-0F86  UpdateOverlay  (FAR CDECL)
 *
 * Re-aims the overlay when the window's visible region or position has
 * changed (or fForce), then repaints it (videoUpdate with a DC).
 */
void FAR UpdateOverlay(HWND hwnd, HDC hdc, BOOL fForce)
{
    RECT    rc;
    HDC     hdcT;
    DWORD   dwOrg;
    int     nClip;
    BOOL    f;

    if (hwnd == NULL || ghVideoExtOut == NULL || !gfOverlay)
        return;

    hdcT = GetDC(NULL);
    f = (GetClipBox(hdcT, &rc) == NULLREGION);
    ReleaseDC(NULL, hdcT);
    if (f) {
        gnLastClip = -1;
        return;
    }

    if (fForce)
        gnLastClip = -1;

    hdcT = GetDC(hwnd);
    nClip = GetClipBox(hdcT, &rc);
    dwOrg = GetDCOrg(hdcT);
    ReleaseDC(hwnd, hdcT);

    if (nClip == gnLastClip && dwOrg == gdwLastOrg && EqualRect(&rc, &grcLastClip))
        return;

    gnLastClip = nClip;
    gdwLastOrg = dwOrg;
    grcLastClip = rc;
    SetOverlayRect(hwnd);
    if (hdc)
        videoUpdate(ghVideoExtOut, hwnd, hdc);
    else
        InvalidateRect(hwnd, NULL, TRUE);
}

/*
 * seg3:0F88-1184  ShowWndProc  (exported ordinal 7)
 */
LRESULT FAR PASCAL ShowWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    MSG         msgT;
    RECT        rc;
    HDC         hdc;
    HWND        hwndActive;
    BOOL        f;
    UINT        n;

    switch (msg) {
    case WM_DESTROY:
        if (gidTimer)
            KillTimer(ghwndShow, gidTimer);
        gidTimer = 0;
        ghwndShow = NULL;
        break;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        if (gfOverlay) {
            UpdateOverlay(hwnd, ps.hdc, TRUE);
        } else {
            SetWindowOrg(hdc, gnHScrollPos, gnVScrollPos);
            DrawFrame(hwnd, hdc);
        }
        EndPaint(hwnd, &ps);
        break;

    case WM_WINDOWPOSCHANGED:
        if (!gfInitializing)
            LayoutWindows();
        /* fall through */
    case WM_ERASEBKGND:
        return 0L;

    case WM_TIMER:
        if (gfOverlay)
            UpdateOverlay(hwnd, NULL, FALSE);
        if (!gfPreview)
            break;

        hdc = GetDC(hwnd);
        f = (GetClipBox(hdc, &rc) != NULLREGION);
        ReleaseDC(hwnd, hdc);
        if (gfCapturing || !f)
            break;

        f = FALSE;
        hwndActive = GetActiveWindow();
        if (hwndActive)
            f = (GetWindowTask(hwndActive) == GetWindowTask(hwnd));
        if (!f && (gnPreviewTicks++ % 50) != 0)
            break;

        videoFrame(ghVideoIn, &gvhFrame);
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        PeekMessage(&msgT, hwnd, WM_TIMER, WM_TIMER, PM_REMOVE | PM_NOYIELD);
        break;

    case WM_PALETTECHANGED:
        if ((HWND)wParam == hwnd || (HWND)wParam == ghwndFrame || (HWND)wParam == ghwndApp)
            break;
        /* fall through */
    case WM_QUERYNEWPALETTE:
        hdc = GetDC(hwnd);
        n = DrawDibRealize(ghdd, hdc, FALSE);
        ReleaseDC(hwnd, hdc);
        InvalidateRect(hwnd, NULL, TRUE);
        return (LONG)(int)n;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
