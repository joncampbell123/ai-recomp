/*
 * preview.c - code segment 19 of VIDEDIT.EXE (0x0898 bytes): File/Play
 * Preview.
 *
 * The preview plays the movie, or the selection, in VidEdit's own window.
 * If the movie has been saved to a file (and nothing forbids it) it is
 * played by MCIAVI through MCI in the frame window; otherwise VidEdit
 * steps through the frames itself on a timer (PreviewIdle) and plays the
 * sound with its own wave output code (segment 7).  A stopped MCI device
 * is noticed by polling its mode every 200 ms.
 *
 * Data: DGROUP module at DS:1104-1171 (initialised) and DS:2A70-2A7B.
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "compress.h"
#include "preview.h"

/* MCI digital video and MCIAVI definitions used here (DIGITALV.H and
   MCIAVI.H of the VfW 1.1 DK) */
#define MCI_DGV_RECT                0x00010000L
#define MCI_DGV_PUT_SOURCE          0x00020000L
#define MCI_DGV_PUT_DESTINATION     0x00040000L
#define MCI_DGV_REALIZE_NORM        0x00010000L
#define MCI_DGV_UPDATE_HDC          0x00020000L
#define MCI_DGV_WINDOW_HWND         0x00010000L
#define MCI_MCIAVI_PLAY_WINDOW      0x01000000L

typedef struct {                        /* MCI_DGV_SET_PARMS */
    DWORD   dwCallback;
    DWORD   dwTimeFormat;
    DWORD   dwAudio;
    DWORD   dwFileFormat;
    DWORD   dwSpeed;
} DGVSETPARMS;

typedef struct {                        /* MCI_DGV_WINDOW_PARMS */
    DWORD   dwCallback;
    HWND    hWnd;
    WORD    wReserved1;
    WORD    nCmdShow;
    WORD    wReserved2;
    LPSTR   lpstrText;
} DGVWINDOWPARMS;

typedef struct {                        /* MCI_DGV_PUT_PARMS */
    DWORD   dwCallback;
    RECT    rc;
} DGVPUTPARMS;

typedef struct {                        /* MCI_DGV_UPDATE_PARMS, as VidEdit */
    DWORD   dwCallback;                 /* was built: no wReserved0 */
    RECT    rc;
    HDC     hDC;
} DGVUPDATEPARMS;

/* string table */
#define IDS_CAPTION             126     /* "VidEdit" */
#define IDS_NOWAVEDEVICE        270     /* "No wave device that can play files in the current format is installed." */
#define IDS_NOMCIAVI            271     /* "Unable to run playback software for video preview: ..." */
#define IDS_MCIPLAYERROR        272     /* "Unable to play file with MCI: %s" */
#define IDS_MCIOPENERROR        273     /* "Unable to open file with MCI: %s" */
#define IDS_PREVIEWSILENT       274     /* "Cannot play current audio format: previewing silently." */

/* initialised data, DS:1104-1171 (after the module's "AnimSTrackBar",
   "GrayBlob" header strings; gfInMemory, gfPreviewing and gfPreviewDrawing are in shared.c) */
WORD        gfPreviewSel        = 0;    /* DS:1122 previewing the selection only */
WORD        gfPreviewSilent     = 0;    /* DS:1124 audio turned off for MCI */
WORD        gwPreview1126       = 0;    /* DS:1126 not used */
static char szAVIVideo[]        = "AVIVideo";   /* DS:1128 MCI device type */
WORD        gwMCIDevice         = 0;    /* DS:1132 the opened movie */
WORD        gwMCIAVIDevice      = 0;    /* DS:1134 MCIAVI opened by type, kept loaded */
WORD        gfWarnedSilent      = 0;    /* DS:1136 "previewing silently" shown once */

/* uninitialised data */
DWORD       gdwPreviewTime;             /* DS:2A70 timeGetTime at the start / last poll */
DWORD       gdwPreviewUSec;             /* DS:2A74 time of the next frame, in microseconds */
WORD        gwPreviewSoundFrame;        /* DS:2A78 next frame to queue sound for */
WORD        gwPreviewSave02aa;          /* DS:2A7A gwFrameUpdate saved during the preview */
WORD        gwPreviewSelEnd;            /* DS:3198 end of the selection */

