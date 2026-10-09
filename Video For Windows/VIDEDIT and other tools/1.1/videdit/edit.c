/*
 * edit.c - VIDEDIT.EXE code segment 4, offsets 102A-2EAB: the Edit menu
 * and some Video menu commands.
 *
 *  - Delete, Cut, Copy and Paste of the selected tracks (gfEditVideo video,
 *    gfEditAudio audio, gfEditMidi the disabled MIDI track), with undo
 *    information and error boxes;
 *  - Paste Palette and Create Palette (optimal palette of a range of
 *    frames, put on the clipboard), Convert Frame Rate;
 *  - Synchronize: a modeless dialog with its own message loop that plays
 *    a sample of the video (frames loaded into memory and drawn on a
 *    timer) with the sound moved by the audio offset;
 *  - turning the clipboard into static data when VidEdit gives it up.
 *
 * Text fields are checked on a timer started by EN_CHANGE (1 s in the
 * Create Palette dialog, 2 s in Synchronize).
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "vemain.h"
#include "frames.h"
#include "preview.h"
#include "compress.h"
#include "dibmap.h"
#include "edit.h"

#define GetSelection(lpwStart, lpwEnd, fCur)    GetSelection(lpwStart, lpwEnd, fCur)

/* string IDs */
#define IDS_APPTITLE            126     /* "VidEdit" (error box caption) */
#define IDS_BADNUMBER           201     /* "Bad numeric value." */
#define IDS_BADCOLORS           202     /* "The number of colors must be between 2 and 256." */
#define IDS_PALNOENTRIES        210     /* "The clipboard palette contains no entries." */
#define IDS_PALNODATA           211     /* "Cannot get the clipboard palette data." */
#define IDS_PASTENOMEM          212     /* "Not enough memory is available to perform the paste." */
#define IDS_NODELVIDEO          220     /* "Unable to delete video information." */
#define IDS_NODELAUDIO          221     /* "Unable to delete waveform audio information." */
#define IDS_BADREDUCE           231     /* "Error in color reduction." */
#define IDS_SYNCNOMEM           235     /* "Not enough memory to load frames for synchronization preview." */
#define IDS_NOCLIPBOARD         240     /* "Unable to access clipboard." */
#define IDS_NOEMPTYCLIP         241     /* "Unable to empty clipboard." */
#define IDS_NOCOPYVIDEO         242     /* "Unable to copy video information to clipboard." */
#define IDS_NOCOPYAUDIO         243     /* "Unable to copy waveform audio information to clipboard." */
#define IDS_NOPASTEVIDEO        245     /* "Unable to paste video information from clipboard." */
#define IDS_NOPASTEAUDIO        246     /* "Unable to paste waveform audio information from clipboard." */
#define IDS_NOPASTEDATA         248     /* "No appropriate data in the clipboard to paste." */
#define IDS_NOWAVEOUT           275     /* "Unable to open waveform output device." */
#define IDS_NOMEMORY            299     /* "Not enough memory to complete operation." */

/* Paste Palette / Create Palette dialogs (IDD_PASTEPALETTE, IDD_CREATEPALETTE) */
#define IDC_PAL_CURRENT         150
#define IDC_PAL_ALL             151
#define IDC_PAL_SELECTION       152
#define IDC_PAL_REMAP           153
#define IDC_PAL_SETSEL          154
#define IDC_PAL_COLORS          155
#define IDC_PAL_PASTE           156     /* Palette Created: "Paste Palette..." */

/* Convert Frame Rate dialog (IDD_CONVERTFRAMERATE) */
#define IDC_FPS_RATE            180

/* Synchronize dialog (IDD_SYNCHRONIZE) */
#define IDC_SYNC_OFFSETLABEL    151
#define IDC_SYNC_OFFSET         152
#define IDC_SYNC_MSLABEL        157
#define IDC_SYNC_OFFSETARROW    142
#define IDC_SYNC_RATE           156
#define IDC_SYNC_PLAYSTARTLABEL 147
#define IDC_SYNC_PLAYSTART      223
#define IDC_SYNC_DURLABEL       148
#define IDC_SYNC_DURATION       155
#define IDC_SYNC_DURARROW       145
#define IDC_SYNC_SECLABEL       149
#define IDC_SYNC_PLAY           150
#define IDC_SYNC_STATUS         154

#define WM_FRAMEBOX_SETMAX      (WM_USER + 100)     /* aviframebox: largest frame number */

#define MB_ERROR                MB_ICONEXCLAMATION

/* initialised data, DS:04C6-0557 (module data starts at DS:04A8; gfEditVideo-04c4 are the tracks) */
static WORD     gwSyncStart = 0;        /* DS:04C6 first frame of the sample */
                                        /* DS:04CA-051B the strings of this file */
static WORD     gidSyncTimer = 0;       /* DS:051C */
static BOOL     gfSyncInit = FALSE;     /* DS:051E */
static HLOCAL   ghSyncFrames = 0;       /* DS:0520 array of DIB "handles" */
static BOOL     gfSyncLoaded = FALSE;   /* DS:0522 */
static HPALETTE ghpalSync = 0;          /* DS:0524 */
static HDC      ghdcSync = 0;           /* DS:053E */
static BOOL     gfSyncPlaying = FALSE;  /* DS:0540 */
static WORD     gcSyncLoops = 0;        /* DS:0542 */

/*
 * DS:11A6, in the initialised data of the selection code in seg5 (next
 * to the selection at DS:11A8/11AA), set while the Set Selection dialog
 * is run from the palette dialogs; seg5 reads it.
 */
/* gfSetSelNested (DS:11A6) is defined in ctrlbars.c */

/* uninitialised */
static WORD     gnPalColors;            /* DS:1C84 Create Palette: colours */
static BOOL     gfPalRemap;             /* DS:1C86 Paste Palette: remap */
static WORD     gidPalRange;            /* DS:1C88 IDC_PAL_CURRENT/ALL/SELECTION */
static WORD     gidPalTimer;            /* DS:1C8A */
static DWORD    gdwNewUSec;             /* DS:1C8C new microseconds per frame */
static int      gnSyncAudioOffset;      /* DS:1C90 ms */
static int      gnSyncMidiOffset;       /* DS:1C92 always 0 */
static DWORD    gdwSyncDuration;        /* DS:1C94 ms */
static DWORD    gdwSyncLength;          /* DS:1C98 length of the movie in ms */
static BOOL     gfSyncShort;            /* DS:1C9C less than a second: no sample */
static WORD     gwSyncMaxStart;         /* DS:1C9E */
static WORD     gwSyncResult;           /* DS:1CA0 0 = running, 1 = OK, 2 = Cancel */
static WORD     gcSyncFrames;           /* DS:1CA2 */
static WORD     gwSyncFrame;            /* DS:1CA4 frame shown */
static HPALETTE ghpalSyncOld;           /* DS:1CA6 */
static DWORD    gdwSyncNext;            /* DS:1CA8 time of the next frame, us */
static HWND     ghwndSync;              /* DS:1CAC */

