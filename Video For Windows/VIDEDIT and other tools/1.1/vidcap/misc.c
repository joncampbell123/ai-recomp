/*
 * misc.c - code segment 1 of VIDCAP.EXE, 0010-0727: helpers for edit
 * controls, number formatting, the profile, dialogs, the palette and
 * message boxes.
 *
 * All functions are FAR PASCAL unless noted (the CDECL ones take a
 * variable argument list).
 */

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "vidcap.h"

/*
 * DS:0032, DS:0072: the 8 first and 8 last entries of the default system
 * palette, as the VGA driver and as some other drivers have them.
 */
static PALETTEENTRY gapeSys1[16] = {
    {   0,   0,   0, 0 }, { 128,   0,   0, 0 }, {   0, 128,   0, 0 }, { 128, 128,   0, 0 },
    {   0,   0, 128, 0 }, { 128,   0, 128, 0 }, {   0, 128, 128, 0 }, { 192, 192, 192, 0 },
    { 128, 128, 128, 0 }, { 255,   0,   0, 0 }, {   0, 255,   0, 0 }, { 255, 255,   0, 0 },
    {   0,   0, 255, 0 }, { 255,   0, 255, 0 }, {   0, 255, 255, 0 }, { 255, 255, 255, 0 }
};
static PALETTEENTRY gapeSys2[16] = {
    {   0,   0,   0, 0 }, { 191,   0,   0, 0 }, {   0, 191,   0, 0 }, { 191, 191,   0, 0 },
    {   0,   0, 191, 0 }, { 191,   0, 191, 0 }, {   0, 191, 191, 0 }, { 192, 192, 192, 0 },
    { 128, 128, 128, 0 }, { 255,   0,   0, 0 }, {   0, 255,   0, 0 }, { 255, 255,   0, 0 },
    {   0,   0, 255, 0 }, { 255,   0, 255, 0 }, {   0, 255, 255, 0 }, { 255, 255, 255, 0 }
};

char        gachMsg[128];               /* DS:0F02 ErrMsg output */

/*
 * seg1:0010-00A0  GetEditInt
 *
 * Reads a non-negative decimal number from an edit control (at most 8
 * digits).  An empty control is set to "0".  *pfOK is FALSE and the
 * result -1 if anything but digits was typed.  The text is selected.
 */
int FAR PASCAL GetEditInt(HWND hwnd, int *pfOK)
{
    char    ach[10];
    char   *p;
    int     n;

    n = GetWindowText(hwnd, ach, 9);
    ach[n] = '\0';
    if (n == 0) {
        *pfOK = TRUE;
        SetEditInt(hwnd, 0);
        return 0;
    }

    SendMessage(hwnd, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));

    for (p = ach; *p; p++) {
        if (!isdigit(*p)) {
            *pfOK = FALSE;
            return -1;
        }
    }

    *pfOK = TRUE;
    return atoi(ach);
}

/*
 * seg1:00A4-00EB  SetEditInt
 */
