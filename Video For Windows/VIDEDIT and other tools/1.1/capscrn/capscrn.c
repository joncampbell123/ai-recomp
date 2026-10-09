/*
 * capscrn.c - code segment 1 of CAPSCRN.EXE (0x1A0E bytes, PRELOAD)
 *
 * CapScrn captures the screen to an AVI file through AVICAP and the
 * screen capture driver (SCRNCAP.DRV).  It runs as an icon: the window is
 * created minimised, WM_QUERYOPEN refuses to restore it, and all commands
 * are items appended to its system menu.  Settings live in [CapScrn] of
 * MMTOOLS.INI.
 *
 * Functions are in the order of the binary.  Unless noted they are FAR
 * _cdecl; the callbacks are FAR PASCAL.
 */

#include <windows.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "capscrn.h"

#ifndef min
#define min(a, b)   (((a) < (b)) ? (a) : (b))
#endif

/* initialised data, DS:0010-0021 */
int         gnFPS       = 2;            /* DS:0010 */
BOOL        gfAudio     = TRUE;         /* DS:0012 */
UINT        gwHotKey    = VK_ESCAPE;    /* DS:0014 */
BOOL        gfHotCtrl   = FALSE;        /* DS:0016 */
BOOL        gfHotShift  = FALSE;        /* DS:0018 */
BOOL        gfIncFile   = TRUE;         /* DS:001A */
BOOL        gfOnTop     = TRUE;         /* DS:001C */
BOOL        gfDriverOK  = FALSE;        /* DS:001E */
BOOL        gfCapturing = FALSE;        /* DS:0020 */

static char gszDriverDesc[]  = "Screen Capture Driver";    /* DS:0022 */
static char gszDrivers[]     = "Drivers";                  /* DS:0038 */
static char gszMSVideo9[]    = "MSVideo9";                 /* DS:0040 */
static char gszScrncapDrv[]  = "Scrncap.drv";              /* DS:004A */
static char gszSystemIni[]   = "System.ini";               /* DS:0056 */
static HOOKPROC ghhkMsgFilter;                             /* DS:0062 previous hook */
static HOOKPROC glpfnMsgFilter;                            /* DS:0066 */
static char gszSection[]     = "CapScrn";                  /* DS:006A */
static char gszIniFile[]     = "mmtools.ini";              /* DS:0072 */
static char gszFmtD[]        = "%d";                       /* DS:007E */
static char gszKeyFPS[]      = "fps";                      /* DS:0082 */
static char gszKeyHotKey[]   = "HotKey";                   /* DS:0086 */
static char gszKeyCtrl[]     = "Ctrl";                     /* DS:008E */
static char gszKeyShift[]    = "Shift";                    /* DS:0094 */
static char gszKeyAudio[]    = "Audio";                    /* DS:009A */
static char gszKeyFile[]     = "File";                     /* DS:00A0 */
static char gszKeyIncFile[]  = "IncFile";                  /* DS:00A6 */
static char gszKeyOnTop[]    = "OnTop";                    /* DS:00AE */
static char gszDotHlp[]      = ".hlp";                     /* DS:00B4 */
static char gszDotQ[]        = ".?";                       /* DS:00B9 */
static char gszDummyName[]   = "Dummyname";                /* DS:00BC */
static char gszEsc[]         = "Esc";                      /* DS:00C6 */
static char gszFmtC[]        = "%c";                       /* DS:00CA */
static char gszFmtF[]        = "F%d";                      /* DS:00CD */
static char gszFmtNumbered[] = "%s%%0%ulu.%s";             /* DS:00D1 */
static char gszFmtNameExt[]  = "%s.%s";                    /* DS:00DE */
static char gszDash[]        = " - ";                      /* DS:00E4 */
static char gszEditCmd[]     = "VIDEdit -n ";              /* DS:00E8 */
static char gszAppNameInit[] = "CAPSCRN";                  /* DS:00F4 */
static char gszTitle[]       = "CapScrn: Screen to AVI";   /* DS:00FC */
static char gszIconName[]    = "CAPSCRN";                  /* DS:0113 */

/* uninitialised data */
HACCEL      ghAccel;                    /* DS:07F0 */
char        gszFNameTmp[256];           /* DS:07F2 */
char        gszAppName[20];             /* DS:08F4 */
char        gszDrive[4];                /* DS:0908 */
HMENU       ghmenuSys;                  /* DS:092C */
char        gszFileTitle[260];          /* DS:092E */
char        gszCaptureFile[260];        /* DS:0A32 */
char        gszHelpFile[260];           /* DS:0B36 */
DWORD       gdwACMVersion;              /* DS:0C3C */
BOOL        gfACM;                      /* DS:0C40 */
char        gachFPS[256];               /* DS:0C42 */
HWND        ghwndApp;                   /* DS:0D42 */
char        gszDefFile[260];            /* DS:0D44 */
int         gidHelp;                    /* DS:0E48 */
char        gszLastCapture[260];        /* DS:0E4A */
HINSTANCE   ghInst;                     /* DS:0F4E */
char        gszExt[256];                /* DS:0F50 */
char        gszDir[256];                /* DS:1050 */
CAPSTATUS   gCapStatus;                 /* DS:1150 */
char        gszFName[256];              /* DS:1188 */
DWORD       gdwUSecPerFrame;            /* DS:1288 */
HWND        ghwndCap;                   /* DS:128C */


/*
 * seg1:0000-002E  ErrorMessage
 */
