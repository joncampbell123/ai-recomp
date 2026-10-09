/*
 * framedrw.c - code segment 6 of VIDEDIT.EXE, s06:5904-6107: drawing the
 * current frame, and the Video Format command
 *
 * A frame is drawn by stretching the part of its DIB it shows (rcSrc)
 * to its place (rcDst) scaled by the zoom (gwZoom, in halves: 2 is 1x).
 * Palette-based DIBs are drawn as DIB_PAL_COLORS with StretchDIBits, RLE
 * delta frames over what is already there, and true-colour ones through
 * DrawDib.  With Full Frame Update the full frame is drawn instead.
 *
 * Functions are in the order of the binary.
 */

#include "videdit.h"
#include "frames.h"

/* "Video Format" dialog (IDD_VIDEOFORMAT) */
#define IDC_VFBITS8             190
#define IDC_VFBITS16            191
#define IDC_VFBITS24            192

PALINDEXBMI gbmiPalIndex;               /* DS:2A94 */

/*
 * s06:5904-5B76  DrawFrameDIB  (NEAR PASCAL)
 *
 * Draws the part lprcSrc of a frame's DIB at lprcDst (movie coordinates,
 * scaled by wZoom / 2) on hdc, realizing hpal.  An RLE frame that is not
 * complete (FF_COMPLETE not in wFlags) is drawn with its skipped pixels
 * transparent.  Fills in biClrUsed (and for palette DIBs biSizeImage) if
 * missing.
 */
BOOL NEAR PASCAL DrawFrameDIB(HDC hdc, HANDLE hdib, HPALETTE hpal, RECT FAR *lprcSrc,
                              RECT FAR *lprcDst, WORD wZoom, WORD wFlags)
{
    LPBITMAPINFOHEADER  lpbi;
    HCURSOR             hcurOld = 0;
    HPALETTE            hpalOld;
    int                 nModeOld;

    if (gwZoom != 2)
        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

    nModeOld = SetStretchBltMode(hdc, COLORONCOLOR);

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
    if (lpbi->biClrUsed == 0 && lpbi->biBitCount <= 8)
        lpbi->biClrUsed = (int)(1 << lpbi->biBitCount);

    if (hpal) {
        hpalOld = SelectPalette(hdc, hpal, FALSE);
        RealizePalette(hdc);
    }

    if (lpbi->biCompression == BI_RLE8 && !(wFlags & FF_COMPLETE)) {
        StretchRLEDIBits(hdc,
                         (WORD)(lprcDst->left * wZoom) >> 1,
                         (WORD)(lprcDst->top * wZoom) >> 1,
                         (WORD)((lprcDst->right - lprcDst->left) * wZoom) >> 1,
                         (WORD)((lprcDst->bottom - lprcDst->top) * wZoom) >> 1,
                         lprcSrc->left,
                         (int)lpbi->biHeight - lprcSrc->bottom,
                         lprcSrc->right - lprcSrc->left,
                         lprcSrc->bottom - lprcSrc->top,
                         (LPBYTE)lpbi + (WORD)lpbi->biSize + (WORD)lpbi->biClrUsed * 4,
                         lpbi, DIB_PAL_COLORS, SRCCOPY);
    } else if (lpbi->biBitCount <= 8) {
        if (lpbi->biSizeImage == 0)
            lpbi->biSizeImage = (DWORD)((((WORD)lpbi->biWidth * lpbi->biBitCount + 31) & ~31) >> 3)
                                * (WORD)lpbi->biHeight;
        StretchDIBits(hdc,
                      (WORD)(lprcDst->left * wZoom) >> 1,
                      (WORD)(lprcDst->top * wZoom) >> 1,
                      (WORD)((lprcDst->right - lprcDst->left) * wZoom) >> 1,
                      (WORD)((lprcDst->bottom - lprcDst->top) * wZoom) >> 1,
                      lprcSrc->left,
                      (int)lpbi->biHeight - lprcSrc->bottom,
                      lprcSrc->right - lprcSrc->left,
                      lprcSrc->bottom - lprcSrc->top,
                      (LPBYTE)lpbi + (WORD)lpbi->biSize + (WORD)lpbi->biClrUsed * 4,
                      (LPBITMAPINFO)lpbi, DIB_PAL_COLORS, SRCCOPY);
    } else {
        DrawDibDraw((HDRAWDIB)ghdd, hdc,
                    (WORD)(lprcDst->left * wZoom) >> 1,
                    (WORD)(lprcDst->top * wZoom) >> 1,
                    (WORD)((lprcDst->right - lprcDst->left) * wZoom) >> 1,
                    (WORD)((lprcDst->bottom - lprcDst->top) * wZoom) >> 1,
                    lpbi, NULL,
                    lprcSrc->left, lprcSrc->top,
                    lprcSrc->right - lprcSrc->left, lprcSrc->bottom - lprcSrc->top, 0);
    }

    if (hpal)
        SelectPalette(hdc, hpalOld, FALSE);
    SetStretchBltMode(hdc, nModeOld);
    GlobalUnlock(hdib);
    if (hcurOld)
        SetCursor(hcurOld);
    return TRUE;
}

