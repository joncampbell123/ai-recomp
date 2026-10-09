/*
 * mcicap.c - code segment 8 of VIDCAP.EXE, 05A6-1DB7: capture under MCI
 * control of a videodisc or VCR.
 *
 * The device is driven through mciSendString with the alias "mciframes"
 * and positions in milliseconds.  With "Play Video" chosen Capture/Video
 * streams while the device plays (capavi.c); with "Step Video" MCI step
 * capture (MCIStepCapture) steps the device frame by frame, grabbing a
 * frame whenever the position passes the time of the next one, with
 * optional temporal averaging of several grabs and 2x spatial averaging
 * (average.c), then replays the segment to record the audio.
 *
 * Functions are FAR CDECL unless noted.
 */

#include <windows.h>
#include <mmsystem.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "vidcap.h"

#define IDT_STEP        901
#define STEP_TICK       33

static DWORD    gdwMCIStartDlg;         /* DS:1452 MCI Settings while open */
static DWORD    gdwMCIStopDlg;          /* DS:1456 */
static UINT     gidStepTimer;           /* DS:145A */
static DWORD    gdwMCIPos;              /* DS:145C current device position */
static BOOL     gfStepping;             /* DS:1460 */
static BOOL     gfAudioRecording;       /* DS:1462 */
static BOOL     gfWaveOpened;           /* DS:1464 */
int             gnMCIVideodiscs;        /* DS:1552 */
BOOL            gfMCIPlay;              /* DS:1554 Play Video rather than Step Video */
static DWORD    gdwMCIEndPos;           /* DS:2602 where stepping ended */
static VIDEOHDR gvh2x;                  /* DS:2690 the double size frame */
static DWORD    gdwMCIStartPos;         /* DS:26BA where stepping started */
static BOOL     gfMCIUseStart;          /* DS:2ED2 set by MCI Settings, never cleared */
static BOOL     gfMCIUseStop;           /* DS:2ED8 */
BOOL            gfCapturing;            /* DS:2FDE */
static DWORD    gdwMCIError;            /* DS:2F00 */
DWORD           gdwMCIStartTime;        /* DS:2F3A ms */
DWORD           gdwMCIStopTime;         /* DS:2FE0 */
static LPBITMAPINFOHEADER glpbi2x;      /* DS:305A */
int             gnMCIVCRs;              /* DS:3062 */

/*
 * seg8:05A6-0600  MCICountDevices
 *
 * The number of MCI devices of a type.  On an error it returns an
 * uninitialised local.
 */
UINT FAR MCICountDevices(UINT wDeviceType)
{
    MCI_SYSINFO_PARMS   mcisip;
    DWORD               dwCount;
    UINT                n;

    mcisip.dwCallback = 0;
    mcisip.lpstrReturn = (LPSTR)&dwCount;
    mcisip.dwRetSize = sizeof(dwCount);
    mcisip.wDeviceType = wDeviceType;
    if (mciSendCommand(0, MCI_SYSINFO, MCI_SYSINFO_QUANTITY, (DWORD)(LPVOID)&mcisip))
        return n;
    return *(UINT FAR *)mcisip.lpstrReturn;
}

/*
 * seg8:0602-0623  MCIDeviceClose
 */
void FAR MCIDeviceClose(void)
{
    mciSendString("close mciframes", NULL, 0, NULL);
}

/*
 * seg8:0624-06A0  MCIDeviceOpen
 */
BOOL FAR MCIDeviceOpen(LPSTR lpszDevice)
{
    char    ach[160];

    wsprintf(ach, "open %s shareable wait alias mciframes", lpszDevice);
    gdwMCIError = mciSendString(ach, NULL, 0, NULL);
    if (gdwMCIError)
        return FALSE;

    gdwMCIError = mciSendString("set mciframes time format milliseconds", NULL, 0, NULL);
    if (gdwMCIError) {
        MCIDeviceClose();
        return FALSE;
    }
    return TRUE;
}

/*
 * seg8:06A2-06FD  MCIDeviceGetPosition  (FAR PASCAL)
 */
BOOL FAR PASCAL MCIDeviceGetPosition(LPDWORD lpdwPos)
{
    char    ach[80];

    gdwMCIError = mciSendString("status mciframes position wait", ach, sizeof(ach), NULL);
    if (gdwMCIError) {
        *lpdwPos = 0;
        return FALSE;
    }
    *lpdwPos = atol(ach);
    return TRUE;
}

