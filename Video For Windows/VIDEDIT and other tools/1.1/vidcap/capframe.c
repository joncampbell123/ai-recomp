/*
 * capframe.c - code segment 8 of VIDCAP.EXE, 0000-05A5: Capture/Frames,
 * capturing single frames on request into the capture file, and the
 * setting up and finishing of the capture file shared with MCI step
 * capture (mcicap.c).
 *
 * Frames are written as plain 00db chunks (no JUNK padding) and indexed
 * as they come.  Functions are FAR CDECL unless noted.
 */

#include <windows.h>
#include <mmsystem.h>
#include <time.h>
#include "vidcap.h"

UINT        gidBkColor      = IDC_BKDEFAULT;    /* DS:0364 Preferences background */
LPAVERAGE   glpAverage      = NULL;             /* DS:0366 step capture averaging */
int         gnAverageFrames = 1;                /* DS:036A [VidCap] AverageFrames */
BOOL        gf2xSpatial     = FALSE;            /* DS:036C [VidCap] StepCaptureAt2x */

BOOL        gfCapFramesOK;                      /* DS:144E no error so far */
BOOL        gfCapFramesStarted;                 /* DS:1450 a frame has been captured */

/*
 * seg8:0000-009F  MCIMsToSMPTE  (FAR PASCAL)
 *
 * Formats a time in ms as hh:mm:ss:ff, counting 25 frames a second
 * (from hundredths).  nHours * 3600 is a 16-bit product, wrong from 19
 * hours on.
 */
void FAR PASCAL MCIMsToSMPTE(DWORD dwMS, LPSTR lpsz)
{
    DWORD   dwSec;
    UINT    nHours;
    UINT    nMinutes;

    dwSec = dwMS / 1000;
    nHours = (UINT)(dwSec / 3600);
    nMinutes = (UINT)((dwSec - nHours * 3600) / 60);
    wsprintf(lpsz, "%02u:%02u:%02u:%02lu", nHours, nMinutes,
             (UINT)(dwSec - nHours * 3600) - nMinutes * 60,
             (LONG)((dwMS - dwSec * 1000) / 10 * 25) / 100);
}

/*
 * seg8:00A2-01AB  CapFramesInit
 *
 * Opens the capture file for frame capture and records when (IDIT) and,
 * under MCI control, where on the source (ISMT) the capture starts.
 */
BOOL FAR CapFramesInit(LPBITMAPINFOHEADER lpbi)
{
    char            ach[40];
    CAPINFOCHUNK    cic;
    DWORD           dwSize;

    dwSize = lpbi ? lpbi->biSizeImage : glpbiCapture->biSizeImage;
    InitIndex(dwSize + 8, CalcWaveBufferSize());
    ghmmio = AVIFileInit(lpbi);

    cic.fccInfoID = mmioFOURCC('I', 'S', 'M', 'T');
    cic.lpData = NULL;
    cic.cbData = 0;
    SetInfoChunk(&cic);

    if (gfMCIControl) {
        MCIMsToSMPTE(gdwMCIStartTime, ach);
        cic.lpData = ach;
        cic.cbData = lstrlen(ach) + 1;
        SetInfoChunk(&cic);
    }

    cic.fccInfoID = mmioFOURCC('I', 'D', 'I', 'T');
    time(&gtimeCapture);
    cic.lpData = (LPSTR)ctime(&gtimeCapture);
    cic.cbData = 26;
    SetInfoChunk(&cic);

    return ghmmio != NULL;
}

/*
 * seg8:01AC-01D7  CapFramesFini
 */
void FAR CapFramesFini(LPBITMAPINFOHEADER lpbi, BOOL fSound, BOOL fJunkChunkWritten)
{
    AVIFileFini(lpbi, ghmmio, fSound, fJunkChunkWritten);
    ghmmio = NULL;
}

/*
 * seg8:01D8-0345  CaptureFrames
 *
 * Capture/Frames: the Capture Frames dialog grabs a frame per click.
 * A failure to start reports "Error while recording" after its own
 * message.
 */