/*
 * s19:0000-00C7  PreviewStartInternal  (NEAR)
 *
 * Starts the preview that VidEdit plays itself.  If the sound can't be
 * played, a movie with video is previewed silently (said once), and a
 * movie without video isn't previewed.
 */
static int NEAR PreviewStartInternal(void)
{
    WORD wStart;

    if (gfEditAudio && !WaveOpenDevice()) {
        if (!gfEditVideo) {
            MessageBeep(MB_ICONEXCLAMATION);
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_NOWAVEDEVICE);
            return 0;
        }
        if (!gfWarnedSilent) {
            gfWarnedSilent = 1;
            MessageBeep(MB_ICONASTERISK);
            ErrorResBox(ghwndApp, ghInst, MB_ICONASTERISK, IDS_CAPTION, IDS_PREVIEWSILENT);
        }
    }

    WaveStop();
    GetSelection(&wStart, NULL, 0);
    if (gfPreviewSel && wStart != (WORD)-1)
        SeekTo(wStart);

    gwPreviewSave02aa = gwFrameUpdate;
    gfPreviewing = 1;
    gfPreviewDrawing = 1;
    PreviewSetTracks();
    gdwPreviewTime = timeGetTime();
    gdwPreviewUSec = gCompOptions.dwUSecPerFrame;
    gwPreviewSoundFrame = 0;
    return 1;
}

/*
 * s19:00C8-016B  PreviewSetTracks
 *
 * Turns MCI's audio and video on or off to match the edited tracks
 * (gfEditAudio audio, gfEditVideo video); audio stays off once it failed.  Without
 * MCI it only updates gfPreviewDrawing and stops the sound when audio is off.
 */
BOOL FAR PreviewSetTracks(void)
{
    DGVSETPARMS set;
    DWORD       dwFlags;

    if (gwMCIDevice) {
        set.dwAudio = MCI_SET_AUDIO_ALL;
        if (gfEditAudio && !gfPreviewSilent)
            dwFlags = MCI_SET_ON;
        else
            dwFlags = MCI_SET_OFF;
        if (mciSendCommand(gwMCIDevice, MCI_SET, dwFlags | MCI_SET_AUDIO, (DWORD)(LPVOID)&set))
            return FALSE;
        if (mciSendCommand(gwMCIDevice, MCI_SET,
                           (gfEditVideo ? MCI_SET_ON : MCI_SET_OFF) | MCI_SET_VIDEO,
                           (DWORD)(LPVOID)&set))
            return FALSE;
    } else if (gfPreviewing) {
        gfPreviewDrawing = (gfEditVideo == 0);
        if (!gfEditAudio)
            WaveStop();
    }
    return TRUE;
}

/*
 * s19:016C-01A7  PreviewSetWindow  (FAR PASCAL)
 *
 * Makes MCIAVI draw into the frame window (or its own window again).
 */
BOOL FAR PASCAL PreviewSetWindow(BOOL fShow)
{
    DGVWINDOWPARMS win;

    win.hWnd = fShow ? ghwndFrame : NULL;
    if (mciSendCommand(gwMCIDevice, MCI_WINDOW, MCI_DGV_WINDOW_HWND | MCI_WAIT,
                       (DWORD)(LPVOID)&win) == 0)
        return TRUE;
    return FALSE;
}

/*
 * s19:01A8-0215  PreviewSetRect
 *
 * Tells MCIAVI where the image is.  The image rectangle in the window is
 * passed as the *source* rectangle, and the destination is reset to the
 * default (no MCI_DGV_RECT).
 */
BOOL FAR PreviewSetRect(void)
{
    DGVPUTPARMS put;

    if (gwMCIDevice) {
        GetClientRect(ghwndFrame, &put.rc);
        FrameClientToImageRect(&put.rc);
        put.rc.right -= put.rc.left;
        put.rc.bottom -= put.rc.top;
        if (mciSendCommand(gwMCIDevice, MCI_PUT, MCI_DGV_RECT | MCI_DGV_PUT_SOURCE | MCI_WAIT,
                           (DWORD)(LPVOID)&put))
            return FALSE;
        if (mciSendCommand(gwMCIDevice, MCI_PUT, MCI_DGV_PUT_DESTINATION | MCI_WAIT,
                           (DWORD)(LPVOID)&put))
            return FALSE;
    }
    return TRUE;
}