void FAR ErrorMessage(HWND hwnd, UINT ids)
{
    char ach[388];

    LoadString(ghInst, ids, ach, sizeof(ach));
    MessageBox(hwnd, ach, gszAppName, MB_ICONEXCLAMATION);
}

/*
 * seg1:0030-0051  Confirm
 */
BOOL FAR Confirm(HWND hwnd, LPSTR lpsz)
{
    return (MessageBox(hwnd, lpsz, gszAppName, MB_ICONQUESTION | MB_OKCANCEL) == IDOK);
}

/*
 * seg1:0052-0097  FrameRateToUSec
 *
 * Frame rate text to microseconds per frame, rounded; 0 for a rate
 * below 0.0001.
 */
DWORD FAR FrameRateToUSec(char *psz)
{
    double d;

    d = atof(psz);
    if (d < 0.0001)
        return 0;

    return (DWORD)floor(1000000.0 / d + 0.5);
}

/*
 * seg1:0098-0114  LoadFilterString
 *
 * Loads a common dialog filter string, turning each '!' into a NUL and
 * stopping after a "!!" (once past the first two characters).
 */
BOOL FAR LoadFilterString(UINT ids, LPSTR lpsz, int cb)
{
    char    ach[256];
    char    chPrev;
    int     i;

    LoadString(ghInst, ids, ach, 255);

    for (i = 0; i < min(cb, 255); i++) {
        if (ach[i] == '!') {
            lpsz[i] = '\0';
            if (i > 1 && chPrev == '!')
                break;
        } else {
            lpsz[i] = ach[i];
        }
        chPrev = ach[i];
    }

    return TRUE;
}

/*
 * seg1:0116-014F  SetHelpContext
 *
 * Records the help context of the dialog that is coming up (0 when it
 * goes away).  While a dialog is up the window gives up "always on
 * top", and the code moves it to the bottom (HWND_BOTTOM, not
 * HWND_NOTOPMOST).
 */