/*
 * seg8:06FE-0774  MCIDeviceSetPosition  (FAR PASCAL)
 */
BOOL FAR PASCAL MCIDeviceSetPosition(DWORD dwPos)
{
    char    achReturn[80];
    char    ach[40];

    gdwMCIError = mciSendString("pause mciframes wait", achReturn, sizeof(achReturn), NULL);
    if (gdwMCIError)
        return FALSE;

    wsprintf(ach, "seek mciframes to %ld wait", dwPos);
    gdwMCIError = mciSendString(ach, achReturn, sizeof(achReturn), NULL);
    return gdwMCIError == 0;
}

/*
 * seg8:0776-07B2  MCIDevicePlay
 */
BOOL FAR MCIDevicePlay(void)
{
    char    ach[80];

    gdwMCIError = mciSendString("play mciframes", ach, sizeof(ach), NULL);
    return gdwMCIError == 0;
}

/*
 * seg8:07B4-07F0  MCIDevicePause
 */
BOOL FAR MCIDevicePause(void)
{
    char    ach[80];

    gdwMCIError = mciSendString("pause mciframes wait", ach, sizeof(ach), NULL);
    return gdwMCIError == 0;
}

/*
 * seg8:07F2-082E  MCIDeviceStop  (unreferenced)
 */
BOOL FAR MCIDeviceStop(void)
{
    char    ach[80];

    gdwMCIError = mciSendString("stop mciframes wait", ach, sizeof(ach), NULL);
    return gdwMCIError == 0;
}

/*
 * seg8:0830-087A  MCIDeviceStep  (FAR PASCAL)
 */
BOOL FAR PASCAL MCIDeviceStep(BOOL fForward)
{
    char    ach[80];

    gdwMCIError = mciSendString(fForward ? "step mciframes wait" : "step mciframes reverse wait",
                                ach, sizeof(ach), NULL);
    return gdwMCIError == 0;
}

/*
 * seg8:087C-08AA  MCIDeviceFreeze
 *
 * Holds the device's picture while a frame is grabbed.
 */
void FAR MCIDeviceFreeze(BOOL fFreeze)
{
    mciSendString(fFreeze ? "freeze mciframes wait" : "unfreeze mciframes wait", NULL, 0, NULL);
}

/*
 * seg8:08AC-08C4  MCIOpen
 */
BOOL FAR MCIOpen(void)
{
    return MCIDeviceOpen(gachMCIDevice);
}

/*
 * seg8:08C6-08DA  MCIClose
 */
void FAR MCIClose(void)
{
    MCIDeviceClose();
}

/*
 * seg8:08DC-0965  MCIMsToTime  (FAR PASCAL)
 *
 * Formats a time in ms as hh:mm:ss.hh.
 */
void FAR PASCAL MCIMsToTime(DWORD dwMS, LPSTR lpsz)
{
    DWORD   dwSec;
    UINT    nHours;
    UINT    nMinutes;

    dwSec = dwMS / 1000;
    nHours = (UINT)(dwSec / 3600);
    nMinutes = (UINT)((dwSec - nHours * 3600) / 60);
    wsprintf(lpsz, "%02u:%02u:%02u.%02lu", nHours, nMinutes,
             (UINT)(dwSec - nHours * 3600) - nMinutes * 60,
             (dwMS - dwSec * 1000) / 10);
}

/*
 * seg8:0968-0AF6  MCITimeToMs  (NEAR PASCAL)
 *
 * Parses [[hh:]mm:]ss[.hh] (at most 11 characters; more than two digits
 * after the point are cut) into ms, -1 if not valid.  Relies on reading
 * DS:0 (always 0) through the NULL that strchr/strrchr return when the
 * character isn't there.
 */
static DWORD NEAR PASCAL MCITimeToMs(LPSTR lpsz)
{
    char    ach[12];
    char    *p;
    char    *pDot;
    long    lHours;
    long    lMinutes;
    long    lSeconds;
    UINT    nHundredths;

    lHours = 0;
    lMinutes = 0;
    nHundredths = 0;

    lstrcpyn(ach, lpsz, sizeof(ach));
    if (ach[0] == 0)
        return (DWORD)-1L;

    for (p = ach; *p; p++) {
        if (!isdigit(*p) && *p != '.' && *p != ':')
            return (DWORD)-1L;
    }

    pDot = strchr(ach, '.');
    if (*pDot) {
        p = strrchr(ach, '.');
        if (p != pDot)
            return (DWORD)-1L;
        p++;
        if (strlen(p) > 2)
            p[2] = 0;
        nHundredths = atoi(p);
        *pDot = 0;
    }

    p = strrchr(ach, ':');
    lSeconds = atoi(*p ? p + 1 : ach);
    if (*p) {
        *p = 0;
        p = strrchr(ach, ':');
        lMinutes = atoi(*p ? p + 1 : ach);
        if (*p) {
            *p = 0;
            lHours = atoi(ach);
        }
    }

    return ((((lHours * 60 + lMinutes) * 60 + lSeconds) * 100) + nHundredths) * 10;
}