/*
 * s19:0216-0259  PreviewUpdatePosition
 *
 * Moves VidEdit's current frame to MCI's position.
 */
void FAR PreviewUpdatePosition(void)
{
    MCI_STATUS_PARMS status;

    if (gwMCIDevice && gfPreviewing) {
        status.dwItem = MCI_STATUS_POSITION;
        mciSendCommand(gwMCIDevice, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&status);
        if ((WORD)status.dwReturn != gwCurFrame)
            SetPosition((WORD)status.dwReturn);
    }
}

/*
 * s19:025A-037F  PreviewIdle
 *
 * Called from the message loop while previewing.  With MCI: every
 * 200 ms, follow the position and stop when the device has stopped.
 * Otherwise: when the next frame is due, queue sound for 15 frames every
 * 8 frames and advance by as many frames as the elapsed time asks for;
 * stop at the end of the movie or selection.
 */
void FAR PreviewIdle(void)
{
    MCI_STATUS_PARMS status;
    DWORD           dwElapsed;
    WORD            wFrame;

    if (gwMCIDevice) {
        if (timeGetTime() > gdwPreviewTime + 200) {
            gdwPreviewTime = timeGetTime();
            PreviewUpdatePosition();
            status.dwItem = MCI_STATUS_MODE;
            mciSendCommand(gwMCIDevice, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&status);
            if (status.dwReturn == MCI_MODE_STOP)
                StopPreview();
        }
        return;
    }

    wFrame = gwCurFrame;
    dwElapsed = timeGetTime() - gdwPreviewTime;
    if (gdwPreviewUSec / 1000 > dwElapsed)
        return;

    if (!(gfPreviewSel ? gwPreviewSelEnd > gwCurFrame : gwLength > gwCurFrame)) {
        StopPreview();
        return;
    }

    if (gfEditAudio && gwPreviewSoundFrame <= gwCurFrame) {
        WavePlay(gwCurFrame, 15, 0L, 0, 0);
        gwPreviewSoundFrame = gwCurFrame + 8;
    }

    while (gdwPreviewUSec / 1000 <= dwElapsed) {
        wFrame++;
        gdwPreviewUSec += gCompOptions.dwUSecPerFrame;
    }
    SetPosition(wFrame);
}

/*
 * s19:0380-05FB  PreviewStartMCI  (NEAR)
 *
 * Opens the movie file with MCIAVI (loading MCIAVI first by opening the
 * device type alone, which stays open until TermPreview), seeks to the
 * start, attaches the frame window and plays to the end or to the end
 * of the selection, with notification to the frame window.  If the wave
 * output can't play the sound, a movie with video plays silently.
 *
 * Quirk: when the selection is previewed the seek goes to its start but
 * the play command has no MCI_FROM (dwFrom is filled in but not used).
 */