/*
 * s04:102A-111B  EditDeleteRange  (NEAR PASCAL)
 *
 * Deletes wCount frames from wStart from the tracks being edited.  If the
 * sound can't be deleted the video deletion is undone.
 */
static BOOL NEAR PASCAL EditDeleteRange(WORD wStart, WORD wCount)
{
    WORD wSelStart, wSelEnd;

    SetUndoTracks(gfEditVideo, gfEditAudio, gfInsertMode);
    GetSelection(&wSelStart, &wSelEnd, FALSE);
    SetUndoSelection(wSelStart, wSelEnd, gwCurFrame);

    if (gfEditVideo && !DeleteFrames(wStart, wCount)) {
        SetUndo(0);
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, IDS_NODELVIDEO);
        return FALSE;
    }

    if (gfEditAudio && !WaveDelete(wStart, wCount)) {
        SetUndoTracks(gfEditVideo, FALSE, gfInsertMode);
        Undo();
        SetUndo(0);
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, IDS_NODELAUDIO);
        return FALSE;
    }

    if (gfInsertMode) {
        SetSelection((WORD)-1, (WORD)-1);
        SeekTo(wStart);
        UpdateLength();
        SetRedoSelection((WORD)-1, (WORD)-1, gwCurFrame);
    } else {
        SeekTo(wStart);
        UpdateLength();
        SetRedoSelection(wSelStart, wSelEnd, gwCurFrame);
    }
    return TRUE;
}

/*
 * s04:111C-1273  PastePaletteDlgProc  (FAR PASCAL, loads DS from SS)
 */
BOOL FAR PASCAL PastePaletteDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WORD    wSelStart, wSelEnd;
    BOOL    f;

    switch (msg) {
    case WM_INITDIALOG:
        GetSelection(&wSelStart, &wSelEnd, FALSE);
        if (wSelStart == (WORD)-1)
            EnableWindow(GetDlgItem(hDlg, IDC_PAL_SELECTION), FALSE);
        CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_ALL);
        CheckDlgButton(hDlg, IDC_PAL_REMAP, TRUE);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (IsDlgButtonChecked(hDlg, IDC_PAL_CURRENT))
                gidPalRange = IDC_PAL_CURRENT;
            else if (IsDlgButtonChecked(hDlg, IDC_PAL_ALL))
                gidPalRange = IDC_PAL_ALL;
            else if (IsDlgButtonChecked(hDlg, IDC_PAL_SELECTION))
                gidPalRange = IDC_PAL_SELECTION;
            gfPalRemap = IsDlgButtonChecked(hDlg, IDC_PAL_REMAP);
            EndDialog(hDlg, TRUE);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        case IDC_PAL_SETSEL:
            gfSetSelNested = TRUE;
            f = SetSelectionDialog(hDlg);
            gfSetSelNested = FALSE;
            GetSelection(&wSelStart, &wSelEnd, FALSE);
            if (f && wSelStart == (WORD)-1) {
                EnableWindow(GetDlgItem(hDlg, IDC_PAL_SELECTION), FALSE);
                CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_CURRENT);
            } else if (f) {
                EnableWindow(GetDlgItem(hDlg, IDC_PAL_SELECTION), TRUE);
                CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_SELECTION);
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/*
 * s04:1274-1623  CreatePaletteDlgProc  (FAR PASCAL, loads DS from SS)
 *
 * The colour count: 2-236 or 256 (anything from 237 to 255 becomes 256);
 * the arrow steps by 1 up to 236 and jumps between 236 and 256.  OK
 * checks with unsigned comparisons, the timer with signed ones.
 */
BOOL FAR PASCAL CreatePaletteDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WORD    wSelStart, wSelEnd;
    BOOL    fOk, f;
    int     n;

    switch (msg) {
    case WM_INITDIALOG:
        GetSelection(&wSelStart, &wSelEnd, FALSE);
        if (wSelStart != (WORD)-1) {
            CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_SELECTION);
        } else {
            CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_ALL);
            EnableWindow(GetDlgItem(hDlg, IDC_PAL_SELECTION), FALSE);
        }
        if (GetFrameCount() <= gwCurFrame)
            EnableWindow(GetDlgItem(hDlg, IDC_PAL_CURRENT), FALSE);
        SetDlgItemInt(hDlg, IDC_PAL_COLORS, 256, FALSE);
        gidPalTimer = 0;
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gidPalTimer) {
                gidPalTimer = IDC_PAL_COLORS;
                if (!SetTimer(hDlg, gidPalTimer, 1000, NULL))
                    SendMessage(hDlg, WM_TIMER, gidPalTimer, 0L);
            }
            if (IsDlgButtonChecked(hDlg, IDC_PAL_SELECTION))
                gidPalRange = IDC_PAL_SELECTION;
            else if (IsDlgButtonChecked(hDlg, IDC_PAL_ALL))
                gidPalRange = IDC_PAL_ALL;
            else if (IsDlgButtonChecked(hDlg, IDC_PAL_CURRENT))
                gidPalRange = IDC_PAL_CURRENT;

            gnPalColors = GetDlgItemInt(hDlg, IDC_PAL_COLORS, &fOk, FALSE);
            if (gnPalColors > 236 && gnPalColors != 256) {
                SetDlgItemInt(hDlg, IDC_PAL_COLORS, 256, FALSE);
                gnPalColors = 256;
            }
            if (fOk && gnPalColors <= 256 && gnPalColors >= 2) {
                EndDialog(hDlg, TRUE);
                return TRUE;
            }
            MessageBeep(MB_ERROR);
            ErrorResBox(hDlg, ghInst, MB_ERROR, IDS_APPTITLE, IDS_BADCOLORS);
            SendDlgItemMessage(hDlg, IDC_PAL_COLORS, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        case IDC_PAL_SETSEL:
            gfSetSelNested = TRUE;
            f = SetSelectionDialog(hDlg);
            gfSetSelNested = FALSE;
            GetSelection(&wSelStart, &wSelEnd, FALSE);
            if (f && wSelStart == (WORD)-1) {
                EnableWindow(GetDlgItem(hDlg, IDC_PAL_SELECTION), FALSE);
                CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_CURRENT);
            } else if (f) {
                EnableWindow(GetDlgItem(hDlg, IDC_PAL_SELECTION), TRUE);
                CheckRadioButton(hDlg, IDC_PAL_CURRENT, IDC_PAL_SELECTION, IDC_PAL_SELECTION);
            }
            return TRUE;

        case IDC_PAL_COLORS:
            if (HIWORD(lParam) == EN_CHANGE) {
                if (gidPalTimer)
                    KillTimer(hDlg, gidPalTimer);
                gidPalTimer = wParam;
                if (!SetTimer(hDlg, gidPalTimer, 1000, NULL))
                    SendMessage(hDlg, WM_TIMER, gidPalTimer, 0L);
            }
            return TRUE;
        }
        return FALSE;

    case WM_TIMER:
        if (gidPalTimer == IDC_PAL_COLORS) {
            KillTimer(hDlg, gidPalTimer);
            gidPalTimer = 0;
            n = GetDlgItemInt(hDlg, IDC_PAL_COLORS, &fOk, FALSE);
            if (n > 236 && n != 256) {
                SetDlgItemInt(hDlg, IDC_PAL_COLORS, 256, FALSE);
                n = 256;
            }
            if (!fOk || n > 256 || n < 2) {
                MessageBeep(0);
                if (n > 256)
                    SetDlgItemInt(hDlg, IDC_PAL_COLORS, 256, FALSE);
                else if (n < 2)
                    SetDlgItemInt(hDlg, IDC_PAL_COLORS, 2, FALSE);
                SendDlgItemMessage(hDlg, IDC_PAL_COLORS, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
            }
        }
        return TRUE;

    case WM_VSCROLL:
        n = GetDlgItemInt(hDlg, IDC_PAL_COLORS, &fOk, FALSE);
        if (n == 256)
            ArrowEditStep(GetDlgItem(hDlg, IDC_PAL_COLORS), wParam, 2L, 256L, 20);
        else if (n == 236)
            ArrowEditStep2(GetDlgItem(hDlg, IDC_PAL_COLORS), wParam, 2L, 256L, 20, 1);
        else
            ArrowEditStep(GetDlgItem(hDlg, IDC_PAL_COLORS), wParam, 2L, 256L, 1);
        return TRUE;
    }
    return FALSE;
}