/*
 * seg8:0AF8-0B91  GetMCIDevice
 *
 * The device chosen in the MCI Settings list; the items read "product,
 * name" and the name is what is after the last ", ".
 */
BOOL FAR GetMCIDevice(HWND hDlg, int FAR *lpiSel, LPSTR lpszDevice)
{
    char    ach[160];
    char    *pEnd;
    char    *p;
    HWND    hwnd;

    hwnd = GetDlgItem(hDlg, IDC_MCIDEVICE);
    *lpiSel = (int)SendMessage(hwnd, CB_GETCURSEL, 0, 0L);
    SendMessage(hwnd, CB_GETLBTEXT, *lpiSel, (LPARAM)(LPSTR)ach);

    pEnd = ach + lstrlen(ach);
    p = pEnd;
    if (pEnd > ach) {
        do {
            if (*p == ' ' && p[-1] == ',') {
                p++;
                break;
            }
            p--;
        } while (p > ach);
    }
    lstrcpy(lpszDevice, p);
    return TRUE;
}

static int  giMCIDevice = 0;            /* DS:04DC */
static BOOL gfMCIBusy = FALSE;          /* DS:04DE reading a position */

/*
 * seg8:0B92-1173  MCIDlgProc
 *
 * MCI Settings: the videodiscs and VCRs, play or step capture, the step
 * averaging and the start and stop times (Set Start/Set Stop read the
 * device's position).  Always returns FALSE.  Quirk: OK while a
 * position is being read is posted again but also carried out at once.
 */
