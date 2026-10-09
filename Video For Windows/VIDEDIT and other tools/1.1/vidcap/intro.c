/*
 * intro.c - code segment 1 of VIDCAP.EXE, 0728-116A: the start-up box and
 * the About box ("VfWIntroBox").
 *
 * The same module as CAPSCRN's intro.c, in a later version: instead of
 * CapScrn's stop key it can show two more lines (strings 107 and 108)
 * and an OK button, which it does when the window has a system menu, as
 * the About box made by AboutBox has.  The start-up box closes on a
 * click (the main window posts WM_CLOSE to it), Esc or Alt+F4.
 *
 * Window words (cbWndExtra 0x14):
 *   0 HINSTANCE   2 title string ID   4 version string ID
 *   6 copyright string ID   8 HGLOBAL of the DIB resource   0A HFONT
 *   0C text line height   0E offset of the bits in the DIB resource
 *   12 height of the two extra lines, 0 without them
 */

#include <windows.h>
#include "vidcap.h"

#define IDOK_INTRO      99

#define IDS_INTRO_LINE1 107     /* "for Microsoft Windows 3.1" */
#define IDS_INTRO_LINE2 108     /* not in the string table */

static char         gszIntroClass[] = "VfWIntroBox";    /* DS:00B2 */
static char         gszDIB[]        = "DIB";            /* DS:00BE */
static char         gszOK[]         = "OK";             /* DS:00C2 */
static char         gszButton[]     = "button";         /* DS:00C5 */
static BOOL         gfIntroRegistered = FALSE;          /* DS:00CC */

static HINSTANCE    ghInstIntro;        /* DS:0F82 */
static UINT         gidsIntroTitle;     /* DS:0F84 */
static UINT         gidsIntroVersion;   /* DS:0F86 */
static UINT         gidsIntroCopyright; /* DS:0F88 */
static UINT         gidIntroDIB;        /* DS:0F8A */

/*
 * seg1:0728-0AC1  IntroInit  (NEAR PASCAL)
 *
 * Sets up the window words, sizes and centres the box and, for the About
 * box, adds the OK button and trims the system menu.  lpsz is a scratch
 * buffer.
 */
static BOOL NEAR PASCAL IntroInit(HWND hwnd, LPSTR lpsz, int cb)
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
    HMENU               hmenu;
    DWORD               dwBase;
    BOOL                fAbout;
    int                 cyLine;
    int                 cyExtra;
    UINT                cx;
    UINT                cy;
    UINT                w;
    UINT                cxButton;
    UINT                cyButton;

    fAbout = (GetWindowLong(hwnd, GWL_STYLE) & WS_SYSMENU) ? TRUE : FALSE;

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

    if (fAbout) {
        w = LOWORD(GetTextExtent(hdc, lpsz, LoadString(ghInst, IDS_INTRO_LINE1, lpsz, cb)));
        if (w > cx)
            cx = w;
        w = LOWORD(GetTextExtent(hdc, lpsz, LoadString(ghInst, IDS_INTRO_LINE2, lpsz, cb)));
        if (w > cx)
            cx = w;
        cyExtra = (cyLine + 4) * 2;
    } else {
        cyExtra = 0;
    }
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

    if (fAbout) {
        dwBase = GetDialogBaseUnits();
        cxButton = (LOWORD(dwBase) * 40) >> 2;
        cyButton = (HIWORD(dwBase) * 14) >> 3;
        CreateWindow(gszButton, gszOK, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                     (cx - cxButton) >> 1, cy, cxButton, cyButton,
                     hwnd, (HMENU)IDOK_INTRO, ghInstIntro, NULL);
        cx += 20;
        cy += cyButton + cyLine + 30;

        hmenu = GetSystemMenu(hwnd, FALSE);
        DeleteMenu(hmenu, SC_MAXIMIZE, MF_BYCOMMAND);
        DeleteMenu(hmenu, SC_MINIMIZE, MF_BYCOMMAND);
        DeleteMenu(hmenu, SC_RESTORE, MF_BYCOMMAND);
        DeleteMenu(hmenu, SC_SIZE, MF_BYCOMMAND);
    }

    MoveWindow(hwnd, (UINT)(GetSystemMetrics(SM_CXSCREEN) - cx) >> 1,
               (UINT)(GetSystemMetrics(SM_CYSCREEN) - cy) >> 1, cx, cy, FALSE);

    GlobalUnlock(hres);
    return TRUE;
}

/*
 * Moves to the next text line: below the picture if it is wider than 72
 * pixels, else to its right.
 */
#define NEXTLINE()                              \
    if (lpbi->biWidth > 72) {                   \
        y += cyLine;                            \
        x = 10;                                 \
    } else {                                    \
        y += cyLine + 4;                        \
        x = (int)lpbi->biWidth + 20;            \
    }

/*
 * seg1:0AC4-0F0C  IntroWndProc  (exported ordinal 13)
 *
 * Quirk of the original: WM_DESTROY deletes the font unless it is
 * SYSTEM_FONT, but the stock font it falls back to is ANSI_VAR_FONT.
 */