/*
 * s04:1624-1669  PaletteCreatedDlgProc  (FAR PASCAL, loads DS from SS)
 */
BOOL FAR PASCAL PaletteCreatedDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_PAL_PASTE:
            EditPastePalette(ghwndApp);
            /* fall through */
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, TRUE);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

/*
 * s04:166A-176F  ComputePalette  (NEAR PASCAL)
 *
 * An optimal palette of nColors for wCount frames from wStart, from the
 * histogram of the full frames.  Cancelling the frame loading
 * (GetFullFrame returns 1) gives no palette and no message.
 */
static HPALETTE NEAR PASCAL ComputePalette(WORD wStart, WORD wCount, int nColors)
{
    HPALETTE        hpal = NULL;
    UINT            ids = 0;
    HCURSOR         hcurOld;
    LPHISTOGRAM     lpHist;
    WORD            w, wEnd, sel;

    hcurOld = SetCursor(LoadCursor(0, IDC_WAIT));

    lpHist = InitHistogram(NULL);
    if (lpHist == NULL) {
        ids = IDS_NOMEMORY;
        goto done;
    }

    wEnd = wStart + wCount;
    for (w = wStart; w < wEnd; w++) {
        sel = GetFullFrame(w, NULL, 0x31);
        if (sel == 1)
            goto done;
        if (sel == 0) {
            ids = IDS_NOMEMORY;
            goto done;
        }
        DibHistogram((LPBITMAPINFOHEADER)MAKELP(sel, 0), NULL, 0, 0, -1, -1, lpHist);
    }

    StatusSetMessage("Computing Optimal Palette");
    hpal = HistogramPalette(lpHist, NULL, nColors);
    if (!hpal)
        ids = IDS_BADREDUCE;

done:
    if (ids) {
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, ids);
    }
    if (lpHist)
        FreeHistogram(lpHist);
    if (hcurOld)
        SetCursor(hcurOld);
    StatusSetMessage(NULL);
    return hpal;
}

/*
 * s04:1770-1819  FrameRateDlgProc  (FAR PASCAL, loads DS from SS)
 *
 * Convert Frame Rate: 1-100 frames per second.
 */
BOOL FAR PASCAL FrameRateDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char ach[32];

    switch (msg) {
    case WM_INITDIALOG:
        FormatFrameRate(ach, gCompOptions.dwUSecPerFrame);
        SetDlgItemText(hDlg, IDC_FPS_RATE, ach);
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            GetDlgItemText(hDlg, IDC_FPS_RATE, ach, sizeof(ach));
            gdwNewUSec = FrameRateToUSec(ach);
            EndDialog(hDlg, TRUE);
            break;
        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            break;
        }
        break;

    case WM_VSCROLL:
        ArrowEditStep(GetDlgItem(hDlg, IDC_FPS_RATE), wParam, 1L, 100L, 1);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

/*
 * s04:181A-1837  EditSetTracks  (FAR PASCAL)
 */
void FAR PASCAL EditSetTracks(BOOL fVideo, BOOL fAudio, BOOL fMidi)
{
    gfEditVideo = fVideo;
    gfEditAudio = fAudio;
    gfEditMidi = fMidi;
    PreviewSetTracks();
}

/*
 * s04:1838-1873  EditCut  (FAR PASCAL)
 */
void FAR PASCAL EditCut(HWND hwnd)
{
    WORD wSelStart, wSelEnd;
    WORD wCount;

    GetSelection(&wSelStart, &wSelEnd, TRUE);
    wCount = wSelEnd - wSelStart;
    if (EditCopy(hwnd)) {
        SetUndo(IDM_CUT);
        EditDeleteRange(wSelStart, wCount);
    }
}

/*
 * s04:1874-19EB  EditCopy  (FAR PASCAL)
 *
 * Copies the selection (or the current frame) of the edited tracks, plus
 * a CF_DSPTEXT description such as "2.000 sec. of video.".  The frame
 * count of the copied video is converted to this movie's rate for the
 * text.  hwnd is not used.
 */
BOOL FAR PASCAL EditCopy(HWND hwnd)
{
    UINT        ids = 0;
    WORD        wSelStart, wSelEnd;
    WORD        wCount;
    HGLOBAL     h;
    LPSTR       lpsz;
    DWORD       dwUSec;
    WORD        nVideo, nAudio;
    char        achVideo[40];
    char        achAudio[40];

    GetSelection(&wSelStart, &wSelEnd, TRUE);
    wCount = wSelEnd - wSelStart;

    if (!OpenClipboard(ghwndFrame)) {
        ids = IDS_NOCLIPBOARD;
        goto done;
    }
    if (!EmptyClipboard()) {
        ids = IDS_NOEMPTYCLIP;
        goto done;
    }
    if (gfEditVideo && !CopyFrames(wSelStart, wCount)) {
        ids = IDS_NOCOPYVIDEO;
        goto done;
    }
    if (gfEditAudio && !WaveCopyToClipboard(wSelStart, wCount)) {
        ids = IDS_NOCOPYAUDIO;
        goto done;
    }

    h = GlobalAlloc(GHND, 40L);
    if (h == 0)
        goto done;
    lpsz = GlobalLock(h);

    nVideo = GetClipboardFrameCount(&dwUSec);
    if (dwUSec)
        nVideo = (WORD)muldiv32((LONG)nVideo, dwUSec, gCompOptions.dwUSecPerFrame);
    nAudio = WaveClipboardFrames();
    FormatFrameTime(nVideo, achVideo);
    FormatFrameTime(nAudio, achAudio);

    if (nVideo && nAudio)
        wsprintf(lpsz, "%ls of video.\n%ls of audio.", (LPSTR)achVideo, (LPSTR)achAudio);
    else if (nVideo)
        wsprintf(lpsz, "%ls of video.", (LPSTR)achVideo);
    else if (nAudio)
        wsprintf(lpsz, "%ls of audio.", (LPSTR)achAudio);

    GlobalUnlock(h);
    SetClipboardData(CF_DSPTEXT, h);

done:
    if (ids != IDS_NOCLIPBOARD)
        CloseClipboard();
    if (ids) {
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, ids);
    }
    return ids == 0;
}