BOOL FAR PASCAL MCIDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char                ach[160];
    char                achName[64];
    MCI_SYSINFO_PARMS   mcisip;
    MCI_OPEN_PARMS      mciop;
    MCI_INFO_PARMS      mciip;
    MCI_GENERIC_PARMS   mcigp;
    HWND                hwndList;
    DWORD               dw;
    BOOL                f;
    int                 n;
    int                 i;

    switch (msg) {
    case WM_INITDIALOG:
        if (gdwMCIStartTime == 0)
            gdwMCIStartTime = 10000;
        if (gdwMCIStopTime == 0)
            gdwMCIStopTime = 20000;
        gfMCIUseStart = gfMCIUseStop = TRUE;

        CheckRadioButton(hDlg, IDC_MCIPLAY, IDC_MCISTEP, gfMCIPlay ? IDC_MCIPLAY : IDC_MCISTEP);
        EnableWindow(GetDlgItem(hDlg, IDC_MCI2XSPATIAL), !gfMCIPlay);
        EnableWindow(GetDlgItem(hDlg, IDC_MCIAVERAGE), !gfMCIPlay);
        SetDlgItemInt(hDlg, IDC_MCIAVERAGE, gnAverageFrames, FALSE);
        CheckDlgButton(hDlg, IDC_MCI2XSPATIAL, gf2xSpatial);

        gdwMCIStopDlg = gdwMCIStopTime;
        gdwMCIStartDlg = gdwMCIStartTime;
        MCIMsToTime(gdwMCIStartTime, ach);
        SetDlgItemText(hDlg, IDC_MCISTARTTIME, ach);
        MCIMsToTime(gdwMCIStopTime, ach);
        SetDlgItemText(hDlg, IDC_MCISTOPTIME, ach);

        hwndList = GetDlgItem(hDlg, IDC_MCIDEVICE);
        mciop.dwCallback = 0;
        mciop.lpstrDeviceType = NULL;
        mciop.lpstrElementName = NULL;
        mciop.lpstrAlias = NULL;
        mciip.dwCallback = 0;
        mciip.lpstrReturn = ach;
        mciip.dwRetSize = 31;
        mcisip.dwCallback = 0;
        mcisip.lpstrReturn = achName;
        mcisip.dwRetSize = sizeof(achName);
        mcigp.dwCallback = 0;

        mcisip.wDeviceType = MCI_DEVTYPE_VIDEODISC;
        for (i = 0; i < gnMCIVideodiscs; i++) {
            mcisip.dwNumber = i + 1;
            mciSendCommand(0, MCI_SYSINFO, MCI_SYSINFO_NAME, (DWORD)(LPVOID)&mcisip);
            mciop.lpstrDeviceType = (LPCSTR)MAKELONG(MCI_DEVTYPE_VIDEODISC, i);
            if (mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE | MCI_WAIT,
                               (DWORD)(LPVOID)&mciop) == 0) {
                if (mciSendCommand(mciop.wDeviceID, MCI_INFO, MCI_INFO_PRODUCT | MCI_WAIT,
                                   (DWORD)(LPVOID)&mciip) == 0) {
                    lstrcat(ach, ", ");
                    lstrcat(ach, achName);
                    SendMessage(hwndList, CB_ADDSTRING, 0, (LPARAM)(LPSTR)ach);
                }
                mciSendCommand(mciop.wDeviceID, MCI_CLOSE, MCI_WAIT, (DWORD)(LPVOID)&mcigp);
            }
        }

        mcisip.wDeviceType = MCI_DEVTYPE_VCR;
        for (i = 0; i < gnMCIVCRs; i++) {
            mcisip.dwNumber = i + 1;
            mciSendCommand(0, MCI_SYSINFO, MCI_SYSINFO_NAME, (DWORD)(LPVOID)&mcisip);
            mciop.lpstrDeviceType = (LPCSTR)MAKELONG(MCI_DEVTYPE_VCR, i);
            if (mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE | MCI_WAIT,
                               (DWORD)(LPVOID)&mciop) == 0) {
                if (mciSendCommand(mciop.wDeviceID, MCI_INFO, MCI_INFO_PRODUCT | MCI_WAIT,
                                   (DWORD)(LPVOID)&mciip) == 0) {
                    lstrcat(ach, ", ");
                    lstrcat(ach, achName);
                    SendMessage(hwndList, CB_ADDSTRING, 0, (LPARAM)(LPSTR)ach);
                }
                mciSendCommand(mciop.wDeviceID, MCI_CLOSE, MCI_WAIT, (DWORD)(LPVOID)&mcigp);
            }
        }

        SendMessage(hwndList, CB_SETCURSEL, giMCIDevice, 0L);
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gfMCIBusy)
                PostMessage(hDlg, msg, wParam, lParam);
            SetFocus(GetDlgItem(hDlg, wParam));
            GetMCIDevice(hDlg, &giMCIDevice, gachMCIDevice);
            gfMCIPlay = IsDlgButtonChecked(hDlg, IDC_MCIPLAY);
            gdwMCIStartTime = gdwMCIStartDlg;
            gdwMCIStopTime = gdwMCIStopDlg;
            gf2xSpatial = IsDlgButtonChecked(hDlg, IDC_MCI2XSPATIAL);
            gnAverageFrames = GetDlgItemInt(hDlg, IDC_MCIAVERAGE, NULL, FALSE);
            EndDialog(hDlg, TRUE);
            break;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            break;

        case IDC_MCIAVERAGE:
            if (HIWORD(lParam) == EN_KILLFOCUS) {
                n = GetDlgItemInt(hDlg, wParam, NULL, FALSE);
                if (n < 1 || n > 100)
                    SetDlgItemInt(hDlg, wParam, 1, FALSE);
            }
            break;

        case IDC_MCIPLAY:
        case IDC_MCISTEP:
            f = IsDlgButtonChecked(hDlg, IDC_MCISTEP);
            EnableWindow(GetDlgItem(hDlg, IDC_MCI2XSPATIAL), f);
            EnableWindow(GetDlgItem(hDlg, IDC_MCIAVERAGE), f);
            break;

        case IDC_MCISTARTTIME:
        case IDC_MCISTOPTIME:
            if (HIWORD(lParam) == EN_KILLFOCUS) {
                GetDlgItemText(hDlg, wParam, ach, sizeof(ach));
                dw = MCITimeToMs(ach);
                if (dw == (DWORD)-1L) {
                    MessageBeep(0);
                    dw = (wParam == IDC_MCISTARTTIME) ? gdwMCIStartDlg : gdwMCIStopDlg;
                }
                if (wParam == IDC_MCISTARTTIME) {
                    gdwMCIStartDlg = dw;
                    MCIMsToTime(dw, ach);
                    SetDlgItemText(hDlg, IDC_MCISTARTTIME, ach);
                } else {
                    gdwMCIStopDlg = dw;
                    MCIMsToTime(dw, ach);
                    SetDlgItemText(hDlg, IDC_MCISTOPTIME, ach);
                }
            }
            break;

        case IDC_MCISETSTART:
        case IDC_MCISETSTOP:
            if (gfMCIBusy) {
                MessageBeep(0);
                break;
            }
            EnableWindow(GetDlgItem(hDlg, wParam), FALSE);
            gfMCIBusy = TRUE;
            GetMCIDevice(hDlg, &giMCIDevice, ach);
            if (MCIDeviceOpen(ach)) {
                if (wParam == IDC_MCISETSTART) {
                    if (MCIDeviceGetPosition(&gdwMCIStartDlg)) {
                        MCIMsToTime(gdwMCIStartDlg, ach);
                        SetDlgItemText(hDlg, IDC_MCISTARTTIME, ach);
                    } else {
                        MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_MCI);
                    }
                } else {
                    if (MCIDeviceGetPosition(&gdwMCIStopDlg)) {
                        MCIMsToTime(gdwMCIStopDlg, ach);
                        SetDlgItemText(hDlg, IDC_MCISTOPTIME, ach);
                    } else {
                        MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_MCI);
                    }
                }
                MCIDeviceClose();
            } else {
                MessageBeep(0);
            }
            gfMCIBusy = FALSE;
            EnableWindow(GetDlgItem(hDlg, wParam), TRUE);
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * seg8:1176-15CF  MCIStepCapture
 *
 * Capture/Video with Step Video: opens the device and the file (index
 * size raised to the maximum meanwhile), sets up averaging (a 2x format
 * on the driver for 2x spatial) and runs the MCI Step Capture dialog.
 * Averaging is only for uncompressed formats.  The device is stepped
 * forward and back once as a test.
 */