void FAR CaptureFrames(void)
{
    char    ach[64];
    char    achFmt[64];

    StatusText(MAKEINTRESOURCE(IDS_STATUS_SETUP));
    ghcurOld = SetCursor(ghcurWait);

    if (gCompVars.hic) {
        if (!ICSeqCompressFrameStart(&gCompVars, (LPBITMAPINFO)glpbiCapture)) {
            MsgBoxID(ghwndApp, ghInst, MB_OK | MB_ICONEXCLAMATION, IDS_APPNAME, IDS_ERR_COMPRESSOR);
            goto Error;
        }
        gCompVars.lpBitsOut = (LPBYTE)gCompVars.lpBitsOut + 8;
    }

    if (!CapFramesInit((LPBITMAPINFOHEADER)gCompVars.lpbiOut)) {
        StatusText(NULL);
        SetCursor(ghcurOld);
        goto Error;
    }

    StatusText(NULL);
    SetCursor(ghcurOld);
    DoDialogParam(IDD_CAPFRAMES, ghwndApp, (FARPROC)CapFrameDlgProc, 0L);
    CapFramesFini((LPBITMAPINFOHEADER)gCompVars.lpbiOut, FALSE, FALSE);
    if (!gfPreview) {
        InvalidateRect(ghwndShow, NULL, TRUE);
        UpdateWindow(ghwndShow);
    }
    goto Done;

Error:
    gfCapFramesOK = FALSE;

Done:
    if (gCompVars.hic) {
        if (gCompVars.lpBitsOut)
            gCompVars.lpBitsOut = (LPBYTE)gCompVars.lpBitsOut - 8;
        ICSeqCompressFrameEnd(&gCompVars);
    }

    if (!gfCapFramesOK) {
        LoadString(ghInst, IDS_ERR_RECORDING, ach, sizeof(ach));
        ErrMsg(ach);
    } else {
        LoadString(ghInst, IDS_STATUS_CAPTURED, achFmt, sizeof(achFmt));
        wsprintf(ach, achFmt, gdwVideoChunks);
        StatusText(ach);
    }
}

/*
 * seg8:0346-05A2  CapFrameDlgProc  (exported ordinal 14)
 *
 * Capture grabs a frame (compressed if a compressor is chosen) and
 * appends it; after the first, Cancel becomes Close.  An error ends the
 * dialog.  Returns a LONG (DX cleared).
 */
BOOL FAR PASCAL CapFrameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char        achFmt[128];
    char        ach[128];
    char        achCount[64];
    MMCKINFO    ck;
    LPVOID      lpData;
    LONG        lSize;
    BOOL        fKey;

    switch (msg) {
    case WM_INITDIALOG:
        LoadString(ghInst, IDS_CONFIRMFRAMES, achFmt, sizeof(achFmt));
        wsprintf(ach, achFmt, (LPSTR)gachCaptureFile);
        SetDlgItemText(hDlg, IDC_CAPFRAMESTEXT, ach);
        gfCapFramesStarted = FALSE;
        gfCapFramesOK = TRUE;
        PositionDialog(hDlg, ghwndShow);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        case IDC_CAPFRAMESCAPTURE:
            videoFrame(ghVideoIn, &gvhFrame);
            if (gvhFrame.dwBytesUsed == 0) {
                gvhFrame.dwBytesUsed = glpbiCapture->biSizeImage;
                gvhFrame.dwFlags |= VHDR_KEYFRAME;
            }

            if (gCompVars.hic) {
                lSize = 0;
                lpData = ICSeqCompressFrame(&gCompVars, 0, glpFrameBits, &fKey, &lSize);
            } else {
                lSize = gvhFrame.dwBytesUsed;
                fKey = (BOOL)(gvhFrame.dwFlags & VHDR_KEYFRAME);
                lpData = glpFrameBits;
            }

            if (!gfPreview) {
                InvalidateRect(ghwndShow, NULL, TRUE);
                UpdateWindow(ghwndShow);
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

            LoadString(ghInst, IDS_STATUS_LNFRAMES, achCount, sizeof(achCount));
            wsprintf(achFmt, achCount, gdwVideoChunks);
            SetDlgItemText(hDlg, IDC_CAPFRAMESCOUNT, achFmt);

            if (!gfCapFramesOK) {
                EndDialog(hDlg, TRUE);
                return TRUE;
            }
            if (!gfCapFramesStarted) {
                gfCapFramesStarted = TRUE;
                LoadString(ghInst, IDS_CLOSEFRAMES, achFmt, sizeof(achFmt));
                SetDlgItemText(hDlg, IDCANCEL, achFmt);
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}