void FAR PASCAL SetEditInt(HWND hwnd, int n)
{
    char ach[10];

    wsprintf(ach, "%d", n);
    SetWindowText(hwnd, ach);
    SendMessage(hwnd, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
}

/*
 * seg1:00EE-0112  EnableDlgItem
 */
void FAR PASCAL EnableDlgItem(HWND hDlg, int id, BOOL fEnable)
{
    EnableWindow(GetDlgItem(hDlg, id), fEnable);
}

/*
 * seg1:0116-015C  SetDlgItemLong
 */
void FAR PASCAL SetDlgItemLong(HWND hDlg, int id, LONG l, BOOL fUnsigned)
{
    char ach[12];

    wsprintf(ach, fUnsigned ? "%lu" : "%ld", l);
    SetDlgItemText(hDlg, id, ach);
}

/*
 * seg1:0160-01C6  GetProfileKeys
 *
 * Returns a LocalAlloc'd list of the keys in a section of lpszFile (NULL
 * if out of memory).  The buffer grows by 128 bytes until the list fits;
 * if LocalReAlloc fails the old block is lost.
 */
char NEAR * FAR PASCAL GetProfileKeys(LPCSTR lpszFile, LPCSTR lpszSection)
{
    UINT    cb;
    HLOCAL  h;

    cb = 128;
    h = NULL;
    for (;;) {
        if (h == NULL) {
            h = LocalAlloc(LPTR, cb);
        } else {
            cb += 128;
            h = LocalReAlloc(h, cb, LMEM_MOVEABLE | LMEM_ZEROINIT);
        }
        if (h == NULL)
            return NULL;
        if (GetPrivateProfileString(lpszSection, NULL, "", (char NEAR *)h, cb, lpszFile) < (int)(cb - 2))
            return (char NEAR *)h;
    }
}

/*
 * seg1:01CA-0202  StrDupLocal
 */
char NEAR * FAR PASCAL StrDupLocal(char NEAR *psz)
{
    char NEAR *p;

    p = (char NEAR *)LocalAlloc(LPTR, lstrlen(psz) + 1);
    if (p)
        lstrcpy(p, psz);
    return p;
}

/*
 * seg1:0206-0258  FrameRateToUSec
 *
 * "15" -> 66667: the frame period in microseconds, rounded.  Rates below
 * 0.0001 give 0.
 */
DWORD FAR PASCAL FrameRateToUSec(char NEAR *psz)
{
    double d;

    d = atof(psz);
    if (d < 0.0001)
        return 0L;
    return (DWORD)floor(1000000.0 / d + 0.5);
}

/*
 * seg1:025C-02D1  USecToFrameRate
 *
 * The reverse, as "%.3f" (0.000 for a zero period).
 */
char NEAR * FAR PASCAL USecToFrameRate(char NEAR *psz, DWORD dwUSec)
{
    double d;

    if (dwUSec == 0)
        d = 0.0;
    else
        d = 1000000.0 / (double)dwUSec;
    sprintf(psz, "%.3f", d);
    return psz;
}

/*
 * seg1:02D4-03BD  ArrowEditChangeStep
 *
 * Like ArrowEditChange, with a step.  The edit control also gets the focus
 * (WM_NEXTDLGCTL) and its text is selected from 0x7FFF to 0.
 */
LONG FAR PASCAL ArrowEditChangeStep(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax, UINT wStep)
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
        wsprintf(ach, "%ld", l);
    } else if (wParam == SB_LINEDOWN) {
        lNew = l - wStep;
        if (lNew < lMin)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld", l);
    } else {
        return l;
    }

    SendMessage(GetParent(hwndEdit), WM_NEXTDLGCTL, (WPARAM)hwndEdit, 1L);
    SetWindowText(hwndEdit, ach);
    SendMessage(hwndEdit, EM_SETSEL, 0, MAKELONG(0x7FFF, 0));
    return l;

beep:
    MessageBeep(0);
    return l;
}

/*
 * seg1:03C0-04A9  ArrowEditChangeStep2
 *
 * The same with separate steps up and down.
 */
LONG FAR PASCAL ArrowEditChangeStep2(HWND hwndEdit, UINT wParam, LONG lMin, LONG lMax,
                                     UINT wStepUp, UINT wStepDown)
{
    char    ach[32];
    LONG    l;
    LONG    lNew;

    GetWindowText(hwndEdit, ach, sizeof(ach));
    l = atol(ach);

    if (wParam == SB_LINEUP) {
        lNew = l + wStepUp;
        if (lNew > lMax)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld", l);
    } else if (wParam == SB_LINEDOWN) {
        lNew = l - wStepDown;
        if (lNew < lMin)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld", l);
    } else {
        return l;
    }

    SendMessage(GetParent(hwndEdit), WM_NEXTDLGCTL, (WPARAM)hwndEdit, 1L);
    SetWindowText(hwndEdit, ach);
    SendMessage(hwndEdit, EM_SETSEL, 0, MAKELONG(0x7FFF, 0));
    return l;

beep:
    MessageBeep(0);
    return l;
}

/*
 * seg1:04AC-04E2  DoDialog
 *
 * DialogBox with the dialog ID as the help context while it is up.
 */
int FAR PASCAL DoDialog(int id, HWND hwndParent, FARPROC lpfn)
{
    int idOld;
    int n;

    idOld = gidHelpContext;
    gidHelpContext = id;
    n = DialogBox(ghInst, MAKEINTRESOURCE(id), hwndParent, (DLGPROC)lpfn);
    gidHelpContext = idOld;
    return n;
}