void FAR MCIStepCapture(void)
{
    char    ach[128];
    char    achFmt[128];
    DWORD   dwIndexSize;
    DWORD   dwCompression;
    BOOL    fMCIOpen;
    BOOL    fFileOpen;
    int     err;

    fMCIOpen = FALSE;
    fFileOpen = FALSE;
    err = 0;
    dwIndexSize = gdwIndexSize;
    gdwIndexSize = 0x51000L;

    StatusText(MAKEINTRESOURCE(IDS_STATUS_SETUP));
    ghcurOld = SetCursor(ghcurWait);

    gdwMCIError = 0;
    glpAverage = NULL;
    glpbi2x = NULL;
    gvh2x = gvhFrame;

    if (gnAverageFrames <= 0)
        gnAverageFrames = 1;
    dwCompression = glpbiCapture->biCompression;
    if (dwCompression)
        gnAverageFrames = 1;

    if (gf2xSpatial && dwCompression == 0) {
        gvh2x.lpData = NULL;
        glpbi2x = (LPBITMAPINFOHEADER)GlobalLock(GlobalAlloc(GHND,
                      sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD)));
        _fmemcpy(glpbi2x, glpbiCapture, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
        glpbi2x->biHeight *= 2;
        glpbi2x->biWidth *= 2;
        glpbi2x->biSizeImage *= 4;
        if (SetDriverFormat(glpbi2x, sizeof(BITMAPINFOHEADER)) == 0) {
            gvh2x.lpData = (LPBYTE)GlobalLock(GlobalAlloc(GHND, glpbi2x->biSizeImage));
            gvh2x.dwBufferLength = glpbi2x->biSizeImage;
        }
        if (gvh2x.lpData == NULL) {
            SetDriverFormat(glpbiCapture, sizeof(BITMAPINFOHEADER));
            gf2xSpatial = FALSE;
            gvh2x = gvhFrame;
        }
    } else {
        gf2xSpatial = FALSE;
    }

    if (gCompVars.hic) {
        if (!ICSeqCompressFrameStart(&gCompVars, (LPBITMAPINFO)glpbiCapture)) {
            MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_COMPRESSOR);
            gfCapFramesOK = FALSE;
            goto Cleanup;
        }
        gCompVars.lpBitsOut = (LPBYTE)gCompVars.lpBitsOut + 8;
    }

    if (MCIOpen()) {
        fMCIOpen = TRUE;
        if (MCIDeviceStep(TRUE)) {
            MCIDeviceStep(FALSE);
            if (CapFramesInit((LPBITMAPINFOHEADER)gCompVars.lpbiOut))
                fFileOpen = TRUE;
            else
                err = IDS_ERR_INIT;
        } else {
            err = IDS_ERR_MCISTEP;
        }
    } else {
        err = IDS_ERR_MCI;
    }
    if (err)
        goto Cleanup;

    if (gf2xSpatial || gnAverageFrames != 1) {
        StatusText(MAKEINTRESOURCE(IDS_BUILDPALMAP));
        if (!AverageFrameInit(&glpAverage, glpbiCapture, ghpalCurrent)) {
            err = IDS_OUTOFMEMORY;
            goto Cleanup;
        }
        StatusText(NULL);
    }
    StatusText(NULL);
    SetCursor(ghcurOld);

    gfCapturing = TRUE;
    DoDialogParam(IDD_MCISTEP, ghwndApp, (FARPROC)MCIStepDlgProc, 0L);
    gfCapturing = FALSE;

    if (!gfCapFramesOK)
        err = IDS_ERR_RECORDING;
    AverageFrameFini(glpAverage);

