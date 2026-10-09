/*
 * edit.h - types and prototypes of the reconstructed VIDEDIT.EXE code in
 * segments 4 and 7: edit.c, view.c, dib.c (seg4) and wave.c (seg7).
 */

#ifndef _EDIT_H_
#define _EDIT_H_

/* ------------------------------------------------------------------ */
/* the WAVE media handler (MEDWAVE.MMH)                                */
/* ------------------------------------------------------------------ */

/*
 * Messages and structures of the MediaMan WAVE logical handler, as in
 * MEDWAVE.H of the 1990 Multimedia Extensions beta SDK; the values match
 * what VidEdit sends.  An HMEDWAVE (HWAVE in MEDWAVE.H) is a handle to a piece of sound owned by
 * the handler (cut/copied data); the handler DLL exports helper functions
 * that VidEdit looks up with GetProcAddress (see the G_ pointers below).
 */
#ifndef medtypeWAVE
#define medtypeWAVE             medFOURCC('W', 'A', 'V', 'E')
#endif

typedef DWORD HMEDWAVE;                 /* MEDWAVE.H calls it HWAVE, which clashes with MMSYSTEM */

#define WAVE_GETSIZE            (MED_USER + 2)  /* -> length in samples */
#define WAVE_CUT                (MED_USER + 3)  /* l1 = position, l2 = length -> HMEDWAVE */
#define WAVE_PASTE              (MED_USER + 4)  /* l1 = position, l2 = HMEDWAVE -> samples pasted */
#define WAVE_READ               (MED_USER + 5)  /* l1 = WaveReadStruct FAR *, l2 = format wanted */
#define WAVE_COPY               (MED_USER + 6)  /* l1 = position, l2 = length -> HMEDWAVE */
#define WAVE_GETDATA            (MED_USER + 9)
#define WAVE_GETFMT             (MED_USER + 10) /* l1 = PCMWAVEFORMAT FAR *, l2 = size */
#define WAVE_GETFMTSIZE         (MED_USER + 11)
#define WAVE_WRITE              (MED_USER + 12) /* l1 = position, l2 = WaveWriteStruct FAR * */
#define WAVE_SETFMT             (MED_USER + 13) /* l1 = format (NULL: ask, l2 = parent window) */
#define WAVE_REALIZE            (MED_USER + 14)
#define WAVE_SETFMTNORESIZE     (MED_USER + 15)

typedef struct _WaveRead {
    LONG    nPosition;
    LONG    nLength;
    LPSTR   fpchBuffer;
} WaveReadStruct;

typedef struct _WaveWrite {
    LONG    nLength;
    LPSTR   fpchBuffer;
} WaveWriteStruct;

/* the MEDWAVE.MMH exports, looked up at start-up (seg2) */
#define MediaWaveFreeHWAVE(hw) \
    ((void (FAR PASCAL *)(HMEDWAVE))glpfnMediaWaveFreeHWAVE)(hw)
#define MediaWaveFormatHWAVE(hw) \
    ((PCMWAVEFORMAT FAR * (FAR PASCAL *)(HMEDWAVE))glpfnMediaWaveFormatHWAVE)(hw)
#define MediaWaveSizeHWAVE(hw) \
    ((LONG (FAR PASCAL *)(HMEDWAVE))glpfnMediaWaveSizeHWAVE)(hw)
#define MediaWaveReadHWAVE(hw, lPos, lLen, lpBuf, lpFmt) \
    ((LONG (FAR PASCAL *)(HMEDWAVE, LONG, LONG, LPSTR, LPVOID))glpfnMediaWaveReadHWAVE)(hw, lPos, lLen, lpBuf, lpFmt)

/* ------------------------------------------------------------------ */
/* global memory helpers                                               */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* view.c (seg4 0000-0C79): the view, class "VidEditWindow"            */
/* ------------------------------------------------------------------ */

extern HWND     ghwndSizeBox;           /* DS:2D12 */
extern HWND     ghwndVScroll;           /* DS:319E */
extern HWND     ghwndHScroll;           /* DS:3260 */

