/*
 * dialogs.c - code segment 5 of VIDCAP.EXE (0x162A bytes): the Set File
 * Size, Capture Video Sequence, Initialization Error, Preferences and
 * Audio Format dialogs, the recording level meter ("VCRLMeter") and the
 * Recording Level dialog that drives it from the wave input.
 *
 * Dialog procedures are FAR PASCAL (not exported; DoDialogParam makes
 * their procedure instances).
 */

#include <windows.h>
#include <mmsystem.h>
#include <ctype.h>
#include "vidcap.h"

#define IDT_FILESIZE    IDC_FILESIZE    /* the edit control's ID */

static UINT     gidSizeTimer = 0;       /* DS:018E */
static BOOL     gfSizeInit = TRUE;      /* DS:0190 */
                                        /* DS:0192 "%d", 0195 "1", 0197 "32767", 019D "1", 019F "%ld" */
static HWAVEIN  ghWaveInLevel = NULL;   /* DS:01A4 */

static DWORD    gdwFreeMB;              /* DS:0FB0 */
static DWORD    gdwFileMB;              /* DS:0FB4 */
static HBRUSH   ghbrInitErr;            /* DS:0FB8 */
static DWORD    gdwWaveFormats;         /* DS:0FBA */
static LPWAVEHDR gapwhLevel[2];         /* DS:0FBE */
static PCMWAVEFORMAT gwfLevel;          /* DS:15F8 */
static BOOL     gfLevelOpen;            /* DS:268E */
static int      giLevelBuf;             /* DS:2EE8 */

/*
 * seg5:0000-02F6  FileSizeDlgProc
 *
 * The size is in MB; "Space Available" is the free space plus the file's
 * present size.  The suggested size of a new file is half the free space
 * up to 60 MB (60 above 120 MB free).  The value is checked 400 ms after
 * typing stops.
 */
BOOL FAR PASCAL FileSizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    achDrive[4];
    char    ach[10];
    char    achTitle[80];
    char    achMsg[128];
    DWORD   dw;
    DWORD   dwT;
    LONG    l;
    int     nDrive;

    switch (msg) {
    case WM_INITDIALOG:
        gidSizeTimer = 0;
        gfSizeInit = TRUE;
        SplitPath((LPSTR)lParam, achDrive, NULL, NULL, NULL);
        if (achDrive[0]) {
            if (islower(achDrive[0]))
                achDrive[0] -= 'a' - 'A';
            nDrive = achDrive[0] - '@';
        } else {
            nDrive = 0;
        }
        l = (LONG)DosDiskFreeSpace(nDrive);
        gdwFreeMB = (l /= 0x100000L);

        if (gfCapFileExisted) {
            l = (LONG)(gdwCapFileSize + 0xFFFFFL) / 0x100000L;
            gdwFileMB = l;
        } else {
            if (l >= 60)
                l = 60;
            else
                l /= 2;
            gdwFileMB = 0;
        }
        dw = l;

        SetDlgItemInt(hDlg, IDC_FILESIZE, (UINT)dw, FALSE);
        gdwFreeMB += gdwFileMB;
        SetDlgItemInt(hDlg, IDC_SPACEAVAIL, (UINT)gdwFreeMB, FALSE);
        if (gdwFreeMB == 0) {
            LoadString(ghInst, IDS_SETFILESIZE_TITLE, achTitle, sizeof(achTitle));
            LoadString(ghInst, IDS_ERR_NOSPACE, achMsg, sizeof(achMsg));
            MessageBox(hDlg, achMsg, achTitle, MB_ICONEXCLAMATION);
            EndDialog(hDlg, 0);
        }
        gfSizeInit = FALSE;
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gidSizeTimer)
                SendMessage(hDlg, WM_TIMER, gidSizeTimer, 0L);
            GetDlgItemText(hDlg, IDC_FILESIZE, ach, sizeof(ach));
            EndDialog(hDlg, (int)StrToLong(ach));
            break;

        case IDCANCEL:
            EndDialog(hDlg, 0);
            break;

        case IDC_FILESIZE:
            if (HIWORD(lParam) != EN_CHANGE || gfSizeInit)
                break;
            if (gidSizeTimer)
                KillTimer(hDlg, gidSizeTimer);
            gidSizeTimer = wParam;
            if (SetTimer(hDlg, gidSizeTimer, 400, NULL) == 0)
                SendMessage(hDlg, WM_TIMER, gidSizeTimer, 0L);
            break;
        }
        break;

    case WM_TIMER:
        if (gidSizeTimer != IDT_FILESIZE)
            break;
        KillTimer(hDlg, gidSizeTimer);
        gidSizeTimer = 0;
        dwT = (DWORD)GetDlgItemInt(hDlg, wParam, NULL, FALSE);
        if (dwT < 1 || (long)dwT > (long)gdwFreeMB) {
            dwT = ((long)dwT < 1) ? 1L : gdwFreeMB;
            SetDlgItemInt(hDlg, wParam, (UINT)dwT, FALSE);
            MessageBeep(0);
            SendDlgItemMessage(hDlg, IDC_FILESIZE, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
        }
        break;

    case WM_VSCROLL:
        ArrowEditChange(GetDlgItem(hDlg, IDC_FILESIZE), wParam, 1L, gdwFreeMB);
        break;
    }

    return FALSE;
}

