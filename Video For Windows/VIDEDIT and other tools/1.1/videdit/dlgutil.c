/*
 * dlgutil.c - dialog and string helpers: code segment 5, s05:1904-1F13
 *
 * Numbers in edit fields and dialog items, frame rates as text, the
 * stepping helpers for the "comarrow" spin controls, DialogBox with the
 * help context, a palette test and ErrorResBox (the WINCOM function,
 * linked in as source).  The module's data is DS:05A9-064B: the format
 * strings and two tables of the 16 static colours.
 */

#include "videdit.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "vedwnd.h"

/* DS:05CC, DS:060C: the first and last 8 static colours of two kinds of
 * display drivers (VGA style with 0x80 halves, and 0xBF halves) */
static PALETTEENTRY gapeStatic1[16] = {
    { 0x00, 0x00, 0x00, 0 }, { 0x80, 0x00, 0x00, 0 }, { 0x00, 0x80, 0x00, 0 }, { 0x80, 0x80, 0x00, 0 },
    { 0x00, 0x00, 0x80, 0 }, { 0x80, 0x00, 0x80, 0 }, { 0x00, 0x80, 0x80, 0 }, { 0xC0, 0xC0, 0xC0, 0 },
    { 0x80, 0x80, 0x80, 0 }, { 0xFF, 0x00, 0x00, 0 }, { 0x00, 0xFF, 0x00, 0 }, { 0xFF, 0xFF, 0x00, 0 },
    { 0x00, 0x00, 0xFF, 0 }, { 0xFF, 0x00, 0xFF, 0 }, { 0x00, 0xFF, 0xFF, 0 }, { 0xFF, 0xFF, 0xFF, 0 }
};
static PALETTEENTRY gapeStatic2[16] = {
    { 0x00, 0x00, 0x00, 0 }, { 0xBF, 0x00, 0x00, 0 }, { 0x00, 0xBF, 0x00, 0 }, { 0xBF, 0xBF, 0x00, 0 },
    { 0x00, 0x00, 0xBF, 0 }, { 0xBF, 0x00, 0xBF, 0 }, { 0x00, 0xBF, 0xBF, 0 }, { 0xC0, 0xC0, 0xC0, 0 },
    { 0x80, 0x80, 0x80, 0 }, { 0xFF, 0x00, 0x00, 0 }, { 0x00, 0xFF, 0x00, 0 }, { 0xFF, 0xFF, 0x00, 0 },
    { 0x00, 0x00, 0xFF, 0 }, { 0xFF, 0x00, 0xFF, 0 }, { 0x00, 0xFF, 0xFF, 0 }, { 0xFF, 0xFF, 0xFF, 0 }
};

/*
 * s05:1904-198F  EditGetInt  (FAR PASCAL)
 *
 * Reads a non-negative number (at most 8 digits) from an edit control and
 * selects its text.  An empty field reads as 0 (and is set to "0"); any
 * non-digit gives -1 with *pfOk FALSE.
 */