void    FAR PASCAL ViewLayout(BOOL fKeepPos);
void    FAR PASCAL ViewDrawBorder(HWND hwnd, HDC hdc);
void    FAR ViewPaintBackground(HWND hwnd, HDC hdc);
BOOL    FAR PASCAL ViewScroll(int dx, int dy);
BOOL    FAR PASCAL ViewAutoScroll(POINT pt);
void    FAR ViewStopAutoScroll(void);
LRESULT FAR PASCAL ViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* framewnd.c (seg4 0C7A-1029): the frame window, class "ShowWindow"   */
/* ------------------------------------------------------------------ */

extern char     gszFrameClass[];        /* DS:0230 "ShowWindow" */

void    FAR PASCAL FrameImageToClient(LPPOINT lppt);
void    FAR PASCAL FrameClientToImage(LPPOINT lppt);
void    FAR PASCAL FrameImageToClientRect(LPRECT lprc);
void    FAR PASCAL FrameClientToImageRect(LPRECT lprc);
BOOL    FAR PASCAL FrameRegisterClass(HINSTANCE hInst, HINSTANCE hPrev);
HDC     FAR FrameGetDC(void);
LRESULT FAR PASCAL FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ------------------------------------------------------------------ */
/* edit.c (seg4 102A-2EAB): the Edit menu                              */
/* ------------------------------------------------------------------ */

extern BOOL     gfSetSelNested;         /* DS:11A6 */

BOOL    FAR PASCAL PastePaletteDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL CreatePaletteDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL PaletteCreatedDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL    FAR PASCAL FrameRateDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PASCAL EditSetTracks(BOOL fVideo, BOOL fAudio, BOOL fMidi);
void    FAR PASCAL EditCut(HWND hwnd);
BOOL    FAR PASCAL EditCopy(HWND hwnd);
void    FAR PASCAL EditPaste(HWND hwnd);
void    FAR PASCAL EditPastePalette(HWND hwnd);
void    FAR PASCAL EditDelete(HWND hwnd);
void    FAR PASCAL EditCreatePalette(HWND hwnd);
void    FAR PASCAL EditConvertFrameRate(HWND hwnd);
BOOL    FAR PASCAL SyncDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void    FAR PumpMessages(HWND hDlg);
void    FAR PASCAL EditSynchronize(HWND hwnd);
void    FAR ClipboardMakeStatic(void);

/* ------------------------------------------------------------------ */
/* wave.c (seg7)                                                       */
/* ------------------------------------------------------------------ */

extern MEDID    gmedidWave;             /* DS:03F0 the movie's audio track */

void  FAR       WaveChooseFormat(void);
void  FAR       WaveUndoFormat(void);
int   FAR PASCAL WaveCountFrames(MEDID medid);
void  FAR       WaveRelease(void);
BOOL  FAR PASCAL WaveInsertElement(WORD wFrame, WORD wReplace, WORD wFrames,
                                   LPWORD lpwFrames, MEDID medid);
BOOL  FAR PASCAL WaveDelete(WORD wStart, WORD wCount);
int   FAR       WaveNumFrames(void);
BOOL  FAR       WaveUndo(void);
void  FAR       WaveClearUndo(void);
BOOL  FAR PASCAL WaveAdjustOffset(LONG lMilliseconds);
BOOL  FAR       WaveSwapOffset(void);
BOOL  FAR       WaveOpenDevice(void);
BOOL  FAR PASCAL WavePlay(WORD wStart, WORD wCount, LONG lMsOffset, BOOL fWait, BOOL fLoop);
void  FAR PASCAL WaveOutMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void  FAR       WaveStop(void);
void  FAR       WaveCloseDevice(void);
void  FAR PASCAL WaveFrameBytes(WORD wFrame, LPDWORD lpdwBytes);
BOOL  FAR PASCAL WaveGetRangeElement(WORD wStart, WORD wCount, MEDID *pmedid, BOOL fShare);
BOOL  FAR       WaveCanPaste(void);
BOOL  FAR PASCAL WaveCopyToClipboard(WORD wStart, WORD wCount);
void  FAR       WaveRenderClipboard(void);
int   FAR       WaveClipboardFrames(void);
BOOL  FAR PASCAL WavePasteFromClipboard(WORD wFrame, WORD wReplace, WORD wFrames);
void  FAR       WaveDestroyClipboard(void);
BOOL  FAR PASCAL WaveGetFormatInfo(LPWORD lpwBits, LPDWORD lpdwRate, LPWORD lpwChannels);
BOOL  FAR PASCAL WaveLoadIntoMemory(WORD wUnused);