/*
 * s04:19EC-1B69  EditPaste  (FAR PASCAL)
 *
 * Pastes the clipboard's video and sound over the selection (or at the
 * current frame).  The length is the longer of the two, so the shorter
 * track is padded.  When video isn't edited, the video frame count is an
 * uninitialised value, but it isn't used then.  hwnd is not used.
 */
void FAR PASCAL EditPaste(HWND hwnd)
{
    UINT    ids;
    UINT    nOk = 0;
    WORD    nVideo;
    WORD    nLength;
    WORD    nAudio;
    WORD    nPaste;
    WORD    nReplace;
    WORD    wSelStart, wSelEnd;
    DWORD   dwUSec;

    nLength = 0;
    if (!OpenClipboard(ghwndFrame)) {
        ids = IDS_NOCLIPBOARD;
        goto done;
    }

    if (gfEditVideo) {
        nVideo = GetClipboardFrameCount(&dwUSec);
        nLength = nVideo;
        if (dwUSec)
            nLength = (WORD)muldiv32((LONG)nVideo, dwUSec, gCompOptions.dwUSecPerFrame);
    }
    if (gfEditAudio) {
        nAudio = WaveClipboardFrames();
        if (nAudio > nLength) {
            nVideo += nAudio - nLength;
            nLength = nAudio;
        }
    }

    if (nLength == 0) {
        ids = IDS_NOPASTEDATA;
        goto done;
    }
    nPaste = nLength;

    GetSelection(&wSelStart, &wSelEnd, FALSE);
    if (wSelStart == (WORD)-1) {
        nReplace = 0;
        wSelStart = gwCurFrame;
        SetUndoSelection((WORD)-1, (WORD)-1, gwCurFrame);
    } else {
        nReplace = wSelEnd - wSelStart;
        SetUndoSelection(wSelStart, wSelEnd, gwCurFrame);
    }

    SetSelection((WORD)-1, (WORD)-1);
    SetUndo(IDM_PASTE);
    SetUndoTracks(gfEditVideo, gfEditAudio, gfInsertMode);

    if (gfEditVideo && !PasteFrames(wSelStart, nReplace, nVideo)) {
        SetUndo(0);
        ids = IDS_NOPASTEVIDEO;
        goto done;
    }
    if (gfEditAudio && !WavePasteFromClipboard(wSelStart, nReplace, nPaste)) {
        SetUndoTracks(gfEditVideo, FALSE, gfInsertMode);
        Undo();
        SetUndo(0);
        ids = IDS_NOPASTEAUDIO;
        goto done;
    }

    SeekTo(wSelStart + nPaste);
    UpdateLength();
    SetRedoSelection((WORD)-1, (WORD)-1, gwCurFrame);
    ids = nOk;

done:
    if (ids != IDS_NOCLIPBOARD)
        CloseClipboard();
    if (ids) {
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, ids);
    }
}

/*
 * s04:1B6A-1CEB  EditPastePalette  (FAR PASCAL)
 *
 * Edit / Paste Palette: applies the clipboard palette to the current
 * frame, all frames or the selection, remapping the pixels if asked.
 * With an unknown range id the frame count is an uninitialised value.
 */
void FAR PASCAL EditPastePalette(HWND hwnd)
{
    FARPROC     lpfn;
    int         f;
    WORD        wStart, wEnd;
    WORD        wCount;
    UINT        ids;
    HPALETTE    hpalClip, hpal;
    int         nEntries;
    LOGPALETTE *pPal;

    lpfn = MakeProcInstance((FARPROC)PastePaletteDlgProc, ghInst);
    f = DoDialog(IDD_PASTEPALETTE, hwnd, lpfn);
    FreeProcInstance(lpfn);
    UpdateWindow(ghwndView);
    UpdateWindow(ghwndApp);
    if (!f)
        return;

    wStart = gwCurFrame;
    switch (gidPalRange) {
    case IDC_PAL_CURRENT:
        wCount = 1;
        break;
    case IDC_PAL_ALL:
        wStart = 0;
        wCount = gwLengthShown;
        break;
    case IDC_PAL_SELECTION:
        GetSelection(&wStart, &wEnd, TRUE);
        wCount = wEnd - wStart;
        break;
    }

    if (!OpenClipboard(ghwndApp)) {
        ids = IDS_PALNODATA;
        goto error;
    }
    hpalClip = GetClipboardData(CF_PALETTE);
    if (hpalClip == 0) {
        CloseClipboard();
        ids = IDS_PALNODATA;
        goto error;
    }
    GetObject(hpalClip, sizeof(int), (LPSTR)&nEntries);
    if (nEntries == 0) {
        CloseClipboard();
        ids = IDS_PALNOENTRIES;
        goto error;
    }
    pPal = (LOGPALETTE *)LocalAlloc(LPTR, sizeof(LOGPALETTE) + nEntries * sizeof(PALETTEENTRY));
    if (pPal == NULL) {
        CloseClipboard();
        goto nomem;
    }
    pPal->palVersion = 0x300;
    pPal->palNumEntries = nEntries;
    GetPaletteEntries(hpalClip, 0, nEntries, pPal->palPalEntry);
    CloseClipboard();
    hpal = CreatePalette(pPal);
    LocalFree((HLOCAL)pPal);
    if (hpal == 0)
        goto nomem;

    SetUndo(IDM_PASTEPALETTE);
    if (ApplyPalette(wStart, wCount, hpal, !gfPalRemap)) {
        UpdateLength();
        return;
    }

nomem:
    ids = IDS_PASTENOMEM;
error:
    MessageBeep(MB_ERROR);
    ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, ids);
}

/*
 * s04:1CEC-1D1B  EditDelete  (FAR PASCAL)
 */
void FAR PASCAL EditDelete(HWND hwnd)
{
    WORD wSelStart, wSelEnd;
    WORD wCount;

    GetSelection(&wSelStart, &wSelEnd, TRUE);
    wCount = wSelEnd - wSelStart;
    SetUndo(IDM_DELETE);
    EditDeleteRange(wSelStart, wCount);
}

