/*
 * uitest.c - scripted UI test driver for the VfW 1.1 tool rebuilds
 * (Win16, Open Watcom)
 *
 * usage (from DOS):  win uitest <script> <log> [<image dir>]
 *
 * Runs a script of commands that start an application, drive it through
 * its menus and dialogs, and record what it shows: window and control
 * trees, menus, window images, INI values and output files.  Running the
 * same script against an original program and its rebuild, each in a
 * fresh copy of Windows, and comparing the logs and images shows whether
 * they behave alike.  Exits Windows at the end of the script.
 *
 * Script commands (one per line, '#' starts a comment):
 *   run <command line>        WinExec
 *   wait <ms>                 pump messages for a while
 *   at <ms>                   wait until <ms> after the start of the script
 *   win <class|*> <title|*> <timeout ms>
 *                             wait for a top-level window and make it current
 *   nowin <class|*> <title|*> <timeout ms>
 *                             wait until no such window exists
 *   child <id>                make a child (by control ID) of the current window current
 *   childclass <class>        make the first child of a class current
 *   pop                       go back to the previous current window
 *   sys <id> | cmd <id>       post WM_SYSCOMMAND / WM_COMMAND
 *   post <msg> <wParam> <lParam> | send <msg> <wParam> <lParam>
 *   click <id>                post WM_COMMAND id/BN_CLICKED from a control
 *   settext <id> <text>       WM_SETTEXT to a control (rest of the line)
 *   gettext <id>              log a control's WM_GETTEXT
 *   check <id> <0|1>          BM_SETCHECK
 *   cbsel <id> <index>        CB_SETCURSEL and CBN_SELCHANGE
 *   lbsel <id> <index>        LB_SETCURSEL and LBN_SELCHANGE
 *   key <vk>                  post WM_KEYDOWN/WM_KEYUP to the current window
 *   char <c>                  post WM_CHAR
 *   lclick <x> <y>            post WM_LBUTTONDOWN/UP at client x, y
 *   dump                      log the current window and its children
 *   menu                      log the current window's menu and system menu
 *   snap <name>               save an image of the current window as <dir>\<name>.BMP
 *                             (on a 256-colour display through the system palette)
 *   clip <name>               save the CF_DIB clipboard data as <dir>\<name>.BMP
 *   file <path>               log size and CRC-32 of a file
 *   avi <path>                log the RIFF chunk structure of an AVI file
 *   ini <file> <section> <key>         log a profile string
 *   iniset <file> <section> <key|-> <value|-> write or delete a profile
 *                             string ("-" for the key deletes the section)
 *   toplist                   log the visible top-level windows
 *   closeclass <class>        post WM_CLOSE to the first window of a class
 *   palette                   realize a new palette in a window of our own
 *   syspal                    log the system palette
 *   close                     post WM_CLOSE to the current window
 *   log <text>                copy a line into the log
 *   exit                      end the script now
 */

#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAXLINE     300

static HINSTANCE    ghInst;
static char         gszLog[128];
static char         gszDir[128];
static HWND         ghwnd;              /* current window */
static HWND         ahwndStack[16];
static int          nStack;
static int          gnDepth;
static DWORD        gcrcTable[256];
static DWORD        gdwStart;

/* ------------------------------------------------------------------ */

static void Log(LPCSTR fmt, ...)
{
    char    ach[512];
    HFILE   h;

    wvsprintf(ach, fmt, (LPSTR)(&fmt + 1));
    lstrcat(ach, "\r\n");
    h = _lopen(gszLog, OF_WRITE);
    if (h != HFILE_ERROR) {
        _llseek(h, 0, 2);
        _lwrite(h, ach, lstrlen(ach));
        _lclose(h);
    }
}

static void Pump(DWORD ms)
{
    DWORD   t0 = GetTickCount();
    MSG     msg;

    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    } while (GetTickCount() - t0 < ms);
}

static LPSTR Arg(LPSTR FAR *pp)
{
    LPSTR p = *pp, s;

    while (*p == ' ' || *p == '\t')
        p++;
    s = p;
    while (*p && *p != ' ' && *p != '\t')
        p++;
    if (*p)
        *p++ = '\0';
    *pp = p;
    return s;
}

