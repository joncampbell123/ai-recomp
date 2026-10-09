/*
 * stats.c - code segment 17 of VIDEDIT.EXE (0x06C9 bytes):
 * Video/Statistics.
 *
 * The dialog (IDD_STATISTICS) shows the length, duration, frame rate,
 * data rate and formats right away, then adds up the video and audio
 * sizes of the frames on a 50 ms timer, 25 frames per tick, showing
 * "Calculating: n / total" until it's done.
 *
 * Data: DGROUP module at DS:0C04-0C7B (format strings only) and
 * DS:25D6-25E5.  Functions are in the order of the binary.
 */

#include "videdit.h"
#include "compress.h"
#include "stats.h"

/* dialog controls */
#define IDC_DATARATE            102
#define IDC_FRAMERATE           103
#define IDC_ESTSIZE             104
#define IDC_VIDEOSIZE           105
#define IDC_AUDIOSIZE           106
#define IDC_AUDIOFORMAT         107
#define IDC_VIDEOFORMAT         108
#define IDC_INTERLEAVE          109
#define IDC_LENGTH              110
#define IDC_DURATION            111
#define IDC_CALCLABEL           116
#define IDC_CALCFRAME           117
#define IDC_CALCTOTAL           118
#define IDC_CALCSLASH           119
#define IDC_STATSFROM           120

#define IDT_STATS               1

/* string table */
#define IDS_STATSCURRENT        461     /* "Statistics from current session" */
#define IDS_FRSEC               463     /* "fr/sec" */
#define IDS_KSEC                464     /* "K/sec" */
#define IDS_FRAMES              465     /* "frames" */
#define IDS_BIT                 466     /* "bit" */
#define IDS_MONO                467     /* "mono" */
#define IDS_STEREO              468     /* "stereo" */
#define IDS_KHZ                 469     /* "kHz" */
#define IDS_NOAUDIO             470     /* "No audio" */
#define IDS_K                   471     /* "K" */
#define IDS_FRAME               472     /* "frame" */

/* uninitialised data */
WORD        gidStatsTimer;              /* DS:25D6 */
WORD        gfStatsDone;                /* DS:25D8 */
WORD        gwStatsFrame;               /* DS:25DA next frame to add up */
DWORD       gdwStatsVideo;              /* DS:25DE */
DWORD       gdwStatsAudio;              /* DS:25E2 */

/*
 * s17:0000-0081  StatsShowVideoFormat  (NEAR PASCAL)
 *
 * "8 bit  Microsoft Video 1": bits of frame 0 (8 if there is none) and
 * the compressor.
 */
static void NEAR PASCAL StatsShowVideoFormat(HWND hDlg, BOOL fCurrent)
{
    char    ach[128];                   /* [bp-9E] */
    char    achBit[25];                 /* [bp-1E] */
    LPSTR   lpszCompressor;             /* [bp-04] */
    HGLOBAL h;
    int     nBits;

    lpszCompressor = GetCompressorName(fCurrent);
    h = (HGLOBAL)GetFullFrame(0, NULL, 0x40);
    if (h) {
        nBits = ((LPBITMAPINFOHEADER)GlobalLock(h))->biBitCount;
        GlobalUnlock(h);
    } else {
        nBits = 8;
    }
    LoadString(ghInst, IDS_BIT, achBit, sizeof(achBit));
    wsprintf(ach, "%d %s  %s", nBits, (LPSTR)achBit, lpszCompressor);
    SetDlgItemText(hDlg, IDC_VIDEOFORMAT, ach);
}

/*
 * s17:0082-0151  StatsShowAudioFormat  (NEAR PASCAL)
 *
 * "16 bit  22 kHz  mono", or "No audio".  fCurrent is not used.
 */