Cleanup:
    if (gCompVars.hic) {
        if (gCompVars.lpBitsOut)
            gCompVars.lpBitsOut = (LPBYTE)gCompVars.lpBitsOut - 8;
        ICSeqCompressFrameEnd(&gCompVars);
    }

    if (gdwMCIError) {
        mciGetErrorString(gdwMCIError, ach, sizeof(ach));
        StatusText(ach);
    }

    if (gf2xSpatial) {
        SetDriverFormat(glpbiCapture, sizeof(BITMAPINFOHEADER));
        GlobalUnlock(GlobalHandle(SELECTOROF(gvh2x.lpData)));
        GlobalFree(GlobalHandle(SELECTOROF(gvh2x.lpData)));
    }
    if (glpbi2x) {
        GlobalUnlock(GlobalHandle(SELECTOROF(glpbi2x)));
        GlobalFree(GlobalHandle(SELECTOROF(glpbi2x)));
    }

    if (fFileOpen)
        CapFramesFini((LPBITMAPINFOHEADER)gCompVars.lpbiOut, gfCaptureAudio, FALSE);
    if (fMCIOpen)
        MCIClose();

    if (!gfPreview && !gfOverlay) {
        InvalidateRect(ghwndShow, NULL, TRUE);
        UpdateWindow(ghwndShow);
    }

    if (err) {
        LoadString(ghInst, err, ach, sizeof(ach));
        ErrMsg(ach);
        StatusText(NULL);
    } else if (gfCaptureAudio && gfAudioLost) {
        MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_AUDIOLOST);
    } else {
        LoadString(ghInst, IDS_STATUS_CAPTURED, achFmt, sizeof(achFmt));
        wsprintf(ach, achFmt, gdwVideoChunks);
        StatusText(ach);
    }

    gdwIndexSize = dwIndexSize;
}

/*
 * seg8:15D0-1DB4  MCIStepDlgProc
 *
 * MCI Step Capture, run by posted commands and a 33 ms timer:
 *   IDC_STEPSTART  seek to the start, note the position
 *   timer          while stepping: grab every frame whose time the
 *                  position has passed (device frozen meanwhile), stop at
 *                  the time limit or the stop time, else step once; while
 *                  recording audio: stop 100 ms before the end position
 *   IDC_STEPSTOP   (Stop) end stepping, go on to the audio
 *   IDC_STEPAUDIO  replay from the start position recording audio
 *   IDC_STEPFRAME  grab, average and write one frame
 *   IDC_STEPCANCEL (Cancel) end
 * Every fifth audio buffer also runs the timer code.  Returns TRUE for
 * everything it handles; WM_CLOSE only kills the timer.
 */