/*
 * s04:1D1C-1E49  EditCreatePalette  (FAR PASCAL)
 *
 * Video / Create Palette: puts an optimal palette of the chosen frames on
 * the clipboard and offers to paste it.  The palette leaks if the
 * clipboard can't be used.
 */
void FAR PASCAL EditCreatePalette(HWND hwnd)
{
    FARPROC     lpfn;
    int         f;
    WORD        wStart, wEnd;
    HPALETTE    hpal;
    UINT        ids;

    lpfn = MakeProcInstance((FARPROC)CreatePaletteDlgProc, ghInst);
    f = DoDialog(IDD_CREATEPALETTE, hwnd, lpfn);
    FreeProcInstance(lpfn);
    UpdateWindow(ghwndView);
    UpdateWindow(ghwndApp);
    if (!f)
        return;

    if (gidPalRange == IDC_PAL_CURRENT) {
        wStart = gwCurFrame;
        wEnd = wStart + 1;
    } else if (gidPalRange == IDC_PAL_SELECTION) {
        GetSelection(&wStart, &wEnd, TRUE);
    } else {
        wStart = 0;
        wEnd = gwLengthShown;
    }

    hpal = ComputePalette(wStart, wEnd - wStart, gnPalColors);
    if (!hpal)
        return;

    if (!OpenClipboard(ghwndFrame)) {
        ids = IDS_NOCLIPBOARD;
    } else if (!EmptyClipboard()) {
        ids = IDS_NOEMPTYCLIP;
    } else {
        SetClipboardData(CF_PALETTE, hpal);
        CloseClipboard();
        lpfn = MakeProcInstance((FARPROC)PaletteCreatedDlgProc, ghInst);
        DoDialog(IDD_PALETTECREATED, hwnd, lpfn);
        FreeProcInstance(lpfn);
        return;
    }

    if (ids != IDS_NOCLIPBOARD)
        CloseClipboard();
    if (ids) {
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, ids);
    }
}

/*
 * s04:1E4A-1EB3  EditConvertFrameRate  (FAR PASCAL)
 */
void FAR PASCAL EditConvertFrameRate(HWND hwnd)
{
    FARPROC lpfn;
    int     f;

    lpfn = MakeProcInstance((FARPROC)FrameRateDlgProc, ghInst);
    f = DoDialog(IDD_CONVERTFRAMERATE, hwnd, lpfn);
    FreeProcInstance(lpfn);
    if (f) {
        SetUndo(0);
        if (ConvertFrameRate(gCompOptions.dwUSecPerFrame, gdwNewUSec, 0, GetFrameCount()))
            SetSelection((WORD)-1, (WORD)-1);
        StatusUpdatePosition();
    }
}

/*
 * s04:1EB4-20D3  SyncInitDialog  (NEAR PASCAL)
 *
 * Initial Synchronize values: offset 0, the sample starting at the
 * current frame and lasting 3 seconds (less near the end).  The sample
 * controls are disabled for a movie shorter than a second, the offset
 * controls when there is no sound.
 */
static void NEAR PASCAL SyncInitDialog(HWND hDlg)
{
    DWORD   dwPos;
    WORD    wSeconds;
    char    ach[20];

    gfSyncInit = TRUE;
    SetDlgItemInt(hDlg, IDC_SYNC_PLAYSTART, gwCurFrame, gfSyncShort = FALSE);
    SetDlgItemInt(hDlg, IDC_SYNC_OFFSET, 0, FALSE);

    if (WaveNumFrames() == 0) {
        EnableWindow(GetDlgItem(hDlg, IDC_SYNC_OFFSETLABEL), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_SYNC_OFFSET), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_SYNC_OFFSETARROW), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_SYNC_MSLABEL), FALSE);
    }

    gdwSyncLength = muldiv32((LONG)gwLengthShown, gCompOptions.dwUSecPerFrame, 1000L);
    dwPos = muldiv32((LONG)gwCurFrame, gCompOptions.dwUSecPerFrame, 1000L);

    if (gdwSyncLength - dwPos < 1000) {
        gwSyncMaxStart = 0;
        wSeconds = 1;
    } else {
        gwSyncMaxStart = (WORD)((gdwSyncLength * 1000 - 1000000L) / gCompOptions.dwUSecPerFrame);
        if (dwPos + 3000 < gdwSyncLength) {
            wSeconds = 3;
        } else {
            wSeconds = (WORD)((gdwSyncLength - dwPos) / 1000);
            if (wSeconds == 0) {
                wSeconds = 1;
                if ((int)gwSyncMaxStart > 0) {
                    SetDlgItemInt(hDlg, IDC_SYNC_PLAYSTART, gwSyncMaxStart, FALSE);
                } else {
                    SetDlgItemInt(hDlg, IDC_SYNC_PLAYSTART, gwSyncMaxStart = 0, FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_SYNC_PLAYSTART), FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_SYNC_DURATION), FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_SYNC_DURARROW), FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_SYNC_PLAYSTARTLABEL), FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_SYNC_DURLABEL), FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_SYNC_SECLABEL), FALSE);
                    gfSyncShort = TRUE;
                }
            }
        }
    }

    SetDlgItemInt(hDlg, IDC_SYNC_DURATION, wSeconds, FALSE);
    SendDlgItemMessage(hDlg, IDC_SYNC_PLAYSTART, WM_FRAMEBOX_SETMAX, gwSyncMaxStart, 0L);
    FormatFrameRate(ach, gCompOptions.dwUSecPerFrame);
    SetDlgItemText(hDlg, IDC_SYNC_RATE, ach);
    gfSyncInit = FALSE;
}

/*
 * s04:20D4-2227  SyncGetValues  (NEAR PASCAL)
 *
 * Reads and checks the fields; on an error the bad field gets the focus
 * and is selected.  Without a sample (movie under a second) the whole
 * movie is played.
 */
static BOOL NEAR PASCAL SyncGetValues(HWND hDlg)
{
    BOOL    fOk;
    UINT    id;
    DWORD   dwStart;
    char    ach[20];

    gnSyncAudioOffset = GetDlgItemInt(hDlg, id = IDC_SYNC_OFFSET, &fOk, TRUE);
    gnSyncMidiOffset = 0;

    if (fOk) {
        gwSyncStart = GetDlgItemInt(hDlg, id = IDC_SYNC_PLAYSTART, &fOk, FALSE);
        if ((int)gwSyncStart < 0 || gwSyncStart > gwLengthShown)
            fOk = FALSE;
    }

    if (fOk) {
        gdwSyncDuration = (DWORD)GetDlgItemInt(hDlg, id = IDC_SYNC_DURATION, &fOk, FALSE) * 1000;
        dwStart = muldiv32((LONG)gwSyncStart, gCompOptions.dwUSecPerFrame, 1000L);
        if (gdwSyncLength >= 1000 && !gfSyncShort) {
            if (gdwSyncDuration == 0 || dwStart + gdwSyncDuration > gdwSyncLength)
                fOk = FALSE;
        } else {
            gdwSyncDuration = gdwSyncLength;
        }
    }

    if (fOk) {
        GetDlgItemText(hDlg, id = IDC_SYNC_RATE, ach, sizeof(ach));
        gdwNewUSec = FrameRateToUSec(ach);
        if (gdwNewUSec == 0)
            fOk = FALSE;
    }

    if (!fOk) {
        MessageBeep(MB_ERROR);
        ErrorResBox(hDlg, ghInst, MB_ERROR, IDS_APPTITLE, IDS_BADNUMBER);
        SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, id), 1L);
        SendDlgItemMessage(hDlg, id, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
    }
    return fOk;
}