/*
 * seg5:02FA-087B  CapSequenceDlgProc
 *
 * Quirks of the original: the time limit is re-checked on every
 * notification from its edit control (a value over 32767 is replaced and
 * then treated as 20); OK with a zero time limit tries to cancel but the
 * second EndDialog makes it OK anyway; and turning MCI control on or off
 * also re-applies the time limit check box.
 */
BOOL FAR PASCAL CapSequenceDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    ach[10];
    HWND    hwnd;
    DWORD   dw;
    LONG    l;
    BOOL    f;
    int     i;

    switch (msg) {
    case WM_INITDIALOG:
        gnMCIVCRs = MCICountDevices(MCI_DEVTYPE_VCR);
        gnMCIVideodiscs = MCICountDevices(MCI_DEVTYPE_VIDEODISC);
        if (gnMCIVCRs + gnMCIVideodiscs == 0) {
            EnableWindow(GetDlgItem(hDlg, IDC_MCICONTROL), FALSE);
            gfMCIControl = FALSE;
        }
        EnableWindow(GetDlgItem(hDlg, IDC_MCISETUP), gfMCIControl);
        CheckDlgButton(hDlg, IDC_MCICONTROL, gfMCIControl);
        CheckDlgButton(hDlg, IDC_TIMELIMIT, gfLimitEnabled);
        if (gfAudioDevice)
            CheckDlgButton(hDlg, IDC_CAPAUDIO, gfCaptureAudio);
        else
            EnableWindow(GetDlgItem(hDlg, IDC_CAPAUDIO), FALSE);
        CheckRadioButton(hDlg, IDC_TODISK, IDC_TOMEMORY, gfCaptureToMemory ? IDC_TOMEMORY : IDC_TODISK);

        USecToFrameRate(ach, gdwMicroSecPerFrame);
        SetDlgItemText(hDlg, IDC_FRAMERATE, ach);
        wsprintf(ach, "%d", gwTimeLimit);
        SetDlgItemText(hDlg, IDC_SECONDS, ach);
        EnableWindow(GetDlgItem(hDlg, IDC_SECONDS), gfLimitEnabled);
        EnableWindow(GetDlgItem(hDlg, IDC_SECONDSLABEL), gfLimitEnabled);
        EnableWindow(GetDlgItem(hDlg, IDC_SECONDSARROW), gfLimitEnabled);
        EnableWindow(GetDlgItem(hDlg, IDC_AUDIOSETUP), gfCaptureAudio && gfAudioDevice);
        PositionDialog(hDlg, ghwndShow);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            gfCaptureAudio = IsDlgButtonChecked(hDlg, IDC_CAPAUDIO);
            gfMCIControl = IsDlgButtonChecked(hDlg, IDC_MCICONTROL);
            gfLimitEnabled = IsDlgButtonChecked(hDlg, IDC_TIMELIMIT);
            gfCaptureToMemory = IsDlgButtonChecked(hDlg, IDC_TOMEMORY);
            GetDlgItemText(hDlg, IDC_FRAMERATE, ach, sizeof(ach));
            gdwMicroSecPerFrame = FrameRateToUSec(ach);
            if (gdwMicroSecPerFrame == 0)
                gdwMicroSecPerFrame = 66666;
            GetDlgItemText(hDlg, IDC_SECONDS, ach, sizeof(ach));
            l = StrToLong(ach);
            if (gwTimeLimit && l <= 0)
                EndDialog(hDlg, FALSE);
            gwTimeLimit = (int)l;
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            break;

        case IDC_FRAMERATE:
            if (HIWORD(lParam) != EN_KILLFOCUS)
                break;
            hwnd = GetDlgItem(hDlg, IDC_FRAMERATE);
            GetDlgItemText(hDlg, IDC_FRAMERATE, ach, sizeof(ach));
            dw = FrameRateToUSec(ach);
            if (dw == 0)
                USecToFrameRate(ach, 66666L);
            else if (dw < 10000)
                USecToFrameRate(ach, 10000L);
            else if (dw > 59999999L)
                USecToFrameRate(ach, 59999999L);
            else
                break;
            SetWindowText(hwnd, ach);
            break;

        case IDC_MCICONTROL:
            EnableWindow(GetDlgItem(hDlg, IDC_MCISETUP), IsDlgButtonChecked(hDlg, IDC_MCICONTROL));
            /* fall through */
        case IDC_TIMELIMIT:
            f = IsDlgButtonChecked(hDlg, IDC_TIMELIMIT);
            EnableWindow(GetDlgItem(hDlg, IDC_SECONDS), f);
            EnableWindow(GetDlgItem(hDlg, IDC_SECONDSLABEL), f);
            EnableWindow(GetDlgItem(hDlg, IDC_SECONDSARROW), f);
            break;

        case IDC_SECONDS:
            GetDlgItemText(hDlg, IDC_SECONDS, ach, sizeof(ach));
            l = StrToLong(ach);
            if (l < 1) {
                SetWindowText(GetDlgItem(hDlg, IDC_SECONDS), "1");
                l = 0;
            } else if (l > 32767) {
                SetWindowText(GetDlgItem(hDlg, IDC_SECONDS), "32767");
                l = 20;
            }
            f = FALSE;
            for (i = 0; !f; i++) {
                if (ach[i] == '\0')
                    break;
                if (ach[i] < '0' || ach[i] > '9')
                    f = TRUE;
            }
            if (!f)
                break;
            hwnd = GetDlgItem(hDlg, IDC_SECONDS);
            if (l == 0) {
                SetWindowText(hwnd, "1");
            } else {
                wsprintf(ach, "%ld", l);
                SetWindowText(hwnd, ach);
            }
            break;

        case IDC_CAPAUDIO:
            EnableWindow(GetDlgItem(hDlg, IDC_AUDIOSETUP), IsDlgButtonChecked(hDlg, IDC_CAPAUDIO));
            break;

        case IDC_AUDIOSETUP:
            DoDialogParam(IDD_AUDIOFORMAT, hDlg, (FARPROC)AudioFormatDlgProc, 0L);
            return TRUE;

        case IDC_VIDEOSETUP:
            gidHelpContext = IDH_VIDEOFORMAT;
            videoDialog(ghVideoIn, hDlg, 0L);
            gidHelpContext = IDD_CAPSEQUENCE;
            UpdateWindow(hDlg);
            GetCaptureFormat();
            GetCapturePalette();
            SizeShowWindow(TRUE);
            InvalidateRect(ghwndShow, NULL, TRUE);
            UpdateWindow(ghwndShow);
            return TRUE;

        case IDC_MCISETUP:
            DoDialogParam(IDD_MCISETUP, hDlg, (FARPROC)MCIDlgProc, 0L);
            return TRUE;

        case IDC_COMPRESSSETUP:
            gidHelpContext = IDH_COMPRESSION;
            ICCompressorChoose(hDlg, ICMF_CHOOSE_KEYFRAME, glpbiCapture, NULL, &gCompVars, NULL);
            gidHelpContext = IDD_CAPSEQUENCE;
            break;
        }
        break;

    case WM_VSCROLL:
        if (GetWindowWord((HWND)HIWORD(lParam), GWW_ID) == IDC_FRAMERATEARROW)
            ArrowEditChangeRate(GetDlgItem(hDlg, IDC_FRAMERATE), wParam, 1L, 100L, 1);
        else
            ArrowEditChange(GetDlgItem(hDlg, IDC_SECONDS), wParam, 1L, 32767L);
        break;
    }

    return FALSE;
}