BOOL FAR PASCAL MCIStepDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char        ach[64];
    char        achFmt[64];
    MMCKINFO    ck;
    LPWAVEHDR   lpwh;
    LPVOID      lpData;
    LONG        lSize;
    BOOL        fKey;
    BOOL        f;
    int         i;

    switch (msg) {
    case WM_CLOSE:
        if (gidStepTimer)
            KillTimer(hDlg, IDT_STEP);
        return TRUE;

    case WM_INITDIALOG:
        PositionDialog(hDlg, ghwndShow);
        gfCapFramesOK = TRUE;
        gfCapFramesStarted = FALSE;
        gfStepping = FALSE;
        gfAudioRecording = FALSE;
        gfWaveOpened = FALSE;

        MCIMsToTime(gdwMCIStartTime, ach);
        SetDlgItemText(hDlg, IDC_STEPSTARTTIME, ach);
        SetDlgItemText(hDlg, IDC_STEPCURTIME, ach);
        MCIMsToTime(gdwMCIStopTime, ach);
        SetDlgItemText(hDlg, IDC_STEPSTOPTIME, ach);
        SetDlgItemText(hDlg, IDC_STEPSTATUS, "");

        if (gfMCIUseStart)
            PostMessage(hDlg, WM_COMMAND, IDC_STEPSTART, 0L);
        gidStepTimer = SetTimer(hDlg, IDT_STEP, STEP_TICK, NULL);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_STEPSTART:
            gfStepping = TRUE;
            if (gfMCIUseStart && !MCIDeviceSetPosition(gdwMCIStartTime)) {
                gfCapFramesOK = FALSE;
                PostMessage(hDlg, WM_COMMAND, IDC_STEPCANCEL, 0L);
            }
            if (!MCIDeviceGetPosition(&gdwMCIStartPos))
                gfCapFramesOK = FALSE;
            return TRUE;

        case IDC_STEPSTOP:
            if (gfAudioRecording)
                goto PostCancel;
            gdwMCIEndPos = gdwMCIPos;
            gfStepping = FALSE;
            if (!gfCaptureAudio)
                goto PostCancel;
            PostMessage(hDlg, WM_COMMAND, IDC_STEPAUDIO, 0L);
            return TRUE;

        case IDC_STEPCANCEL:
            gfStepping = FALSE;
            gfAudioRecording = FALSE;
            if (gfWaveOpened) {
                waveInStop(ghWaveIn);
                waveInReset(ghWaveIn);
                AVIAudioFini();
            }
            EndDialog(hDlg, TRUE);
            return TRUE;

        case IDC_STEPAUDIO:
            LoadString(ghInst, IDS_STATUS_AUDIO, ach, sizeof(ach));
            SetDlgItemText(hDlg, IDC_STEPSTATUS, ach);
            MCIDeviceSetPosition(gdwMCIStartPos);
            gfStepping = FALSE;
            if (!gfCapFramesOK || !gfCaptureAudio)
                return TRUE;

            if (!AVIAudioInit(hDlg) && !AVIAudioPrepare())
                gfAudioRecording = TRUE;
            if (!gfAudioRecording) {
                AVIAudioFini();
                gfCapFramesOK = FALSE;
                return TRUE;
            }
            gfWaveOpened = TRUE;
            gfCapFramesOK = FALSE;
            if (MCIDevicePlay())
                gfCapFramesOK = TRUE;
            if (gfCapFramesOK)
                waveInStart(ghWaveIn);
            else
                gfAudioRecording = FALSE;
            return TRUE;

        case IDC_STEPFRAME:
            if (glpAverage) {
                AverageFrameStart(glpAverage);
                for (i = 0; i < gnAverageFrames; i++) {
                    videoFrame(ghVideoIn, &gvh2x);
                    if (gf2xSpatial)
                        AverageShrink2x(glpAverage, glpbi2x, gvh2x.lpData, glpbiCapture, gvhFrame.lpData);
                    AverageFrameAdd(glpAverage, glpFrameBits);
                }
                AverageFrameEnd(glpAverage, glpFrameBits);
            } else {
                videoFrame(ghVideoIn, &gvhFrame);
            }
            InvalidateRect(ghwndShow, NULL, TRUE);
            UpdateWindow(ghwndShow);

            if (gCompVars.hic) {
                lpData = ICSeqCompressFrame(&gCompVars, 0, glpFrameBits, &fKey, &lSize);
            } else {
                lSize = gvhFrame.dwBytesUsed;
                fKey = (BOOL)(gvhFrame.dwFlags & VHDR_KEYFRAME);
                lpData = glpFrameBits;
            }

            ck.cksize = lSize;
            ck.ckid = MAKEAVICKID(cktypeDIBbits, 0);
            ck.fccType = 0;
            if (mmioCreateChunk(ghmmio, &ck, 0))
                gfCapFramesOK = FALSE;
            if (gfCapFramesOK && mmioWrite(ghmmio, (HPSTR)lpData, lSize) != lSize)
                gfCapFramesOK = FALSE;
            if (gfCapFramesOK && mmioAscend(ghmmio, &ck, 0))
                gfCapFramesOK = FALSE;
            if (!IndexVideo(lSize, fKey))
                gfCapFramesOK = FALSE;

            LoadString(ghInst, IDS_STATUS_LNFRAMES, achFmt, sizeof(achFmt));
            wsprintf(ach, achFmt, gdwVideoChunks);
            SetDlgItemText(hDlg, IDC_STEPSTATUS, ach);

            if (!gfCapFramesOK) {
                SendMessage(hDlg, WM_COMMAND, IDC_STEPCANCEL, 0L);
                return TRUE;
            }
            if (!gfCapFramesStarted)
                gfCapFramesStarted = TRUE;
            return TRUE;
        }
        return FALSE;

    case WM_TIMER:
        if (wParam != IDT_STEP)
            return TRUE;
        goto Tick;

    case MM_WIM_DATA:
        lpwh = (LPWAVEHDR)lParam;
        if (lpwh->dwBytesRecorded == 0)
            return TRUE;

        ck.cksize = lpwh->dwBytesRecorded;
        ck.ckid = MAKEAVICKID(cktypeWAVEbytes, 1);
        ck.fccType = 0;
        if (mmioCreateChunk(ghmmio, &ck, 0))
            gfAudioRecording = gfCapFramesOK = FALSE;
        if (gfCapFramesOK &&
            mmioWrite(ghmmio, (HPSTR)lpwh->lpData, lpwh->dwBytesRecorded) != (LONG)lpwh->dwBytesRecorded)
            gfAudioRecording = gfCapFramesOK = FALSE;
        if (gfCapFramesOK && mmioAscend(ghmmio, &ck, 0))
            gfAudioRecording = gfCapFramesOK = FALSE;
        if (IndexAudio(lpwh->dwBytesRecorded))
            gdwWaveBytes += lpwh->dwBytesRecorded;
        else
            gfCapFramesOK = FALSE;

        if (gfAudioRecording) {
            f = FALSE;
            for (i = 0; i < 4; i++) {
                if (!(galpwh[i]->dwFlags & WHDR_DONE))
                    f = TRUE;
            }
            if (!f)
                gfAudioLost = TRUE;
        }
        waveInAddBuffer(ghWaveIn, lpwh, sizeof(WAVEHDR));
        if (gdwWaveChunks % 5)
            return TRUE;
        goto Tick;

    default:
        return FALSE;
    }