/*
 * s04:2228-2281  SyncFreeFrames  (NEAR PASCAL)
 */
static BOOL NEAR PASCAL SyncFreeFrames(HWND hDlg)
{
    WORD     w;
    HGLOBAL *ph;

    if (ghSyncFrames) {
        for (w = gwSyncStart; gcSyncFrames + gwSyncStart > w; w++) {
            ph = (HGLOBAL *)ghSyncFrames + (w - gwSyncStart);
            if (*ph)
                GlobalFree(*ph);
        }
        LocalFree(ghSyncFrames);
        ghSyncFrames = 0;
    }
    gfSyncLoaded = FALSE;
    return TRUE;
}

/*
 * s04:2282-2419  SyncLoadFrames  (NEAR PASCAL)
 *
 * Loads the frames of the sample (as many as the new frame rate plays in
 * the sample's duration) as copies of the full frames, set to
 * DIB_PAL_COLORS with the palette of the first one.  The start time at
 * the new rate is computed and not used.
 */
static BOOL NEAR PASCAL SyncLoadFrames(HWND hDlg)
{
    DWORD       dwDuration;
    UINT        ids = 0;
    UINT        nOk;
    HCURSOR     hcurOld;
    WORD        w;
    HANDLE      h;
    HGLOBAL    *ph;

    dwDuration = gdwSyncDuration;

    if (ghSyncFrames && !SyncFreeFrames(hDlg))
        return FALSE;

    hcurOld = SetCursor(LoadCursor(0, IDC_WAIT));
    SetDlgItemText(hDlg, IDC_SYNC_STATUS, "Getting Video Frames...");

    muldiv32((LONG)gwSyncStart, gdwNewUSec, 1000L);
    gcSyncFrames = (WORD)muldiv32(dwDuration, 1000L, gdwNewUSec);
    if (gcSyncFrames + gwSyncStart > gwLengthShown)
        gcSyncFrames = gwLengthShown - gwSyncStart;

    ghSyncFrames = LocalAlloc(LPTR, gcSyncFrames * sizeof(HGLOBAL));
    if (ghSyncFrames == 0) {
        ids = IDS_NOMEMORY;
        goto done;
    }

    nOk = 0;
    for (w = gwSyncStart; gwSyncStart + gcSyncFrames > w; w++)
        ((HGLOBAL *)ghSyncFrames)[w - gwSyncStart] = 0;

    for (w = gwSyncStart; gcSyncFrames + gwSyncStart > w; w++) {
        h = (HANDLE)GetFullFrame(w, (gwSyncStart == w) ? &ghpalSync : NULL, 0x51);
        if (h == (HANDLE)1) {
            ids = 1;
            goto done;
        }
        if (h)
            h = CopyHandle(h);
        if (!h) {
            ids = IDS_SYNCNOMEM;
            goto done;
        }
        ph = (HGLOBAL *)ghSyncFrames + (w - gwSyncStart);
        *ph = h;
        SetDibUsage(h, ghpalSync, DIB_PAL_COLORS);
    }

    gfSyncLoaded = TRUE;
    ids = nOk;

done:
    SetDlgItemText(hDlg, IDC_SYNC_STATUS, NULL);
    if (hcurOld)
        SetCursor(hcurOld);
    if (ids) {
        SyncFreeFrames(hDlg);
        if (ids != 1) {
            MessageBeep(MB_ERROR);
            ErrorResBox(hDlg, ghInst, MB_ERROR, IDS_APPTITLE, ids);
        }
    }
    return ids == 0;
}

/*
 * s04:241A-25A9  SyncPlay  (NEAR PASCAL)
 *
 * Starts the sample: the first frame is drawn now, the sound plays in a
 * loop from the sample start minus the audio offset, and the frames
 * follow on the timer driven by SyncPlayIdle.
 */
static BOOL NEAR PASCAL SyncPlay(HWND hDlg)
{
    BOOL                f = TRUE;
    HCURSOR             hcurOld;
    LPBITMAPINFOHEADER  lpbi;

    if (gidSyncTimer)
        SendMessage(hDlg, WM_TIMER, gidSyncTimer, 0L);

    hcurOld = SetCursor(LoadCursor(0, IDC_WAIT));
    ghwndSync = hDlg;

    if (!gfSyncLoaded && !SyncLoadFrames(hDlg)) {
        f = FALSE;
        goto done;
    }

    if (!WaveOpenDevice()) {
        MessageBeep(MB_ERROR);
        ErrorResBox(ghwndApp, ghInst, MB_ERROR, IDS_APPTITLE, IDS_NOWAVEOUT);
        f = FALSE;
        goto done;
    }

    ghdcSync = GetDC(ghwndFrame);
    ghpalSyncOld = SelectPalette(ghdcSync, ghpalSync, FALSE);
    RealizePalette(ghdcSync);
    gcSyncLoops = 0;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(((HGLOBAL *)ghSyncFrames)[0]);
    if (lpbi->biBitCount <= 8)
        StretchDIBits(ghdcSync, 0, 0, gcxMovie, gcyMovie, 0, 0, gcxMovie, gcyMovie,
                      (LPSTR)lpbi + (WORD)lpbi->biSize + ((WORD)lpbi->biClrUsed << 2),
                      (LPBITMAPINFO)lpbi, f, SRCCOPY);
    else
        DrawDibDraw(ghdd, ghdcSync, 0, 0, gcxMovie, gcyMovie, lpbi, NULL, 0, 0, -1, -1, 0);
    GlobalUnlock(((HGLOBAL *)ghSyncFrames)[0]);

    if (!WavePlay(gwSyncStart, gcSyncFrames, (LONG)gnSyncAudioOffset, FALSE, f)) {
        f = FALSE;
        goto done;
    }

    gdwSyncNext = timeGetTime() * 1000 + gdwNewUSec;
    gwSyncFrame = gwSyncStart;

done:
    if (hcurOld)
        SetCursor(hcurOld);
    gfSyncPlaying = f;
    if (f)
        SetDlgItemText(hDlg, IDC_SYNC_PLAY, "&Stop");
    return f;
}