static void NEAR PASCAL StatsShowAudioFormat(HWND hDlg, BOOL fCurrent)
{
    char    ach[128];                   /* [bp-C4] */
    char    achKHz[20];                 /* [bp-44] */
    char    achBit[20];                 /* [bp-30] */
    char    achChannels[20];            /* [bp-1C] */
    DWORD   dwRate;                     /* [bp-08] */
    WORD    wChannels;                  /* [bp-04] */
    WORD    wBits;                      /* [bp-02] */

    if (WaveGetFormatInfo(&wBits, &dwRate, &wChannels)) {
        LoadString(ghInst, IDS_BIT, achBit, sizeof(achBit));
        LoadString(ghInst, IDS_KHZ, achKHz, sizeof(achKHz));
        LoadString(ghInst, wChannels == 1 ? IDS_MONO : IDS_STEREO, achChannels, sizeof(achChannels));
        wsprintf(ach, "%d %s  %d %s  %s", wBits, (LPSTR)achBit, (int)(dwRate / 1000),
                 (LPSTR)achKHz, (LPSTR)achChannels);
    } else {
        LoadString(ghInst, IDS_NOAUDIO, ach, sizeof(ach));
    }
    SetDlgItemText(hDlg, IDC_AUDIOFORMAT, ach);
}

/*
 * s17:0152-0697  StatsDlgProc  (FAR PASCAL, loads DS from SS, not
 * exported, returns a LONG)
 *
 * Quirks of the original:
 *  - The wait cursor is loaded from VidEdit's own instance, which has
 *    no such cursor, so the cursor disappears while the dialog starts.
 *  - If the sizes of a frame can't be had, the counts so far are shown
 *    and the timer restarted, which tries the same frame again forever.
 *  - "Calculating" shows the number of the frame before the next one.
 */