static int NEAR PreviewStartMCI(void)
{
    MCI_OPEN_PARMS  open;               /* [bp-36] */
    MCI_PLAY_PARMS  play;               /* [bp-22] */
    MCI_SEEK_PARMS  seek;               /* [bp-16] */
    WORD            wEnd;               /* [bp-0E] */
    WORD            wStart;             /* [bp-0C] */
    WORD            wUnused;            /* [bp-0A] */
    DWORD           dwErr;              /* [bp-08] */
    DWORD           dwFlags;            /* [bp-04] */
    int             fOK = 0;

    if (!gwMCIDevice) {
        open.lpstrDeviceType = szAVIVideo;
        if (!gwMCIAVIDevice) {
            StatusSetMessage("Loading MCIAVI...");
            dwErr = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE, (DWORD)(LPVOID)&open);
            if (dwErr) {
                char ach[128];

                mciGetErrorString(dwErr, ach, sizeof(ach));
                MessageBeep(MB_ICONEXCLAMATION);
                ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_NOMCIAVI,
                           (LPSTR)ach);
                goto done;
            }
            gwMCIAVIDevice = open.wDeviceID;
        }
        open.lpstrElementName = gszFileName;
        StatusSetMessage("Initializing MCI...");
        dwErr = mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT,
                               (DWORD)(LPVOID)&open);
        if (dwErr) {
            char ach[128];

            mciGetErrorString(dwErr, ach, sizeof(ach));
            MessageBeep(MB_ICONEXCLAMATION);
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_MCIOPENERROR,
                       (LPSTR)ach);
            goto done;
        }
        gwMCIDevice = open.wDeviceID;
    }

    wStart = gwCurFrame;
    play.dwFrom = wStart;
    dwFlags = MCI_MCIAVI_PLAY_WINDOW | MCI_NOTIFY;
    if (gfPreviewSel) {
        GetSelection(&wStart, &wEnd, 0);
        if (wStart != (WORD)-1 && wEnd != (WORD)-1) {
            play.dwTo = wEnd;
            dwFlags = MCI_MCIAVI_PLAY_WINDOW | MCI_TO | MCI_NOTIFY;
        }
    }

    seek.dwTo = wStart;
    dwErr = mciSendCommand(gwMCIDevice, MCI_SEEK, MCI_TO | MCI_WAIT, (DWORD)(LPVOID)&seek);
    if (dwErr)
        goto error;

    if (!PreviewSetWindow(TRUE))
        goto done;
    if (!PreviewSetRect())
        goto done;
    gfPreviewSilent = 0;
    PreviewSetTracks();
    wUnused = 0;

    for (;;) {
        gfPreviewDrawing = 1;
        StatusSetMessage("Starting preview...");
        ValidateRect(ghwndFrame, NULL);
        play.dwCallback = (DWORD)(UINT)ghwndFrame;
        dwErr = mciSendCommand(gwMCIDevice, MCI_PLAY, dwFlags, (DWORD)(LPVOID)&play);
        if (dwErr != MCIERR_WAVE_OUTPUTSINUSE)
            break;
        if (!gfEditVideo) {
            MessageBeep(MB_ICONEXCLAMATION);
            ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_NOWAVEDEVICE);
            fOK = 0;
            goto done;
        }
        if (!gfWarnedSilent) {
            gfWarnedSilent = 1;
            MessageBeep(MB_ICONASTERISK);
            ErrorResBox(ghwndApp, ghInst, MB_ICONASTERISK, IDS_CAPTION, IDS_PREVIEWSILENT);
        }
        gfPreviewSilent = 1;
        PreviewSetTracks();
    }

    if (dwErr == 0) {
        InvalidateRect(ghwndFrame, NULL, FALSE);
        gfPreviewing = 1;
        gdwPreviewTime = timeGetTime();
        fOK = 1;
        goto done;
    }

error:
    {
        char ach[128];

        gfPreviewDrawing = 0;
        mciGetErrorString(dwErr, ach, sizeof(ach));
        MessageBeep(MB_ICONEXCLAMATION);
        ErrorResBox(ghwndApp, ghInst, MB_ICONEXCLAMATION, IDS_CAPTION, IDS_MCIPLAYERROR,
                   (LPSTR)ach);
    }

done:
    StatusSetMessage(NULL);
    return fOK;
}

/*
 * s19:05FC-0627  PreviewPaint  (FAR PASCAL)
 *
 * WM_PAINT of the frame window during an MCI preview.
 */
void FAR PASCAL PreviewPaint(HDC hdc)
{
    DGVUPDATEPARMS update;

    if (gwMCIDevice) {
        update.hDC = hdc;
        mciSendCommand(gwMCIDevice, MCI_UPDATE, MCI_DGV_UPDATE_HDC | MCI_WAIT,
                       (DWORD)(LPVOID)&update);
    }
}

/*
 * s19:0628-0643  PreviewRealize
 *
 * WM_QUERYNEWPALETTE / WM_PALETTECHANGED during an MCI preview.
 */
void FAR PreviewRealize(void)
{
    if (gwMCIDevice)
        mciSendCommand(gwMCIDevice, MCI_REALIZE, MCI_DGV_REALIZE_NORM, 0L);
}

/*
 * s19:0644-0771  StopPreview
 *
 * Ends the preview: tool bar buttons back, MCI stopped (and rewound to
 * frame 0 if it played to the end of the movie), or VidEdit's own
 * playback stopped and its sound closed.
 */