void FAR SetHelpContext(HWND hwnd, int idHelp)
{
    SetWindowPos(hwnd, (gfOnTop && idHelp == 0) ? HWND_TOPMOST : HWND_BOTTOM,
                 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    gidHelp = idHelp;
}

/*
 * seg1:0150-01B4  HelpMsgFilter  (exported ordinal 3, WH_MSGFILTER hook)
 *
 * F1 in a dialog box or menu opens help on the current context.
 */
DWORD FAR PASCAL HelpMsgFilter(int nCode, WORD wParam, LPMSG lpMsg)
{
    if (nCode >= 0 && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_F1) {
        if (gidHelp)
            WinHelp(ghwndApp, gszHelpFile, HELP_CONTEXT, (DWORD)gidHelp);
        else
            WinHelp(ghwndApp, gszHelpFile, HELP_INDEX, 0L);
    }

    return DefHookProc(nCode, wParam, (LONG)lpMsg, &ghhkMsgFilter);
}

/*
 * seg1:01B6-0251  InitHelp
 *
 * The help file is the module file name with ".hlp" for its extension.
 */
BOOL FAR InitHelp(void)
{
    int     n;
    LPSTR   p;

    n = GetModuleFileName(ghInst, gszHelpFile, sizeof(gszHelpFile));
    p = gszHelpFile + n;
    while (p > gszHelpFile) {
        if (*p == '.') {
            *p = '\0';
            break;
        }
        n--;
        p = AnsiPrev(gszHelpFile, p);
    }
    lstrcat(gszHelpFile, (n + 5 < sizeof(gszHelpFile)) ? gszDotHlp : gszDotQ);

    glpfnMsgFilter = (HOOKPROC)MakeProcInstance((FARPROC)HelpMsgFilter, ghInst);
    ghhkMsgFilter = SetWindowsHook(WH_MSGFILTER, glpfnMsgFilter);
    gidHelp = 0;

    return TRUE;
}

/*
 * seg1:0252-0270  TermHelp
 */
void FAR TermHelp(void)
{
    if (ghhkMsgFilter) {
        UnhookWindowsHook(WH_MSGFILTER, glpfnMsgFilter);
        FreeProcInstance((FARPROC)glpfnMsgFilter);
    }
}

/*
 * seg1:0272-036E  ReadSettings
 *
 * The frame rate is clamped with unsigned compares, so a negative value
 * in the INI file becomes 20.
 */
BOOL FAR ReadSettings(void)
{
    UINT n;

    n = GetPrivateProfileInt(gszSection, gszKeyFPS, 2, gszIniFile);
    n = min(n, 20);
    if (n < 1)
        n = 1;
    gnFPS = n;
    gdwUSecPerFrame = (DWORD)(1000000.0 / (int)n);

    gwHotKey   = GetPrivateProfileInt(gszSection, gszKeyHotKey, VK_ESCAPE, gszIniFile);
    gfHotCtrl  = GetPrivateProfileInt(gszSection, gszKeyCtrl, 0, gszIniFile);
    gfHotShift = GetPrivateProfileInt(gszSection, gszKeyShift, 0, gszIniFile);
    gfAudio    = GetPrivateProfileInt(gszSection, gszKeyAudio, 1, gszIniFile);
    gfIncFile  = GetPrivateProfileInt(gszSection, gszKeyIncFile, 1, gszIniFile);
    gfOnTop    = GetPrivateProfileInt(gszSection, gszKeyOnTop, 1, gszIniFile);

    LoadString(ghInst, IDS_DEFFILE, gszDefFile, sizeof(gszDefFile));
    GetPrivateProfileString(gszSection, gszKeyFile, gszDefFile, gszCaptureFile,
                            sizeof(gszCaptureFile), gszIniFile);

    return TRUE;
}

/*
 * seg1:0370-04BA  WriteSettings
 */
BOOL FAR WriteSettings(void)
{
    char ach[40];

    wsprintf(ach, gszFmtD, gnFPS);
    WritePrivateProfileString(gszSection, gszKeyFPS, ach, gszIniFile);
    wsprintf(ach, gszFmtD, gwHotKey);
    WritePrivateProfileString(gszSection, gszKeyHotKey, ach, gszIniFile);
    wsprintf(ach, gszFmtD, gfHotCtrl);
    WritePrivateProfileString(gszSection, gszKeyCtrl, ach, gszIniFile);
    wsprintf(ach, gszFmtD, gfHotShift);
    WritePrivateProfileString(gszSection, gszKeyShift, ach, gszIniFile);
    wsprintf(ach, gszFmtD, gfAudio);
    WritePrivateProfileString(gszSection, gszKeyAudio, ach, gszIniFile);
    wsprintf(ach, gszFmtD, gfIncFile);
    WritePrivateProfileString(gszSection, gszKeyIncFile, ach, gszIniFile);
    wsprintf(ach, gszFmtD, gfOnTop);
    WritePrivateProfileString(gszSection, gszKeyOnTop, ach, gszIniFile);
    WritePrivateProfileString(gszSection, gszKeyFile, gszCaptureFile, gszIniFile);

    return TRUE;
}

/*
 * seg1:04BC-05EB  ConnectDriver
 *
 * Looks for "Screen Capture Driver" among the 10 capture driver slots.
 * If it isn't installed, it writes [Drivers] MSVideo9=Scrncap.drv to
 * SYSTEM.INI and tries slot 9.  In either case the MSVideo9 entry is
 * deleted afterwards, also when it was there before (whatever driver it
 * named).  Creates the hidden capture window and connects it.
 */
BOOL FAR ConnectDriver(HWND hwndParent)
{
    char    achDesc[80];
    int     i;
    BOOL    fFound = FALSE;
    BOOL    fOK;
    int     iDriver;

    for (i = 0; i < 10; i++) {
        if (capGetDriverDescription(i, achDesc, sizeof(achDesc), NULL, 0) &&
            _fstrncmp(achDesc, gszDriverDesc, 21) == 0) {
            fFound = TRUE;
            break;
        }
    }
    iDriver = i;

    if (!fFound) {
        iDriver = 9;
        WritePrivateProfileString(gszDrivers, gszMSVideo9, gszScrncapDrv, gszSystemIni);
        WritePrivateProfileString(NULL, NULL, NULL, gszSystemIni);
        if (capGetDriverDescription(9, achDesc, sizeof(achDesc), NULL, 0) &&
            _fstrncmp(achDesc, gszDriverDesc, 21) == 0)
            fFound = TRUE;
    }

    fOK = fFound;
    if (fFound) {
        ghwndCap = capCreateCaptureWindow(gszDummyName, WS_CHILD, 0, 0, 0, 0, hwndParent, 0);
        fOK = (BOOL)SendMessage(ghwndCap, WM_CAP_DRIVER_CONNECT, iDriver, 0L);
        SendMessage(ghwndCap, WM_CAP_GET_STATUS, sizeof(CAPSTATUS), (LPARAM)(LPVOID)&gCapStatus);
        if (ghwndCap && fOK)
            gfDriverOK = TRUE;
        else
            gfDriverOK = FALSE;
    }

    WritePrivateProfileString(gszDrivers, gszMSVideo9, NULL, gszSystemIni);
    WritePrivateProfileString(NULL, NULL, NULL, gszSystemIni);

    return fOK;
}

/*
 * seg1:05EC-06E7  ShowAudioFormat
 *
 * Puts the ACM description of the capture window's audio format into the
 * dialog's format text.
 */
void FAR ShowAudioFormat(HWND hDlg)
{
    ACMFORMATDETAILS    afd;
    DWORD               cb;
    LPWAVEFORMATEX      pwfx;

    cb = SendMessage(ghwndCap, WM_CAP_GET_AUDIOFORMAT, 0, 0L);
    if (cb == 0)
        return;

    pwfx = (LPWAVEFORMATEX)GlobalLock(GlobalAlloc(GHND, cb));
    if (pwfx == NULL)
        return;

    if (SendMessage(ghwndCap, WM_CAP_GET_AUDIOFORMAT, (WPARAM)cb, (LPARAM)pwfx)) {
        _fmemset(&afd, 0, sizeof(afd));
        afd.cbStruct    = sizeof(afd);
        afd.pwfx        = pwfx;
        afd.cbwfx       = (pwfx->wFormatTag == WAVE_FORMAT_PCM) ? sizeof(PCMWAVEFORMAT)
                                                                 : pwfx->cbSize + sizeof(WAVEFORMATEX);
        afd.dwFormatTag = pwfx->wFormatTag;
        acmFormatDetails(NULL, &afd, ACM_FORMATDETAILSF_FORMAT);
        SetDlgItemText(hDlg, IDC_AUDIOFORMAT, afd.szFormat);
    }

    GlobalUnlock(GlobalHandle(SELECTOROF(pwfx)));
    GlobalFree(GlobalHandle(SELECTOROF(pwfx)));
}

/*
 * seg1:06E8-0B72  PrefsDlgProc  (FAR PASCAL, not exported)
 *
 * Quirks of the original:
 *  - The stop key list is "Esc", 'A'..'Z', F2..F12 (F1 is help), but
 *    reading it back counts only 25 letters, so 'Z' comes back as F1.
 *    F1 is not in the list, so the next time the selection is an
 *    uninitialised value (the same happens for any key not listed).
 *  - On OK the frame rate is first read as an integer (clamped to 1..20)
 *    and then, if the text contains a '.', as a decimal number.  The loop
 *    that looks for the '.' clears every character before it, so only
 *    the fraction is converted: "0.5" works, "7.5" gives 0.5 fps.
 */
BOOL FAR PASCAL PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char                ach[64];
    HWND                hwnd;
    int                 iSel;           /* [bp-6] */
    UINT                ch;
    int                 i;
    BOOL                fFound;
    DWORD               dw;
    DWORD               cbFormat;       /* [bp-0A] */
    LPWAVEFORMATEX      pwfx;
    ACMFORMATCHOOSE     afc;
    int                 idHelpSave;

    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemInt(hDlg, IDC_FRAMERATE, gnFPS, FALSE);
        CheckDlgButton(hDlg, IDC_AUDIO, gfAudio);
        EnableWindow(GetDlgItem(hDlg, IDC_AUDIOGROUP), gCapStatus.fAudioHardware);
        EnableWindow(GetDlgItem(hDlg, IDC_AUDIO), gCapStatus.fAudioHardware);
        EnableWindow(GetDlgItem(hDlg, IDC_AUDIOFORMATLABEL), gCapStatus.fAudioHardware);
        EnableWindow(GetDlgItem(hDlg, IDC_AUDIOCHANGE), gfACM && gCapStatus.fAudioHardware);

        if (gfACM) {
            ShowAudioFormat(hDlg);
        } else {
            LoadString(ghInst, IDS_DEFAUDIOFORMAT, ach, sizeof(ach));
            SetDlgItemText(hDlg, IDC_AUDIOFORMAT, ach);
            EnableWindow(GetDlgItem(hDlg, IDC_AUDIOFORMAT), FALSE);
        }

        CheckDlgButton(hDlg, IDC_HOTCTRL, gfHotCtrl);
        CheckDlgButton(hDlg, IDC_HOTSHIFT, gfHotShift);

        hwnd = GetDlgItem(hDlg, IDC_HOTKEY);
        SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)gszEsc);
        if (gwHotKey == VK_ESCAPE)
            iSel = 0;
        for (ch = 'A'; ch <= 'Z'; ch++) {
            if (gwHotKey == ch)
                iSel = (int)SendMessage(hwnd, CB_GETCOUNT, 0, 0L);
            wsprintf(ach, gszFmtC, ch);
            SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)ach);
        }
        for (ch = VK_F2; ch <= VK_F12; ch++) {
            if (gwHotKey == ch)
                iSel = (int)SendMessage(hwnd, CB_GETCOUNT, 0, 0L);
            wsprintf(ach, gszFmtF, ch - (VK_F1 - 1));
            SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)ach);
        }
        SendMessage(hwnd, CB_SETCURSEL, iSel, 0L);

        CheckDlgButton(hDlg, IDC_INCFILE, gfIncFile);
        CheckDlgButton(hDlg, IDC_ONTOP, gfOnTop);
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            gnFPS = GetDlgItemInt(hDlg, IDC_FRAMERATE, NULL, FALSE);
            if (gnFPS > 20)
                gnFPS = 20;
            if (gnFPS < 1)
                gnFPS = 1;
            gdwUSecPerFrame = (DWORD)(1000000.0 / gnFPS);

            GetDlgItemText(hDlg, IDC_FRAMERATE, gachFPS, sizeof(gachFPS));
            fFound = FALSE;
            for (i = 0; i < sizeof(gachFPS); i++) {
                if (gachFPS[i] == '.') {
                    fFound = TRUE;
                    break;
                }
                gachFPS[i] = '\0';
            }
            if (fFound) {
                dw = FrameRateToUSec(gachFPS);
                if (dw)
                    gdwUSecPerFrame = dw;
            }

            gfAudio    = IsDlgButtonChecked(hDlg, IDC_AUDIO);
            gfHotCtrl  = IsDlgButtonChecked(hDlg, IDC_HOTCTRL);
            gfHotShift = IsDlgButtonChecked(hDlg, IDC_HOTSHIFT);

            iSel = (int)SendMessage(GetDlgItem(hDlg, IDC_HOTKEY), CB_GETCURSEL, 0, 0L);
            if (iSel == 0)
                gwHotKey = VK_ESCAPE;
            else if (iSel - 1 < 25)
                gwHotKey = iSel - 1 + 'A';
            else
                gwHotKey = iSel - 1 + (VK_F1 - 25);

            gfIncFile = IsDlgButtonChecked(hDlg, IDC_INCFILE);
            gfOnTop   = IsDlgButtonChecked(hDlg, IDC_ONTOP);
            SetWindowPos(GetParent(hDlg), gfOnTop ? HWND_TOPMOST : HWND_BOTTOM,
                         0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
            /* fall through */

        case IDCANCEL:
            EndDialog(hDlg, wParam);
            break;

        case IDC_AUDIOCHANGE:
            acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &cbFormat);
            if (SendMessage(ghwndCap, WM_CAP_GET_AUDIOFORMAT, 0, 0L) >= cbFormat)
                cbFormat = SendMessage(ghwndCap, WM_CAP_GET_AUDIOFORMAT, 0, 0L);

            pwfx = (LPWAVEFORMATEX)GlobalLock(GlobalAlloc(GHND, cbFormat));
            SendMessage(ghwndCap, WM_CAP_GET_AUDIOFORMAT, (WPARAM)cbFormat, (LPARAM)pwfx);

            _fmemset(&afc, 0, sizeof(afc));
            afc.cbStruct  = sizeof(afc);
            afc.fdwStyle  = ACMFORMATCHOOSE_STYLEF_INITTOWFXSTRUCT;
            afc.fdwEnum   = ACM_FORMATENUMF_HARDWARE | ACM_FORMATENUMF_INPUT;
            afc.hwndOwner = hDlg;
            afc.pwfx      = pwfx;
            afc.cbwfx     = cbFormat;

            idHelpSave = gidHelp;
            SetHelpContext(GetParent(hDlg), HELP_AUDIOFORMAT);
            if (acmFormatChoose(&afc) == MMSYSERR_NOERROR) {
                SendMessage(ghwndCap, WM_CAP_SET_AUDIOFORMAT, (WPARAM)cbFormat, (LPARAM)pwfx);
                ShowAudioFormat(hDlg);
            }
            SetHelpContext(GetParent(hDlg), idHelpSave);
            SetActiveWindow(hDlg);

            GlobalUnlock(GlobalHandle(SELECTOROF(pwfx)));
            GlobalFree(GlobalHandle(SELECTOROF(pwfx)));
            break;
        }
        break;

    case WM_VSCROLL:
        ArrowEditChange(GetDlgItem(hDlg, IDC_FRAMERATE), wParam, 1L, 20L);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}

