/*
 * intro.c - code segment 3 of CAPSCRN.EXE (0x0870 bytes, PRELOAD)
 *
 * The start-up box ("CSIntroBox"): a popup with the "DIB" resource, the
 * program name and version in bold, the copyright line and, for
 * CapScrn, the key that stops a capture.  It closes after 5 seconds, on
 * a key or a mouse click.  A DIB wider than 72 pixels goes above the
 * text, a narrower one to its left.
 *
 * Window words (cbWndExtra 0x14):
 *   0 HINSTANCE   2 title string ID   4 version string ID
 *   6 copyright string ID   8 HGLOBAL of the DIB resource   0A HFONT
 *   0C text line height   0E offset of the bits in the DIB resource
 *   12 line height + 8, also used as "show the stop key" (never 0)
 */

#include <windows.h>
#include "capscrn.h"

#define IDT_INTRO       100
#define IDM_INTRODONE   99

static char         gszIntroClass[] = "CSIntroBox";     /* DS:0146 */
static char         gszDIB[]        = "DIB";            /* DS:0151 */
static char         gszIntroEsc[]   = "Esc";            /* DS:0155 */
static char         gszIntroFmtF[]  = "F%d";            /* DS:0159 */
static char         gszIntroFmtC[]  = "%c ";            /* DS:015D */
static BOOL         gfIntroRegistered = FALSE;          /* DS:0162 */

static HINSTANCE    ghInstIntro;        /* DS:07CE */
static UINT         gidsIntroTitle;     /* DS:07D0 */
static UINT         gidsIntroVersion;   /* DS:07D2 */
static UINT         gidsIntroCopyright; /* DS:07D4 */
static UINT         gidIntroDIB;        /* DS:07D6 */

/*
 * seg3:0000-0253  IntroInit
 *
 * Sets up the window words and sizes and centres the box.  lpsz is a
 * scratch buffer.
 */
BOOL FAR IntroInit(HWND hwnd, LPSTR lpsz, int cb)
{
    LOGFONT             lf;
    TEXTMETRIC          tm;
    HFONT               hfont;
    HFONT               hf;
    HRSRC               hrsrc;
    HGLOBAL             hres;
    LPSTR               lp;
    LPBITMAPINFOHEADER  lpbi;
    HDC                 hdc;
    HGDIOBJ             hfOld;
    int                 cyLine;
    int                 cyExtra;
    UINT                cx;
    UINT                cy;
    UINT                w;

    hfont = GetStockObject(ANSI_VAR_FONT);
    if (GetObject(hfont, sizeof(LOGFONT), &lf)) {
        lf.lfWeight = FW_BOLD;
        hf = CreateFontIndirect(&lf);
        if (hf)
            hfont = hf;
    }

    SetWindowWord(hwnd, 0, (WORD)ghInstIntro);
    SetWindowWord(hwnd, 2, gidsIntroTitle);
    SetWindowWord(hwnd, 4, gidsIntroVersion);
    SetWindowWord(hwnd, 6, gidsIntroCopyright);
    SetWindowWord(hwnd, 0x0A, (WORD)hfont);

    hrsrc = FindResource(ghInstIntro, MAKEINTRESOURCE(gidIntroDIB), gszDIB);
    if (hrsrc == NULL)
        return FALSE;
    hres = LoadResource(ghInstIntro, hrsrc);
    if (hres == NULL)
        return FALSE;
    SetWindowWord(hwnd, 8, (WORD)hres);
    lp = LockResource(hres);
    if (lp == NULL)
        return FALSE;

    /* the resource is a .BMP file: header, then the BITMAPINFO */
    SetWindowLong(hwnd, 0x0E, ((BITMAPFILEHEADER FAR *)lp)->bfOffBits);
    lpbi = (LPBITMAPINFOHEADER)(lp + sizeof(BITMAPFILEHEADER));

    hdc = GetDC(hwnd);
    if (hdc == NULL)
        return FALSE;
    hfOld = SelectObject(hdc, hfont);
    if (hfOld == NULL)
        return FALSE;

    GetTextMetrics(hdc, &tm);
    SetWindowWord(hwnd, 0x0C, tm.tmHeight);
    cyLine = tm.tmHeight;

    cx = LOWORD(GetTextExtent(hdc, lpsz, LoadString(ghInstIntro, gidsIntroTitle, lpsz, cb)));
    cx += LOWORD(GetTextExtent(hdc, lpsz, LoadString(ghInstIntro, gidsIntroVersion, lpsz, cb))) + 8;
    w = LOWORD(GetTextExtent(hdc, lpsz, LoadString(ghInstIntro, gidsIntroCopyright, lpsz, cb)));
    if (w > cx)
        cx = w;

    cyExtra = cyLine + 8;
    SetWindowWord(hwnd, 0x12, cyExtra);

    SelectObject(hdc, hfOld);
    ReleaseDC(hwnd, hdc);

    if (lpbi->biWidth > 72) {
        w = (UINT)lpbi->biWidth;
        if (w > cx)
            cx = w;
        cx += 20;
        cy = (cyLine + 14) * 2 + (UINT)lpbi->biHeight + cyExtra;
    } else {
        cx += (UINT)lpbi->biWidth + 30;
        cy = (cyLine + 2) * 2 + cyExtra;
        if ((UINT)lpbi->biHeight > cy)
            cy = (UINT)lpbi->biHeight;
        cy += 20;
    }

    MoveWindow(hwnd, (UINT)(GetSystemMetrics(SM_CXSCREEN) - cx) >> 1,
               (UINT)(GetSystemMetrics(SM_CYSCREEN) - cy) >> 1, cx, cy, FALSE);

    GlobalUnlock(hres);
    return TRUE;
}