/*
 * s06:5B7A-5BCA  FillBackground  (NEAR PASCAL)
 *
 * Fills the movie's area (at the zoom) with the background brush.
 */
static void NEAR PASCAL FillBackground(HDC hdc)
{
    HBRUSH  hbrOld;
    int     cx;
    int     cy;

    cx = (WORD)(gwZoom * gcxMovie) >> 1;
    cy = (WORD)(gwZoom * gcyMovie) >> 1;

    hbrOld = SelectObject(hdc, (HBRUSH)ghbrBtnFace);
    PatBlt(hdc, 0, 0, cx, cy, PATCOPY);
    if (hbrOld)
        SelectObject(hdc, hbrOld);
}

/*
 * s06:5BCE-5C07  EraseMovie  (NEAR PASCAL)
 *
 * Fills the movie's area (at the zoom) with white.
 */
static void NEAR PASCAL EraseMovie(HDC hdc)
{
    RECT    rc;

    rc.right = (WORD)(gwZoom * gcxMovie) >> 1;
    rc.bottom = (WORD)(gwZoom * gcyMovie) >> 1;
    rc.left = 0;
    rc.top = 0;
    FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
}

/*
 * s06:5C0A-5C41  FramesChanged
 *
 * Frames iStart.. (nFrames, 0: to the end) changed: the frame window is
 * repainted if the current frame is among them, and the full frame is
 * built again next time.
 */
void FAR PASCAL FramesChanged(UINT iStart, UINT nFrames, BOOL fUnused)
{
    if (nFrames == 0)
        nFrames = gnFrames - iStart;

    if (gwCurFrame >= iStart && iStart + nFrames >= gwCurFrame)
        InvalidateRect((HWND)ghwndFrame, NULL, TRUE);

    gfFullFrameStale = TRUE;
}

/*
 * s06:5C44-5D05  VideoFormatDlgProc
 *
 * The Video Format dialog: 8, 16 or 24 bits; ends with the choice (0 for
 * Cancel).  It loads DS from SS on entry, unlike VidEdit's other
 * callbacks.
 */
LONG FAR PASCAL VideoFormatDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        CheckDlgButton(hdlg, IDC_VFBITS8, gwVideoBits == 8 ? 1 : 0);
        CheckDlgButton(hdlg, IDC_VFBITS16, gwVideoBits == 16 ? 1 : 0);
        CheckDlgButton(hdlg, IDC_VFBITS24, gwVideoBits == 24 ? 1 : 0);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (IsDlgButtonChecked(hdlg, IDC_VFBITS8))
                EndDialog(hdlg, 8);
            else if (IsDlgButtonChecked(hdlg, IDC_VFBITS16))
                EndDialog(hdlg, 16);
            else if (IsDlgButtonChecked(hdlg, IDC_VFBITS24))
                EndDialog(hdlg, 24);
            break;

        case IDCANCEL:
            EndDialog(hdlg, 0);
            break;
        }
        break;
    }
    return FALSE;
}

/*
 * s06:5D08-5D78  VideoFormat
 *
 * Video Format command: changes the bit depth all frames are converted
 * to (undoable).
 */
void FAR VideoFormat(void)
{
    FARPROC lpfn;
    UINT    wBits;

    lpfn = MakeProcInstance((FARPROC)VideoFormatDlgProc, (HINSTANCE)ghInst);
    wBits = DoDialog(IDD_VIDEOFORMAT, (HWND)ghwndApp, lpfn);
    FreeProcInstance(lpfn);

    if (wBits && gwVideoBits != wBits) {
        SetUndo(IDM_VIDEOFORMAT);
        gwVideoBitsUndo = gwVideoBits;
        gwVideoBits = wBits;
        if (wBits == 8 && gnMovieColors < 64)
            gnMovieColors = 64;
        FramesChanged(0, 0, FALSE);
    }
}

/*
 * s06:5D7A-5D9E  UndoVideoFormat
 */
void FAR UndoVideoFormat(void)
{
    UINT    t;

    if (gwVideoBitsUndo != gwVideoBits) {
        t = gwVideoBitsUndo;
        gwVideoBitsUndo = gwVideoBits;
        gwVideoBits = t;
        FramesChanged(0, 0, FALSE);
    }
}

/*
 * s06:5DA0-608E  PaintFrame
 *
 * Paints the current frame (gwCurFrame) on hdc: the frame's own part, or with
 * Full Frame Update (gwFrameUpdate == IDM_FULLFRAMEUPDATE) the full frame.  With
 * fErase the rest of the movie's area is filled with white (all of it
 * for a transparent RLE frame).  Past the end the background is drawn.
 *
 * The full frame's header is copied into gbmiPalIndex, whose colour
 * table is the identity, to draw it as DIB_PAL_COLORS.
 */