/*
 * seg1:0B74-0CDD  ParseFileNumber  (NEAR PASCAL)
 *
 * Splits a capture file name such as "cap0000" into a wsprintf format for
 * the next names ("cap%04lu." plus the extension, which is empty for a
 * name from _splitpath) and returns the current number.  *pdwMax gets
 * the largest number the digits can hold.  The extension is whatever
 * follows the last '.' that comes after the last '\', ':' or '!'.
 */
static DWORD NEAR PASCAL ParseFileNumber(LPSTR lpszName, LPSTR lpszFormat, LPDWORD pdwMax)
{
    char    ach[260];
    LPSTR   p;
    LPSTR   pExt;
    LPSTR   pEnd;
    DWORD   dwNum;
    DWORD   dwMul;
    UINT    cDigits;

    p = ach;
    while (*lpszName)
        *p++ = *lpszName++;
    *p = '\0';
    pEnd = p;
    pExt = p;

    /* look back for an extension */
    for (;;) {
        if (p == ach || *p == '\\' || *p == ':' || *p == '!') {
            p = pEnd;
            pExt = pEnd;
            break;
        }
        if (*--p == '.') {
            pExt = p + 1;
            break;
        }
    }

    /* the digits in front of it */
    dwNum   = 0;
    cDigits = 0;
    dwMul   = 1;
    for (p--; p >= ach && *p >= '0' && *p <= '9'; p--) {
        dwNum += (DWORD)(*p - '0') * dwMul;
        dwMul *= 10;
        cDigits++;
    }
    *pdwMax = dwMul - 1;
    p[1] = '\0';

    if (cDigits)
        wsprintf(lpszFormat, gszFmtNumbered, (LPSTR)ach, cDigits, pExt);
    else
        wsprintf(lpszFormat, gszFmtNameExt, (LPSTR)ach, pExt);

    return dwNum;
}