/*
 * Draws the level bar between two levels (0-100) in black or white.
 */
#define LEVELX(n, cx)   ((UINT)((n) * (cx)) / 100)

/*
 * seg5:087E-0C12  RLMeterProc  (exported ordinal 9)
 *
 * The recording level meter: a black bar for the level (WM_USER+1, 0-100)
 * and a red line at the peak, which is reset every 2 seconds.
 *
 * Window words (cbWndExtra 0x14):
 *   2 HPEN   4 timer   6 peak   8 peak line drawn at   0A level
 */
LRESULT FAR PASCAL RLMeterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT        rc;
    RECT        rcT;
    HPEN        hpen;
    HDC         hdc;
    UINT        nLevel;
    UINT        nPeak;
    UINT        nDrawn;
    UINT        idTimer;
    int         x;

    switch (msg) {
    case WM_CREATE:
        SetWindowWord(hwnd, 6, 0);
        SetWindowWord(hwnd, 8, 0);
        SetWindowWord(hwnd, 0x0A, 0);
        SetWindowWord(hwnd, 2, (WORD)CreatePen(PS_SOLID, 2, RGB(255, 0, 0)));
        SetWindowWord(hwnd, 4, SetTimer(hwnd, 0, 2000, NULL));
        break;

    case WM_DESTROY:
        hpen = (HPEN)GetWindowWord(hwnd, 2);
        if (hpen) {
            DeleteObject(hpen);
            SetWindowWord(hwnd, 2, 0);
        }
        idTimer = GetWindowWord(hwnd, 4);
        if (idTimer == 0)
            break;
        KillTimer(hwnd, idTimer);
        SetWindowWord(hwnd, 4, 0);
        break;

    case WM_PAINT:
        hpen = (HPEN)GetWindowWord(hwnd, 2);
        nLevel = GetWindowWord(hwnd, 0x0A);
        nPeak = GetWindowWord(hwnd, 6);
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        rcT = rc;
        rcT.right = LEVELX(nLevel, rc.right);
        rcT.left = 0;
        SetBkColor(ps.hdc, RGB(0, 0, 0));
        ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rcT, NULL, 0, NULL);
        x = (int)((long)rc.right * nPeak / 100L);
        SelectObject(ps.hdc, hpen);
        MoveTo(ps.hdc, x, 0);
        LineTo(ps.hdc, x, rc.bottom);
        EndPaint(hwnd, &ps);
        break;

    case WM_TIMER:
        SetWindowWord(hwnd, 6, 0);
        break;

    case WM_USER + 1:
        hpen = (HPEN)GetWindowWord(hwnd, 2);
        nDrawn = GetWindowWord(hwnd, 8);
        nLevel = GetWindowWord(hwnd, 0x0A);
        nPeak = GetWindowWord(hwnd, 6);
        GetClientRect(hwnd, &rc);
        hdc = GetDC(hwnd);

        if (wParam > nPeak) {
            SetWindowWord(hwnd, 6, wParam);
            nPeak = wParam;
        }
        if (nDrawn != nPeak) {
            /* erase the old peak line */
            rcT = rc;
            x = LEVELX(nDrawn, rc.right);
            rcT.right = x + 1;
            rcT.left = x - 1;
            SetBkColor(hdc, RGB(255, 255, 255));
            ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rcT, NULL, 0, NULL);
        }

        rcT = rc;
        if (nLevel < wParam) {
            rcT.left = LEVELX(nLevel, rc.right);
            rcT.right = LEVELX(rc.right, wParam);
            rcT.left--;
            if (rcT.left < 0)
                rcT.left = 0;
            SetBkColor(hdc, RGB(0, 0, 0));
            ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rcT, NULL, 0, NULL);
        } else if (nLevel > wParam) {
            rcT.right = LEVELX(nLevel, rc.right);
            rcT.left = LEVELX(rc.right, wParam);
            SetBkColor(hdc, RGB(255, 255, 255));
            ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rcT, NULL, 0, NULL);
        }
        SetWindowWord(hwnd, 0x0A, wParam);

        if (nDrawn != nPeak) {
            x = (int)((long)rc.right * nPeak / 100L);
            SelectObject(hdc, hpen);
            MoveTo(hdc, x, 0);
            LineTo(hdc, x, rc.bottom);
            SetWindowWord(hwnd, 8, nPeak);
        }
        ReleaseDC(hwnd, hdc);
        return 0L;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
 * seg5:0C16-0C54  LevelDlgProc
 */