/*
 * seg1:04E6-0607  IsSystemPalette
 *
 * TRUE if a 256-colour palette starts and ends with the 16 static
 * colours: those of gapeSys1, gapeSys2 or the current system palette
 * (red, green and blue compared).
 */
BOOL FAR PASCAL IsSystemPalette(HPALETTE hpal)
{
    PALETTEENTRY    ape[16];
    PALETTEENTRY    apeSys[16];
    WORD            nEntries;
    HDC             hdc;
    int             i;

    if (hpal == NULL)
        return FALSE;
    GetObject(hpal, sizeof(nEntries), &nEntries);
    if (nEntries != 256)
        return FALSE;

    GetPaletteEntries(hpal, 0, 8, &ape[0]);
    GetPaletteEntries(hpal, 248, 8, &ape[8]);

    for (i = 0; i < 16; i++)
        if (gapeSys1[i].peRed != ape[i].peRed || gapeSys1[i].peGreen != ape[i].peGreen ||
            gapeSys1[i].peBlue != ape[i].peBlue)
            break;
    if (i == 16)
        return TRUE;

    for (i = 0; i < 16; i++)
        if (gapeSys2[i].peRed != ape[i].peRed || gapeSys2[i].peGreen != ape[i].peGreen ||
            gapeSys2[i].peBlue != ape[i].peBlue)
            break;
    if (i == 16)
        return TRUE;

    hdc = GetDC(NULL);
    GetSystemPaletteEntries(hdc, 0, 8, &apeSys[0]);
    GetSystemPaletteEntries(hdc, 248, 8, &apeSys[8]);
    ReleaseDC(NULL, hdc);

    for (i = 0; i < 16; i++)
        if (apeSys[i].peRed != ape[i].peRed || apeSys[i].peGreen != ape[i].peGreen ||
            apeSys[i].peBlue != ape[i].peBlue)
            break;
    if (i == 16)
        return TRUE;

    return FALSE;
}

/*
 * seg1:060A-065A  ErrMsg  (FAR CDECL)
 *
 * A formatted message box with the "VidCap" title and the exclamation
 * icon, owned by the main window.  Returns 0.
 */
int FAR CDECL ErrMsg(LPSTR lpszFormat, ...)
{
    char ach[20];

    LoadString(ghInst, IDS_APPNAME, ach, sizeof(ach));
    wvsprintf(gachMsg, lpszFormat, (LPSTR)(&lpszFormat + 1));
    MessageBox(ghwndApp, gachMsg, ach, MB_ICONEXCLAMATION);
    return 0;
}

/*
 * seg1:065C-0727  MsgBoxID  (FAR CDECL)
 *
 * A message box from string resources: idsFormat is formatted with the
 * extra arguments.  Without an instance handle it is taken from the
 * window; without either it only beeps.  Returns the MessageBox result,
 * 0 if a string or memory is missing.
 */
int FAR CDECL MsgBoxID(HWND hwnd, HINSTANCE hInst, UINT fuStyle, UINT idsTitle, UINT idsFormat, ...)
{
    char NEAR  *pszMsg;
    char NEAR  *pszFormat;
    int         n;

    if (hInst == NULL) {
        if (hwnd == NULL) {
            MessageBeep(0);
            return 0;
        }
        hInst = (HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE);
    }

    n = 0;
    pszMsg = (char NEAR *)LocalAlloc(LPTR, 256);
    pszFormat = (char NEAR *)LocalAlloc(LPTR, 128);
    if (pszMsg && pszFormat &&
        LoadString(hInst, idsFormat, pszFormat, 128)) {
        wvsprintf(pszMsg, pszFormat, (LPSTR)(&idsFormat + 1));
        if (LoadString(hInst, idsTitle, pszFormat, 128))
            n = MessageBox(hwnd, pszMsg, pszFormat, fuStyle);
    }

    if (pszMsg)
        LocalFree((HLOCAL)pszMsg);
    if (pszFormat)
        LocalFree((HLOCAL)pszFormat);
    return n;
}