/*
 * seg1:0CDE-0D59  SetCaptureFile
 *
 * Remembers the capture file name and shows it in the title.
 */
BOOL FAR SetCaptureFile(HWND hwnd, LPSTR lpszFile)
{
    char ach[260];

    lstrcpy(gszCaptureFile, lpszFile);
    _splitpath(gszCaptureFile, gszDrive, gszDir, gszFName, gszExt);
    _makepath(gszFileTitle, NULL, NULL, gszFName, gszExt);

    lstrcpy(ach, gszAppName);
    lstrcat(ach, gszDash);
    lstrcat(ach, gszFileTitle);
    SetWindowText(hwnd, ach);

    return TRUE;
}

/*
 * seg1:0D5A-0E30  IncrementFileName
 *
 * Moves on to the next numbered capture file.  Callers pass the capture
 * file name as a second argument, which is not used.
 */
BOOL FAR IncrementFileName(HWND hwnd)
{
    char    achFormat[260];
    DWORD   dwMax;
    DWORD   dwNum;
    char   *p;

    dwNum = ParseFileNumber(gszFName, achFormat, &dwMax);

    if (dwNum + 1 <= dwMax) {
        dwNum++;
        wsprintf(gszFNameTmp, achFormat, dwNum);
        for (p = gszFNameTmp; p < gszFNameTmp + strlen(gszFNameTmp); p++)
            if (*p == '.')
                *p = '\0';
        _makepath(gszFileTitle, gszDrive, gszDir, gszFNameTmp, gszExt);
        return SetCaptureFile(hwnd, gszFileTitle);
    }

    if (gfIncFile && dwNum != 0 && dwNum == dwMax)
        ErrorMessage(hwnd, IDS_ERR_CANTINCREMENT);

    return FALSE;
}

/*
 * seg1:0E32-0F29  SetCaptureFileDlg
 */