/*
 * s04:25AA-26ED  SyncPlayIdle  (NEAR)
 *
 * Shows the frame that is due, skipping frames when late; after the
 * fifth loop the sample stops itself by "pressing" Stop.  The time is
 * timeGetTime() * 1000, which wraps after about 71 minutes of Windows
 * time.
 */
static BOOL NEAR SyncPlayIdle(void)
{
    HGLOBAL             h;
    LPBITMAPINFOHEADER  lpbi;

    if (timeGetTime() * 1000 < gdwSyncNext)
        return TRUE;

    while (timeGetTime() * 1000 > gdwSyncNext) {
        if (gwSyncStart + gcSyncFrames <= ++gwSyncFrame) {
            gwSyncFrame = gwSyncStart;
            if (++gcSyncLoops > 4)
                PostMessage(ghwndSync, WM_COMMAND, IDC_SYNC_PLAY, 0L);
        }
        gdwSyncNext += gdwNewUSec;
    }

    h = ((HGLOBAL *)ghSyncFrames)[gwSyncFrame - gwSyncStart];
    if (h == 0)
        return FALSE;

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(h);
    if (lpbi->biBitCount <= 8)
        StretchDIBits(ghdcSync, 0, 0, gcxMovie, gcyMovie, 0, 0, gcxMovie, gcyMovie,
                      (LPSTR)lpbi + (WORD)lpbi->biSize + ((WORD)lpbi->biClrUsed << 2),
                      (LPBITMAPINFO)lpbi, DIB_PAL_COLORS, SRCCOPY);
    else
        DrawDibDraw(ghdd, ghdcSync, 0, 0, gcxMovie, gcyMovie, lpbi, NULL, 0, 0, -1, -1, 0);
    GlobalUnlock(((HGLOBAL *)ghSyncFrames)[gwSyncFrame - gwSyncStart]);
    return TRUE;
}

/*
 * s04:26EE-273F  SyncStop  (NEAR PASCAL)
 */
static void NEAR PASCAL SyncStop(HWND hDlg)
{
    if (gfSyncPlaying) {
        gfSyncPlaying = FALSE;
        SetDlgItemText(hDlg, IDC_SYNC_PLAY, "&Play");
        if (ghdcSync) {
            SelectPalette(ghdcSync, ghpalSyncOld, FALSE);
            ReleaseDC(ghwndFrame, ghdcSync);
            ghdcSync = 0;
        }
        WaveCloseDevice();
    }
}

/*
 * s04:2740-2CA5  SyncDlgProc  (FAR PASCAL, loads DS from SS)
 *
 * At start the dialog moves out of the way of the frame window: below
 * it, else to its right, else above it, else to its left, else into the
 * bottom right corner.  The "above" and "left" positions are taken when
 * they are not zero (rather than not negative) and then clamped to the
 * screen.  Edited fields are checked 2 seconds after the last change.
 */
BOOL FAR PASCAL SyncDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RECT    rcDlg, rcFrame, rc;
    int     x, y, cx, cy;
    int     n;
    BOOL    fOk;
    WORD    w, wMax, id;
    DWORD   dwPos;
    int     nMin, nMax;
    char    ach[20];

    switch (msg) {
    case WM_INITDIALOG:
        SyncInitDialog(hDlg);
        gfSyncLoaded = FALSE;
        gidSyncTimer = 0;

        GetWindowRect(hDlg, &rcDlg);
        GetWindowRect(ghwndFrame, &rcFrame);
        if (IntersectRect(&rc, &rcDlg, &rcFrame)) {
            cy = rcDlg.bottom - rcDlg.top;
            cx = rcDlg.right - rcDlg.left;
            if ((WORD)(cy + rcFrame.bottom + 1) < gcyScreen) {
                y = rcFrame.bottom + 1;
                x = (rcFrame.right - rcFrame.left) / 2 - cx / 2 + rcFrame.left;
            } else if ((WORD)(rcFrame.right + cx + 1) < gcxScreen) {
                x = rcFrame.right + 1;
                y = (rcFrame.bottom - rcFrame.top) / 2 - cy / 2 + rcFrame.top;
            } else if ((y = rcFrame.top - cy - 1) != 0) {
                x = (rcFrame.right - rcFrame.left) / 2 - cx / 2 + rcFrame.left;
            } else if ((x = rcFrame.left - cx - 1) != 0) {
                y = (rcFrame.bottom - rcFrame.top) / 2 - cy / 2 + rcFrame.top;
            } else {
                y = gcyScreen - cy;
                x = gcxScreen - cx;
            }

            if (x < 0)
                x = 0;
            else if ((WORD)(x + cx) > gcxScreen)
                x = gcxScreen - cx;
            if (y < 0)
                y = 0;
            else if ((WORD)(y + cy) > gcyScreen)
                y = gcyScreen - cy;

            SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (gidSyncTimer)
                SendMessage(hDlg, WM_TIMER, gidSyncTimer, 0L);
            SyncStop(hDlg);
            if (SyncGetValues(hDlg)) {
                gwSyncResult = 1;
                SyncFreeFrames(hDlg);
                InvalidateRect(ghwndFrame, NULL, TRUE);
            }
            return TRUE;

        case IDCANCEL:
            goto cancel;

        case IDC_SYNC_PLAY:
            if (!gfSyncPlaying) {
                if (SyncGetValues(hDlg))
                    SyncPlay(hDlg);
            } else {
                SyncStop(hDlg);
            }
            return TRUE;

        case IDC_SYNC_OFFSET:
        case IDC_SYNC_OFFSET + 1:
        case IDC_SYNC_DURATION:
        case IDC_SYNC_RATE:
        case IDC_SYNC_PLAYSTART:
            if (HIWORD(lParam) == EN_CHANGE && !gfSyncInit) {
                if (gidSyncTimer)
                    KillTimer(hDlg, gidSyncTimer);
                gidSyncTimer = wParam;
                if (!SetTimer(hDlg, gidSyncTimer, 2000, NULL))
                    SendMessage(hDlg, WM_TIMER, gidSyncTimer, 0L);
            }
            return TRUE;
        }
        return FALSE;

    case WM_TIMER:
        if (wParam != gidSyncTimer)
            return TRUE;
        KillTimer(hDlg, gidSyncTimer);

        switch (gidSyncTimer) {
        case IDC_SYNC_OFFSET:
        case IDC_SYNC_OFFSET + 1:
            n = GetDlgItemInt(hDlg, gidSyncTimer, &fOk, TRUE);
            if (fOk && n >= -5000 && n <= 5000)
                break;
            if (!fOk)
                n = 0;
            else
                n = (n < -5000) ? -5000 : 5000;
            SetDlgItemInt(hDlg, gidSyncTimer, n, TRUE);
            goto badfield;

        case IDC_SYNC_DURATION:
            w = GetDlgItemInt(hDlg, IDC_SYNC_PLAYSTART, &fOk, gfSyncLoaded = FALSE);
            dwPos = muldiv32((LONG)w, gCompOptions.dwUSecPerFrame, 1000L);
            wMax = (WORD)((gdwSyncLength - dwPos) / 1000);
            w = GetDlgItemInt(hDlg, IDC_SYNC_DURATION, &fOk, FALSE);
            if (fOk && w >= 1 && wMax >= w)
                break;
            if (w < 1)
                goto setduration1;
            SetDlgItemInt(hDlg, IDC_SYNC_DURATION, wMax, FALSE);
            goto badfield;

        case IDC_SYNC_RATE:
            GetDlgItemText(hDlg, IDC_SYNC_RATE, ach, sizeof(ach));
            if (FrameRateToUSec(ach) != 0)
                break;
            lstrcpy(ach, "1.000");
            SetDlgItemText(hDlg, IDC_SYNC_RATE, ach);
            goto badfield;

        case IDC_SYNC_PLAYSTART:
            w = GetDlgItemInt(hDlg, gidSyncTimer, &fOk, gfSyncLoaded = FALSE);
            if (fOk && (int)w >= 0 && w <= gwSyncMaxStart) {
                dwPos = muldiv32((LONG)w, gCompOptions.dwUSecPerFrame, 1000L);
                wMax = (WORD)((gdwSyncLength - dwPos) / 1000);
                if (GetDlgItemInt(hDlg, IDC_SYNC_DURATION, &fOk, FALSE) > wMax)
                    SetDlgItemInt(hDlg, IDC_SYNC_DURATION, wMax, FALSE);
                break;
            }
            if (!fOk) {
                SetDlgItemInt(hDlg, IDC_SYNC_PLAYSTART, 0, FALSE);
                goto badfield;
            }
            SetDlgItemInt(hDlg, IDC_SYNC_PLAYSTART, gwSyncMaxStart, FALSE);
        setduration1:
            SetDlgItemInt(hDlg, IDC_SYNC_DURATION, 1, FALSE);
        badfield:
            MessageBeep(0);
            SendDlgItemMessage(hDlg, wParam, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
            break;
        }
        SyncStop(hDlg);
        gidSyncTimer = 0;
        return TRUE;

    case WM_VSCROLL:
        id = GetWindowWord((HWND)HIWORD(lParam), GWW_ID) + 10;
        if (id == IDC_SYNC_OFFSET) {
            ArrowEditStep(GetDlgItem(hDlg, id), wParam, -5000L, 5000L, 100);
            return TRUE;
        }
        if (id == IDC_SYNC_PLAYSTART)
            nMin = 0;
        else if (id == IDC_SYNC_DURATION || id == IDC_SYNC_RATE)
            nMin = 1;
        else
            nMin = -5000;
        if (id == IDC_SYNC_PLAYSTART)
            nMax = gwLengthShown - 1;
        else if (id == IDC_SYNC_DURATION)
            nMax = 10;
        else if (id == IDC_SYNC_RATE)
            nMax = 100;
        else
            nMax = 5000;
        ArrowEditStep(GetDlgItem(hDlg, id), wParam, (LONG)nMin, (LONG)(WORD)nMax, 1);
        return TRUE;

    case WM_CLOSE:
    cancel:
        SyncStop(hDlg);
        SyncFreeFrames(hDlg);
        WaveCloseDevice();
        gwSyncResult = 2;
        InvalidateRect(ghwndFrame, NULL, TRUE);
        return TRUE;
    }
    return FALSE;
}