BOOL FAR PASCAL LevelDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        gfLevelOpen = TRUE;
        break;
    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDCANCEL)
            gfLevelOpen = FALSE;
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

/*
 * seg5:0C58-0D46  InitErrDlgProc
 *
 * Shows the driver's error message (lParam) in red.  Continue ends the
 * dialog with TRUE, Exit with FALSE.
 */
BOOL FAR PASCAL InitErrDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT pt;

    switch (msg) {
    case WM_DESTROY:
        DeleteObject(ghbrInitErr);
        break;

    case WM_CTLCOLOR:
        if (GetDlgItem(hDlg, IDC_INITERRTEXT) != (HWND)LOWORD(lParam))
            break;
        SetTextColor((HDC)wParam, RGB(255, 0, 0));
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        UnrealizeObject(ghbrInitErr);
        pt.x = 0;
        pt.y = 0;
        ClientToScreen(hDlg, &pt);
        SetBrushOrg((HDC)wParam, pt.x, pt.y);
        return (BOOL)ghbrInitErr;

    case WM_INITDIALOG:
        ghbrInitErr = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        SetDlgItemText(hDlg, IDC_INITERRTEXT, (LPSTR)lParam);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
        case IDC_CONTINUE:
        case IDC_CONTINUE + 2:
            EndDialog(hDlg, TRUE);
            break;
        case IDCANCEL:
        case IDC_EXIT:
            EndDialog(hDlg, FALSE);
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * seg5:0D4A-0EC0  PrefsDlgProc
 *
 * The index holds 34,816 frames for "32,000" and 331,776 for "324,000".
 */
BOOL FAR PASCAL PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BOOL    fCenterOld;
    int     idBkOld;

    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hDlg, IDC_STATUSBAR, gfStatusBar);
        CheckDlgButton(hDlg, IDC_TOOLBARCHK, gfToolBar);
        CheckDlgButton(hDlg, IDC_CENTERIMAGE, gfCenterImage);
        CheckRadioButton(hDlg, IDC_BKDEFAULT, IDC_BKBLACK, gidBkColor);
        CheckRadioButton(hDlg, IDC_INDEX32K, IDC_INDEX324K,
                         (gdwIndexSize > 0x8800L) ? IDC_INDEX324K : IDC_INDEX32K);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            fCenterOld = gfCenterImage;
            idBkOld = gidBkColor;
            ShowBars(IsDlgButtonChecked(hDlg, IDC_TOOLBARCHK), IsDlgButtonChecked(hDlg, IDC_STATUSBAR));
            if (IsDlgButtonChecked(hDlg, IDC_BKDEFAULT))
                gidBkColor = IDC_BKDEFAULT;
            else if (IsDlgButtonChecked(hDlg, IDC_BKLTGRAY))
                gidBkColor = IDC_BKLTGRAY;
            else
                gidBkColor = IsDlgButtonChecked(hDlg, IDC_BKDKGRAY) ? IDC_BKDKGRAY : IDC_BKBLACK;
            gfCenterImage = IsDlgButtonChecked(hDlg, IDC_CENTERIMAGE);
            if (idBkOld != gidBkColor)
                InvalidateRect(ghwndFrame, NULL, TRUE);
            if (fCenterOld != gfCenterImage) {
                SizeShowWindow(FALSE);
                InvalidateRect(ghwndShow, NULL, TRUE);
            }
            gdwIndexSize = IsDlgButtonChecked(hDlg, IDC_INDEX32K) ? 0x8800L : 0x51000L;
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * seg5:0EC4-0F67  GetAudioFormatDlg
 */