BOOL FAR SetCaptureFileDlg(HWND hwnd)
{
    OPENFILENAME    ofn;
    char            achFilter[128];
    char            achTitle[128];
    char            achFile[144];
    char            achDefExt[16];
    BOOL            fOK = FALSE;

    _fmemset(&ofn, 0, sizeof(ofn));
    lstrcpy(achFile, gszCaptureFile);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.hInstance   = ghInst;
    LoadFilterString(IDS_FILTER, achFilter, sizeof(achFilter));
    ofn.lpstrFilter       = achFilter;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter    = 0;
    ofn.nFilterIndex      = 0;
    ofn.lpstrFile         = achFile;
    ofn.nMaxFile          = sizeof(achFile);
    ofn.lpstrFileTitle    = NULL;
    ofn.nMaxFileTitle     = 0;
    ofn.lpstrInitialDir   = NULL;
    ofn.Flags = OFN_NOREADONLYRETURN | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    LoadString(ghInst, IDS_DEFEXT, achDefExt, sizeof(achDefExt));
    ofn.lpstrDefExt = achDefExt;
    ofn.lCustData   = 0;
    LoadString(ghInst, IDS_SETFILE_TITLE, achTitle, sizeof(achTitle));
    ofn.lpstrTitle  = achTitle;

    if (GetOpenFileName(&ofn)) {
        SetCaptureFile(hwnd, achFile);
        fOK = TRUE;
    }

    return fOK;
}

/*
 * seg1:0F2A-0F9C  ConfirmOverwrite  (FAR PASCAL)
 *
 * Asks before overwriting an existing capture file, but only when file
 * name incrementing is on.
 */
BOOL FAR PASCAL ConfirmOverwrite(HWND hwnd, LPSTR lpszFile)
{
    OFSTRUCT    of;
    char        achFormat[260];
    char        achMsg[260];

    if (!gfIncFile)
        return TRUE;

    if (OpenFile(lpszFile, &of, OF_EXIST) == HFILE_ERROR)
        return TRUE;

    LoadString(ghInst, IDS_OVERWRITE, achFormat, sizeof(achFormat));
    wsprintf(achMsg, achFormat, lpszFile);

    return Confirm(hwnd, achMsg);
}

/*
 * seg1:0F9E-0FFE  EditCapture
 *
 * Starts VidEdit on the last capture file.
 */
BOOL FAR EditCapture(void)
{
    char    ach[258];
    HCURSOR hcur;
    BOOL    fOK = TRUE;

    lstrcpy(ach, gszEditCmd);
    lstrcat(ach, gszLastCapture);

    hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (WinExec(ach, SW_SHOWNORMAL) < 32) {
        MessageBeep(0);
        fOK = FALSE;
    }
    SetCursor(hcur);

    return fOK;
}

/* seg1:1982 and seg1:1A00, below */
int  FAR InitApplication(void);
void FAR TermApplication(void);