/*
 * seg3:0254-076C  IntroWndProc  (exported ordinal 5)
 *
 * Quirks of the original:
 *  - The stop key F1 is shown as "p" (its virtual key code as a
 *    character): only F2..F12 are formatted as "F%d".
 *  - WM_DESTROY deletes the font unless it is SYSTEM_FONT, but the stock
 *    font it falls back to is ANSI_VAR_FONT.
 */
LRESULT FAR PASCAL IntroWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT         ps;
    char                ach[128];
    char                ach2[128];
    HDC                 hdc;
    HGDIOBJ             hfOld;
    HFONT               hf;
    HINSTANCE           hInst;
    HWND                hwndOwner;
    LPSTR               lp;
    LPBITMAPINFOHEADER  lpbi;
    LPSTR               lpBits;
    int                 cyLine;
    int                 fStopKey;
    int                 n;
    int                 x;
    int                 y;
    long                l;

    switch (msg) {
    case WM_CREATE:
        if (!IntroInit(hwnd, ach, sizeof(ach)))
            return -1L;
        SetTimer(hwnd, IDT_INTRO, 5000, NULL);
        SetCapture(hwnd);
        break;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_INTRO);
        ReleaseCapture();
        hf = (HFONT)GetWindowWord(hwnd, 0x0A);
        if (GetStockObject(SYSTEM_FONT) != hf)
            DeleteObject(hf);
        FreeResource((HGLOBAL)GetWindowWord(hwnd, 8));
        break;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        fStopKey = GetWindowWord(hwnd, 0x12);

        lp = LockResource((HGLOBAL)GetWindowWord(hwnd, 8));
        lpbi = (LPBITMAPINFOHEADER)(lp + sizeof(BITMAPFILEHEADER));
        lpBits = lp + (UINT)GetWindowLong(hwnd, 0x0E);

        StretchDIBits(hdc, 10, 10, (int)lpbi->biWidth, (int)lpbi->biHeight,
                      0, 0, (int)lpbi->biWidth, (int)lpbi->biHeight,
                      lpBits, (LPBITMAPINFO)lpbi, DIB_RGB_COLORS, SRCCOPY);

        hfOld = SelectObject(hdc, (HGDIOBJ)GetWindowWord(hwnd, 0x0A));
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkColor(hdc, RGB(255, 255, 255));
        hInst = (HINSTANCE)GetWindowWord(hwnd, 0);
        cyLine = GetWindowWord(hwnd, 0x0C);

        /* name and version on the first line */
        n = LoadString(hInst, GetWindowWord(hwnd, 2), ach, sizeof(ach));
        if (lpbi->biWidth > 72) {
            y = (int)lpbi->biHeight + 20;
            x = 10;
        } else {
            if (fStopKey) {
                l = (lpbi->biHeight - (cyLine << 2) - 12) / 2 + 10;
                y = (l < 10) ? 10 : (int)l;
            } else {
                l = (lpbi->biHeight - 4) / 2 - cyLine + 10;
                y = (l < 10) ? 10 : (int)l;
            }
            x = (int)lpbi->biWidth + 20;
        }
        TextOut(hdc, x, y, ach, n);
        x += LOWORD(GetTextExtent(hdc, ach, n)) + 8;
        n = LoadString(hInst, GetWindowWord(hwnd, 4), ach, sizeof(ach));
        TextOut(hdc, x, y, ach, n);

        /* copyright */
        n = LoadString(hInst, GetWindowWord(hwnd, 6), ach, sizeof(ach));
        if (lpbi->biWidth > 72) {
            y += cyLine;
            x = 10;
        } else {
            y += cyLine + 4;
            x = (int)lpbi->biWidth + 20;
        }
        TextOut(hdc, x, y, ach, n);

        /* the stop key, in red */
        if (fStopKey) {
            SetTextColor(hdc, RGB(255, 0, 0));
            n = LoadString(hInst, IDS_INTRO_STOPKEY, ach, sizeof(ach));
            if (lpbi->biWidth > 72) {
                y += cyLine;
                x = 10;
            } else {
                y += cyLine + 4;
                x = (int)lpbi->biWidth + 20;
            }
            TextOut(hdc, x, y, ach, n);

            n = 0;
            ach[0] = '\0';
            if (gfHotCtrl) {
                n = LoadString(hInst, IDS_INTRO_CTRL, ach2, sizeof(ach2));
                lstrcpy(ach, ach2);
            }
            if (gfHotShift) {
                n += LoadString(hInst, IDS_INTRO_SHIFT, ach2, sizeof(ach2));
                lstrcat(ach, ach2);
            }
            if (gwHotKey == VK_ESCAPE)
                lstrcpy(ach2, gszIntroEsc);
            else if ((int)gwHotKey >= VK_F2 && (int)gwHotKey <= VK_F12)
                wsprintf(ach2, gszIntroFmtF, gwHotKey - (VK_F1 - 1));
            else
                wsprintf(ach2, gszIntroFmtC, gwHotKey);
            n += lstrlen(ach2);
            lstrcat(ach, ach2);

            if (lpbi->biWidth > 72) {
                y += cyLine;
                x = 10;
            } else {
                y += cyLine + 4;
                x = (int)lpbi->biWidth + 20;
            }
            TextOut(hdc, x, y, ach, n);
        }

        SelectObject(hdc, hfOld);
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 8));
        EndPaint(hwnd, &ps);
        return 0L;

    case WM_CLOSE:
        hwndOwner = GetWindow(hwnd, GW_OWNER);
        if (hwndOwner)
            EnableWindow(hwndOwner, TRUE);
        break;

    case WM_COMMAND:
        if (wParam != IDM_INTRODONE)
            break;
        /* fall through */
    case WM_TIMER:
        PostMessage(hwnd, WM_CLOSE, 0, 0L);
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) != SC_CLOSE)
            break;
        PostMessage(hwnd, WM_CLOSE, 0, 0L);
        return 0L;

    case WM_CHAR:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        PostMessage(hwnd, WM_COMMAND, IDM_INTRODONE, 0L);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * seg3:076E-078B  IntroBoxDone  (FAR PASCAL, unreferenced)
 */