void FAR PASCAL PaintFrame(HDC hdc, BOOL fErase)
{
    LPBITMAPINFOHEADER  lpbi;
    HPFRAME             pfr;
    HCURSOR             hcurOld;
    HPALETTE            hpal;
    HPALETTE            hpalOld;
    HANDLE              hdib;
    BOOL                fFull;
    BOOL                fTransparent;
    int                 nModeOld;
    UINT                i;

    if (gwCurFrame >= gnFrames) {
        FillBackground(hdc);
        return;
    }

    fFull = (gwFrameUpdate == IDM_FULLFRAMEUPDATE);
    if (fFull) {
        hdib = GetFullFrame(gwCurFrame, &hpal, GFF_CONVERT | GFF_ESCAPE | GFF_REBUILD);
        if (hdib == 0 || hdib == 1) {
            hdib = 0;
            fFull = FALSE;
        }
    }
    if (!fFull)
        hdib = GetFrameDIB(gwCurFrame, &hpal, 0);

    if (hdib == 0) {
        if (fErase)
            EraseMovie(hdc);
        return;
    }

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hdib);
    fTransparent = (lpbi->biCompression == BI_RLE8 && !(gpFrames[gwCurFrame].wFlags & FF_COMPLETE));
    GlobalUnlock(hdib);

    if (fErase && fTransparent)
        EraseMovie(hdc);

    if (!fFull) {
        pfr = &gpFrames[gwCurFrame];
        DrawFrameDIB(hdc, hdib, hpal, (RECT FAR *)&pfr->rcSrc, (RECT FAR *)&pfr->rcDst, gwZoom,
                     pfr->wFlags);
        if (fErase && !fTransparent) {
            pfr = &gpFrames[gwCurFrame];
            ExcludeClipRect(hdc,
                            (WORD)(pfr->rcDst.left * gwZoom) >> 1,
                            (WORD)(pfr->rcDst.top * gwZoom) >> 1,
                            (WORD)(pfr->rcDst.right * gwZoom) >> 1,
                            (WORD)(pfr->rcDst.bottom * gwZoom) >> 1);
            EraseMovie(hdc);
        }
        return;
    }

    hcurOld = 0;
    if (gwZoom != 2)
        hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (hpal) {
        hpalOld = SelectPalette(hdc, hpal, FALSE);
        RealizePalette(hdc);
    }

    gbmiPalIndex.bmiHeader = *lpbi;
    if (!gfPalIndexReady) {
        for (i = 0; i < 256; i++)
            gbmiPalIndex.awIndex[i] = i;
        gfPalIndexReady = TRUE;
    }

    nModeOld = SetStretchBltMode(hdc, COLORONCOLOR);

    if (lpbi->biBitCount <= 8) {
        StretchDIBits(hdc, 0, 0,
                      (WORD)(gwZoom * gcxMovie) >> 1, (WORD)(gwZoom * gcyMovie) >> 1,
                      0, 0, gcxMovie, gcyMovie,
                      (LPBYTE)lpbi + (WORD)lpbi->biSize + (WORD)lpbi->biClrUsed * 4,
                      (LPBITMAPINFO)&gbmiPalIndex, DIB_PAL_COLORS, SRCCOPY);
    } else {
        DrawDibDraw((HDRAWDIB)ghdd, hdc, 0, 0,
                    (WORD)(gwZoom * gcxMovie) >> 1, (WORD)(gwZoom * gcyMovie) >> 1,
                    lpbi, NULL, 0, 0, gcxMovie, gcyMovie, 0);
    }

    SetStretchBltMode(hdc, nModeOld);
    if (hpal)
        SelectPalette(hdc, hpalOld, FALSE);
    if (hcurOld)
        SetCursor(hcurOld);
}

/*
 * s06:6092-6104  RealizeFramePalette
 *
 * Realizes the current frame's palette (for a frame without one, the
 * DrawDib palette of a true-colour movie or the gray palette) on hdc;
 * returns the number of entries realized.
 */
UINT FAR PASCAL RealizeFramePalette(HDC hdc)
{
    HPALETTE    hpal;
    HPALETTE    hpalOld;
    UINT        n;

    if (gwCurFrame >= gnFrames)
        return 0;

    GetFrameDIB(gwCurFrame, &hpal, GFD_PALETTE);
    if (hpal == 0) {
        if (gwVideoBits > 8)
            hpal = DrawDibGetPalette((HDRAWDIB)ghdd);
        else
            hpal = DibGetGrayPalette();
    }
    if (hpal == 0)
        return 0;

    hpalOld = SelectPalette(hdc, hpal, FALSE);
    n = RealizePalette(hdc);
    SelectPalette(hdc, hpalOld, FALSE);
    return n;
}