/*
 * seg1:1000-1141  WinMain
 */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow)
{
    MSG msg;
    int i;

    _fmemcpy(gszAppName, gszAppNameInit, 8);
    ghInst = hInstance;
    ghAccel = LoadAccelerators(hInstance, gszAppName);

    if (hPrevInstance) {
        ErrorMessage(NULL, IDS_ERR_RUNNING);
        return 0;
    }

    i = InitApplication();
    if (i == -1) {
        ErrorMessage(NULL, IDS_ERR_REGCLASS);
        return i;
    }

    gdwACMVersion = acmGetVersion();
    gfACM = (HIBYTE(HIWORD(gdwACMVersion)) >= 2);

    ReadSettings();
    IntroBox(ghInst, IDS_INTRO_NAME, IDS_INTRO_VERSION, IDS_INTRO_COPYRIGHT, IDB_INTRO);

    ghwndApp = CreateWindow(gszAppName, gszTitle,
                            WS_MINIMIZE | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                            CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                            NULL, NULL, ghInst, NULL);
    if (ghwndApp == NULL) {
        ErrorMessage(NULL, IDS_ERR_CREATEWINDOW);
        return 2;
    }

    ShowWindow(ghwndApp, SW_SHOWMINIMIZED);

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(ghwndApp, ghAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    TermApplication();

    return msg.wParam;
}

/*
 * seg1:1142-117C  YieldCallback  (FAR PASCAL _loadds, capture yield callback)
 */
LRESULT FAR PASCAL _loadds YieldCallback(HWND hwnd)
{
    MSG msg;

    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 1L;
}

/*
 * seg1:117E-11AA  StatusCallback  (FAR PASCAL _loadds)
 *
 * Shows the icon again when the capture is over and frames are being
 * written.
 */
LRESULT FAR PASCAL _loadds StatusCallback(HWND hwnd, int nID, LPSTR lpsz)
{
    if (nID == IDS_CAP_STAT_CAP_FINI) {
        ShowWindow(ghwndApp, SW_SHOWNA);
        UpdateWindow(ghwndApp);
    }

    return 1L;
}

/*
 * seg1:11AC-11E2  ErrorCallback  (FAR PASCAL _loadds)
 */
LRESULT FAR PASCAL _loadds ErrorCallback(HWND hwnd, int nErrID, LPSTR lpsz)
{
    if (lpsz && *lpsz)
        MessageBox(hwnd, lpsz, gszAppName, MB_ICONEXCLAMATION);

    return 1L;
}

/*
 * seg1:11E4-1980  WndProc  (exported ordinal 1, by non-resident name)
 *
 * Quirks of the original:
 *  - WM_CLOSE sends WM_CAP_DRIVER_DISCONNECT to the main window instead of
 *    the capture window; the driver is disconnected when the capture
 *    window is destroyed with its parent.
 *  - The proc instances for the capture callbacks are never freed.
 *  - WM_SIZE returns 1, WM_MOVE and WM_QUERYOPEN return 0.
 */
LRESULT FAR PASCAL WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char            ach[128];
    char            achMsg[256];
    PAINTSTRUCT     ps;
    CAPTUREPARMS    cp;
    FARPROC         lpfn;
    int             ids;

    switch (msg) {
    case WM_CREATE:
        if (!ConnectDriver(hwnd)) {
            ErrorMessage(hwnd, IDS_ERR_NODRIVER);
            PostMessage(hwnd, WM_CLOSE, 0, 0L);
        }
        SetCaptureFile(hwnd, gszCaptureFile);
        lstrcpy(gszLastCapture, gszCaptureFile);
        gfAudio = (gfAudio && gCapStatus.fAudioHardware) ? TRUE : FALSE;

        ghmenuSys = GetSystemMenu(hwnd, FALSE);
        AppendMenu(ghmenuSys, MF_SEPARATOR, 0, NULL);
        LoadString(ghInst, IDS_MENU_SETFILE, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_SETFILE, ach);
        LoadString(ghInst, IDS_MENU_SETWINDOW, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_SETWINDOW, ach);
        LoadString(ghInst, IDS_MENU_PREFERENCES, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_PREFERENCES, ach);
        AppendMenu(ghmenuSys, MF_SEPARATOR, 0, NULL);
        LoadString(ghInst, IDS_MENU_CAPTURE, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_CAPTURE, ach);
        AppendMenu(ghmenuSys, MF_SEPARATOR, 0, NULL);
        LoadString(ghInst, IDS_MENU_COPYFRAME, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_COPYFRAME, ach);
        LoadString(ghInst, IDS_MENU_EDIT, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_EDIT, ach);
        AppendMenu(ghmenuSys, MF_SEPARATOR, 0, NULL);
        LoadString(ghInst, IDS_MENU_HELP, ach, sizeof(ach));
        AppendMenu(ghmenuSys, MF_STRING, IDM_HELP, ach);

        /* drop Restore, Size, Minimize, Maximize, a separator and Switch To */
        DeleteMenu(ghmenuSys, 0, MF_BYPOSITION);
        DeleteMenu(ghmenuSys, 1, MF_BYPOSITION);
        DeleteMenu(ghmenuSys, 1, MF_BYPOSITION);
        DeleteMenu(ghmenuSys, 1, MF_BYPOSITION);
        DeleteMenu(ghmenuSys, 3, MF_BYPOSITION);
        DeleteMenu(ghmenuSys, 3, MF_BYPOSITION);
        DrawMenuBar(hwnd);

        if (gfOnTop)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetActiveWindow(hwnd);

        lpfn = MakeProcInstance((FARPROC)YieldCallback, ghInst);
        SendMessage(ghwndCap, WM_CAP_SET_CALLBACK_YIELD, 0, (LPARAM)lpfn);
        lpfn = MakeProcInstance((FARPROC)ErrorCallback, ghInst);
        SendMessage(ghwndCap, WM_CAP_SET_CALLBACK_ERROR, 0, (LPARAM)lpfn);
        lpfn = MakeProcInstance((FARPROC)StatusCallback, ghInst);
        SendMessage(ghwndCap, WM_CAP_SET_CALLBACK_STATUS, 0, (LPARAM)lpfn);

        InitHelp();
        break;

    case WM_PAINT:
        _fmemset(&ps, 0, sizeof(ps));
        SetBkMode(BeginPaint(hwnd, &ps), TRANSPARENT);
        EndPaint(hwnd, &ps);
        break;

    case WM_CLOSE:
        TermHelp();
        SendMessage(hwnd, WM_CAP_DRIVER_DISCONNECT, 0, 0L);
        WriteSettings();
        DestroyWindow(hwnd);
        if (hwnd == ghwndApp)
            PostQuitMessage(0);
        break;

    case WM_QUERYENDSESSION:
        if (gfCapturing) {
            SendMessage(ghwndCap, WM_CAP_STOP, 0, 0L);
            ErrorMessage(hwnd, IDS_CAPSTOPPED);
            break;
        }
        if (gidHelp) {
            ErrorMessage(hwnd, IDS_CLOSEFIRST);
            break;
        }
        WriteSettings();
        return 1L;

    case WM_SIZE:
        return 1L;

    case WM_MOVE:
    case WM_QUERYOPEN:
        break;

    case WM_COMMAND:
        if (gfCapturing)
            break;
        if (wParam != IDM_CAPTURE)
            return DefWindowProc(hwnd, msg, wParam, lParam);

        if (!ConfirmOverwrite(hwnd, gszCaptureFile))
            break;

        SendMessage(ghwndCap, WM_CAP_GET_SEQUENCE_SETUP, sizeof(CAPTUREPARMS), (LPARAM)(LPVOID)&cp);
        cp.dwRequestMicroSecPerFrame = gdwUSecPerFrame;
        cp.vKeyAbort = (gfHotShift ? 0x4000 : 0) | (gfHotCtrl ? 0x8000 : 0) | gwHotKey;
        cp.wNumVideoRequested = 4;
        cp.wNumAudioRequested = 10;
        cp.fLimitEnabled = TRUE;
        cp.wTimeLimit = 30000 / gnFPS;
        cp.fCaptureAudio = gfAudio;
        cp.wPercentDropForError = 20;
        cp.wChunkGranularity = 32;
        cp.fMakeUserHitOKToCapture = FALSE;
        cp.fYield = FALSE;
        cp.fAbortLeftMouse = FALSE;
        cp.fAbortRightMouse = FALSE;
        cp.fMCIControl = FALSE;
        cp.fDisableWriteCache = FALSE;
        SendMessage(ghwndCap, WM_CAP_SET_SEQUENCE_SETUP, sizeof(CAPTUREPARMS), (LPARAM)(LPVOID)&cp);
        SendMessage(ghwndCap, WM_CAP_FILE_SET_CAPTURE_FILE, 0, (LPARAM)(LPSTR)gszCaptureFile);

        /* get the icon off the screen before capturing it */
        sndPlaySound(NULL, 0);
        ShowWindow(hwnd, SW_HIDE);
        UpdateWindow(hwnd);
        UpdateWindow(GetDesktopWindow());
        UpdateWindow(GetActiveWindow());

        gfCapturing = TRUE;
        SendMessage(ghwndCap, WM_CAP_SEQUENCE, 0, 0L);
        gfCapturing = FALSE;

        EnableMenuItem(ghmenuSys, IDM_EDIT, MF_ENABLED);
        lstrcpy(gszLastCapture, gszCaptureFile);
        if (gfIncFile)
            IncrementFileName(hwnd /*, gszCaptureFile */);
        ShowWindow(hwnd, SW_SHOWNA);
        MessageBeep(0);
        SetActiveWindow(hwnd);

        ids = 0;
        SendMessage(ghwndCap, WM_CAP_GET_STATUS, sizeof(CAPSTATUS), (LPARAM)(LPVOID)&gCapStatus);
        if (gCapStatus.dwCurrentVideoFrame >= 29900) {
            ids = IDS_ERR_MAXFRAMES;
        } else {
            if (gCapStatus.dwReturn >= IDS_CAP_FILE_OPEN_ERROR &&
                gCapStatus.dwReturn <= IDS_CAP_COMPRESSOR_ERROR)
                ids = IDS_ERR_CAPDISKFULL;
            if (gCapStatus.dwReturn >= IDS_CAP_WAVE_OPEN_ERROR &&
                gCapStatus.dwReturn <= IDS_CAP_VIDEO_SIZE_ERROR)
                ids = IDS_ERR_CAPSETUP;
        }
        if (ids)
            ErrorMessage(hwnd, ids);
        break;

    case WM_SYSCOMMAND:
        if (gfCapturing)
            break;

        switch (wParam) {
        case IDM_SETFILE:
            SetHelpContext(hwnd, HELP_SETFILE);
            if (SetCaptureFileDlg(hwnd))
                SendMessage(ghwndCap, WM_CAP_FILE_SET_CAPTURE_FILE, 0, (LPARAM)(LPSTR)gszCaptureFile);
            SetHelpContext(hwnd, 0);
            break;

        case IDM_EDIT:
            if (!EditCapture())
                ErrorMessage(hwnd, IDS_ERR_NOEDITOR);
            break;

        case IDM_SETWINDOW:
            SetHelpContext(hwnd, HELP_SETWINDOW);
            if (gfDriverOK)
                SendMessage(ghwndCap, WM_CAP_DLG_VIDEOFORMAT, 0, 0L);
            SetHelpContext(hwnd, 0);
            break;

        case IDM_PREFERENCES:
            lpfn = MakeProcInstance((FARPROC)PrefsDlgProc, ghInst);
            SetHelpContext(hwnd, HELP_PREFERENCES);
            DialogBox(ghInst, MAKEINTRESOURCE(IDD_PREFERENCES), hwnd, (DLGPROC)lpfn);
            SetHelpContext(hwnd, 0);
            FreeProcInstance(lpfn);
            break;

        case IDM_COPYFRAME:
            SendMessage(ghwndCap, WM_CAP_GRAB_FRAME, 0, 0L);
            if (!SendMessage(ghwndCap, WM_CAP_EDIT_COPY, 0, 0L))
                ErrorMessage(hwnd, IDS_ERR_NOMEM);
            break;

        case IDM_CAPTURE:
            SendMessage(hwnd, WM_COMMAND, IDM_CAPTURE, 0L);
            break;

        case IDM_HELP:
            WinHelp(ghwndApp, gszHelpFile, HELP_INDEX, 0L);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        break;

    case WM_PALETTECHANGED:
        if (gfCapturing)
            PostMessage(ghwndApp, WM_USER_PALCHANGE, 0, 0L);
        break;

    case WM_USER_PALCHANGE:
        SendMessage(ghwndCap, WM_CAP_STOP, 0, 0L);
        LoadString(ghInst, IDS_CAPSTOPPED, ach, sizeof(ach));
        LoadString(ghInst, IDS_PALCHANGE, achMsg, sizeof(achMsg));
        MessageBox(ghwndApp, achMsg, ach, MB_ICONHAND);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0L;
}

/*
 * seg1:1982-19FF  InitApplication
 *
 * Registers the "ComArrow" class and the main window class.  The class
 * names a menu (gszAppName) that does not exist.  Returns 0, or -1 if the
 * main class can't be registered.
 */
int FAR InitApplication(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    ArrowInit(ghInst);

    wc.style         = CS_BYTEALIGNWINDOW | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = ghInst;
    wc.hIcon         = LoadIcon(ghInst, gszIconName);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = gszAppName;
    wc.lpszClassName = gszAppName;

    return RegisterClass(&wc) ? 0 : -1;
}

/*
 * seg1:1A00-1A0D  TermApplication
 */
void FAR TermApplication(void)
{
    UnregisterClass(gszAppName, ghInst);
}