void FAR StopPreview(void)
{
    MCI_STATUS_PARMS status;
    MCI_SEEK_PARMS  seek;
    WORD            wPos;
    HDC             hdc;

    if (IsWindow(ghwndTransport)) {
        toolbarModifyState(ghwndTransport, 0, 1);
        toolbarModifyState(ghwndTransport, 1, 0);
    }
    StatusSetMessage(NULL);

    if (!gfPreviewing)
        return;

    if (gwMCIDevice) {
        mciSendCommand(gwMCIDevice, MCI_STOP, MCI_WAIT, 0L);
        status.dwItem = MCI_STATUS_POSITION;
        mciSendCommand(gwMCIDevice, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&status);
        wPos = (WORD)status.dwReturn;
        if (GetFrameCount() <= wPos && !gfPreviewSel) {
            seek.dwTo = 0;
            mciSendCommand(gwMCIDevice, MCI_SEEK, MCI_TO | MCI_WAIT, (DWORD)(LPVOID)&seek);
            wPos = 0;
        }
        gfPreviewing = 0;
        SetPosition(wPos);
        gfPreviewDrawing = 0;
        PreviewSetWindow(FALSE);
        InvalidateRect(ghwndFrame, NULL, FALSE);
        return;
    }

    gwFrameUpdate = gwPreviewSave02aa;
    gfPreviewing = 0;
    if (gwLength <= gwCurFrame && !gfPreviewSel) {
        SetPosition(0);
    } else {
        hdc = GetDC(ghwndView);
        ViewDrawBorder(ghwndView, hdc);
        ReleaseDC(ghwndView, hdc);
    }
    gfPreviewDrawing = 0;
    WaveCloseDevice();
}

/*
 * s19:0772-083F  StartPreview  (FAR PASCAL)
 *
 * File/Play Preview (fSelection FALSE: from the current frame, or from
 * the start when both tracks are at their end) and the selection
 * preview (TRUE).  MCI is used when the movie is a file MCIAVI can play
 * (gfDirty and gfInMemory not set).
 */
void FAR PASCAL StartPreview(BOOL fSelection)
{
    WORD    wStart;                     /* [bp-6] */
    WORD    wEnd;                       /* [bp-4] */
    HCURSOR hcurOld;                    /* [bp-2] */
    int     fOK;

    if (!fSelection && GetFrameCount() <= gwCurFrame && WaveNumFrames() - 1 <= gwCurFrame)
        SeekTo(0);

    gfPreviewSilent = 0;
    toolbarModifyState(ghwndTransport, 0, 0);
    toolbarModifyState(ghwndTransport, 1, 1);
    hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

    GetSelection(&wStart, &wEnd, 0);
    if (fSelection && wStart != (WORD)-1 && wEnd != (WORD)-1)
        gfPreviewSel = 1;
    else
        gfPreviewSel = 0;
    gwPreviewSelEnd = wEnd;

    if (!gfDirty && IsAviFile() && !gfInMemory)
        fOK = PreviewStartMCI();
    else
        fOK = PreviewStartInternal();

    if (hcurOld)
        SetCursor(hcurOld);
    if (!fOK)
        ClosePreview();
}

/*
 * s19:0840-0869  ClosePreview
 *
 * Stops the preview and closes the movie in MCI (MCIAVI stays loaded).
 */
void FAR ClosePreview(void)
{
    StopPreview();
    if (gwMCIDevice) {
        mciSendCommand(gwMCIDevice, MCI_CLOSE, MCI_WAIT, 0L);
        gwMCIDevice = 0;
    }
}

/*
 * s19:086A-0893  TermPreview
 *
 * At exit: closes the movie and the MCIAVI device.
 */
void FAR TermPreview(void)
{
    if (gwMCIAVIDevice) {
        ClosePreview();
        mciSendCommand(gwMCIAVIDevice, MCI_CLOSE, MCI_WAIT, 0L);
        gwMCIAVIDevice = 0;
    }
}

/*
 * s19:0894-0896  PreviewNotify  (FAR PASCAL)
 *
 * MM_MCINOTIFY of the frame window: does nothing (the end of an MCI
 * preview is found by PreviewIdle).
 */
void FAR PASCAL PreviewNotify(LPARAM lParam, WPARAM wParam)
{
}