static LPSTR Rest(LPSTR p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static long Num(LPSTR s)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return strtol(s + 2, NULL, 16);
    return atol(s);
}

static LPSTR Indent(int n)
{
    static char ach[2][64];
    static int  k;
    LPSTR       p = ach[k ^= 1];

    if (n > 60)
        n = 60;
    _fmemset(p, ' ', n);
    p[n] = '\0';
    return p;
}

static LPSTR Fcc(DWORD fcc)
{
    static char ach[4][5];
    static int  k;
    LPSTR       p = ach[k = (k + 1) & 3];

    _fmemcpy(p, &fcc, 4);
    p[4] = '\0';
    return p;
}

static LPCSTR Star(LPSTR s)
{
    return (s[0] == '*' && s[1] == '\0') ? NULL : s;
}

/* ------------------------------------------------------------------ */
/* window and menu dumps                                               */
/* ------------------------------------------------------------------ */

static void DumpOne(HWND hwnd, HWND hwndParent)
{
    char    achClass[64];
    char    achText[256];
    char    achItem[128];
    char    achIndent[40];
    RECT    rc;
    POINT   pt;
    DWORD   dwStyle;
    int     i, n, sel;

    GetClassName(hwnd, achClass, sizeof(achClass));
    /* GetWindowText on another task's control returns the window title,
     * not the control's contents; ask the control */
    achText[0] = '\0';
    SendMessage(hwnd, WM_GETTEXT, sizeof(achText), (LPARAM)(LPSTR)achText);
    GetWindowRect(hwnd, &rc);
    if (hwndParent) {
        pt.x = rc.left; pt.y = rc.top;
        ScreenToClient(hwndParent, &pt);
        rc.right += pt.x - rc.left; rc.bottom += pt.y - rc.top;
        rc.left = pt.x; rc.top = pt.y;
    }
    dwStyle = GetWindowLong(hwnd, GWL_STYLE);
    lstrcpy(achIndent, Indent(gnDepth * 2));
    Log("%s%s id=%d style=%08lX rect=%d,%d,%d,%d vis=%d en=%d text=\"%s\"", (LPSTR)achIndent,
        (LPSTR)achClass, GetDlgCtrlID(hwnd), dwStyle, rc.left, rc.top, rc.right, rc.bottom,
        IsWindowVisible(hwnd) ? 1 : 0, IsWindowEnabled(hwnd) ? 1 : 0, (LPSTR)achText);

    if (lstrcmpi(achClass, "Button") == 0) {
        Log("%s  check=%d", (LPSTR)achIndent, (int)SendMessage(hwnd, BM_GETCHECK, 0, 0L));
    } else if (lstrcmpi(achClass, "ComboBox") == 0) {
        n = (int)SendMessage(hwnd, CB_GETCOUNT, 0, 0L);
        sel = (int)SendMessage(hwnd, CB_GETCURSEL, 0, 0L);
        Log("%s  count=%d sel=%d", (LPSTR)achIndent, n, sel);
        for (i = 0; i < n && i < 64; i++) {
            achItem[0] = '\0';
            if (SendMessage(hwnd, CB_GETLBTEXTLEN, i, 0L) < (LONG)sizeof(achItem))
                SendMessage(hwnd, CB_GETLBTEXT, i, (LPARAM)(LPSTR)achItem);
            Log("%s  [%d] \"%s\"", (LPSTR)achIndent, i, (LPSTR)achItem);
        }
    } else if (lstrcmpi(achClass, "ListBox") == 0) {
        n = (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0L);
        sel = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0L);
        Log("%s  count=%d sel=%d", (LPSTR)achIndent, n, sel);
        for (i = 0; i < n && i < 64; i++) {
            achItem[0] = '\0';
            if (SendMessage(hwnd, LB_GETTEXTLEN, i, 0L) < (LONG)sizeof(achItem))
                SendMessage(hwnd, LB_GETTEXT, i, (LPARAM)(LPSTR)achItem);
            Log("%s  [%d] \"%s\"", (LPSTR)achIndent, i, (LPSTR)achItem);
        }
    } else if (lstrcmpi(achClass, "ScrollBar") == 0) {
        int nMin, nMax;
        GetScrollRange(hwnd, SB_CTL, &nMin, &nMax);
        Log("%s  range=%d..%d pos=%d", (LPSTR)achIndent, nMin, nMax, GetScrollPos(hwnd, SB_CTL));
    }
}