/*
 * s04:2CA6-2CFF  PumpMessages
 *
 * Dispatches all waiting messages, through IsDialogMessage for hDlg.
 */
void FAR PumpMessages(HWND hDlg)
{
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (!hDlg || !IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

/*
 * s04:2D00-2DF7  EditSynchronize  (FAR PASCAL)
 *
 * Video / Synchronize: runs the modeless dialog with the main window and
 * view disabled.  On OK with a change, sets the new frame rate and moves
 * the sound (and the MIDI track, whose offset is always 0).
 */
void FAR PASCAL EditSynchronize(HWND hwnd)
{
    FARPROC lpfn;
    HWND    hDlg;

    lpfn = MakeProcInstance((FARPROC)SyncDlgProc, ghInst);
    gidHelpContext = IDD_SYNCHRONIZE;
    hDlg = CreateDialog(ghInst, MAKEINTRESOURCE(IDD_SYNCHRONIZE), hwnd, lpfn);
    ShowWindow(hDlg, SW_SHOWNORMAL);
    UpdateWindow(hDlg);
    EnableWindow(ghwndApp, FALSE);
    EnableWindow(ghwndView, FALSE);

    gwSyncResult = 0;
    do {
        if (gfSyncPlaying)
            SyncPlayIdle();
        PumpMessages(hDlg);
    } while (gwSyncResult == 0);

    EnableWindow(ghwndApp, TRUE);
    EnableWindow(ghwndView, TRUE);
    DestroyWindow(hDlg);
    gidHelpContext = 0;
    FreeProcInstance(lpfn);

    if (gwSyncResult == 1 &&
        (gnSyncAudioOffset != 0 || gnSyncMidiOffset != 0 || gdwNewUSec != gCompOptions.dwUSecPerFrame)) {
        SetUndo(IDM_SYNCHRONIZE);
        SetUSecPerFrame(gdwNewUSec);
        WaveAdjustOffset((LONG)gnSyncAudioOffset);
        MidiAdjustOffset((LONG)gnSyncMidiOffset);
        UpdateLength();
    }
}

/*
 * s04:2DF8-2EAB  ClipboardMakeStatic
 *
 * When VidEdit owns the clipboard with more than one frame or with sound
 * (data that depends on this instance), replaces it with just a DIB, a
 * palette and a bitmap of the frame.  Also called on
 * WM_RENDERALLFORMATS, at exit and at the end of the Windows session.
 */
void FAR ClipboardMakeStatic(void)
{
    HANDLE      hdib;
    HPALETTE    hpal;
    HBITMAP     hbm;

    if (GetClipboardOwner() != ghwndFrame)
        return;
    if (!OpenClipboard(ghwndFrame))
        return;

    if (GetClipboardFrameCount(NULL) > 1 || WaveClipboardFrames()) {
        hdib = GetClipboardData(CF_DIB);
        if (hdib)
            hdib = CopyHandle(hdib);
        hpal = GetClipboardData(CF_PALETTE);
        if (hpal)
            hpal = CopyPalette(hpal);
        hbm = BitmapFromDib(hdib, hpal, 0);

        EmptyClipboard();
        if (hdib)
            SetClipboardData(CF_DIB, hdib);
        if (hpal)
            SetClipboardData(CF_PALETTE, hpal);
        if (hbm)
            SetClipboardData(CF_BITMAP, hbm);
    }
    CloseClipboard();
}