int FAR PASCAL EditGetInt(HWND hwnd, BOOL NEAR *pfOk)
{
    char    ach[10];
    char   *p;
    int     n;

    n = GetWindowText(hwnd, ach, 9);
    ach[n] = '\0';
    if (n == 0) {
        *pfOk = TRUE;
        EditSetInt(hwnd, 0);
        return 0;
    }

    SendMessage(hwnd, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
    for (p = ach; *p; p++) {
        if (!isdigit(*p)) {
            *pfOk = FALSE;
            return -1;
        }
    }
    *pfOk = TRUE;
    return atoi(ach);
}

/*
 * s05:1992-19CD  EditSetInt  (FAR PASCAL)
 */
void FAR PASCAL EditSetInt(HWND hwnd, int n)
{
    char ach[10];

    wsprintf(ach, "%d", n);
    SetWindowText(hwnd, ach);
    SendMessage(hwnd, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
}

/*
 * s05:19D0-19E6  EnableDlgItem  (FAR PASCAL)
 */
void FAR PASCAL EnableDlgItem(HWND hDlg, int id, BOOL fEnable)
{
    EnableWindow(GetDlgItem(hDlg, id), fEnable);
}

/*
 * s05:19EA-1A1F  SetDlgItemLong  (FAR PASCAL)
 */
void FAR PASCAL SetDlgItemLong(HWND hDlg, int id, DWORD dw, BOOL fUnsigned)
{
    char ach[12];

    wsprintf(ach, fUnsigned ? "%lu" : "%ld", dw);
    SetDlgItemText(hDlg, id, ach);
}

/*
 * s05:1A22-1A7B  GetProfileKeys  (FAR PASCAL)
 *
 * All the key names of a section of a private profile file, in a local
 * block that grows in steps of 128 bytes until they fit.  If growing
 * fails the old block is lost.
 */
PSTR FAR PASCAL GetProfileKeys(LPCSTR lpszFile, LPCSTR lpszSection)
{
    int     cb = 0x80;
    PSTR    psz = NULL;

    for (;;) {
        if (psz == NULL)
            psz = (PSTR)LocalAlloc(LPTR, cb);
        else {
            cb += 0x80;
            psz = (PSTR)LocalReAlloc((HLOCAL)psz, cb, LMEM_MOVEABLE | LMEM_ZEROINIT);
        }
        if (psz == NULL)
            return NULL;
        if (GetPrivateProfileString(lpszSection, NULL, "", psz, cb, lpszFile) < cb - 2)
            return psz;
    }
}

/*
 * s05:1A7E-1AAA  LocalStrDup  (FAR PASCAL)
 */
PSTR FAR PASCAL LocalStrDup(PSTR psz)
{
    PSTR p;

    p = (PSTR)LocalAlloc(LPTR, lstrlen(psz) + 1);
    if (p)
        lstrcpy(p, psz);
    return p;
}

/*
 * s05:1AAE-1AF5  FrameRateToUSec  (FAR PASCAL)
 *
 * "15.000" frames per second as microseconds per frame, rounded; rates
 * below 0.0001 give 0.
 */
DWORD FAR PASCAL FrameRateToUSec(PSTR psz)
{
    double d;

    d = atof(psz);
    if (d < 0.0001)
        return 0L;
    return (DWORD)floor(1000000.0 / d + 0.5);
}

/*
 * s05:1AF8-1B5A  FormatFrameRate  (FAR PASCAL)
 *
 * Microseconds per frame as "%.3f" frames per second.
 */
PSTR FAR PASCAL FormatFrameRate(PSTR psz, DWORD dwUSec)
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
 * s05:1B5E-1C34  ArrowEditStep  (FAR PASCAL)
 *
 * SB_LINEUP adds wStep and SB_LINEDOWN subtracts it, within lMin..lMax
 * (beeping instead of passing a limit).  The edit control gets the focus
 * and the caret goes to the end.  Returns the value.
 */
LONG FAR PASCAL ArrowEditStep(HWND hwndEdit, UINT code, LONG lMin, LONG lMax, WORD wStep)
{
    char    ach[32];
    LONG    l, lNew;

    GetWindowText(hwndEdit, ach, sizeof(ach));
    l = atol(ach);

    if (code == SB_LINEUP) {
        lNew = l + wStep;
        if (lNew > lMax)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld", l);
    } else if (code == SB_LINEDOWN) {
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
 * s05:1C38-1D0E  ArrowEditStep2  (FAR PASCAL)
 *
 * The same with different steps up and down.
 */
LONG FAR PASCAL ArrowEditStep2(HWND hwndEdit, UINT code, LONG lMin, LONG lMax,
                               WORD wStepUp, WORD wStepDown)
{
    char    ach[32];
    LONG    l, lNew;

    GetWindowText(hwndEdit, ach, sizeof(ach));
    l = atol(ach);

    if (code == SB_LINEUP) {
        lNew = l + wStepUp;
        if (lNew > lMax)
            goto beep;
        l = lNew;
        wsprintf(ach, "%ld", l);
    } else if (code == SB_LINEDOWN) {
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
 * s05:1D12-1D3A  DoDialog  (FAR PASCAL)
 *
 * DialogBox with the template ID as the help context (gidHelpContext) while the
 * dialog is up.
 */
int FAR PASCAL DoDialog(WORD id, HWND hwnd, FARPROC lpfn)
{
    WORD    idOld;
    int     r;

    idOld = gidHelpContext;
    gidHelpContext = id;
    r = DialogBox(ghInst, MAKEINTRESOURCE(id), hwnd, (DLGPROC)lpfn);
    gidHelpContext = idOld;
    return r;
}

/*
 * s05:1D3E-1E51  PaletteHasStaticColors  (FAR PASCAL)
 *
 * TRUE for a 256-colour palette whose first and last 8 entries are the
 * static colours: of a VGA-style driver, of a driver with 0xBF halves, or
 * of the current system palette.  The flags bytes are not compared.
 */
BOOL FAR PASCAL PaletteHasStaticColors(HPALETTE hpal)
{
    PALETTEENTRY ape[16];
    PALETTEENTRY apeSys[16];
    WORD    nColors;
    HDC     hdc;
    int     i;

    if (hpal == NULL)
        return FALSE;
    GetObject(hpal, sizeof(nColors), &nColors);
    if (nColors != 256)
        return FALSE;

    GetPaletteEntries(hpal, 0, 8, &ape[0]);
    GetPaletteEntries(hpal, 248, 8, &ape[8]);

    for (i = 0; i < 16; i++)
        if (gapeStatic1[i].peRed != ape[i].peRed || gapeStatic1[i].peGreen != ape[i].peGreen ||
            gapeStatic1[i].peBlue != ape[i].peBlue)
            break;
    if (i == 16)
        return TRUE;

    for (i = 0; i < 16; i++)
        if (gapeStatic2[i].peRed != ape[i].peRed || gapeStatic2[i].peGreen != ape[i].peGreen ||
            gapeStatic2[i].peBlue != ape[i].peBlue)
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
    return i == 16;
}

/*
 * s05:1E54-1F13  ErrorResBox  (FAR _cdecl)
 *
 * MessageBox with the text formatted (wvsprintf) from a string resource
 * and the caption from another.  With no instance the window's is used;
 * with neither it only beeps.  Returns the MessageBox result, 0 on
 * failure.
 */
int FAR _cdecl ErrorResBox(HWND hwnd, HINSTANCE hInst, UINT flags, UINT idAppName,
                           UINT idErrorStr, ...)
{
    PSTR    pszText;
    PSTR    pszFmt;
    int     r = 0;

    if (hInst == NULL) {
        if (hwnd == NULL) {
            MessageBeep(0);
            return 0;
        }
        hInst = (HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE);
    }

    pszText = (PSTR)LocalAlloc(LPTR, 256);
    pszFmt = (PSTR)LocalAlloc(LPTR, 128);
    if (pszText && pszFmt && LoadString(hInst, idErrorStr, pszFmt, 128)) {
        wvsprintf(pszText, pszFmt, (LPSTR)(&idErrorStr + 1));
        if (LoadString(hInst, idAppName, pszFmt, 128))
            r = MessageBox(hwnd, pszText, pszFmt, flags);
    }

    if (pszText)
        LocalFree((HLOCAL)pszText);
    if (pszFmt)
        LocalFree((HLOCAL)pszFmt);
    return r;
}