void FAR PASCAL GetAudioFormatDlg(HWND hDlg)
{
    if (IsDlgButtonChecked(hDlg, IDC_8BIT))
        gwBits = 8;
    if (IsDlgButtonChecked(hDlg, IDC_16BIT))
        gwBits = 16;
    if (IsDlgButtonChecked(hDlg, IDC_MONO))
        gwChannels = 1;
    if (IsDlgButtonChecked(hDlg, IDC_STEREO))
        gwChannels = 2;
    if (IsDlgButtonChecked(hDlg, IDC_11KHZ))
        gwFrequency = 11025;
    if (IsDlgButtonChecked(hDlg, IDC_22KHZ))
        gwFrequency = 22050;
    if (IsDlgButtonChecked(hDlg, IDC_44KHZ))
        gwFrequency = 44100;
}

/*
 * seg5:0F6A-0FB1  GetWaveInFormats  (NEAR)
 *
 * The WAVE_FORMAT_* bits of all wave input devices.
 */
static DWORD NEAR GetWaveInFormats(void)
{
    WAVEINCAPS  wic;
    DWORD       dwFormats;
    UINT        i;

    dwFormats = 0;
    for (i = 0; (int)i < (int)waveInGetNumDevs(); i++)
        if (waveInGetDevCaps(i, &wic, sizeof(wic)) == 0)
            dwFormats |= wic.dwFormats;
    return dwFormats;
}