BOOL FAR PASCAL IntroBoxDone(HWND hwnd)
{
    if (!IsWindow(hwnd))
        return FALSE;
    DestroyWindow(hwnd);
    return TRUE;
}

/*
 * seg3:078E-07FB  IntroRegister
 */
BOOL FAR IntroRegister(void)
{
    WNDCLASS wc;

    if (!gfIntroRegistered) {
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = gszIntroClass;
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.hInstance     = ghInstIntro;
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = IntroWndProc;
        wc.cbWndExtra    = 0x14;
        wc.cbClsExtra    = 0;
        if (!RegisterClass(&wc))
            return FALSE;
        gfIntroRegistered = TRUE;
    }

    return TRUE;
}

/*
 * seg3:07FC-086F  IntroBox  (FAR PASCAL)
 */
HWND FAR PASCAL IntroBox(HINSTANCE hInst, UINT idsTitle, UINT idsVersion, UINT idsCopyright, UINT idDIB)
{
    HWND hwnd;

    ghInstIntro = hInst;
    if (!IntroRegister())
        return NULL;

    gidsIntroTitle     = idsTitle;
    gidsIntroVersion   = idsVersion;
    gidsIntroCopyright = idsCopyright;
    gidIntroDIB        = idDIB;

    hwnd = CreateWindow(gszIntroClass, NULL, WS_POPUP | WS_BORDER,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                        NULL, NULL, hInst, NULL);
    if (hwnd == NULL)
        return NULL;

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    return hwnd;
}