LONG FAR PASCAL _loadds StatsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char    achUnit[128];               /* [bp-112] */
    char    ach[128];                   /* [bp-92] */
    DWORD   dwPalette;                  /* [bp-12] */
    DWORD   dwOther;                    /* [bp-0E] */
    DWORD   dwAudio;                    /* [bp-0A] */
    DWORD   dwVideo;                    /* [bp-06] */
    HCURSOR hcurOld;                    /* [bp-02] */
    int     n;

    switch (msg) {
    case WM_INITDIALOG:
        hcurOld = SetCursor(LoadCursor(ghInst, IDC_WAIT));
        ShowWindow(GetDlgItem(hDlg, IDC_STATSFROM), SW_HIDE);
        gwStatsFrame = 0;
        wsprintf(ach, "%d", 0);
        FrameBoxSetText(GetDlgItem(hDlg, IDC_CALCFRAME), ach);
        wsprintf(ach, "%d", gwLengthShown);
        FrameBoxSetText(GetDlgItem(hDlg, IDC_CALCTOTAL), ach);

        LoadString(ghInst, gwLengthShown == 1 ? IDS_FRAME : IDS_FRAMES, achUnit, sizeof(achUnit));
        wsprintf(ach, "%d %s", gwLengthShown, (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_LENGTH, ach);
        FormatFrameTime(gwLengthShown, ach);
        SetDlgItemText(hDlg, IDC_DURATION, ach);

        LoadString(ghInst, IDS_KSEC, achUnit, sizeof(achUnit));
        wsprintf(ach, "%lu %s", gCompOptions.dwDataRate >> 10, (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_DATARATE, ach);

        LoadString(ghInst, IDS_FRSEC, achUnit, sizeof(achUnit));
        FormatFrameRate(ach, gCompOptions.dwUSecPerFrame);
        lstrcat(ach, " ");
        lstrcat(ach, achUnit);
        SetDlgItemText(hDlg, IDC_FRAMERATE, ach);

        LoadString(ghInst, IDS_K, achUnit, sizeof(achUnit));
        gdwStatsAudio = gdwStatsVideo = 0;
        wsprintf(ach, "0 %s", (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_VIDEOSIZE, ach);
        wsprintf(ach, "0 %s", (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_AUDIOSIZE, ach);
        wsprintf(ach, "0 %s", (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_ESTSIZE, ach);

        StatsShowVideoFormat(hDlg, TRUE);
        StatsShowAudioFormat(hDlg, TRUE);

        LoadString(ghInst, gCompOptions.dwInterleave == 1 ? IDS_FRAME : IDS_FRAMES,
                   achUnit, sizeof(achUnit));
        wsprintf(ach, "%lu %s", gCompOptions.dwInterleave, (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_INTERLEAVE, ach);

        gfStatsDone = 0;
        if (gwLengthShown) {
            gidStatsTimer = SetTimer(hDlg, IDT_STATS, 50, NULL);
            if (!gidStatsTimer)
                PostMessage(hDlg, WM_TIMER, IDT_STATS, 0L);
        } else {
            gfStatsDone = 1;
        }
        SetCursor(hcurOld);
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gidStatsTimer)
                KillTimer(hDlg, IDT_STATS);
            EndDialog(hDlg, TRUE);
            break;
        case IDCANCEL:
            if (gidStatsTimer)
                KillTimer(hDlg, IDT_STATS);
            EndDialog(hDlg, FALSE);
            break;
        }
        break;

    case WM_TIMER:
        gidStatsTimer = 0;
        KillTimer(hDlg, IDT_STATS);
        for (n = 0; ; ) {
            if (!GetFrameSizes(gwStatsFrame, &dwVideo, &dwAudio, &dwOther, &dwPalette))
                goto progress;
            gdwStatsVideo += dwVideo;
            gdwStatsAudio += dwAudio;
            if (++gwStatsFrame == gwLengthShown)
                break;
            if (++n >= 25)
                goto progress;
        }

        ShowWindow(GetDlgItem(hDlg, IDC_CALCLABEL), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_CALCFRAME), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_CALCSLASH), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_CALCTOTAL), SW_HIDE);
        LoadString(ghInst, IDS_STATSCURRENT, ach, sizeof(ach));
        SetDlgItemText(hDlg, IDC_STATSFROM, ach);
        ShowWindow(GetDlgItem(hDlg, IDC_STATSFROM), SW_SHOWNA);
        gfStatsDone = 1;
        goto sizes;

    progress:
        wsprintf(ach, "%d", gwStatsFrame - 1);
        FrameBoxSetText(GetDlgItem(hDlg, IDC_CALCFRAME), ach);
        wsprintf(ach, "%d", gwLengthShown);
        FrameBoxSetText(GetDlgItem(hDlg, IDC_CALCTOTAL), ach);

    sizes:
        LoadString(ghInst, IDS_K, achUnit, sizeof(achUnit));
        wsprintf(ach, "%lu %s", (gdwStatsVideo + 1023) >> 10, (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_VIDEOSIZE, ach);
        wsprintf(ach, "%lu %s", (gdwStatsAudio + 1023) >> 10, (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_AUDIOSIZE, ach);
        wsprintf(ach, "%lu %s", (gdwStatsAudio + gdwStatsVideo + 1023) >> 10, (LPSTR)achUnit);
        SetDlgItemText(hDlg, IDC_ESTSIZE, ach);
        if (!gfStatsDone) {
            gidStatsTimer = SetTimer(hDlg, IDT_STATS, 50, NULL);
            if (!gidStatsTimer)
                SendMessage(hDlg, WM_TIMER, IDT_STATS, 0L);
        }
        break;

    default:
        return 0L;
    }
    return 1L;
}

/*
 * s17:0698-06C8  DoStatistics  (FAR PASCAL)
 *
 * Video/Statistics.
 */
void FAR PASCAL DoStatistics(HWND hwndParent)
{
    FARPROC lpfn;

    lpfn = MakeProcInstance((FARPROC)StatsDlgProc, ghInst);
    DoDialog(IDD_STATISTICS, hwndParent, lpfn);
    FreeProcInstance(lpfn);
}