/*
 * seg5:0FB2-1140  AudioFormatDlgProc
 *
 * Formats no wave input device has are disabled.  Level tries the
 * selected format without keeping it.
 */
BOOL FAR PASCAL AudioFormatDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    UINT    wChannels;
    UINT    wFrequency;
    UINT    wBits;

    switch (msg) {
    case WM_INITDIALOG:
        gdwWaveFormats = GetWaveInFormats();
        if (gdwWaveFormats) {
            if (!(gdwWaveFormats & (WAVE_FORMAT_4M08 | WAVE_FORMAT_4S08 | WAVE_FORMAT_4M16 | WAVE_FORMAT_4S16)))
                EnableWindow(GetDlgItem(hDlg, IDC_44KHZ), FALSE);
            if (!(gdwWaveFormats & (WAVE_FORMAT_2M08 | WAVE_FORMAT_2S08 | WAVE_FORMAT_2M16 | WAVE_FORMAT_2S16)))
                EnableWindow(GetDlgItem(hDlg, IDC_22KHZ), FALSE);
            if (!(LOWORD(gdwWaveFormats) & 0x0AAA))
                EnableWindow(GetDlgItem(hDlg, IDC_STEREO), FALSE);
            if (!(LOWORD(gdwWaveFormats) & 0x0CCC))
                EnableWindow(GetDlgItem(hDlg, IDC_16BIT), FALSE);
        }
        CheckRadioButton(hDlg, IDC_8BIT, IDC_16BIT, (gwBits == 8) ? IDC_8BIT : IDC_16BIT);
        CheckRadioButton(hDlg, IDC_MONO, IDC_STEREO, (gwChannels == 1) ? IDC_MONO : IDC_STEREO);
        CheckRadioButton(hDlg, IDC_11KHZ, IDC_44KHZ,
                         (gwFrequency == 11025) ? IDC_11KHZ :
                         (gwFrequency == 22050) ? IDC_22KHZ : IDC_44KHZ);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            GetAudioFormatDlg(hDlg);
            EndDialog(hDlg, TRUE);
            break;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            break;
        case IDC_LEVEL:
            wChannels = gwChannels;
            wFrequency = gwFrequency;
            wBits = gwBits;
            GetAudioFormatDlg(hDlg);
            RecordLevel(hDlg);
            gwChannels = wChannels;
            gwFrequency = wFrequency;
            gwBits = wBits;
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * seg5:1144-11E0  StrToLong  (FAR CDECL)
 */
LONG FAR StrToLong(LPSTR lpsz)
{
    LONG    l;
    BOOL    fNeg;

    l = 0;
    if (*lpsz == '+') {
        lpsz++;
        fNeg = FALSE;
    } else if (*lpsz == '-') {
        lpsz++;
        fNeg = TRUE;
    } else {
        fNeg = FALSE;
    }
    while (*lpsz >= '0' && *lpsz <= '9') {
        l = l * 10 + (*lpsz - '0');
        lpsz++;
    }
    return fNeg ? -l : l;
}

/*
 * seg5:11E2-124D  DispatchPending  (FAR CDECL)
 */
void FAR DispatchPending(HWND hDlg)
{
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (hDlg == NULL || !IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

/*
 * seg5:124E-139F  OpenLevelWaveIn  (FAR CDECL)
 *
 * Opens the wave input in the selected format with two 512-byte buffers.
 *
 * Quirk of the original: the average bytes per second is a 16-bit
 * product, wrong for 44 kHz 16-bit stereo.
 */
BOOL FAR OpenLevelWaveIn(HWND hwnd)
{
    LPWAVEHDR   lpwh;
    UINT        ids;
    UINT        nBlockAlign;
    int         i;

    gwfLevel.wf.wFormatTag = WAVE_FORMAT_PCM;
    gwfLevel.wf.nChannels = gwChannels;
    gwfLevel.wf.nSamplesPerSec = gwFrequency;
    nBlockAlign = (gwChannels * gwBits) >> 3;
    gwfLevel.wf.nAvgBytesPerSec = (WORD)(nBlockAlign * gwFrequency);
    gwfLevel.wf.nBlockAlign = nBlockAlign;
    gwfLevel.wBitsPerSample = gwBits;

    ghWaveInLevel = NULL;
    if (waveInOpen(&ghWaveInLevel, (UINT)WAVE_MAPPER, (LPWAVEFORMAT)&gwfLevel, 0L, 0L, 0L)) {
        ids = IDS_ERR_WAVEOPEN;
        goto error;
    }

    _fmemset(gapwhLevel, 0, sizeof(gapwhLevel));
    for (i = 0; i < 2; i++) {
        lpwh = (LPWAVEHDR)GlobalLock(GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(WAVEHDR) + 0x200L));
        gapwhLevel[i] = lpwh;
        if (lpwh == NULL) {
            ids = IDS_ERR_WAVEMEM;
            goto error;
        }
        lpwh->lpData = (LPSTR)lpwh + sizeof(WAVEHDR);
        lpwh->dwBufferLength = 0x200;
        lpwh->dwBytesRecorded = 0;
        lpwh->dwUser = 0;
        lpwh->dwFlags = 0;
        lpwh->dwLoops = 0;
        if (waveInPrepareHeader(ghWaveInLevel, lpwh, sizeof(WAVEHDR))) {
            ids = IDS_ERR_WAVEPREPARE;
            goto error;
        }
        if (waveInAddBuffer(ghWaveInLevel, gapwhLevel[i], sizeof(WAVEHDR))) {
            ids = IDS_ERR_WAVEADD;
            goto error;
        }
    }

    giLevelBuf = 0;
    waveInStart(ghWaveInLevel);
    return TRUE;

error:
    MsgBoxID(hwnd, ghInst, MB_ICONEXCLAMATION, IDS_APPNAME2, ids);
    CloseLevelWaveIn();
    return FALSE;
}

/*
 * seg5:13A0-1413  CloseLevelWaveIn  (FAR CDECL)
 */
int FAR CloseLevelWaveIn(void)
{
    int i;

    if (ghWaveInLevel) {
        waveInStop(ghWaveInLevel);
        waveInReset(ghWaveInLevel);
        for (i = 0; i < 2; i++) {
            if (gapwhLevel[i]) {
                waveInUnprepareHeader(ghWaveInLevel, gapwhLevel[i], sizeof(WAVEHDR));
                GlobalFree((HGLOBAL)SELECTOROF(gapwhLevel[i]));
            }
            gapwhLevel[i] = NULL;
        }
        waveInClose(ghWaveInLevel);
        ghWaveInLevel = NULL;
    }
    return 0;
}

/*
 * The level of one sample, 0-100.
 */
static UINT NEAR SampleLevel(LPBYTE lp, int i)
{
    int v;

    if (gwBits == 8)
        v = lp[i] - 0x80;
    else
        v = (signed char)lp[i + 1];
    if (v < 0)
        v = -v;
    return (v * 100) / 128;
}

/*
 * seg5:1414-1628  RecordLevel  (FAR CDECL)
 *
 * The Recording Level dialog: runs its own message loop, showing the
 * peak of each buffer on the meters until the dialog is closed.
 */
void FAR RecordLevel(HWND hwndParent)
{
    LPWAVEHDR   lpwh;
    FARPROC     lpfn;
    HWND        hDlg;
    UINT        nLevelL;
    UINT        nLevelR;
    UINT        n;
    int         i;
    BOOL        f;

    if (!OpenLevelWaveIn(hwndParent))
        return;

    lpfn = MakeProcInstance((FARPROC)LevelDlgProc, ghInst);
    if (gwChannels == 2) {
        gidHelpContext = IDD_LEVELSTEREO;
        hDlg = CreateDialog(ghInst, MAKEINTRESOURCE(IDD_LEVELSTEREO), hwndParent, (DLGPROC)lpfn);
    } else {
        gidHelpContext = IDD_LEVELMONO;
        hDlg = CreateDialog(ghInst, MAKEINTRESOURCE(IDD_LEVELMONO), hwndParent, (DLGPROC)lpfn);
    }
    ShowWindow(hDlg, SW_SHOWNORMAL);
    UpdateWindow(hDlg);
    EnableWindow(hwndParent, FALSE);
    gfLevelOpen = f = TRUE;

    while (f) {
        DispatchPending(hDlg);
        lpwh = gapwhLevel[giLevelBuf];
        if (lpwh->dwFlags & WHDR_DONE) {
            nLevelL = 0;
            nLevelR = 0;
            for (i = 0; i < 0x200; ) {
                n = SampleLevel((LPBYTE)lpwh->lpData, i);
                i++;
                if (gwBits > 8)
                    i++;
                if (nLevelL < n)
                    nLevelL = n;
                if (gwChannels == 2) {
                    n = SampleLevel((LPBYTE)lpwh->lpData, i);
                    i++;
                    if (gwBits > 8)
                        i++;
                    if (nLevelR < n)
                        nLevelR = n;
                }
            }
            if (waveInAddBuffer(ghWaveInLevel, gapwhLevel[giLevelBuf], sizeof(WAVEHDR))) {
                f = FALSE;
                StatusText(MAKEINTRESOURCE(IDS_ERR_WAVEADD));
            }
            if (++giLevelBuf >= 2)
                giLevelBuf = 0;
            SendDlgItemMessage(hDlg, IDC_METER1, WM_USER + 1, nLevelL, 0L);
            if (gwChannels == 2)
                SendDlgItemMessage(hDlg, IDC_METER2, WM_USER + 1, nLevelR, 0L);
        }
        if (!gfLevelOpen)
            break;
    }

    EnableWindow(hwndParent, TRUE);
    SetActiveWindow(hwndParent);
    DestroyWindow(hDlg);
    gidHelpContext = 0;
    FreeProcInstance(lpfn);
    CloseLevelWaveIn();
}