LRESULT FAR PASCAL IntroWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT         ps;
    char                ach[128];
    HDC                 hdc;
    HGDIOBJ             hfOld;
    HFONT               hf;
    HINSTANCE           hInst;
    HWND                hwndOwner;
    LPSTR               lp;
    LPBITMAPINFOHEADER  lpbi;
    LPSTR               lpBits;
    int                 cyLine;
    int                 cyExtra;
    int                 n;
    int                 x;
    int                 y;
    long                l;

    switch (msg) {
    case WM_CREATE:
        if (!IntroInit(hwnd, ach, sizeof(ach)))
            return -1L;
        break;

    case WM_DESTROY:
        hf = (HFONT)GetWindowWord(hwnd, 0x0A);
        if (GetStockObject(SYSTEM_FONT) != hf)
            DeleteObject(hf);
        FreeResource((HGLOBAL)GetWindowWord(hwnd, 8));
        break;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        cyExtra = GetWindowWord(hwnd, 0x12);

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
            if (cyExtra) {
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
        TextOut(hdc, x, y, ach, LoadString(hInst, GetWindowWord(hwnd, 4), ach, sizeof(ach)));

        if (cyExtra) {
            n = LoadString(hInst, IDS_INTRO_LINE1, ach, sizeof(ach));
            NEXTLINE();
            TextOut(hdc, x, y, ach, n);
            n = LoadString(hInst, IDS_INTRO_LINE2, ach, sizeof(ach));
            NEXTLINE();
            TextOut(hdc, x, y, ach, n);
        }

        /* copyright */
        n = LoadString(hInst, GetWindowWord(hwnd, 6), ach, sizeof(ach));
        NEXTLINE();
        TextOut(hdc, x, y, ach, n);

        SelectObject(hdc, hfOld);
        GlobalUnlock((HGLOBAL)GetWindowWord(hwnd, 8));
        EndPaint(hwnd, &ps);
        return 0L;

    case WM_CLOSE:
        hwndOwner = GetWindow(hwnd, GW_OWNER);
        if (hwndOwner)
            EnableWindow(hwndOwner, TRUE);
        break;

    case WM_CHAR:
        if (wParam == VK_ESCAPE)
            PostMessage(hwnd, WM_COMMAND, IDOK_INTRO, 0L);
        break;

    case WM_COMMAND:
        if (wParam == IDOK_INTRO)
            PostMessage(hwnd, WM_CLOSE, 0, 0L);
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) != SC_CLOSE)
            break;
        PostMessage(hwnd, WM_CLOSE, 0, 0L);
        return 0L;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * seg1:0F10-0F38  IntroBoxDone
 */
BOOL FAR PASCAL IntroBoxDone(HWND hwnd)
{
    if (!IsWindow(hwnd))
        return FALSE;
    DestroyWindow(hwnd);
    return TRUE;
}

/*
 * seg1:0F3C-0FB9  IntroRegister  (FAR CDECL)
 *
 * Registers the class for the application's instance (ghInst), not the
 * one passed to IntroBox.
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
        wc.hInstance     = ghInst;
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
 * seg1:0FBA-1038  IntroBox
 *
 * The start-up box: modeless, no caption.
 */
HWND FAR PASCAL IntroBox(HINSTANCE hInst, UINT idsTitle, UINT idsVersion, UINT idsCopyright, UINT idDIB)
{
    HWND hwnd;

    if (!IntroRegister())
        return NULL;

    ghInstIntro        = hInst;
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

/*
 * seg1:103C-1168  AboutBox
 *
 * The same box with a caption and an OK button, run modally with its own
 * message loop until it gets WM_CLOSE.  Keys go to the button; Enter is
 * turned into a space so the button takes it.
 */
void FAR PASCAL AboutBox(HINSTANCE hInst, HWND hwndParent, UINT idsCaption, UINT idsTitle,
                         UINT idsVersion, UINT idsCopyright, UINT idDIB)
{
    char    ach[128];
    MSG     msg;
    HWND    hwnd;
    BOOL    fDone;

    fDone = FALSE;
    if (!IntroRegister())
        return;

    ghInstIntro        = hInst;
    gidsIntroTitle     = idsTitle;
    gidsIntroVersion   = idsVersion;
    gidsIntroCopyright = idsCopyright;
    gidIntroDIB        = idDIB;

    LoadString(hInst, idsCaption, ach, sizeof(ach));
    hwnd = CreateWindowEx(WS_EX_DLGMODALFRAME, gszIntroClass, ach,
                          WS_POPUP | WS_CAPTION | WS_SYSMENU,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          hwndParent, NULL, hInst, NULL);
    if (hwnd == NULL)
        return;

    EnableWindow(GetWindow(hwnd, GW_OWNER), FALSE);
    ShowWindow(hwnd, SW_SHOWNORMAL);

    do {
        if (!GetMessage(&msg, NULL, 0, 0))
            return;
        if (msg.hwnd == hwnd && msg.message == WM_CLOSE)
            fDone = TRUE;
        TranslateMessage(&msg);
        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) && msg.hwnd == hwnd)
            msg.hwnd = GetWindow(hwnd, GW_CHILD);
        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) && msg.wParam == VK_RETURN &&
            GetWindow(hwnd, GW_CHILD) == msg.hwnd)
            msg.wParam = VK_SPACE;
        DispatchMessage(&msg);
    } while (!fDone);
}