Tick:
    if (gfStepping) {
        while (gfCapFramesOK) {
            if (!MCIDeviceGetPosition(&gdwMCIPos))
                gfCapFramesOK = FALSE;

            if ((gfLimitEnabled && gdwMCIPos - gdwMCIStartPos > (DWORD)gwTimeLimit * 1000) ||
                (gfMCIUseStop && gdwMCIPos > gdwMCIStopTime)) {
                gdwMCIEndPos = gdwMCIPos;
                gfStepping = FALSE;
                PostMessage(hDlg, WM_COMMAND, gfCaptureAudio ? IDC_STEPAUDIO : IDC_STEPCANCEL, 0L);
            } else {
                if (muldiv32(gdwVideoChunks, gdwMicroSecPerFrame, 1000L) + gdwMCIStartPos >= gdwMCIPos)
                    break;
                MCIDeviceFreeze(TRUE);
                SendMessage(hDlg, WM_COMMAND, IDC_STEPFRAME, 0L);
                MCIDeviceFreeze(FALSE);
            }
            if (!gfStepping)
                break;
        }
        if (gfStepping && mciSendString("step mciframes wait", NULL, 0, NULL))
            gfCapFramesOK = FALSE;
        if (!gfCapFramesOK)
            PostMessage(hDlg, WM_COMMAND, IDC_STEPCANCEL, 0L);
    } else if (gfAudioRecording && MCIDeviceGetPosition(&gdwMCIPos) &&
               gdwMCIEndPos - 100 <= gdwMCIPos) {
        gfAudioRecording = FALSE;
        waveInStop(ghWaveIn);
        waveInReset(ghWaveIn);
        MCIDevicePause();
        goto PostCancel;
    }

    MCIMsToTime(gdwMCIPos, ach);
    SetDlgItemText(hDlg, IDC_STEPCURTIME, ach);
    if (glpAverage == NULL && gfPreview) {
        videoFrame(ghVideoIn, &gvhFrame);
        InvalidateRect(ghwndShow, NULL, TRUE);
        UpdateWindow(ghwndShow);
    }
    return TRUE;

PostCancel:
    PostMessage(hDlg, WM_COMMAND, IDC_STEPCANCEL, 0L);
    return TRUE;
}
