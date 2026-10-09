/*
 * avicap.h - stand-in for the AVICAP header of the Video for Windows 1.1
 * DK, written for the Open Watcom build.
 *
 * Only what the VfW 1.1 tools use.  Win16 layouts.  The functions are
 * imported from AVICAP.DLL by ordinal in the linker directives.
 */

#ifndef _INC_AVICAP
#define _INC_AVICAP

#define VFWAPI                          FAR PASCAL

#define WM_CAP_START                    WM_USER
#define WM_CAP_SET_CALLBACK_ERROR       (WM_CAP_START + 2)
#define WM_CAP_SET_CALLBACK_STATUS      (WM_CAP_START + 3)
#define WM_CAP_SET_CALLBACK_YIELD       (WM_CAP_START + 4)
#define WM_CAP_DRIVER_CONNECT           (WM_CAP_START + 10)
#define WM_CAP_DRIVER_DISCONNECT        (WM_CAP_START + 11)
#define WM_CAP_FILE_SET_CAPTURE_FILE    (WM_CAP_START + 20)
#define WM_CAP_EDIT_COPY                (WM_CAP_START + 30)
#define WM_CAP_SET_AUDIOFORMAT          (WM_CAP_START + 35)
#define WM_CAP_GET_AUDIOFORMAT          (WM_CAP_START + 36)
#define WM_CAP_DLG_VIDEOFORMAT          (WM_CAP_START + 41)
#define WM_CAP_GET_STATUS               (WM_CAP_START + 54)
#define WM_CAP_GRAB_FRAME               (WM_CAP_START + 60)
#define WM_CAP_SEQUENCE                 (WM_CAP_START + 62)
#define WM_CAP_SET_SEQUENCE_SETUP       (WM_CAP_START + 64)
#define WM_CAP_GET_SEQUENCE_SETUP       (WM_CAP_START + 65)
#define WM_CAP_STOP                     (WM_CAP_START + 68)

/* error and status IDs passed to the callbacks and left in dwReturn */
#define IDS_CAP_WAVE_OPEN_ERROR         419
#define IDS_CAP_VIDEO_SIZE_ERROR        428
#define IDS_CAP_FILE_OPEN_ERROR         429
#define IDS_CAP_COMPRESSOR_ERROR        440
#define IDS_CAP_STAT_CAP_FINI           503

#ifndef RC_INVOKED

#pragma pack(push, 1)

typedef struct tagCapStatus {
    UINT        uiImageWidth;                   /* +00 */
    UINT        uiImageHeight;                  /* +02 */
    BOOL        fLiveWindow;                    /* +04 */
    BOOL        fOverlayWindow;                 /* +06 */
    BOOL        fScale;                         /* +08 */
    POINT       ptScroll;                       /* +0A */
    BOOL        fUsingDefaultPalette;           /* +0E */
    BOOL        fAudioHardware;                 /* +10 */
    BOOL        fCapFileExists;                 /* +12 */
    DWORD       dwCurrentVideoFrame;            /* +14 */
    DWORD       dwCurrentVideoFramesDropped;    /* +18 */
    DWORD       dwCurrentWaveSamples;           /* +1C */
    DWORD       dwCurrentTimeElapsedMS;         /* +20 */
    HPALETTE    hPalCurrent;                    /* +24 */
    BOOL        fCapturingNow;                  /* +26 */
    DWORD       dwReturn;                       /* +28 */
    WORD        wNumVideoAllocated;             /* +2C */
    WORD        wNumAudioAllocated;             /* +2E */
} CAPSTATUS, *PCAPSTATUS, FAR *LPCAPSTATUS;     /* 0x30 bytes */

typedef struct tagCaptureParms {
    DWORD       dwRequestMicroSecPerFrame;      /* +00 */
    BOOL        fMakeUserHitOKToCapture;        /* +04 */
    WORD        wPercentDropForError;           /* +06 */
    BOOL        fYield;                         /* +08 */
    DWORD       dwIndexSize;                    /* +0A */
    WORD        wChunkGranularity;              /* +0E */
    BOOL        fUsingDOSMemory;                /* +10 */
    WORD        wNumVideoRequested;             /* +12 */
    BOOL        fCaptureAudio;                  /* +14 */
    WORD        wNumAudioRequested;             /* +16 */
    WORD        vKeyAbort;                      /* +18 */
    BOOL        fAbortLeftMouse;                /* +1A */
    BOOL        fAbortRightMouse;               /* +1C */
    BOOL        fLimitEnabled;                  /* +1E */
    WORD        wTimeLimit;                     /* +20 */
    BOOL        fMCIControl;                    /* +22 */
    BOOL        fStepMCIDevice;                 /* +24 */
    DWORD       dwMCIStartTime;                 /* +26 */
    DWORD       dwMCIStopTime;                  /* +2A */
    BOOL        fStepCaptureAt2x;               /* +2E */
    WORD        wStepCaptureAverageFrames;      /* +30 */
    DWORD       dwAudioBufferSize;              /* +32 */
    BOOL        fDisableWriteCache;             /* +36 */
} CAPTUREPARMS, *PCAPTUREPARMS, FAR *LPCAPTUREPARMS;   /* 0x38 bytes */

#pragma pack(pop)

HWND VFWAPI capCreateCaptureWindow(LPCSTR lpszWindowName, DWORD dwStyle, int x, int y,
                                   int nWidth, int nHeight, HWND hwndParent, int nID);  /* AVICAP.2 */
BOOL VFWAPI capGetDriverDescription(WORD wDriverIndex, LPSTR lpszName, int cbName,
                                    LPSTR lpszVer, int cbVer);                          /* AVICAP.3 */

#endif /* RC_INVOKED */

#endif /* _INC_AVICAP */