/* ------------------------------------------------------------------ */
/* dib.c (seg4 2EAC-4B29): a DIB "handle" is the selector of a locked  */
/* block with the BITMAPINFOHEADER at offset 0                         */
/* ------------------------------------------------------------------ */

HANDLE   FAR OpenDIB(LPSTR szFile);
BOOL     FAR WriteDIB(LPSTR szFile, HANDLE hdib);
BOOL     FAR DibInfo(HANDLE hbi, LPBITMAPINFOHEADER lpbi);
HPALETTE FAR CreateBIPalette(LPBITMAPINFOHEADER lpbi);
HPALETTE FAR CreateDibPalette(HANDLE hdib);
HPALETTE FAR CreateColorPalette(void);
HPALETTE FAR CreateSystemPalette(void);
HPALETTE FAR CopyPalette(HPALETTE hpal);
HANDLE   FAR ReadDibBitmapInfo(int fh);
WORD     FAR PaletteSize(LPBITMAPINFOHEADER lpbi);
WORD     FAR DibNumColors(LPBITMAPINFOHEADER lpbi);
HANDLE   FAR DibFromBitmap(HBITMAP hbm, DWORD biStyle, WORD biBits, HPALETTE hpal, UINT wUsage);
HBITMAP  FAR BitmapFromDib(HANDLE hdib, HPALETTE hpal, UINT wUsage);
HANDLE   FAR DibFromDib(HANDLE hdib, DWORD biStyle, WORD biBits, HPALETTE hpal, UINT wUsage);
HANDLE   FAR DibFromBitmapPalette(HBITMAP hbm, WORD biBits, HPALETTE hpal);
void     FAR MapBits8(BYTE huge *pb, DWORD cb, LPBYTE xlat);
void     FAR MapBits4(BYTE huge *pb, DWORD cb, LPBYTE xlat);
void     FAR MapBitsRLE8(BYTE huge *pb, DWORD cb, LPBYTE xlat);
void     FAR MapBitsRLE4(BYTE huge *pb, DWORD cb, LPBYTE xlat);
BOOL     FAR MapDibToPalette(HANDLE hdib, HPALETTE hpal);
BOOL     FAR StretchBitmap(HDC hdc, int x, int y, int dx, int dy, HBITMAP hbm,
                           int x0, int y0, int dx0, int dy0, DWORD rop);
BOOL     FAR DrawBitmap(HDC hdc, int x, int y, HBITMAP hbm, DWORD rop);
BOOL     FAR DrawDib(HDC hdc, int x, int y, HANDLE hdib, HPALETTE hpal, UINT wUsage);
BOOL     FAR SetDibUsageFill(HANDLE hdib, HPALETTE hpal, UINT wUsage);
BOOL     FAR SetDibUsage(HANDLE hdib, HPALETTE hpal, UINT wUsage);
BOOL     FAR SetPalFlags(HPALETTE hpal, int iIndex, int cntEntries, UINT wFlags);
BOOL     FAR PalEq(HPALETTE hpal1, HPALETTE hpal2);
BOOL     FAR StretchDibBlt(HDC hdc, int x, int y, int dx, int dy, HANDLE hdib,
                           int x0, int y0, int dx0, int dy0, LONG rop, UINT wUsage);
BOOL     FAR DibBlt(HDC hdc, int x0, int y0, int dx, int dy, HANDLE hdib,
                    int x1, int y1, LONG rop, UINT wUsage);
LPVOID   FAR DibXYH(HANDLE hdib, int x, int y);
void     FAR DibNothing(void);
LPVOID   FAR DibXY(LPBITMAPINFOHEADER lpbi, int x, int y);
HANDLE   FAR CreateDib(int bits, int dx, int dy);
HANDLE   FAR CopyHandle(HANDLE h);

#endif /* _EDIT_H_ */