static void DumpTree(HWND hwnd, HWND hwndParent)
{
    HWND h;

    DumpOne(hwnd, hwndParent);
    gnDepth++;
    for (h = GetWindow(hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        DumpTree(h, hwnd);
    gnDepth--;
}

static void DumpMenu(HMENU hmenu, int depth)
{
    char    ach[128];
    int     i, n;
    UINT    id, st;
    HMENU   hsub;

    n = GetMenuItemCount(hmenu);
    for (i = 0; i < n; i++) {
        ach[0] = '\0';
        GetMenuString(hmenu, i, ach, sizeof(ach), MF_BYPOSITION);
        id = GetMenuItemID(hmenu, i);
        st = GetMenuState(hmenu, i, MF_BYPOSITION);
        Log("%smenu[%d] id=%d state=%04X \"%s\"", Indent(depth * 2), i, (int)id, st, (LPSTR)ach);
        hsub = GetSubMenu(hmenu, i);
        if (hsub)
            DumpMenu(hsub, depth + 1);
    }
}

/* ------------------------------------------------------------------ */
/* images                                                              */
/* ------------------------------------------------------------------ */

static void WriteBMP(LPCSTR name, LPBITMAPINFOHEADER lpbi, DWORD cbHeader, BYTE _huge *bits, DWORD cbBits)
{
    char                ach[160];
    BITMAPFILEHEADER    bf;
    HFILE               h;

    wsprintf(ach, "%s\\%s.BMP", (LPSTR)gszDir, name);
    h = _lcreat(ach, 0);
    if (h == HFILE_ERROR) {
        Log("  cannot create %s", (LPSTR)ach);
        return;
    }
    bf.bfType = 0x4D42;
    bf.bfSize = sizeof(bf) + cbHeader + cbBits;
    bf.bfReserved1 = bf.bfReserved2 = 0;
    bf.bfOffBits = sizeof(bf) + cbHeader;
    _lwrite(h, (LPCSTR)&bf, sizeof(bf));
    _lwrite(h, (LPCSTR)lpbi, (UINT)cbHeader);
    _hwrite(h, (LPCSTR)bits, cbBits);
    _lclose(h);
}

static DWORD Crc(DWORD crc, BYTE _huge *p, DWORD cb)
{
    crc = ~crc;
    while (cb--)
        crc = gcrcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/*
 * Logs the system palette: a CRC and the 256 entries as RRGGBB.
 */
static void SysPal(void)
{
    PALETTEENTRY    ape[256];
    HDC             hdc;
    int             i, j, n;
    char            ach[100];
    char           *p;

    hdc = GetDC(NULL);
    n = GetSystemPaletteEntries(hdc, 0, 256, ape);
    ReleaseDC(NULL, hdc);
    Log("syspal %d crc=%08lX", n, Crc(0, (BYTE _huge *)ape, sizeof(ape)));
    for (i = 0; i < 256; i += 8) {
        p = ach + wsprintf(ach, "  %3d:", i);
        for (j = i; j < i + 8; j++)
            p += wsprintf(p, " %02X%02X%02X", ape[j].peRed, ape[j].peGreen, ape[j].peBlue);
        Log("%s", (LPSTR)ach);
    }
}

static void Snap(LPCSTR name)
{
    RECT                rc;
    HDC                 hdcScreen, hdcMem;
    HBITMAP             hbm, hbmOld;
    BITMAPINFOHEADER    bi;
    DWORD               cb;
    HGLOBAL             h;
    BYTE _huge         *p;
    int                 w, ht;

    if (!IsWindow(ghwnd)) {
        Log("  snap: no window");
        return;
    }
    GetWindowRect(ghwnd, &rc);
    w = rc.right - rc.left;
    ht = rc.bottom - rc.top;
    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    hbm = CreateCompatibleBitmap(hdcScreen, w, ht);
    hbmOld = SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, ht, hdcScreen, rc.left, rc.top, SRCCOPY);
    SelectObject(hdcMem, hbmOld);

    _fmemset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = ht;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    cb = (DWORD)((w * 3 + 3) & ~3) * ht;
    /* zeroed, so the row padding is the same in every run */
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, cb);
    p = (BYTE _huge *)GlobalLock(h);
    if (GetDeviceCaps(hdcScreen, BITSPIXEL) == 8) {
        /* On a palette display GetDIBits translates through the screen
           DC's palette, and the result depends on how palettes were
           realized before.  Take the device's pixel values and look them
           up in the system palette instead. */
        PALETTEENTRY ape[256];
        WORD cbRow = (w + 1) & ~1, cbDib = (w * 3 + 3) & ~3;
        HGLOBAL hRaw = GlobalAlloc(GMEM_MOVEABLE, (DWORD)cbRow * ht);
        BYTE _huge *r = (BYTE _huge *)GlobalLock(hRaw);
        int x, y;
        GetSystemPaletteEntries(hdcScreen, 0, 256, ape);
        GetBitmapBits(hbm, (DWORD)cbRow * ht, (LPSTR)r);
        for (y = 0; y < ht; y++) {
            BYTE _huge *d = p + (DWORD)cbDib * (ht - 1 - y);
            BYTE _huge *src = r + (DWORD)cbRow * y;
            for (x = 0; x < w; x++) {
                PALETTEENTRY pe = ape[src[x]];
                d[x * 3] = pe.peBlue; d[x * 3 + 1] = pe.peGreen; d[x * 3 + 2] = pe.peRed;
            }
        }
        GlobalUnlock(hRaw);
        GlobalFree(hRaw);
    } else {
        GetDIBits(hdcScreen, hbm, 0, ht, (LPSTR)p, (LPBITMAPINFO)&bi, DIB_RGB_COLORS);
    }
    WriteBMP(name, &bi, sizeof(bi), p, cb);
    Log("snap %s %dx%d at %d,%d crc=%08lX", name, w, ht, rc.left, rc.top, Crc(0, p, cb));
    GlobalUnlock(h);
    GlobalFree(h);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

static void Clip(LPCSTR name)
{
    HGLOBAL             h;
    LPBITMAPINFOHEADER  lpbi;
    DWORD               cbHeader, cb;
    int                 nColors;

    if (!OpenClipboard(NULL)) {
        Log("clip: cannot open clipboard");
        return;
    }
    h = GetClipboardData(CF_DIB);
    if (h == NULL) {
        Log("clip %s: no CF_DIB", name);
    } else {
        lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
        nColors = lpbi->biClrUsed ? (int)lpbi->biClrUsed : (lpbi->biBitCount <= 8 ? 1 << lpbi->biBitCount : 0);
        cbHeader = lpbi->biSize + nColors * sizeof(RGBQUAD);
        /* only the image: the block is usually larger */
        cb = lpbi->biSizeImage;
        if (cb == 0 || lpbi->biCompression == BI_RGB)
            cb = (((lpbi->biWidth * lpbi->biBitCount + 31) & ~31L) >> 3) * lpbi->biHeight;
        if (cb > GlobalSize(h) - cbHeader)
            cb = GlobalSize(h) - cbHeader;
        WriteBMP(name, lpbi, cbHeader, (BYTE _huge *)lpbi + cbHeader, cb);
        Log("clip %s %ldx%ld bpp=%d crc=%08lX", name, lpbi->biWidth, lpbi->biHeight, lpbi->biBitCount,
            Crc(0, (BYTE _huge *)lpbi, cbHeader + cb));
        GlobalUnlock(h);
    }
    CloseClipboard();
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */

static void FileInfo(LPCSTR path)
{
    HFILE           h;
    static char     buf[4096];
    DWORD           crc = 0, cb = 0;
    UINT            n;

    h = _lopen(path, OF_READ);
    if (h == HFILE_ERROR) {
        Log("file %s: missing", path);
        return;
    }
    while ((n = _lread(h, buf, sizeof(buf))) > 0 && n != (UINT)-1) {
        crc = Crc(crc, (BYTE _huge *)buf, n);
        cb += n;
    }
    _lclose(h);
    Log("file %s: %lu bytes crc=%08lX", path, cb, crc);
}

/* log the chunk tree; headers ("avih", "strh", "strf") in hex */
static void AviWalk(HFILE h, DWORD end, int depth)
{
    DWORD   hdr[3];
    DWORD   pos;
    char    ach[256];
    BYTE    b[64];
    UINT    i, n;
    DWORD   cMovi = 0, cbMovi = 0;

    while ((pos = _llseek(h, 0, 1)) + 8 <= end) {
        if (_lread(h, (LPSTR)hdr, 8) != 8 || hdr[0] == 0)
            break;
        if (hdr[0] == mmioFOURCC('R', 'I', 'F', 'F') || hdr[0] == mmioFOURCC('L', 'I', 'S', 'T')) {
            _lread(h, (LPSTR)&hdr[2], 4);
            if (hdr[2] == mmioFOURCC('m', 'o', 'v', 'i') || hdr[2] == mmioFOURCC('r', 'e', 'c', ' ')) {
                /* summarise the data chunks instead of listing them */
                DWORD e = pos + 8 + hdr[1];
                DWORD c[2];
                while (_llseek(h, 0, 1) + 8 <= e && _lread(h, (LPSTR)c, 8) == 8) {
                    cMovi++;
                    cbMovi += c[1];
                    _llseek(h, (c[1] + 1) & ~1L, 1);
                }
                Log("%s%s %s %lu: %lu chunks, %lu bytes", Indent(depth * 2), Fcc(hdr[0]),
                    Fcc(hdr[2]), hdr[1], cMovi, cbMovi);
                _llseek(h, e + (hdr[1] & 1), 0);
            } else {
                Log("%s%s %s %lu", Indent(depth * 2), Fcc(hdr[0]), Fcc(hdr[2]), hdr[1]);
                AviWalk(h, pos + 8 + hdr[1], depth + 1);
                _llseek(h, pos + 8 + ((hdr[1] + 1) & ~1L), 0);
            }
        } else {
            n = (hdr[1] < sizeof(b)) ? (UINT)hdr[1] : sizeof(b);
            _lread(h, (LPSTR)b, n);
            ach[0] = '\0';
            if (hdr[0] != mmioFOURCC('J', 'U', 'N', 'K') && hdr[0] != mmioFOURCC('i', 'd', 'x', '1'))
                for (i = 0; i < n; i++)
                    wsprintf(ach + lstrlen(ach), "%02X", b[i]);
            Log("%s%s %lu %s", Indent(depth * 2), Fcc(hdr[0]), hdr[1], (LPSTR)ach);
            _llseek(h, pos + 8 + ((hdr[1] + 1) & ~1L), 0);
        }
    }
}

static void Avi(LPCSTR path)
{
    HFILE h = _lopen(path, OF_READ);

    if (h == HFILE_ERROR) {
        Log("avi %s: missing", path);
        return;
    }
    Log("avi %s", path);
    {
        DWORD cb = _llseek(h, 0, 2);
        _llseek(h, 0, 0);
        AviWalk(h, cb, 1);
    }
    _lclose(h);
}

/* ------------------------------------------------------------------ */
/* palette change                                                      */
/* ------------------------------------------------------------------ */

LRESULT FAR PASCAL __export PalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void PaletteChange(void)
{
    static int      n;
    WNDCLASS        wc;
    HWND            hwnd;
    HDC             hdc;
    HPALETTE        hpal, hpalOld;
    LOGPALETTE     *plp;
    int             i;

    if (n++ == 0) {
        _fmemset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = PalWndProc;
        wc.hInstance = ghInst;
        wc.lpszClassName = "UiTestPal";
        wc.hbrBackground = GetStockObject(BLACK_BRUSH);
        RegisterClass(&wc);
    }
    hwnd = CreateWindow("UiTestPal", "", WS_POPUP | WS_VISIBLE, 0, 0, 8, 8, NULL, NULL, ghInst, NULL);
    plp = (LOGPALETTE *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + 236 * sizeof(PALETTEENTRY));
    plp->palVersion = 0x300;
    plp->palNumEntries = 236;
    for (i = 0; i < 236; i++) {
        plp->palPalEntry[i].peRed = (BYTE)(i * 7 + n * 13);
        plp->palPalEntry[i].peGreen = (BYTE)(i * 3 + n * 5);
        plp->palPalEntry[i].peBlue = (BYTE)(255 - i);
        plp->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
    }
    hpal = CreatePalette(plp);
    hdc = GetDC(hwnd);
    hpalOld = SelectPalette(hdc, hpal, FALSE);
    Log("palette: realized %d", RealizePalette(hdc));
    Pump(500);
    SelectPalette(hdc, hpalOld, FALSE);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    DeleteObject(hpal);
    LocalFree((HLOCAL)plp);
}

/* ------------------------------------------------------------------ */

static HWND WaitWin(LPSTR cls, LPSTR title, DWORD timeout)
{
    DWORD   t0 = GetTickCount();
    HWND    h;

    for (;;) {
        h = FindWindow(Star(cls), Star(title));
        if (h || GetTickCount() - t0 >= timeout)
            return h;
        Pump(100);
    }
}

static BOOL Command(LPSTR line)
{
    LPSTR   p = line;
    LPSTR   cmd = Arg(&p);
    LPSTR   a, b, c, d;
    HWND    h;
    char    ach[256];

    if (*cmd == '\0' || *cmd == '#')
        return TRUE;

    if (lstrcmpi(cmd, "run") == 0) {
        Log("run %s = %u", Rest(p), WinExec(Rest(p), SW_SHOWNORMAL));
    } else if (lstrcmpi(cmd, "wait") == 0) {
        Pump(Num(Arg(&p)));
    } else if (lstrcmpi(cmd, "at") == 0) {
        DWORD t = (DWORD)Num(Arg(&p));
        if (GetTickCount() - gdwStart > t)
            Log("at %lu: LATE", t);
        while (GetTickCount() - gdwStart < t)
            Pump(50);
    } else if (lstrcmpi(cmd, "win") == 0) {
        a = Arg(&p); b = Arg(&p); c = Arg(&p);
        h = WaitWin(a, b, Num(c));
        if (h) {
            if (nStack < 16) ahwndStack[nStack++] = ghwnd;
            ghwnd = h;
        }
        Log("win %s %s: %s", a, b, (LPSTR)(h ? "found" : "NOT FOUND"));
    } else if (lstrcmpi(cmd, "nowin") == 0) {
        DWORD t0 = GetTickCount();
        a = Arg(&p); b = Arg(&p); c = Arg(&p);
        while ((h = FindWindow(Star(a), Star(b))) != NULL && GetTickCount() - t0 < (DWORD)Num(c))
            Pump(100);
        Log("nowin %s %s: %s", a, b, (LPSTR)(h ? "STILL THERE" : "gone"));
    } else if (lstrcmpi(cmd, "child") == 0) {
        h = GetDlgItem(ghwnd, (int)Num(Arg(&p)));
        if (h) {
            if (nStack < 16) ahwndStack[nStack++] = ghwnd;
            ghwnd = h;
        }
        Log("child: %s", (LPSTR)(h ? "found" : "NOT FOUND"));
    } else if (lstrcmpi(cmd, "childclass") == 0) {
        char achClass[64];
        a = Arg(&p);
        for (h = GetWindow(ghwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
            GetClassName(h, achClass, sizeof(achClass));
            if (lstrcmpi(achClass, a) == 0)
                break;
        }
        if (h) {
            if (nStack < 16) ahwndStack[nStack++] = ghwnd;
            ghwnd = h;
        }
        Log("childclass %s: %s", a, (LPSTR)(h ? "found" : "NOT FOUND"));
    } else if (lstrcmpi(cmd, "pop") == 0) {
        if (nStack)
            ghwnd = ahwndStack[--nStack];
    } else if (lstrcmpi(cmd, "sys") == 0) {
        PostMessage(ghwnd, WM_SYSCOMMAND, (WPARAM)Num(Arg(&p)), 0L);
    } else if (lstrcmpi(cmd, "cmd") == 0) {
        PostMessage(ghwnd, WM_COMMAND, (WPARAM)Num(Arg(&p)), 0L);
    } else if (lstrcmpi(cmd, "post") == 0 || lstrcmpi(cmd, "send") == 0) {
        a = Arg(&p); b = Arg(&p); c = Arg(&p);
        if (cmd[0] == 'p' || cmd[0] == 'P')
            PostMessage(ghwnd, (UINT)Num(a), (WPARAM)Num(b), Num(c));
        else
            Log("send = %ld", SendMessage(ghwnd, (UINT)Num(a), (WPARAM)Num(b), Num(c)));
    } else if (lstrcmpi(cmd, "click") == 0) {
        int id = (int)Num(Arg(&p));
        PostMessage(ghwnd, WM_COMMAND, id, MAKELONG(GetDlgItem(ghwnd, id), BN_CLICKED));
    } else if (lstrcmpi(cmd, "watchkey") == 0) {
        int vk = (int)Num(Arg(&p));
        DWORD t0 = GetTickCount(), ms = (DWORD)Num(Arg(&p));
        int last = 0, cur, n = 0;
        while (GetTickCount() - t0 < ms) {
            cur = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
            if (cur != last) {
                n++;
                last = cur;
            }
            Pump(10);
        }
        Log("watchkey %d: %d transitions", vk, n);
    } else if (lstrcmpi(cmd, "gettext") == 0) {
        int id = (int)Num(Arg(&p));
        ach[0] = '\0';
        Log("gettext %d: len=%ld get=%ld \"%s\"", id,
            SendDlgItemMessage(ghwnd, id, WM_GETTEXTLENGTH, 0, 0L),
            SendDlgItemMessage(ghwnd, id, WM_GETTEXT, sizeof(ach), (LPARAM)(LPSTR)ach), (LPSTR)ach);
    } else if (lstrcmpi(cmd, "settext") == 0) {
        int id = (int)Num(Arg(&p));
        /* SetDlgItemText on another task's control only sets its title */
        SendDlgItemMessage(ghwnd, id, WM_SETTEXT, 0, (LPARAM)Rest(p));
    } else if (lstrcmpi(cmd, "check") == 0) {
        int id = (int)Num(Arg(&p));
        SendDlgItemMessage(ghwnd, id, BM_SETCHECK, (WPARAM)Num(Arg(&p)), 0L);
    } else if (lstrcmpi(cmd, "cbsel") == 0 || lstrcmpi(cmd, "lbsel") == 0) {
        int id = (int)Num(Arg(&p));
        int i = (int)Num(Arg(&p));
        BOOL cb = (cmd[0] == 'c' || cmd[0] == 'C');
        SendDlgItemMessage(ghwnd, id, cb ? CB_SETCURSEL : LB_SETCURSEL, i, 0L);
        SendMessage(ghwnd, WM_COMMAND, id, MAKELONG(GetDlgItem(ghwnd, id), cb ? CBN_SELCHANGE : LBN_SELCHANGE));
    } else if (lstrcmpi(cmd, "key") == 0) {
        int vk = (int)Num(Arg(&p));
        PostMessage(ghwnd, WM_KEYDOWN, vk, 1L);
        PostMessage(ghwnd, WM_KEYUP, vk, 0xC0000001L);
    } else if (lstrcmpi(cmd, "char") == 0) {
        a = Arg(&p);
        PostMessage(ghwnd, WM_CHAR, (BYTE)a[0], 1L);
    } else if (lstrcmpi(cmd, "lclick") == 0) {
        int x = (int)Num(Arg(&p));
        int y = (int)Num(Arg(&p));
        PostMessage(ghwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELONG(x, y));
        PostMessage(ghwnd, WM_LBUTTONUP, 0, MAKELONG(x, y));
    } else if (lstrcmpi(cmd, "dump") == 0) {
        Log("dump:");
        if (IsWindow(ghwnd))
            DumpTree(ghwnd, NULL);
        else
            Log("  no window");
    } else if (lstrcmpi(cmd, "menu") == 0) {
        HMENU hm;
        Log("menu:");
        if ((hm = GetMenu(ghwnd)) != NULL)
            DumpMenu(hm, 1);
        Log("system menu:");
        if ((hm = GetSystemMenu(ghwnd, FALSE)) != NULL)
            DumpMenu(hm, 1);
    } else if (lstrcmpi(cmd, "snap") == 0) {
        Snap(Arg(&p));
    } else if (lstrcmpi(cmd, "clip") == 0) {
        Clip(Arg(&p));
    } else if (lstrcmpi(cmd, "file") == 0) {
        FileInfo(Arg(&p));
    } else if (lstrcmpi(cmd, "avi") == 0) {
        Avi(Arg(&p));
    } else if (lstrcmpi(cmd, "ini") == 0) {
        a = Arg(&p); b = Arg(&p); c = Arg(&p);
        GetPrivateProfileString(b, c, "<none>", ach, sizeof(ach), a);
        Log("ini %s [%s] %s = \"%s\"", a, b, c, (LPSTR)ach);
    } else if (lstrcmpi(cmd, "iniset") == 0) {
        a = Arg(&p); b = Arg(&p); c = Arg(&p); d = Rest(p);
        WritePrivateProfileString(b, (c[0] == '-' && c[1] == '\0') ? NULL : c,
                                  (d[0] == '-' && d[1] == '\0') ? NULL : d, a);
        WritePrivateProfileString(NULL, NULL, NULL, a);
    } else if (lstrcmpi(cmd, "toplist") == 0) {
        Log("toplist:");
        for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
            if (IsWindowVisible(h)) {
                char achClass[64];
                GetClassName(h, achClass, sizeof(achClass));
                GetWindowText(h, ach, sizeof(ach));
                Log("  %s \"%s\"", (LPSTR)achClass, (LPSTR)ach);
            }
    } else if (lstrcmpi(cmd, "closeclass") == 0) {
        a = Arg(&p);
        h = FindWindow(a, NULL);
        Log("closeclass %s: %s", a, (LPSTR)(h ? "posted" : "NOT FOUND"));
        if (h)
            PostMessage(h, WM_CLOSE, 0, 0L);
    } else if (lstrcmpi(cmd, "syspal") == 0) {
        SysPal();
    } else if (lstrcmpi(cmd, "palette") == 0) {
        PaletteChange();
    } else if (lstrcmpi(cmd, "close") == 0) {
        PostMessage(ghwnd, WM_CLOSE, 0, 0L);
    } else if (lstrcmpi(cmd, "log") == 0) {
        Log("%s", Rest(p));
    } else if (lstrcmpi(cmd, "exit") == 0) {
        return FALSE;
    } else {
        Log("unknown command %s", cmd);
    }
    return TRUE;
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    char        achScript[128];
    char        line[MAXLINE];
    HFILE       h;
    static char buf[16384];
    UINT        cb, i, n;
    LPSTR       p = lpCmd;
    DWORD       c;
    int         k, j;

    ghInst = hInst;
    for (k = 0; k < 256; k++) {
        c = k;
        for (j = 0; j < 8; j++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        gcrcTable[k] = c;
    }

    lstrcpy(achScript, Arg(&p));
    lstrcpy(gszLog, Arg(&p));
    lstrcpy(gszDir, Arg(&p));
    if (gszDir[0] == '\0')
        lstrcpy(gszDir, ".");
    _lclose(_lcreat(gszLog, 0));

    h = _lopen(achScript, OF_READ);
    if (h == HFILE_ERROR) {
        Log("cannot open script %s", (LPSTR)achScript);
    } else {
        cb = _lread(h, buf, sizeof(buf) - 1);
        _lclose(h);
        buf[cb] = '\0';
        gdwStart = GetTickCount();
        Pump(1000);
        for (i = 0; i < cb; ) {
            for (n = 0; i < cb && buf[i] != '\n' && n < MAXLINE - 1; i++)
                if (buf[i] != '\r')
                    line[n++] = buf[i];
            line[n] = '\0';
            i++;
            if (!Command(line))
                break;
            Pump(50);
        }
    }
    Log("end");
    ExitWindows(0, 0);
    return 0;
}
